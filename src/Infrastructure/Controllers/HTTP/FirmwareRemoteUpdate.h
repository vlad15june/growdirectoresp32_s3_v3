#pragma once

#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"
#include <WiFi.h>
#include <Update.h>
#include "esp_task_wdt.h"
#include <cstdarg>
#include <cstdio>

// Firmware update status constants
#define FIRMWARE_UPDATE_STATE_DONE 3
#define FIRMWARE_UPDATE_STATE_ERROR 4
#define FIRMWARE_UPDATE_STATE_HTTP_ERROR 5

class FirmwareRemoteUpdate : public IFirmwareRemoteUpdate {
private:
    IHttpClientAdapter* _httpClient;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    ISettings* _settings;
    IOtaUpdateLog* _otaLog;

    char _log_buf[256];

    void otaWrite(const char* format, ...) {
        if (_otaLog == nullptr) {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _otaLog->write(buffer);
    }

    void log(const char* format, ...) {
        if (_logger != nullptr && _debugSystem != nullptr &&
            _debugSystem->isFeatureEnabled(DebugFeatures::UPDATE_FIRMWARE_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            _logger->println(DebugFeatures::UPDATE_FIRMWARE_DEBUG, buffer);
        }
    }
    
    void putUpdateStatus(uint8_t status) {
        if (_settings != nullptr) {
            _settings->setUpdateStatus(status);
        }
    }
    
    void setFirmwareUpdateCommandResultStatus(const char* status, const char* message) {
        log("FIRMWARE_UPD: Status: %s, Message: %s", status, message);
        // TODO: Implement command result status setting if needed
    }
    
    IHttpClientAdapter* createNewHttpClient() {
        return new HttpClientAdapter();
    }

public:
    FirmwareRemoteUpdate(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr, ISettings* settings = nullptr, IOtaUpdateLog* otaLog = nullptr)
        : _httpClient(createNewHttpClient()),
          _logger(logger),
          _debugSystem(debugSystem),
          _settings(settings),
          _otaLog(otaLog) {
    }
    
    ~FirmwareRemoteUpdate() {
        if (_httpClient != nullptr) {
            _httpClient->end();
            delete _httpClient;
            _httpClient = nullptr;
        }
    }
    
    bool updateFirmwareFromURL(const char* url) override {
        if (_otaLog) {
            _otaLog->startSession(url);
        }
        otaWrite("DOWNLOAD START");

        if (WiFi.status() != WL_CONNECTED) {
            log("FIRMWARE_UPD: WiFi not connected");
            otaWrite("ERROR: WiFi not connected at download time");
            return false;
        }

        log("UPDATE_FIRMWARE_DEBUG: url=%s", url);
        
        // Clean up existing client
        if (_httpClient != nullptr) {
            _httpClient->end();
            delete _httpClient;
        }
        _httpClient = createNewHttpClient();
        
        bool isBegin = _httpClient->begin(url);
        log("UPDATE_FIRMWARE_DEBUG: isBegin=%s", isBegin ? "true" : "false");

        if (!isBegin) {
            log("FIRMWARE_UPD: Failed to begin HTTP connection");
            otaWrite("ERROR: HTTP begin failed");
            return false;
        }

        int httpCode = _httpClient->GET();
        log("UPDATE_FIRMWARE_DEBUG: updateFirmwareFromURL get firmware httpCode=%d", httpCode);
        otaWrite("HTTP begin=%d GET code=%d", isBegin ? 1 : 0, httpCode);

        if (httpCode != 200) {
            String errorMessage = "HTTP error: " + String(httpCode);
            log("FIRMWARE_UPD: HTTP error: %s", errorMessage.c_str());
            otaWrite("ERROR: HTTP code=%d", httpCode);

            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_HTTP_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", errorMessage.c_str());
            return false;
        }
        
        // Use the new streaming methods for proper OTA update
        int contentLength = _httpClient->getSize();
        log("FIRMWARE_UPD: Content length: %d bytes", contentLength);
        otaWrite("contentLength=%d freeSketch=%u", contentLength, (unsigned)ESP.getFreeSketchSpace());

        if (contentLength <= 0) {
            log("FIRMWARE_UPD: Invalid content length");
            otaWrite("ERROR: invalid contentLength=%d", contentLength);
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", "Invalid content length");
            return false;
        }

        bool canBegin = Update.begin(contentLength);
        otaWrite("Update.begin canBegin=%d", canBegin ? 1 : 0);
        if (!canBegin) {
            log("FIRMWARE_UPD: Not enough space to begin OTA");
            otaWrite("ERROR: not enough space, contentLength=%d", contentLength);
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", "Not enough space to begin OTA");
            return false;
        }

        Update.onProgress([this](size_t cur, size_t total) {
            esp_task_wdt_reset();
            if (_otaLog) {
                _otaLog->writeProgress(cur, total);
            }
        });

        // Stream the firmware data
        WiFiClient* client = _httpClient->getStreamPtr();
        if (client == nullptr) {
            log("FIRMWARE_UPD: Failed to get stream pointer");
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", "Failed to get stream pointer");
            return false;
        }
        
        size_t written = Update.writeStream(*client);
        
        if (written != contentLength) {
            log("FIRMWARE_UPD: Written only : %d/%d", written, contentLength);
            otaWrite("ERROR: written only %u/%d", (unsigned)written, contentLength);
            String errorMessage = "Written only : " + String(written) + "/" + String(contentLength);
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", errorMessage.c_str());
            return false;
        }
        
        if (!Update.end()) {
            log("FIRMWARE_UPD: Error Occurred. Error #: %d", Update.getError());
            otaWrite("ERROR: Update.end failed err=%d", Update.getError());
            String errorMessage = "Error Occurred. Error #: " + String(Update.getError());
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", errorMessage.c_str());
            return false;
        }
        
        if (!Update.isFinished()) {
            log("FIRMWARE_UPD: Update not finished. Something went wrong!");
            otaWrite("ERROR: Update.end ok but not finished");
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", "Update not finished. Something went wrong!");
            return false;
        }

        log("FIRMWARE_UPD: Update successfully completed. Rebooting...");
        otaWrite("UPDATE OK -> reboot to new firmware");
        _httpClient->end();
        putUpdateStatus(FIRMWARE_UPDATE_STATE_DONE);

        return true;
    }

    bool isUpdateInProgress() override {
        return Update.isRunning();
    }
};
