#pragma once

#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Enums/FirmwareUpdateStateEnum.h"
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"
#include <WiFi.h>
#include <Update.h>
#include "esp_task_wdt.h"
#include <cstdarg>
#include <cstdio>

// HTTP budget of the OTA download, ms - both for opening the connection and for waiting for
// data. Same 30 s every other HTTP call of the firmware uses (RestApiService.h); left alone,
// HTTPClient keeps its own default of 5 s on both (HTTPCLIENT_DEFAULT_TCP_TIMEOUT), which is
// less than a loaded link needs either to finish the TLS handshake (the session then dies
// with -1) or to deliver the first bytes of a 1.5 MB binary (-11), in both cases with the
// server answering normally.
#define FIRMWARE_UPDATE_HTTP_TIMEOUT_MS 30000

class FirmwareRemoteUpdate : public IFirmwareRemoteUpdate {
private:
    IHttpClientAdapter* _httpClient;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    ISettings* _settings;
    IOtaUpdateLog* _otaLog;

    char _log_buf[256];
    char _lastErrorMessage[128];
    int _lastHttpCode = 0;

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

    // Single place where a failure of the session is recorded: the reason is kept for the
    // caller (it ends up in the command result the backend stores) and written to the OTA
    // log as the `ERROR: <reason>` line it has always been.
    void otaError(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vsnprintf(_lastErrorMessage, sizeof(_lastErrorMessage), format, args);
        va_end(args);

        otaWrite("ERROR: %s", _lastErrorMessage);
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
        _lastErrorMessage[0] = '\0';
    }
    
    ~FirmwareRemoteUpdate() {
        if (_httpClient != nullptr) {
            _httpClient->end();
            delete _httpClient;
            _httpClient = nullptr;
        }
    }
    
    bool updateFirmwareFromURL(const char* url) override {
        _lastErrorMessage[0] = '\0';
        _lastHttpCode = 0;

        if (_otaLog) {
            _otaLog->startSession(url);
        }
        otaWrite("DOWNLOAD START");

        if (WiFi.status() != WL_CONNECTED) {
            log("FIRMWARE_UPD: WiFi not connected");
            otaError("WiFi not connected at download time");
            return false;
        }

        log("UPDATE_FIRMWARE_DEBUG: url=%s", url);
        
        // Clean up existing client
        if (_httpClient != nullptr) {
            _httpClient->end();
            delete _httpClient;
        }
        _httpClient = createNewHttpClient();

        // Must be set before begin(): the client keeps the values until it is told otherwise,
        // and a fresh client starts from the library default of 5 s on both budgets. Both are
        // needed - opening the connection and waiting for data fail separately, with -1 and
        // -11 respectively, and this module has been seen to lose sessions to each of them.
        _httpClient->setConnectTimeout(FIRMWARE_UPDATE_HTTP_TIMEOUT_MS);
        _httpClient->setTimeout(FIRMWARE_UPDATE_HTTP_TIMEOUT_MS);
        otaWrite("HTTP timeout=%u ms connect=%u ms",
                 (unsigned)FIRMWARE_UPDATE_HTTP_TIMEOUT_MS,
                 (unsigned)FIRMWARE_UPDATE_HTTP_TIMEOUT_MS);

        bool isBegin = _httpClient->begin(url);
        log("UPDATE_FIRMWARE_DEBUG: isBegin=%s", isBegin ? "true" : "false");

        if (!isBegin) {
            log("FIRMWARE_UPD: Failed to begin HTTP connection");
            otaError("HTTP begin failed");
            return false;
        }

        int httpCode = _httpClient->GET();
        _lastHttpCode = httpCode;
        log("UPDATE_FIRMWARE_DEBUG: updateFirmwareFromURL get firmware httpCode=%d", httpCode);
        otaWrite("HTTP begin=%d GET code=%d", isBegin ? 1 : 0, httpCode);

        if (httpCode != 200) {
            String errorMessage = "HTTP error: " + String(httpCode);
            log("FIRMWARE_UPD: HTTP error: %s", errorMessage.c_str());
            otaError("HTTP code=%d", httpCode);

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
            otaError("invalid contentLength=%d", contentLength);
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", "Invalid content length");
            return false;
        }

        bool canBegin = Update.begin(contentLength);
        otaWrite("Update.begin canBegin=%d", canBegin ? 1 : 0);
        if (!canBegin) {
            log("FIRMWARE_UPD: Not enough space to begin OTA");
            otaError("not enough space, contentLength=%d", contentLength);
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
            otaError("no stream pointer");
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", "Failed to get stream pointer");
            return false;
        }
        
        size_t written = Update.writeStream(*client);
        
        if (written != contentLength) {
            log("FIRMWARE_UPD: Written only : %d/%d", written, contentLength);
            otaError("written only %u/%d", (unsigned)written, contentLength);
            String errorMessage = "Written only : " + String(written) + "/" + String(contentLength);
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", errorMessage.c_str());
            return false;
        }
        
        if (!Update.end()) {
            log("FIRMWARE_UPD: Error Occurred. Error #: %d", Update.getError());
            otaError("Update.end failed err=%d", Update.getError());
            String errorMessage = "Error Occurred. Error #: " + String(Update.getError());
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", errorMessage.c_str());
            return false;
        }
        
        if (!Update.isFinished()) {
            log("FIRMWARE_UPD: Update not finished. Something went wrong!");
            otaError("Update.end ok but not finished");
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

    const char* getLastErrorMessage() const override {
        return _lastErrorMessage;
    }

    int getLastHttpCode() const override {
        return _lastHttpCode;
    }
};
