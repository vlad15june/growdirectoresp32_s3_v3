#pragma once

#include "ISpiffsFileWriter.h"
#include "ISpiffsFileOps.h"
#include "SpiffsFileReplacer.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <stdarg.h>
#include <string>

/**
 * @brief SPIFFS implementation of the streaming writer (GDV1-3827)
 *
 * Writes into `<target>.tmp` and hands it to SpiffsFileReplacer in commit(), so the stored file is
 * either the previous content or the complete new content — never a truncated mix, and never absent.
 */
class SpiffsFileWriter : public ISpiffsFileWriter {
public:
    SpiffsFileWriter(const char* filePath, ISpiffsFileOps* fileOps, ILogger* logger = nullptr,
                     IDebugSystem* debugSystem = nullptr)
        : logger(logger),
          debugSystem(debugSystem),
          replacer(fileOps, logger, debugSystem),
          targetPath(filePath != nullptr ? filePath : ""),
          tempPath(replacer.tempPathFor(targetPath)),
          written(0),
          failed(false),
          committed(false) {
        file = SPIFFS.open(tempPath.c_str(), "w");
        if (!file) {
            log("SpiffsFileWriter: Failed to open temp file for writing: %s", tempPath.c_str());
            failed = true;
        }
    }

    ~SpiffsFileWriter() override {
        if (!committed) {
            abort();
        }
    }

    bool isOpen() const {
        return !failed;
    }

    bool write(const char* chunk) override {
        if (failed || !file) {
            return false;
        }
        if (chunk == nullptr) {
            return true;
        }
        size_t expected = strlen(chunk);
        if (expected == 0) {
            return true;
        }
        size_t wrote = file.print(chunk);
        written += wrote;
        if (wrote != expected) {
            log("SpiffsFileWriter: Write incomplete: expected %d, wrote %d bytes to %s",
                static_cast<int>(expected), static_cast<int>(wrote), tempPath.c_str());
            failed = true;
            return false;
        }
        return true;
    }

    bool commit() override {
        if (failed) {
            abort();
            return false;
        }
        if (file) {
            file.flush();
            file.close();
        }

        unsigned int storedSize = 0;
        File check = SPIFFS.open(tempPath.c_str(), "r");
        if (check) {
            storedSize = check.size();
            check.close();
        }
        if (storedSize != written) {
            log("SpiffsFileWriter: Temp file size mismatch for %s: expected %d, stored %d",
                tempPath.c_str(), static_cast<int>(written), static_cast<int>(storedSize));
            SPIFFS.remove(tempPath.c_str());
            committed = true; // nothing left to abort
            failed = true;
            return false;
        }

        // The old file is moved aside rather than removed, so it exists under some name at every
        // instant of the swap (SpiffsFileReplacer).
        if (!replacer.replace(tempPath, targetPath)) {
            SPIFFS.remove(tempPath.c_str());
            committed = true; // nothing left to abort
            failed = true;
            return false;
        }

        committed = true;
        log("SpiffsFileWriter: Wrote %d bytes to file: %s", static_cast<int>(written), targetPath.c_str());
        return true;
    }

    void abort() override {
        if (file) {
            file.close();
        }
        if (SPIFFS.exists(tempPath.c_str())) {
            SPIFFS.remove(tempPath.c_str());
        }
        failed = true;
        committed = true; // aborted once, nothing left for the destructor
        log("SpiffsFileWriter: Write aborted, %s left untouched", targetPath.c_str());
    }

    size_t bytesWritten() const override {
        return written;
    }

private:
    void log(const char* format, ...) const {
        if (logger != nullptr &&
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::SPIFFS_DEBUG))) {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            logger->println(DebugFeatures::SPIFFS_DEBUG, buffer);
        }
    }

    ILogger* logger;
    IDebugSystem* debugSystem;
    SpiffsFileReplacer replacer;
    std::string targetPath;
    std::string tempPath;
    File file;
    size_t written;
    bool failed;
    bool committed;
};
