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
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Core/Domain/Data/Entities/Rule.h"

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
    RestAPIService(IModuleConfig *moduleConfig, ISettings *settings, ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr, IJsonConverter *jsonConverter = nullptr)
        : _httpClient(createNewHttpClient()),
          _moduleConfig(moduleConfig),
          _settings(settings),
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
        if (_moduleConfig && _settings)
        {
            _settings->getServerUrl(_serverUrl, BUFSIZE);
            _settings->getToken(_token, BUFSIZE);
            log("RestAPIService: Initialized with server URL: %s", _serverUrl);
            return true;
        }
        else
        {
            log("RestAPIService: Warning - ModuleConfig or Settings not available");
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

    bool sendToServerModuleStructure(const ModuleStructure &moduleStructure) override
    {
        if (init() == false)
        {
            log("NET: sendToServerModuleStructure - Initialization failed");
            return false;
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
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }

    bool sendRuleStateCommand(const char *msg) override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendRuleStateCommand - ModuleConfig or Settings not available");
            return false;
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
            return true;
        } else {
            trackApiSendFailure();
            return false;
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

    bool sendFirmwareVersion() override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendFirmwareVersion - ModuleConfig or Settings not available");
            return false;
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
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }

    bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& commandUpdate) override
    {
        if (_jsonConverter == nullptr) {
            log("NET: sendDeviceCommandExecutionResult - JSON converter not available");
            return false;
        }

        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendDeviceCommandExecutionResult - ModuleConfig or Settings not available");
            return false;
        }

        // Convert DeviceCommandUpdateDto to JSON
        std::string jsonMsg = _jsonConverter->deviceCommandUpdateToJson(commandUpdate);
        log("NET: Device command result %s\n", jsonMsg.c_str());

        if (jsonMsg.length() < 10)
        {
            log("NET: Device command empty");
            return false;
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

            httpResponseCode = _httpClient->POST(jsonMsg.c_str());

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            if (httpResponseCode != 200)
            {
                std::string errorBody = _httpClient->getString();
                log("NET: HTTP Error body: %s", errorBody.c_str());
                delay(200 * tries);
            }
            _httpClient->end();
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

    bool sendAnalogInputDeviceCommandExecutionResult(const char *msg) override
    {
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendAnalogInputDeviceCommandExecutionResult - ModuleConfig or Settings not available");
            return false;
        }

        log("NET: Device command result %s", msg);

        if (strlen(msg) < 10)
        {
            log("NET: Device command empty");
            return false;
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
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }

    bool sendModuleCommandExecutionResult(const ModuleCommandUpdateDto& commandUpdate) override
    {
        if (_jsonConverter == nullptr) {
            log("NET: sendModuleCommandExecutionResult - JSON converter not available");
            return false;
        }

        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendModuleCommandExecutionResult - ModuleConfig or Settings not available");
            return false;
        }

        // Convert ModuleCommandUpdateDto to JSON
        std::string jsonMsg = _jsonConverter->moduleCommandUpdateToJson(commandUpdate);
        log("NET: Module command result %s", jsonMsg.c_str());

        if (jsonMsg.length() < 10)
        {
            log("NET: Module command empty");
            return false;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();

        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/module_command?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: POST %s, Body: %s", _urlBuffer, jsonMsg.c_str());

        byte tries = 0;
        int httpResponseCode = 0;
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            _httpClient->addHeader("Content-Type", "application/json");

            httpResponseCode = _httpClient->POST(jsonMsg.c_str());

            log("NET: HTTP response %d, try = %d", httpResponseCode, tries);

            _httpClient->end();
            if (httpResponseCode != 200)
            {
                std::string errorBody = _httpClient->getString();
                log("NET: HTTP Error body: %s", errorBody.c_str());
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
                std::string errorBody = _httpClient->getString();
                log("NET: HTTP Error body: %s", errorBody.c_str());
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

    std::vector<DeviceCommandDto> getDeviceCommand() override
    {
        std::vector<DeviceCommandDto> commands;
        
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: getDeviceCommand - ModuleConfig or Settings not available");
            return commands;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/devices_command?token=%s&grow_director_uid=%s&status=new",
                 _serverUrl, _token, moduleUid);

        log("NET: GET %s", _urlBuffer);

        byte tries = 0;
        int httpResponseCode = 0;
        std::string responseBody;
        
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            
            httpResponseCode = _httpClient->GET();
            
            if (httpResponseCode > 0)
            {
                log("NET: HTTP Response code: %d", httpResponseCode);
                
                // Get response body
                responseBody = _httpClient->getString();
                log("NET: HTTP Response: %s", responseBody.c_str());
            }
            else
            {
                log("NET: HTTP Response Error code: %d", httpResponseCode);
            }
            
            _httpClient->end();
            delay(200);
            tries++;
        }
        
        if (httpResponseCode == 200 && !responseBody.empty() && _jsonConverter != nullptr)
        {
            // Parse JSON response to array of DeviceCommandDto
            if (_jsonConverter->deviceCommandsFromJson(responseBody, commands))
            {
                log("NET: Device commands parsed successfully - count: %d", commands.size());
                trackApiSendSuccess();
            }
            else
            {
                log("NET: Failed to parse device commands JSON");
                commands.clear();
            }
        }
        else
        {
            if (httpResponseCode != 200)
                trackApiSendFailure();
        }
        
        return commands;
    }

    std::vector<ModuleCommandDto> getModuleCommand() override
    {
        std::vector<ModuleCommandDto> commands;
        
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: getModuleCommand - ModuleConfig or Settings not available");
            return commands;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/module_command?token=%s&grow_director_uid=%s&statuses=New",
                 _serverUrl, _token, moduleUid);

        log("NET: GET %s", _urlBuffer);

        byte tries = 0;
        int httpResponseCode = 0;
        std::string responseBody;
        
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            
            httpResponseCode = _httpClient->GET();
            
            if (httpResponseCode > 0)
            {
                log("NET: HTTP Response code: %d", httpResponseCode);
                
                // Get response body
                responseBody = _httpClient->getString();
                log("NET: HTTP Response: %s", responseBody.c_str());
            }
            else
            {
                log("NET: HTTP Response Error code: %d", httpResponseCode);
            }
            
            _httpClient->end();
            delay(200);
            tries++;
        }
        
        if (httpResponseCode == 200 && !responseBody.empty() && _jsonConverter != nullptr)
        {
            // Parse JSON response to array of ModuleCommandDto
            if (_jsonConverter->moduleCommandsFromJson(responseBody, commands))
            {
                log("NET: Module commands parsed successfully - count: %d", commands.size());
                trackApiSendSuccess();
            }
            else
            {
                log("NET: Failed to parse module commands JSON");
                commands.clear();
            }
        }
        else
        {
            if (httpResponseCode != 200)
                trackApiSendFailure();
        }
        
        return commands;
    }

    std::vector<Rule> getRuleList() override
    {
        std::vector<Rule> rules;
        
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: getRuleList - ModuleConfig or Settings not available");
            return rules;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/rulelist?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: GET %s", _urlBuffer);

        byte tries = 0;
        int httpResponseCode = 0;
        std::string responseBody;
        
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            
            httpResponseCode = _httpClient->GET();
            
            if (httpResponseCode > 0)
            {
                log("NET: HTTP Response code: %d", httpResponseCode);
                
                // Get response body
                responseBody = _httpClient->getString();
                log("NET: HTTP Response: %s", responseBody.c_str());
            }
            else
            {
                log("NET: HTTP Response Error code: %d", httpResponseCode);
            }
            
            _httpClient->end();
            delay(200);
            tries++;
        }
        
        if (httpResponseCode == 200 && !responseBody.empty() && _jsonConverter != nullptr)
        {
            // Parse JSON response to array of Rule
            if (_jsonConverter->rulesFromJson(responseBody, rules))
            {
                log("NET: Rules parsed successfully - count: %d", rules.size());
                trackApiSendSuccess();
            }
            else
            {
                log("NET: Failed to parse rules JSON");
                rules.clear();
            }
        }
        else
        {
            if (httpResponseCode != 200)
                trackApiSendFailure();
        }
        
        return rules;
    }

    std::vector<SensorType> getSensorTypesList() override
    {
        std::vector<SensorType> sensorTypes;
        
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: getSensorTypesList - ModuleConfig or Settings not available");
            return sensorTypes;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/Hardware/sensor_models?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: GET %s", _urlBuffer);

        byte tries = 0;
        int httpResponseCode = 0;
        std::string responseBody;
        
        while (tries < 3 && httpResponseCode != 200)
        {
            _httpClient->setTimeout(30000);
            _httpClient->begin(_urlBuffer);
            
            httpResponseCode = _httpClient->GET();
            
            if (httpResponseCode > 0)
            {
                log("NET: HTTP Response code: %d", httpResponseCode);
                
                // Get response body
                responseBody = _httpClient->getString();
                log("NET: HTTP Response: %s", responseBody.c_str());
            }
            else
            {
                log("NET: HTTP Response Error code: %d", httpResponseCode);
            }
            
            _httpClient->end();
            delay(200);
            tries++;
        }
        
        if (httpResponseCode == 200 && !responseBody.empty() && _jsonConverter != nullptr)
        {
            // Parse JSON response to array of SensorType
            if (_jsonConverter->sensorTypesFromJson(responseBody, sensorTypes))
            {
                log("NET: Sensor types parsed successfully - count: %d", sensorTypes.size());
                trackApiSendSuccess();
            }
            else
            {
                log("NET: Failed to parse sensor types JSON");
                sensorTypes.clear();
            }
        }
        else
        {
            if (httpResponseCode != 200)
                trackApiSendFailure();
        }
        
        return sensorTypes;
    }

    // Implementations of network diagnostic counter methods
    int getApiSendFails() const override { return _apiSendFails; }
    void resetApiSendFails() override { _apiSendFails = 0; }
    
    float getApiSendFailRatio() const override { return _apiSendFailRatio; }
    void resetApiSendFailRatio() override { _apiSendFailRatio = 0.0f; }
    
    int getCountOffline() const override { return _countOffline; }
    void resetCountOffline() override { _countOffline = 0; }
};
