#ifndef MOCK_SPIFFS_ADAPTER_H
#define MOCK_SPIFFS_ADAPTER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsFileReplacer.h"
#include "Core/Domain/Enums/FileSystemStatusEnum.h"

class MockSpiffsAdapter : public ISpiffsAdapter {
private:
    std::map<std::string, std::string> fileSystem;
    std::map<std::string, bool> directories;
    bool shouldFail = false;
    // GDV1-3827: how the payload reached storage, so a test can tell a streamed write from one big buffer
    size_t largestWriteChunk = 0;
    size_t writeChunkCount = 0;
    size_t maxTotalWriteBytes = 0; // 0 = no limit; otherwise the stream fails once it is exceeded
    std::string renameFailSource;  // renaming this file fails, the way flash can refuse one operation

    /**
     * @brief In-memory ISpiffsFileWriter: streams into "<path>.tmp" and publishes it on commit
     *
     * Publishing goes through the product's SpiffsFileReplacer, so tests exercise the real swap
     * sequence and this mock only supplies the file system underneath it.
     */
    class StreamWriter : public ISpiffsFileWriter {
    public:
        StreamWriter(MockSpiffsAdapter* owner, const char* filePath)
            : owner(owner),
              replacer(owner),
              targetPath(filePath != nullptr ? filePath : ""),
              tempPath(replacer.tempPathFor(targetPath)) {
            owner->fileSystem[tempPath] = "";
        }

        ~StreamWriter() override {
            if (!committed) {
                abort();
            }
        }

        bool write(const char* chunk) override {
            if (failed) return false;
            std::string piece = chunk != nullptr ? chunk : "";
            if (piece.empty()) return true;

            if (piece.size() > owner->largestWriteChunk) {
                owner->largestWriteChunk = piece.size();
            }
            owner->writeChunkCount++;

            if (owner->maxTotalWriteBytes > 0 && written + piece.size() > owner->maxTotalWriteBytes) {
                size_t room = owner->maxTotalWriteBytes > written ? owner->maxTotalWriteBytes - written : 0;
                owner->fileSystem[tempPath] += piece.substr(0, room);
                written += room;
                failed = true;
                return false;
            }

            owner->fileSystem[tempPath] += piece;
            written += piece.size();
            return true;
        }

        bool commit() override {
            if (failed || owner->shouldFail) {
                abort();
                return false;
            }
            if (!replacer.replace(tempPath, targetPath)) {
                owner->fileSystem.erase(tempPath);
                failed = true;
                committed = true; // nothing left to abort
                return false;
            }
            committed = true;
            return true;
        }

        void abort() override {
            owner->fileSystem.erase(tempPath);
            failed = true;
            committed = true;
        }

        size_t bytesWritten() const override { return written; }

    private:
        MockSpiffsAdapter* owner;
        SpiffsFileReplacer replacer;
        std::string targetPath;
        std::string tempPath;
        size_t written = 0;
        bool failed = false;
        bool committed = false;
    };

public:
    void setShouldFail(bool fail) { shouldFail = fail; }
    void clearFileSystem() { fileSystem.clear(); directories.clear(); }

    // GDV1-3827 test helpers
    size_t getLargestWriteChunk() const { return largestWriteChunk; }
    size_t getWriteChunkCount() const { return writeChunkCount; }
    void resetWriteStats() { largestWriteChunk = 0; writeChunkCount = 0; }
    void setMaxTotalWriteBytes(size_t limit) { maxTotalWriteBytes = limit; }
    // Renaming this exact file fails; everything else keeps working, which is how a single refused
    // flash operation looks from the outside.
    void setRenameFailSource(const char* filePath) { renameFailSource = filePath != nullptr ? filePath : ""; }
    std::string getFileContent(const char* fileName) {
        auto it = fileSystem.find(fileName);
        return it != fileSystem.end() ? it->second : std::string();
    }

    // Test utilities
    bool writeToFile(const char* fileName, const char* content) {
        if (shouldFail) return false;
        fileSystem[fileName] = content;
        return true;
    }
    
    bool removeDirectory(const char* dirName) {
        if (shouldFail) return false;
        directories.erase(dirName);
        return true;
    }

    bool fileExists(const char* fileName) override {
        return fileSystem.find(fileName) != fileSystem.end();
    }

    bool deleteFile(const char* filePath) override {
        if (shouldFail) return false;
        fileSystem.erase(filePath);
        return true;
    }

    // SPIFFS.rename() refuses an existing destination and fails on a missing source — both matter
    // for the swap sequence, so the mock refuses them too (GDV1-3827).
    bool renameFile(const char* fromPath, const char* toPath) override {
        if (shouldFail) return false;
        if (!renameFailSource.empty() && renameFailSource == fromPath) return false;

        auto source = fileSystem.find(fromPath);
        if (source == fileSystem.end()) return false;
        if (fileSystem.find(toPath) != fileSystem.end()) return false;

        fileSystem[toPath] = source->second;
        fileSystem.erase(fromPath);
        return true;
    }


    std::vector<std::string> listFiles(const char* dirName) {
        return std::vector<std::string>();
    }
    
    // ISpiffsAdapter interface implementation
    void initSPIFFS() override {
        // Mock implementation - does nothing
    }
    
    std::string readSPIFFSstr(const char* filePath) override {
        if (shouldFail) return "";
        auto it = fileSystem.find(filePath);
        return (it != fileSystem.end()) ? it->second : "";
    }
    
    void removeFile(const char* filePath) override {
        if (!shouldFail) {
            fileSystem.erase(filePath);
        }
    }
    
    bool rewriteToFile(const char* filePath, const char* body) override {
        std::string content = body != nullptr ? body : "";
        if (content.size() > largestWriteChunk) {
            largestWriteChunk = content.size();
        }
        writeChunkCount++;
        if (!shouldFail) {
            fileSystem[filePath] = content;
            return true;
        }
        return false;
    }

    std::unique_ptr<ISpiffsFileWriter> openForRewrite(const char* filePath) override {
        if (shouldFail) return nullptr;
        return std::unique_ptr<ISpiffsFileWriter>(new StreamWriter(this, filePath));
    }

    bool appendToFile(const char* filePath, const char* body) override {
        if (shouldFail) return false;
        fileSystem[filePath] += body;
        fileSystem[filePath] += "\n";
        return true;
    }

    unsigned int getFileSize(const char* filePath) override {
        auto it = fileSystem.find(filePath);
        return (it != fileSystem.end()) ? (unsigned int)it->second.length() : 0;
    }

    bool directoryExists(const char* path) override {
        if (shouldFail) return false;
        return directories.find(path) != directories.end();
    }
    
    bool createDirectory(const char* path) override {
        if (shouldFail) return false;
        directories[path] = true;
        return true;
    }
    
    FileSystemStatusEnum getFileSystemStatus() override {
        return shouldFail ? FILE_SYSTEM_STATUS_FAIL : FILE_SYSTEM_STATUS_DONE;
    }
};

#endif // MOCK_SPIFFS_ADAPTER_H