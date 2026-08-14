#pragma once

#include "ISpiffsAdapter.h"
#include "SpiffsFileWriter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <esp_partition.h>
#include <memory>
#include <stdarg.h>
#include <string>

class SpiffsAdapter : public ISpiffsAdapter {
public:
    ILogger *logger;
    IDebugSystem *debugSystem;
    FileSystemStatusEnum fileSystemStatus;

    SpiffsAdapter(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr)
        : logger(logger), debugSystem(debugSystem), fileSystemStatus(FILE_SYSTEM_STATUS_NONE) {}

    void setLogger(ILogger *loggerInstance) {
        logger = loggerInstance;
    }

    void setDebugSystem(IDebugSystem *debugSystemInstance) {
        debugSystem = debugSystemInstance;
    }

    void log(const char *format, ...) {
        if (logger != nullptr &&
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::SPIFFS_DEBUG))) {
            char buffer[1024];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            logger->println(DebugFeatures::SPIFFS_DEBUG, buffer);
        }
    }

    void initSPIFFS() override {
        unsigned long t0 = millis();
        Serial.print("SPIFFS: begin(false)...");
        Serial.flush();
        bool mounted = SPIFFS.begin(false);
        Serial.printf(" %s in %lu ms\n", mounted ? "ok" : "fail", millis() - t0);
        Serial.flush();

        if (mounted) {
            Serial.println("SPIFFS Mounted successfully");
            fileSystemStatus = FILE_SYSTEM_STATUS_DONE;
            return;
        }

        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
        if (part == NULL) {
            Serial.println("SPIFFS: partition not found");
            fileSystemStatus = FILE_SYSTEM_STATUS_FAIL;
            return;
        }

        t0 = millis();
        Serial.printf("SPIFFS: erasing partition (%u bytes)...", part->size);
        Serial.flush();
        esp_err_t err = esp_partition_erase_range(part, 0, part->size);
        Serial.printf(" %s in %lu ms\n", err == ESP_OK ? "ok" : "fail", millis() - t0);
        Serial.flush();

        if (err != ESP_OK) {
            Serial.println("SPIFFS: partition erase failed");
            fileSystemStatus = FILE_SYSTEM_STATUS_FAIL;
            return;
        }

        t0 = millis();
        Serial.print("SPIFFS: begin(true) after erase...");
        Serial.flush();
        mounted = SPIFFS.begin(true);
        Serial.printf(" %s in %lu ms\n", mounted ? "ok" : "fail", millis() - t0);
        Serial.flush();

        if (mounted) {
            Serial.println("SPIFFS Mounted successfully");
            fileSystemStatus = FILE_SYSTEM_STATUS_DONE;
        } else {
            Serial.println("SPIFFS Mount Failed");
            fileSystemStatus = FILE_SYSTEM_STATUS_FAIL;
        }
    }

    std::string readSPIFFSstr(const char* filePath) override {
        File file = SPIFFS.open(filePath, "r");
        if (!file) {
            log("Failed to open file for reading: %s", filePath);
            return "";
        }
        String content = file.readString();
        file.close();

        log("Read file: %s, length: %d", filePath, content.length());
        #ifdef DEBUG
        log("File content: %s", content.c_str());
        #endif
        
        return std::string(content.c_str());
    }

    void removeFile(const char* filePath) override {
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

    bool rewriteToFile(const char* filePath, const char* body) override {
        File file = SPIFFS.open(filePath, "w");
        if (!file) {
            log("Failed to open file for writing: %s", filePath);
            return false;
        }
        size_t bodyLen = strlen(body);
        size_t written = file.print(body);
        file.close();
        if (written != bodyLen) {
            log("Write incomplete: expected %d, wrote %d bytes to %s", bodyLen, written, filePath);
            return false;
        }
        log("Wrote %d bytes to file: %s", written, filePath);
        return true;
    }

    std::unique_ptr<ISpiffsFileWriter> openForRewrite(const char* filePath) override {
        std::unique_ptr<SpiffsFileWriter> writer(new SpiffsFileWriter(filePath, logger, debugSystem));
        if (!writer->isOpen()) {
            log("Failed to open file for streaming write: %s", filePath);
            return nullptr;
        }
        log("Streaming write started for file: %s", filePath);
        return writer;
    }

    bool appendToFile(const char* filePath, const char* body) override {
        File file = SPIFFS.open(filePath, "a");
        if (!file) {
            log("Failed to open file for append: %s", filePath);
            return false;
        }
        size_t written = file.print(body);
        written += file.print("\n");
        file.close();
        size_t expected = strlen(body) + 1;
        if (written != expected) {
            log("Append incomplete: expected %d, wrote %d bytes to %s", expected, written, filePath);
            return false;
        }
        return true;
    }

    unsigned int getFileSize(const char* filePath) override {
        File file = SPIFFS.open(filePath, "r");
        if (!file) {
            return 0;
        }
        unsigned int size = file.size();
        file.close();
        return size;
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

    FileSystemStatusEnum getFileSystemStatus() override {
        log("SPIFFS status: %s", getFileSystemStatusName(fileSystemStatus));
        return fileSystemStatus;
    }
};