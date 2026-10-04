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
    // How often a file was read back, so a test can tell a cached read from one that went to
    // storage.
    size_t fileReadCount = 0;
    size_t maxTotalWriteBytes = 0; // 0 = no limit; otherwise a write fails once it is exceeded
    std::string renameFailSource;  // renaming this file fails, the way flash can refuse one operation
    size_t readsToRefuse = 0;      // the next reads answer as a file that could not be opened
    size_t readsToShorten = 0;     // the next reads bring back half of the file
    size_t lookupsToRefuse = 0;    // the next existence checks answer that the file is not there
    size_t opensToRefuse = 0;      // the next opens fail, as they do on a heap that cannot serve one
    size_t statsToRefuse = 0;      // the next index lookups fail
    size_t characterToDrop = 0;    // the next read loses the character at this index...
    bool dropCharacter = false;    // ...when this is set

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
    size_t getFileReadCount() const { return fileReadCount; }
    void resetFileReadCount() { fileReadCount = 0; }
    void setMaxTotalWriteBytes(size_t limit) { maxTotalWriteBytes = limit; }
    // Renaming this exact file fails; everything else keeps working, which is how a single refused
    // flash operation looks from the outside.
    void setRenameFailSource(const char* filePath) { renameFailSource = filePath != nullptr ? filePath : ""; }
    // The next `count` reads come back empty - what SpiffsAdapter::readSPIFFSstr() answers for a
    // file it could not open - while writes go on working.
    void refuseNextReads(size_t count) { readsToRefuse = count; }
    // The next `count` reads bring back the first half of the file - fewer bytes than it holds, as
    // a read into a string that could not grow does.
    void shortenNextReads(size_t count) { readsToShorten = count; }
    // The next `count` existence checks answer "not there" whatever is stored. SPIFFS.exists()
    // opens the file to answer (VFSImpl::exists() builds a VFSFileImpl: malloc, strdup, fopen), so
    // a file system that cannot serve an open answers this way about a file it holds.
    void refuseNextLookups(size_t count) { lookupsToRefuse = count; }
    // The next `count` opens fail - every call that opens the file on the device: a read, the
    // lookup behind fileExists() and getFileSize(). A heap that cannot serve the allocations of an
    // open (VFSImpl builds a VFSFileImpl: malloc, strdup, fopen) fails all of them alike; statFile()
    // does not open the file and still answers.
    void refuseNextOpens(size_t count) { opensToRefuse = count; }
    // The next `count` statFile() calls answer that the lookup failed.
    void refuseNextStats(size_t count) { statsToRefuse = count; }
    // The next read brings back every character but the one at `index` - what a read into a string
    // does when one of its reallocations fails and a later one succeeds (Stream::readString()
    // appends character by character and does not check the append).
    void dropCharacterFromNextRead(size_t index) { characterToDrop = index; dropCharacter = true; }
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
        if (opensToRefuse > 0) {
            opensToRefuse--;
            return false;
        }
        if (lookupsToRefuse > 0) {
            lookupsToRefuse--;
            return false;
        }
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
        if (opensToRefuse > 0) {
            opensToRefuse--;
            return "";
        }
        if (readsToRefuse > 0) {
            readsToRefuse--;
            return "";
        }
        fileReadCount++;
        auto it = fileSystem.find(filePath);
        if (it != fileSystem.end() && readsToShorten > 0) {
            readsToShorten--;
            return it->second.substr(0, it->second.size() / 2);
        }
        if (it != fileSystem.end() && dropCharacter) {
            dropCharacter = false;
            std::string content = it->second;
            if (characterToDrop < content.size()) {
                content.erase(characterToDrop, 1);
            }
            return content;
        }
        return (it != fileSystem.end()) ? it->second : "";
    }
    
    void removeFile(const char* filePath) override {
        if (!shouldFail) {
            fileSystem.erase(filePath);
        }
    }
    
    // Opened with "w", as SpiffsAdapter::rewriteToFile() opens it, and SPIFFS empties the stored file
    // when it opens it so (O_TRUNC): a write the flash stops part of the way leaves at most what
    // went in before the stop, never the previous file. This stand-in keeps exactly that much.
    bool rewriteToFile(const char* filePath, const char* body) override {
        std::string content = body != nullptr ? body : "";
        if (content.size() > largestWriteChunk) {
            largestWriteChunk = content.size();
        }
        writeChunkCount++;
        if (!shouldFail && maxTotalWriteBytes > 0 && content.size() > maxTotalWriteBytes) {
            fileSystem[filePath] = content.substr(0, maxTotalWriteBytes);
            return false;
        }
        if (!shouldFail) {
            fileSystem[filePath] = content;
            return true;
        }
        return false;
    }

    std::unique_ptr<ISpiffsFileWriter> openForRewrite(const char* filePath) override {
        if (shouldFail) return nullptr;
        if (opensToRefuse > 0) {
            opensToRefuse--;
            return nullptr;
        }
        return std::unique_ptr<ISpiffsFileWriter>(new StreamWriter(this, filePath));
    }

    bool appendToFile(const char* filePath, const char* body) override {
        if (shouldFail) return false;
        fileSystem[filePath] += body;
        fileSystem[filePath] += "\n";
        return true;
    }

    unsigned int getFileSize(const char* filePath) override {
        if (opensToRefuse > 0) {
            opensToRefuse--;
            return 0;
        }
        auto it = fileSystem.find(filePath);
        return (it != fileSystem.end()) ? (unsigned int)it->second.length() : 0;
    }

    // Not marked `override`, so that this stand-in also builds against an ISpiffsAdapter that does
    // not declare it; against one that does, it overrides it all the same.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif
    virtual bool statFile(const char* filePath, size_t& size, bool& missing) {
        size = 0;
        missing = false;
        if (shouldFail) return false;
        if (statsToRefuse > 0) {
            statsToRefuse--;
            return false;
        }
        auto it = fileSystem.find(filePath);
        if (it == fileSystem.end()) {
            missing = true;
            return false;
        }
        size = it->second.size();
        return true;
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

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