#pragma once

#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"

// Forward declarations for external dependencies
const char* firmwareVersion;

class RestAPIService : public IRestAPIService {
private:
    IHttpClientAdapter* _httpClient;
    IModuleConfig* _moduleConfig;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    ISettings* _settings;
    
    char _serverUrl[BUFSIZE];
    char _token[BUFSIZE];
    char _log_buf[128];
    char _urlBuffer[256]; // Buffer for URL construction
    
    void log(const char *format, ...) {
        if (_logger != nullptr && _debugSystem != nullptr && 
            (_debugSystem->isFeatureEnabled(DebugFeatures::NET_DEBUG) || 
             _debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(buffer);
        }
    }

    // Helper method to create a new HTTP client
    IHttpClientAdapter* createNewHttpClient() {
        return new HttpClientAdapter();
    }
    
    // Helper to build URL for API endpoints
    const char* buildUrl(const char* endpoint, const char* moduleUid = nullptr) {
        if (moduleUid == nullptr && _moduleConfig != nullptr) {
            moduleUid = _moduleConfig->getModuleUid();
        }
        
        if (moduleUid != nullptr) {
            snprintf(_urlBuffer, sizeof(_urlBuffer), "%s%s?token=%s&grow_director_uid=%s", 
                     _serverUrl, endpoint, _token, moduleUid);
        } else {
            snprintf(_urlBuffer, sizeof(_urlBuffer), "%s%s?token=%s", 
                     _serverUrl, endpoint, _token);
        }
        
        return _urlBuffer;
    }

public:
    RestAPIService(IModuleConfig* moduleConfig, ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr) 
        : _httpClient(createNewHttpClient()), 
          _moduleConfig(moduleConfig),
          _logger(logger),
          _debugSystem(debugSystem) {
        
        if (_moduleConfig) {
            _settings = _moduleConfig->getSettings();
            if (_settings) {
                _settings->getServerUrl(_serverUrl, BUFSIZE);
                _settings->getToken(_token, BUFSIZE);
                log("RestAPIService: Initialized with server URL: %s", _serverUrl);
            } else {
                log("RestAPIService: Warning - Settings not available");
                strcpy(_serverUrl, "");
                strcpy(_token, "");
            }
        } else {
            log("RestAPIService: Warning - ModuleConfig not provided");
            _settings = nullptr;
            strcpy(_serverUrl, "");
            strcpy(_token, "");
        }
    }

    ~RestAPIService() {
        if (_httpClient != nullptr) {
            _httpClient->end();
            delete _httpClient;
            _httpClient = nullptr;
        }
    }

    bool isModuleOnline() override {
// if (WiFi.status() != WL_CONNECTED) {
        //     return false;
        // }

        if (_settings == nullptr) {
            log("NET: isModuleOnline - Settings not available");
            return false;
        }

        const char* url = buildUrl("/api/v1/HardWare/TestConnection", nullptr);

        _httpClient->end();
        _httpClient->setTimeout(2000);
        _httpClient->begin(url);

        int httpResponseCode = _httpClient->GET();
        _httpClient->end();
        _httpClient->setTimeout(30000);

        if (httpResponseCode > 0) {
            return true;
        } else {
            log("PING_ERROR: HTTP Response code: %d\n", httpResponseCode);
            return false;
        }
    }

    void sendToServerModuleStructure(const ModuleStructure& moduleStructure) override {
        if (_settings == nullptr) {
            log("NET: sendToServerModuleStructure - Settings not available");
            return;
        }

        // Convert module structure to JSON
        std::string structureJson = moduleStructure.toJson();
        log("NET: /api/v1/HardWare/structure -> %s", structureJson.c_str());

        const char* url = buildUrl("/api/v1/HardWare/structure");

        // Ensure we have a clean HTTP client
        if (_httpClient == nullptr) {
            _httpClient = createNewHttpClient();
        } else {
            _httpClient->end();
        }

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200) {
            _httpClient->setTimeout(30000);
            _httpClient->begin(url);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(structureJson.c_str());

            log("NET: /api/v1/HardWare/structure -> response %d, try = %d", httpResponseCode, tries);

            _httpClient->end();
            tries++;
        }

        if (tries >= 3) {
            log("NET: Failed to send structure after 3 attempts, restarting...");
            ESP.restart();
        }
    }

    void sendRuleStateCommand(const char* msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendRuleStateCommand - ModuleConfig or Settings not available");
            return;
        }
        log("NET: Rule state command: %s", msg);

        const char* moduleUid = _moduleConfig->getModuleUid();
        snprintf(_urlBuffer, sizeof(_urlBuffer), "%s/api/v1/HardWare/rules/state?token=%s&grow_director_uid=%s", 
                 _serverUrl, _token, moduleUid);

        _httpClient->end();
        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);
        _httpClient->addHeader("Content-Type", "application/json");

        int httpResponseCode = _httpClient->POST(msg);

        log("NET: HTTP response %d", httpResponseCode);

        // Clean up HTTP client
        _httpClient->end();
    }

    bool sendByUidRuleStateCommand(const char* uid, const char* msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendByUidRuleStateCommand - ModuleConfig or Settings not available");
            return false;
        }

        log("NET: --sendByUidRuleStateCommand--");

        const char* moduleUid = _moduleConfig->getModuleUid();
        snprintf(_urlBuffer, sizeof(_urlBuffer), 
                "%s/api/v1/HardWare/rules/%s/state?token=%s&grow_director_uid=%s", 
                _serverUrl, uid, _token, moduleUid);
        
        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);
        _httpClient->addHeader("Content-Type", "application/json");

        int httpResponseCode = _httpClient->POST(msg);

        log("NET: HTTP response %d", httpResponseCode);

        // Clean up HTTP client
        _httpClient->end();

        return httpResponseCode == 200;
    }

    void sendFirmwareVersion() override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendFirmwareVersion - ModuleConfig or Settings not available");
            return;
        }

        byte tries = 0;
        int httpResponseCode = 0;
        const char* moduleUid = _moduleConfig->getModuleUid();
        
        // Create the JSON message
        char jsonMsg[256];
        snprintf(jsonMsg, sizeof(jsonMsg), "{\"version\": \"%s\",\"growdirectorUid\": \"%s\"}", 
                 firmwareVersion, moduleUid);
        
        // Build the URL
        snprintf(_urlBuffer, sizeof(_urlBuffer), 
                "%s/api/v1/HardWare/firmware?token=%s&grow_director_uid=%s", 
                _serverUrl, _token, moduleUid);

        log("NET: POST %s, body=%s", _urlBuffer, jsonMsg);

        while (tries < 3 && httpResponseCode != 200) {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(jsonMsg);

            log("NET: /api/v1/HardWare/firmware response %d, try = %d", httpResponseCode, tries);

            _httpClient->end();
            tries++;
        }        
    }

    void sendDeviceCommandExecutionResult(const char* msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendDeviceCommandExecutionResult - ModuleConfig or Settings not available");
            return;
        }

        log("NET: Device command result %s\n", msg);

        if (strlen(msg) < 10) {
            log("NET: Device command empty");
            return;
        }

        const char* moduleUid = _moduleConfig->getModuleUid();
        
        // Build the URL
        snprintf(_urlBuffer, sizeof(_urlBuffer), 
                "%s/api/v1/HardWare/devices_command?token=%s&grow_director_uid=%s", 
                _serverUrl, _token, moduleUid);
        
        log("NET: POST %s", _urlBuffer);

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200) {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            // Clean up HTTP client
            _httpClient->end();
            delay(200);
            tries++;
        }
    }

    void sendAnalogInputDeviceCommandExecutionResult(const char* msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendAnalogInputDeviceCommandExecutionResult - ModuleConfig or Settings not available");
            return;
        }

        log("NET: Device command result %s", msg);

        if (strlen(msg) < 10) {
            log("NET: Device command empty");
            return;
        }

        const char* moduleUid = _moduleConfig->getModuleUid();
        
        // Build the URL
        snprintf(_urlBuffer, sizeof(_urlBuffer), 
                "%s/api/v1/HardWare/devices_command?token=%s&grow_director_uid=%s", 
                _serverUrl, _token, moduleUid);
        
        log("NET: POST %s", _urlBuffer);

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200) {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            // Clean up HTTP client
            _httpClient->end();
            
            delay(200);
            tries++;
        }
    }

    bool sendModuleCommandExecutionResult(const char* msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendModuleCommandExecutionResult - ModuleConfig or Settings not available");
            return false;
        }
        log("Module command result %s", msg);

        if (strlen(msg) < 10) {
            log("NET: Module command empty");
            return false;
        }

        const char* moduleUid = _moduleConfig->getModuleUid();
        
        // Build the URL
        snprintf(_urlBuffer, sizeof(_urlBuffer), 
                "%s/api/v1/HardWare/module_command?token=%s&grow_director_uid=%s", 
                _serverUrl, _token, moduleUid);
        
        log("NET: POST %s, Body: %s", _urlBuffer, msg);

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200) {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            // Free resources
            _httpClient->end();
            if (httpResponseCode != 200) {
                delay(1000 * tries);
            }
            tries++;
        }        

        return httpResponseCode == 200;
    }
};
