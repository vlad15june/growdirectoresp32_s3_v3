#pragma once
#include <string>

class ISpiffsAdapter {
public:
    virtual ~ISpiffsAdapter() = default;

    virtual void initSPIFFS() = 0;
    virtual std::string readSPIFFSstr(const char* filePath) = 0;
    virtual void RemoveFile(const char* filePath) = 0;
    virtual void RewriteToFile(const char* filePath, const char* body) = 0;
    virtual bool directoryExists(const char* path) = 0;
    virtual bool createDirectory(const char* path) = 0;
};