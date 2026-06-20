#pragma once
#include <string>
#include "Core/Domain/Enums/FileSystemStatusEnum.h"

class ISpiffsAdapter {
public:
    virtual ~ISpiffsAdapter() = default;

    virtual void initSPIFFS() = 0;
    virtual std::string readSPIFFSstr(const char* filePath) = 0;
    virtual void removeFile(const char* filePath) = 0;
    virtual bool rewriteToFile(const char* filePath, const char* body) = 0;
    virtual bool appendToFile(const char* filePath, const char* body) = 0;
    virtual unsigned int getFileSize(const char* filePath) = 0;
    virtual bool directoryExists(const char* path) = 0;
    virtual bool createDirectory(const char* path) = 0;
    virtual FileSystemStatusEnum getFileSystemStatus() = 0;
};