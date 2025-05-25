#pragma once

#include "ISpiffsAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <stdarg.h>

class SpiffsAdapter : public ISpiffsAdapter {
public:
    ILogger *logger;
    IDebugSystem *debugSystem;

    SpiffsAdapter(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr)
        : logger(logger), debugSystem(debugSystem) {}

    void setLogger(ILogger *loggerInstance) {
        logger = loggerInstance;
    }

    void setDebugSystem(IDebugSystem *debugSystemInstance) {
        debugSystem = debugSystemInstance;
    }

    void log(const char *format, ...) {
        if (logger != nullptr &&
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::SPIFFS_DEBUG))) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            logger->println(DebugFeatures::SPIFFS_DEBUG, buffer);
        }
    }

    void initSPIFFS() override {
        if (!SPIFFS.begin(true)) {
            log("SPIFFS Mount Failed");
        } else {
            log("SPIFFS Mounted successfully");
        }
    }

    String readSPIFFSstr(const char* filePath) override {
        File file = SPIFFS.open(filePath, "r");
        if (!file) {
            log("Failed to open file for reading: %s", filePath);
            return "";
        }
        String content = file.readString();
        file.close();
        log("Read file: %s, length: %d", filePath, content.length());
        return content;
    }

    void RemoveFile(const char* filePath) override {
        if (SPIFFS.exists(filePath)) {
            if (SPIFFS.remove(filePath)) {
                log("File removed: %s", filePath);
            } else {
                log("Failed to remove file: %s", filePath);
            }
        } else {
            log("File does not exist: %s", filePath);
        }
    }

    void RewriteToFile(const char* filePath, const char* body) override {
        File file = SPIFFS.open(filePath, "w");
        if (!file) {
            log("Failed to open file for writing: %s", filePath);
            return;
        }
        size_t written = file.print(body);
        file.close();
        log("Wrote %d bytes to file: %s", written, filePath);
    }

    bool directoryExists(const char* path) override {
        File dir = SPIFFS.open(path);
        if (!dir) {
            log("Directory does not exist: %s", path);
            return false;
        }
        if (!dir.isDirectory()) {
            dir.close();
            log("Path exists but is not a directory: %s", path);
            return false;
        }
        dir.close();
        log("Directory exists: %s", path);
        return true;
    }

    bool createDirectory(const char* path) override {
        if (SPIFFS.mkdir(path)) {
            log("Directory created: %s", path);
            return true;
        }
        log("Failed to create directory: %s", path);
        return false;
    }
};