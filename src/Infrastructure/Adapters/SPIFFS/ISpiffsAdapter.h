#pragma once

#include <Arduino.h>

class ISpiffsAdapter {
public:
    virtual ~ISpiffsAdapter() = default;

    virtual void initSPIFFS() = 0;
    virtual String readSPIFFSstr(String filePath) = 0;
    virtual void RemoveFile(String filePath) = 0;
    virtual void RewriteToFile(String filePath, String body) = 0;
};
