#ifndef MOCK_SPIFFS_ADAPTER_H
#define MOCK_SPIFFS_ADAPTER_H

#include <string>
#include <vector>
#include <map>
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include "Core/Domain/Enums/FileSystemStatusEnum.h"

class MockSpiffsAdapter : public ISpiffsAdapter {
private:
    std::map<std::string, std::string> fileSystem;
    std::map<std::string, bool> directories;
    bool shouldFail = false;
    
public:
    void setShouldFail(bool fail) { shouldFail = fail; }
    void clearFileSystem() { fileSystem.clear(); directories.clear(); }
    
    // Test utilities
    bool writeToFile(const char* fileName, const char* content) {
        if (shouldFail) return false;
        fileSystem[fileName] = content;
        return true;
    }
    
    bool appendToFile(const char* fileName, const char* content) {
        if (shouldFail) return false;
        fileSystem[fileName] += content;
        return true;
    }
    
    bool removeDirectory(const char* dirName) {
        if (shouldFail) return false;
        directories.erase(dirName);
        return true;
    }
    
    uint32_t getFileSize(const char* fileName) {
        auto it = fileSystem.find(fileName);
        return (it != fileSystem.end()) ? it->second.length() : 0;
    }
    
    bool fileExists(const char* fileName) {
        return fileSystem.find(fileName) != fileSystem.end();
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
    
    void RemoveFile(const char* filePath) override {
        if (!shouldFail) {
            fileSystem.erase(filePath);
        }
    }
    
    bool RewriteToFile(const char* filePath, const char* body) override {
        if (!shouldFail) {
            fileSystem[filePath] = body;
            return true;
        }
        return false;
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