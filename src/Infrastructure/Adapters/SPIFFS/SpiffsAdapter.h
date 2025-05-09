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
            logger->println("spiffs", buffer);
        }
    }

    void initSPIFFS() override {
        if (!SPIFFS.begin(true)) {
            log("SPIFFS Mount Failed");
        } else {
            log("SPIFFS Mounted successfully");
        }
    }

    String readSPIFFSstr(String filePath) override {
        File file = SPIFFS.open(filePath, "r");
        if (!file) {
            log("Failed to open file for reading: %s", filePath.c_str());
            return "";
        }
        String content = file.readString();
        file.close();
        log("Read file: %s, length: %d", filePath.c_str(), content.length());
        return content;
    }

    void RemoveFile(String filePath) override {
        if (SPIFFS.exists(filePath)) {
            if (SPIFFS.remove(filePath)) {
                log("File removed: %s", filePath.c_str());
            } else {
                log("Failed to remove file: %s", filePath.c_str());
            }
        } else {
            log("File does not exist: %s", filePath.c_str());
        }
    }

    void RewriteToFile(String filePath, String body) override {
        File file = SPIFFS.open(filePath, "w");
        if (!file) {
            log("Failed to open file for writing: %s", filePath.c_str());
            return;
        }
        size_t written = file.print(body);
        file.close();
        log("Wrote %d bytes to file: %s", written, filePath.c_str());
    }
};
