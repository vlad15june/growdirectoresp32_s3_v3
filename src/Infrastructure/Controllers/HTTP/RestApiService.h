#pragma once

#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Application/DTOs/DiagnosticDto.h"

class RestAPIService : public IRestAPIService
{
private:
    IHttpClientAdapter *_httpClient;
    IModuleConfig *_moduleConfig;
    ILogger *_logger;
    IDebugSystem *_debugSystem;
    ISettings *_settings;
    IJsonConverter *_jsonConverter;

    char _serverUrl[BUFSIZE];
    char _token[BUFSIZE];
    char _log_buf[128];
    char _urlBuffer[256];
    
    // Network diagnostic counters - moved from DiagnosticService
    int _apiSendFails;
    float _apiSendFailRatio;
    int _countOffline;

    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr &&
            (_debugSystem->isFeatureEnabled(DebugFeatures::NET_DEBUG)))
        {
            char buffer[2048];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::NET_DEBUG, buffer);
        }
    }

    IHttpClientAdapter *createNewHttpClient()
    {
        return new HttpClientAdapter();
    }

    const char *buildUrl(const char *endpoint, const char *moduleUid = nullptr)
    {
        if (moduleUid == nullptr && _moduleConfig != nullptr)
        {
            moduleUid = _moduleConfig->getModuleUid();
        }

        if (moduleUid != nullptr)
        {
            snprintf(_urlBuffer, sizeof(_urlBuffer), "%s%s?token=%s&grow_director_uid=%s",
                     _serverUrl, endpoint, _token, moduleUid);
        }
        else
        {
            snprintf(_urlBuffer, sizeof(_urlBuffer), "%s%s?token=%s",
                     _serverUrl, endpoint, _token);
        }

        return _urlBuffer;
    }

    // Helper method to track API failures
    void trackApiSendFailure() {
        _apiSendFails++;
        _apiSendFailRatio++;
        _countOffline++;
        log("NET: API send failure tracked - Fails: %d, Ratio: %.2f, Offline Count: %d", 
            _apiSendFails, _apiSendFailRatio, _countOffline);
    }
    
    // Helper method to track API successes
    void trackApiSendSuccess() {
        if (_apiSendFailRatio > 0) {
            _apiSendFailRatio--;
        }
        _countOffline = 0;
        log("NET: API send success tracked - Ratio: %.2f, Offline Count reset", _apiSendFailRatio);
    }

public:
    RestAPIService(IModuleConfig *moduleConfig, ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr, IJsonConverter *jsonConverter = nullptr)
        : _httpClient(createNewHttpClient()),
          _moduleConfig(moduleConfig),
          _logger(logger),
          _debugSystem(debugSystem),
          _jsonConverter(jsonConverter),
          _apiSendFails(0),
          _apiSendFailRatio(0),
          _countOffline(0)
    {
    }

    ~RestAPIService()
    {
        if (_httpClient != nullptr)
        {
            _httpClient->end();
            delete _httpClient;
            _httpClient = nullptr;
        }
    }

    bool init() override
    {
        if (_moduleConfig)
        {
            _settings = _moduleConfig->getSettings();
            if (_settings)
            {
                _settings->getServerUrl(_serverUrl, BUFSIZE);
                _settings->getToken(_token, BUFSIZE);
                log("RestAPIService: Initialized with server URL: %s", _serverUrl);
                return true;
            }
            else
            {
                log("RestAPIService: Warning - Settings not available");
                strcpy(_serverUrl, "");
                strcpy(_token, "");
                return false;
            }
        }
        else
        {
            log("RestAPIService: Warning - ModuleConfig not provided");
            _settings = nullptr;
            strcpy(_serverUrl, "");
            strcpy(_token, "");
            return false;
        }
    }

    bool isModuleOnline() override
    {
        if (_settings == nullptr)
        {
            log("NET: isModuleOnline - Settings not available");
            return false;
        }

        const char *url = buildUrl("/api/v1/HardWare/TestConnection", nullptr);

        _httpClient->end();
        _httpClient->setTimeout(2000);
        _httpClient->begin(url);

        int httpResponseCode = _httpClient->GET();
        _httpClient->end();
        _httpClient->setTimeout(30000);

        if (httpResponseCode > 0)
        {
            return true;
        }
        else
        {
            log("PING_ERROR: HTTP Response code: %d\n", httpResponseCode);
            return false;
        }
    }

    void sendToServerModuleStructure(const ModuleStructure &moduleStructure) override
    {
        if (init() == false)
        {
            log("NET: sendToServerModuleStructure - Initialization failed");
            return;
        }

        std::string structureJson;
        if (_jsonConverter)
        {
            structureJson = _jsonConverter->moduleStructureToJson(moduleStructure);
        }
        else
        {
            structureJson = "{}";
        }
        log("NET: /api/v1/HardWare/structure -> %s", structureJson.c_str());

        const char *url = buildUrl("/api/v1/HardWare/structure");
        log("NET: Using URL for structure upload: %s", url);

        if (_httpClient == nullptr)
        {
            _httpClient = createNewHttpClient();
        }
        else
        {
            _httpClient->end();
        }

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(url);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(structureJson.c_str());

            log("NET: /api/v1/HardWare/structure -> response %d, try = %d", httpResponseCode, tries);

            _httpClient->end();
            tries++;
        }

        if (httpResponseCode == 200) {
            trackApiSendSuccess();
        } else {
            trackApiSendFailure();
            
            if (tries >= 3)
            {
                log("NET: Failed to send structure after 3 attempts, restarting...");
                ESP.restart();
            }
        }
    }

    void sendRuleStateCommand(const char *msg) override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendRuleStateCommand - ModuleConfig or Settings not available");
            return;
        }
        log("NET: Rule state command: %s", msg);

        const char *moduleUid = _moduleConfig->getModuleUid();
        snprintf(_urlBuffer, sizeof(_urlBuffer), "%s/api/v1/HardWare/rules/state?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        _httpClient->end();
        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);
        _httpClient->addHeader("Content-Type", "application/json");

        int httpResponseCode = _httpClient->POST(msg);

        log("NET: HTTP response %d", httpResponseCode);

        _httpClient->end();
        
        if (httpResponseCode == 200) {
            trackApiSendSuccess();
        } else {
            trackApiSendFailure();
        }
    }

    bool sendByUidRuleStateCommand(const char *uid, const char *msg) override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendByUidRuleStateCommand - ModuleConfig or Settings not available");
            return false;
        }

        log("NET: --sendByUidRuleStateCommand--");

        const char *moduleUid = _moduleConfig->getModuleUid();
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/rules/%s/state?token=%s&grow_director_uid=%s",
                 _serverUrl, uid, _token, moduleUid);

        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);
        _httpClient->addHeader("Content-Type", "application/json");

        int httpResponseCode = _httpClient->POST(msg);

        log("NET: HTTP response %d", httpResponseCode);

        _httpClient->end();

        if (httpResponseCode == 200) {
            trackApiSendSuccess();
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }

    void sendFirmwareVersion() override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendFirmwareVersion - ModuleConfig or Settings not available");
            return;
        }

        byte tries = 0;
        int httpResponseCode = 0;
        const char *moduleUid = _moduleConfig->getModuleUid();
        const char *firmwareVersion = _moduleConfig->getFirmwareVersion();

        char jsonMsg[256];
        snprintf(jsonMsg, sizeof(jsonMsg), "{\"version\": \"%s\",\"growdirectorUid\": \"%s\"}",
                 firmwareVersion, moduleUid);

        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/firmware?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: POST %s, body=%s", _urlBuffer, jsonMsg);

        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(jsonMsg);

            log("NET: /api/v1/HardWare/firmware response %d, try = %d", httpResponseCode, tries);

            _httpClient->end();
            tries++;
        }
        
        if (httpResponseCode == 200) {
            trackApiSendSuccess();
        } else {
            trackApiSendFailure();
        }
    }

    void sendDeviceCommandExecutionResult(const char *msg) override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendDeviceCommandExecutionResult - ModuleConfig or Settings not available");
            return;
        }

        log("NET: Device command result %s\n", msg);

        if (strlen(msg) < 10)
        {
            log("NET: Device command empty");
            return;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();

        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/devices_command?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: POST %s", _urlBuffer);

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            _httpClient->end();
            delay(200);
            tries++;
        }
        
        if (httpResponseCode == 200) {
            trackApiSendSuccess();
        } else {
            trackApiSendFailure();
        }
    }

    void sendAnalogInputDeviceCommandExecutionResult(const char *msg) override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendAnalogInputDeviceCommandExecutionResult - ModuleConfig or Settings not available");
            return;
        }

        log("NET: Device command result %s", msg);

        if (strlen(msg) < 10)
        {
            log("NET: Device command empty");
            return;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();

        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/devices_command?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: POST %s", _urlBuffer);

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            _httpClient->end();

            delay(200);
            tries++;
        }
        
        if (httpResponseCode == 200) {
            trackApiSendSuccess();
        } else {
            trackApiSendFailure();
        }
    }

    bool sendModuleCommandExecutionResult(const char *msg) override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendModuleCommandExecutionResult - ModuleConfig or Settings not available");
            return false;
        }
        log("Module command result %s", msg);

        if (strlen(msg) < 10)
        {
            log("NET: Module command empty");
            return false;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();

        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/module_command?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: POST %s, Body: %s", _urlBuffer, msg);

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(msg);

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            _httpClient->end();
            if (httpResponseCode != 200)
            {
                delay(1000 * tries);
            }
            tries++;
        }

        if (httpResponseCode == 200) {
            trackApiSendSuccess();
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }

    bool sendDiagnosticData(const DiagnosticDto& diagnosticData) override {
        if (_jsonConverter == nullptr) {
            log("NET: sendDiagnosticData - JSON converter not available");
            return false;
        }

        if (_settings == nullptr || _moduleConfig == nullptr) {
            log("NET: sendDiagnosticData - Settings or ModuleConfig not available");
            return false;
        }

        if (!isModuleOnline()) {
            log("NET: sendDiagnosticData - Module is offline");
            return false;
        }

        std::string diagnosticJson = _jsonConverter->diagnosticToJson(diagnosticData);
        log("NET: /api/v1/HardWare/diagnostic -> %s", diagnosticJson.c_str());

        const char* moduleUid = _moduleConfig->getModuleUid();
        snprintf(_urlBuffer, sizeof(_urlBuffer), 
                "%s/api/v1/HardWare/diagnostic?token=%s&grow_director_uid=%s",
                _serverUrl, _token, moduleUid);

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200) {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(diagnosticJson.c_str());
            
            log("NET: /api/v1/HardWare/diagnostic -> response %d, try = %d", httpResponseCode, tries);
            
            _httpClient->end();
            if (httpResponseCode != 200) {
                delay(200);
            }
            tries++;
        }

        if (httpResponseCode == 200) {
            trackApiSendSuccess();
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }
    
    bool sendModuleUpdateData(const UpdateDto& updateData) override {
        if (_jsonConverter == nullptr) {
            log("NET: sendModuleUpdateData - JSON converter not available");
            return false;
        }

        if (_settings == nullptr || _moduleConfig == nullptr) {
            log("NET: sendModuleUpdateData - Settings or ModuleConfig not available");
            return false;
        }

        std::string updateJson = _jsonConverter->updateToJson(updateData);
        log("NET: /api/v1/HardWare/update -> %s", updateJson.c_str());

        const char* moduleUid = _moduleConfig->getModuleUid();
        snprintf(_urlBuffer, sizeof(_urlBuffer), 
                "%s/api/v1/HardWare/update?token=%s&grow_director_uid=%s",
                _serverUrl, _token, moduleUid);

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200) {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(updateJson.c_str());
            
            log("NET: /api/v1/HardWare/update -> response %d, try = %d", httpResponseCode, tries);
            
            _httpClient->end();
            if (httpResponseCode != 200) {
                delay(200);
            }
            tries++;
        }

        if (httpResponseCode == 200) {
            trackApiSendSuccess();
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }

    // Implementations of network diagnostic counter methods
    int getApiSendFails() const override { return _apiSendFails; }
    void resetApiSendFails() override { _apiSendFails = 0; }
    
    float getApiSendFailRatio() const override { return _apiSendFailRatio; }
    void resetApiSendFailRatio() override { _apiSendFailRatio = 0.0f; }
    
    int getCountOffline() const override { return _countOffline; }
    void resetCountOffline() override { _countOffline = 0; }
};
