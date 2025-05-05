#pragma once

#include <ESP32Ping.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"

// Forward declarations for external dependencies
extern const char* firmwareVersion;
extern String growDirectorStructureStr;


class RestAPIService : public IRestAPIService {
private:
    HTTPClient* _http;
    IModuleConfig* _moduleConfig;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    ISettings* _settings;
    
    char _serverUrl[BUFSIZE];
    char _token[BUFSIZE];
    char _log_buf[128];
    
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

public:
    RestAPIService(IModuleConfig* moduleConfig, ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr) 
        : _http(new HTTPClient()), 
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
        if (_http != nullptr) {
            _http->end();
            delete _http;
            _http = nullptr;
        }
    }

    bool isModuleOnline() override {
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }

        if (_settings == nullptr) {
            log("NET: isModuleOnline - Settings not available");
            return false;
        }

        // Your Domain name with URL path or IP address with path
        String serverName = String(_serverUrl) + "/api/v1/HardWare/TestConnection";

        _http->end();
        _http->setTimeout(2000);
        _http->begin(serverName);

        int httpResponseCode = _http->GET();
        _http->end();
        _http->setTimeout(30000);

        if (httpResponseCode > 0) {
            return true;
        } else {
            log("PING_ERROR: HTTP Response code: %d\n", httpResponseCode);
            

            if (_http != NULL) {
                _http->end();
                delete _http;
            }
            _http = new HTTPClient();

            return false;
        }
    }

    void sendToServerModuleStructure() override {
        if (_settings == nullptr) {
            log("NET: sendToServerGrowDirectorStructure - Settings not available");
            return;
        }

        log("NET: /api/v1/HardWare/structure -> %s", growDirectorStructureStr.c_str());


        String serverName = String(_serverUrl) + "/api/v1/HardWare/structure" + "?token=" + _token;

        if (_http != NULL) {
            _http->end();
        } else {
            _http = new HTTPClient();
        }

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200) {
            _http->setTimeout(30000);
            _http->begin(serverName);
            _http->addHeader("Content-Type", "application/json");

            httpResponseCode = _http->POST(growDirectorStructureStr);

            log("NET: /api/v1/HardWare/structure -> response %d, try = %d", httpResponseCode, tries);

            _http->end();
            tries++;
        }

        if (tries >= 3) {
            log("NET: Failed to send structure after 3 attempts, restarting...");
            ESP.restart();
        }

        if (_http != NULL) {
            _http->end();
            delete _http;
        }
        _http = new HTTPClient();
    }

    void sendRuleStateCommand(String msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendRuleStateCommand - ModuleConfig or Settings not available");
            return;
        }

        log("NET: --sendRuleStateCommand--");

        if (_debugSystem && 
            (_debugSystem->isFeatureEnabled(DebugFeatures::NET_DEBUG) || 
             _debugSystem->isFeatureEnabled(DebugFeatures::RULE_SYNC_DEBUG))) {
            log("NET: Rule state command: %s", msg.c_str());
        }

        String moduleUid = _moduleConfig->getModuleUid();
        String serverName = String(_serverUrl) + "/api/v1/HardWare/rules/state" + "?token=" + _token + "&grow_director_uid=" + moduleUid;

        _http->end();
        _http->setTimeout(30000);
        _http->begin(serverName);
        _http->addHeader("Content-Type", "application/json");

        int httpResponseCode = _http->POST(msg);

        log("NET: HTTP response %d", httpResponseCode);


        // Free resources
        _http->end();
        delete _http;
        _http = new HTTPClient();
    }

    bool sendByUidRuleStateCommand(String uid, String msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendByUidRuleStateCommand - ModuleConfig or Settings not available");
            return false;
        }

        log("NET: --sendByUidRuleStateCommand--");

        String moduleUid = _moduleConfig->getModuleUid();
        String serverName = String(_serverUrl) + "/api/v1/HardWare/rules/" + uid + "/state" + 
                            "?token=" + _token + "&grow_director_uid=" + moduleUid;

        _http->end();
        delete _http;
        _http = new HTTPClient();
        _http->setTimeout(30000);
        _http->begin(serverName);
        _http->addHeader("Content-Type", "application/json");

        int httpResponseCode = _http->POST(msg);

        log("NET: HTTP response %d", httpResponseCode);

        // Free resources
        _http->end();
        delete _http;
        _http = new HTTPClient();

        return httpResponseCode == 200;
    }

    void sendFirmwareVersion() override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendFirmwareVersion - ModuleConfig or Settings not available");
            return;
        }

        log("NET: --sendFirmwareVersion--");

        byte tries = 0;
        int httpResponseCode = 0;
        String moduleUid = _moduleConfig->getModuleUid();
        String msg = "{\"version\": \"" + String(firmwareVersion) + "\",\"growdirectorUid\": \"" + moduleUid + "\"}";
        String serverName = String(_serverUrl) + "/api/v1/HardWare/firmware" + 
                            "?token=" + _token + "&grow_director_uid=" + moduleUid;

        log("NET: POST %s, body=%s", serverName.c_str(), msg.c_str());

        while (tries < 3 && httpResponseCode != 200) {
            _http->setTimeout(30000);
            _http->begin(serverName);
            _http->addHeader("Content-Type", "application/json");

            httpResponseCode = _http->POST(msg);

            log("NET: /api/v1/HardWare/firmware response %d, try = %d", httpResponseCode, tries);

            _http->end();
            tries++;
        }
        
        // Free resources
        if (_http != NULL) {
            _http->end();
            delete _http;
        }
        _http = new HTTPClient();
    }

    void sendDeviceCommandExecutionResult(String msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendDeviceCommandExecutionResult - ModuleConfig or Settings not available");
            return;
        }

        log("NET: Device command result %s\n", msg.c_str());

        if (msg.length() < 10) {
            log("NET: Device command empty");
            return;
        }

        String moduleUid = _moduleConfig->getModuleUid();
        String strMqtt = msg;
        String mqttTopic = "hardware/devices_command/post/" + moduleUid;

        byte tries = 0;
        int httpResponseCode = 0;
        String serverName = String(_serverUrl) + "/api/v1/HardWare/devices_command" + 
                            "?token=" + _token + "&grow_director_uid=" + moduleUid;
        
        log("NET: POST %s", serverName.c_str());

        while (tries < 3 && httpResponseCode != 200) {
            _http->setTimeout(30000);
            _http->begin(serverName);
            _http->addHeader("Content-Type", "application/json");

            httpResponseCode = _http->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            // Free resources
            _http->end();
            delete _http;
            _http = new HTTPClient();

            delay(200);
            tries++;
        }
    }

    void sendAnalogInputDeviceCommandExecutionResult(String msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendAnalogInputDeviceCommandExecutionResult - ModuleConfig or Settings not available");
            return;
        }

        log("NET: Device command result %s", msg.c_str());

        if (msg.length() < 10) {
            log("NET: Device command empty");
            return;
        }

        String moduleUid = _moduleConfig->getModuleUid();
        byte tries = 0;
        int httpResponseCode = 0;
        String serverName = String(_serverUrl) + "/api/v1/HardWare/devices_command" + 
                            "?token=" + _token + "&grow_director_uid=" + moduleUid;
        
        log("NET: POST %s", serverName.c_str());

        while (tries < 3 && httpResponseCode != 200) {
            _http->setTimeout(30000);
            _http->begin(serverName);
            _http->addHeader("Content-Type", "application/json");

            httpResponseCode = _http->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            // Free resources
            _http->end();
            delete _http;
            _http = new HTTPClient();
            delay(200);
            tries++;
        }
    }

    bool sendModuleCommandExecutionResult(String msg) override {
        if (_moduleConfig == nullptr || _settings == nullptr) {
            log("NET: sendModuleCommandExecutionResult - ModuleConfig or Settings not available");
            return false;
        }
        log("Module command result %s", msg.c_str());

        if (msg.length() < 10) {
            log("NET: Module command empty");
            return false;
        }

        String moduleUid = _moduleConfig->getModuleUid();
        String strMqtt = msg;
        String mqttTopic = "hardware/module_command/post/" + moduleUid;

        byte tries = 0;
        int httpResponseCode = 0;
        String serverName = String(_serverUrl) + "/api/v1/HardWare/module_command" + 
                            "?token=" + _token + "&grow_director_uid=" + moduleUid;
        
        log("NET: POST %s, Body: %s", serverName.c_str(), msg.c_str());

        while (tries < 3 && httpResponseCode != 200) {
            _http->setTimeout(30000);
            _http->begin(serverName);
            _http->addHeader("Content-Type", "application/json");

            httpResponseCode = _http->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            // Free resources
            _http->end();
            if (httpResponseCode != 200) {
                delay(1000 * tries);
            }
            tries++;
        }
        delete _http;
        _http = new HTTPClient();

        return httpResponseCode == 200;
    }
};
