#pragma once

#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Infrastructure/Adapters/Http/LoggableUrl.h"
#include "Infrastructure/Adapters/Json/RuleStreamReader.h"
#include "Infrastructure/Adapters/Json/SensorTypeStreamReader.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

class RestAPIService : public IRestAPIService
{
private:
    // One HTTPClient, one URL buffer and one token buffer are shared by every method below, so
    // two tasks calling this object at the same time would interleave two requests on one
    // connection and overwrite each other's URL. Recursive because sendDiagnosticData() calls
    // isModuleOnline(), which locks again.
    //
    // The outbox was meant to retire this lock by making its network task the only caller. It has
    // not, and the callers that remain are worth writing down rather than rediscovering:
    //
    //  - the network command task, which runs the slow parts of the complex module commands:
    //    getSensorTypesList() for ScanSensors and queryModuleDeletion() for DeleteModule, both
    //    from ModuleCommandExecutionUseCase::executeNetworkWork(). It runs alongside the outbox's
    //    own network task, and this lock is what keeps those two off one connection.
    //  - ModuleSetupService and the startup/OTA path, which run before the loop is pacing
    //    anything but share this object with it afterwards.
    //
    // The control loop is not one of them. CommandService is driven from
    // MqttManager::loop(), but its fetches (getDeviceCommand(), getModuleCommand()) and all five
    // reports go through the outbox, the slow commands are submitted to the bus and network
    // command tasks, and what it still executes inline - BlinkingStart/Stop, Reboot,
    // DeleteSensors, AddWifi - writes state and drives pins without ever calling this object.
    // RepublishModuleStructureUseCase was the last loop-side caller and submits to the outbox now.
    //
    // The lock stays: three tasks still share this object - the outbox's network task, the command
    // network task and the startup path - and an unguarded overlap here is a corrupted URL on a
    // live connection, not a slowdown.
    std::recursive_mutex _apiMutex;

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

            IDebugFeature* debugFeature = _debugSystem->getFeature(DebugFeatures::NET_DEBUG);
            if (debugFeature)
            {
                _logger->println(DebugFeatures::NET_DEBUG, buffer, debugFeature->getLogChannel());
            }
            else
            {
                _logger->println(DebugFeatures::NET_DEBUG, buffer);
            }
        }
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

    // One line per fetch that brings a body back: which endpoint, what the server said, and how
    // many bytes of answer the module took in. The size is the point - it is what the fetch weighs
    // on the heap, and the one figure that tells a routine answer from the one that did not fit.
    // Zero when the body was not read: the transport failed, or the status was not one to read.
    // The only line on these paths that carries the size, and the only one that carries both. The
    // status on its own is said once more when the transport failed: by the "Error code" line that
    // those paths write just before this one.
    void logFetch(const char *endpoint, int httpResponseCode, size_t bodyBytes)
    {
        log("NET: GET %s -> response %d, body %lu bytes", endpoint, httpResponseCode,
            (unsigned long)bodyBytes);
    }

public:
    // The deletion query's transport budget, named because the watchdog threshold counts it:
    // the three attempts run *inside* one _apiMutex hold, so one hold is
    // DELETION_QUERY_MAX_ATTEMPTS x (connect + read + pause) - the longest hold this transport
    // makes, and the number every waiter behind the lock inherits into its own feed gap. The
    // watchdog test derives its model of this hold from these constants, so changing the
    // budget breaks a test instead of silently outdating the threshold table in constants.h.
    static constexpr uint8_t DELETION_QUERY_MAX_ATTEMPTS = 3;
    static constexpr uint32_t DELETION_QUERY_READ_TIMEOUT_MS = 10000;
    static constexpr uint32_t DELETION_QUERY_RETRY_PAUSE_MS = 200;

    // What HTTPClient spends opening the connection when setConnectTimeout() is not called -
    // this transport never calls it, so every request here pays the framework default
    // (HTTPCLIENT_DEFAULT_TCP_TIMEOUT) on top of its read timeout. Named for the same reason
    // as the budget above: the watchdog table counts it per attempt.
    static constexpr uint32_t DEFAULT_CONNECT_TIMEOUT_MS = 5000;

    // The HTTP client is handed in, not built here. A `new HttpClientAdapter()` on this line
    // would make every method in this file device-only: naming the concrete adapter pulls in
    // <HTTPClient.h> and <WiFiClientSecure.h>, the file could not be compiled on the host, and
    // no test could execute a line of it. Everything else here speaks to IHttpClientAdapter.
    //
    // Ownership: borrowed, never owned. The adapter is created by whoever creates this service -
    // globals.h on the device, the test's stand-in on the host - and has to outlive it. This
    // class closes the connection in its destructor but does not free the adapter.
    RestAPIService(IHttpClientAdapter *httpClient, IModuleConfig *moduleConfig, ISettings *settings, ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr, IJsonConverter *jsonConverter = nullptr)
        : _httpClient(httpClient),
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
        // Closes the connection, does not free the adapter - see the constructor on ownership.
        if (_httpClient != nullptr)
        {
            _httpClient->end();
        }
    }

    bool init() override
    {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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
            log("PING_ERROR: HTTP Response code: %d", httpResponseCode);
            return false;
        }
    }

    bool sendToServerModuleStructure(const ModuleStructure &moduleStructure) override
    {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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
        log("NET: Using URL for structure upload: %.*s", HttpLog::loggablePathLength(url), url);

        if (_httpClient == nullptr)
        {
            log("NET: sendToServerModuleStructure - HTTP client not available");
            return false;
        }
        _httpClient->end();

        // One attempt. The retry budget belongs to whoever owns the call, and that is the outbox
        // for every structure this firmware sends. Three attempts here would have been multiplied
        // by the outbox's four - twelve 30 s POSTs for one structure - and, worse, they would have
        // made a single outbox attempt 90 s long while it holds _apiMutex, which is the figure
        // runNetworkTask() has to stay under the task watchdog with.
        _httpClient->setTimeout(30000);
        _httpClient->begin(url);
        _httpClient->addHeader("Content-Type", "application/json");

        int httpResponseCode = _httpClient->POST(structureJson.c_str());

        log("NET: /api/v1/HardWare/structure -> response %d", httpResponseCode);

        _httpClient->end();

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
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendByUidRuleStateCommand - ModuleConfig or Settings not available");
            return false;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/rules/%s/state?token=%s&grow_director_uid=%s",
                 _serverUrl, uid, _token, moduleUid);

        log("NET: /api/v1/HardWare/rules/%s/state -> %s", uid, msg);

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
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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

        log("NET: POST %.*s, body=%s", HttpLog::loggablePathLength(_urlBuffer), _urlBuffer, jsonMsg);

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
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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

        log("NET: POST %.*s", HttpLog::loggablePathLength(_urlBuffer), _urlBuffer);

        // One attempt, 30 s. The retry budget lives in the outbox dispatcher, the only caller
        // of this method: retrying belongs with the task that owns the network, not duplicated
        // per endpoint.
        int httpResponseCode = 0;

        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);
        _httpClient->addHeader("Content-Type", "application/json");

        httpResponseCode = _httpClient->POST(jsonMsg.c_str());

        log("NET: HTTP response %d", httpResponseCode);

        if (httpResponseCode != 200)
        {
            std::string errorBody = _httpClient->getString();
            log("NET: HTTP Error body: %s", errorBody.c_str());
        }
        _httpClient->end();

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
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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

        log("NET: POST %.*s", HttpLog::loggablePathLength(_urlBuffer), _urlBuffer);

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

    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& commandUpdates) override
    {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
        if (_jsonConverter == nullptr) {
            log("NET: sendModuleCommandExecutionResult - JSON converter not available");
            return false;
        }

        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: sendModuleCommandExecutionResult - ModuleConfig or Settings not available");
            return false;
        }

        // Convert array of ModuleCommandUpdateDto to JSON array
        std::string jsonMsg = _jsonConverter->moduleCommandUpdatesToJson(commandUpdates);
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

        log("NET: POST %.*s, Body: %s", HttpLog::loggablePathLength(_urlBuffer), _urlBuffer, jsonMsg.c_str());

        // One attempt, 30 s - the retry budget lives in the outbox dispatcher, the only caller.
        int httpResponseCode = 0;

        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);
        _httpClient->addHeader("Content-Type", "application/json");

        httpResponseCode = _httpClient->POST(jsonMsg.c_str());

        log("NET: HTTP response %d", httpResponseCode);

        _httpClient->end();
        if (httpResponseCode != 200)
        {
            std::string errorBody = _httpClient->getString();
            log("NET: HTTP Error body: %s", errorBody.c_str());
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
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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

        // One attempt, 30 s: the retry policy for this endpoint lives in the outbox dispatcher,
        // which is its only caller now. See getRuleList() above.
        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);
        _httpClient->addHeader("Content-Type", "application/json");

        int httpResponseCode = _httpClient->POST(diagnosticJson.c_str());

        log("NET: /api/v1/HardWare/diagnostic -> response %d", httpResponseCode);

        _httpClient->end();

        if (httpResponseCode == 200) {
            trackApiSendSuccess();
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }
    
    bool sendModuleUpdateData(const UpdateDto& updateData) override {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
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

        // One attempt, 30 s: the retry policy for this endpoint lives in the outbox dispatcher,
        // which is its only caller now. See getRuleList() above.
        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);
        _httpClient->addHeader("Content-Type", "application/json");

        int httpResponseCode = _httpClient->POST(updateJson.c_str());

        log("NET: /api/v1/HardWare/update -> response %d", httpResponseCode);

        if (httpResponseCode != 200) {
            std::string errorBody = _httpClient->getString();
            log("NET: HTTP Error body: %s", errorBody.c_str());
        }

        _httpClient->end();

        if (httpResponseCode == 200) {
            trackApiSendSuccess();
            return true;
        } else {
            trackApiSendFailure();
            return false;
        }
    }

    std::vector<DeviceCommandDto> getDeviceCommand(bool* httpFailed = nullptr) override
    {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
        std::vector<DeviceCommandDto> commands;

        if (httpFailed != nullptr)
            *httpFailed = false;

        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: getDeviceCommand - ModuleConfig or Settings not available");
            if (httpFailed != nullptr)
                *httpFailed = true;
            return commands;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/devices_command?token=%s&grow_director_uid=%s&status=new",
                 _serverUrl, _token, moduleUid);

        log("NET: GET %.*s", HttpLog::loggablePathLength(_urlBuffer), _urlBuffer);

        // One attempt, 30 s - the retry budget lives in the outbox dispatcher, the only caller.
        int httpResponseCode = 0;
        std::string responseBody;

        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);

        httpResponseCode = _httpClient->GET();

        if (httpResponseCode > 0)
        {
            // Get response body
            responseBody = _httpClient->getString();
            log("NET: HTTP Response: %s", responseBody.c_str());
        }
        else
        {
            log("NET: HTTP Response Error code: %d", httpResponseCode);
        }

        _httpClient->end();
        logFetch("/api/v1/HardWare/devices_command", httpResponseCode, responseBody.size());

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
            {
                trackApiSendFailure();
                if (httpFailed != nullptr)
                    *httpFailed = true;
            }
        }

        return commands;
    }

    std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) override
    {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
        std::vector<ModuleCommandDto> commands;

        if (httpFailed != nullptr)
            *httpFailed = false;

        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: getModuleCommand - ModuleConfig or Settings not available");
            if (httpFailed != nullptr)
                *httpFailed = true;
            return commands;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/module_command?token=%s&grow_director_uid=%s&statuses=New",
                 _serverUrl, _token, moduleUid);

        log("NET: GET %.*s", HttpLog::loggablePathLength(_urlBuffer), _urlBuffer);

        // One attempt, 30 s. Retries inside this method would multiply with the outbox
        // dispatcher's own budget and dominate the network task's watchdog margin - the retry
        // budget is the dispatcher's, and the caller is its network task.
        int httpResponseCode = 0;
        std::string responseBody;

        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);

        httpResponseCode = _httpClient->GET();

        if (httpResponseCode > 0)
        {
            // Get response body
            responseBody = _httpClient->getString();
            log("NET: HTTP Response: %s", responseBody.c_str());
        }
        else
        {
            log("NET: HTTP Response Error code: %d", httpResponseCode);
        }

        _httpClient->end();
        logFetch("/api/v1/HardWare/module_command", httpResponseCode, responseBody.size());

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
            {
                trackApiSendFailure();
                if (httpFailed != nullptr)
                    *httpFailed = true;
            }
        }

        return commands;
    }

    std::vector<Rule> getRuleList(bool* httpFailed = nullptr, bool* readWhole = nullptr) override
    {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
        std::vector<Rule> rules;

        if (httpFailed != nullptr)
            *httpFailed = false;

        // Until the body has been read, nothing about it is known - and a caller that acts on a
        // rule set as a complete statement must not do so on an answer that never arrived.
        if (readWhole != nullptr)
            *readWhole = false;

        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: getRuleList - ModuleConfig or Settings not available");
            if (httpFailed != nullptr)
                *httpFailed = true;
            return rules;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/HardWare/rulelist?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: GET %.*s", HttpLog::loggablePathLength(_urlBuffer), _urlBuffer);

        // One attempt, 30 s. The retry budget lives in the outbox dispatcher, the only caller
        // of this method: retrying belongs with the task that owns the network, not duplicated
        // per endpoint. The timeout stays 30 s - that is an agreement with the server, not a
        // number of ours.
        int httpResponseCode = 0;

        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);

        httpResponseCode = _httpClient->GET();

        if (httpResponseCode <= 0)
        {
            log("NET: HTTP Response Error code: %d", httpResponseCode);
        }

        if (httpResponseCode != 200)
        {
            _httpClient->end();
            logFetch("/api/v1/HardWare/rulelist", httpResponseCode, 0);
            trackApiSendFailure();
            if (httpFailed != nullptr)
                *httpFailed = true;
            return rules;
        }

        if (_jsonConverter == nullptr)
        {
            _httpClient->end();
            log("NET: getRuleList - 200 with no usable body");
            if (httpFailed != nullptr)
                *httpFailed = true;
            return rules;
        }

        // The answer is read as it arrives and never held whole. A rule is ~650 bytes of JSON, so
        // getString() asks for one heap block the size of the account's rule set, and the module
        // has to have that free, contiguous, on top of the rules it is about to build out of it
        // and the rules it still holds. Read this way the transient is one rule, whatever the
        // account owns.
        //
        // The three outcomes stay exactly as far apart as they were. A body the module could not
        // read must not be reported as an answer: the module would record a successful
        // synchronization against a response it did not understand, and - because a snapshot is
        // authoritative - the merge would mark every local rule DELETING. But `[]` from a server
        // that answered *is* an answer, "this module has no rules", and reporting that as a
        // transport failure is what made a new module ask four times, back off 10.5 s, record a
        // failed sync and do it again five minutes later, for ever.
        // Read with the module's limits, because this answer comes from outside: no more rules
        // than the module keeps, and no rule larger than it accepts. Both are applied as the body
        // goes past, so a rule past either costs the bytes it was cut into and nothing else.
        RuleStreamReader reader(*_jsonConverter, rules, RuleLimits::MAX_RULES_PER_MODULE,
                                RuleStreamReader::MAX_RULE_BYTES);
        const bool bodyRead = _httpClient->readBodyInto(reader);
        _httpClient->end();

        const JsonListParse parsed = bodyRead ? reader.finish() : JsonListParse::Failed;
        logFetch("/api/v1/HardWare/rulelist", httpResponseCode, reader.bytesFed());
        log("NET: getRuleList - rules: %d", (int)reader.readCount());

        // A rule the module did not take is left out and the rest of the snapshot stands. Silence
        // here would make a rule set that is quietly one rule short look like the account's - and
        // it is the caller's business too, because a snapshot the module did not read whole is not
        // a statement about which rules the account has.
        if (reader.skippedCount() > 0)
        {
            log("NET: getRuleList - %d rules could not be read and were skipped",
                (int)reader.skippedCount());
        }
        if (reader.refusedCount() > 0)
        {
            log("NET: getRuleList - %d rules were refused: past the %d this module keeps, or "
                "larger than %d bytes",
                (int)reader.refusedCount(), (int)RuleLimits::MAX_RULES_PER_MODULE,
                (int)RuleStreamReader::MAX_RULE_BYTES);
        }
        if (readWhole != nullptr)
        {
            *readWhole = reader.readWhole();
        }

        if (parsed == JsonListParse::Failed)
        {
            if (reader.bytesFed() == 0)
            {
                log("NET: getRuleList - 200 with no usable body");
            }
            else
            {
                log("NET: Failed to parse rules JSON");
                log("NET: getRuleList - Response (first %d chars): %s...",
                    (int)strlen(reader.prefix()), reader.prefix());
            }
            rules.clear();
            if (httpFailed != nullptr)
                *httpFailed = true;
            return rules;
        }

        log("NET: Rules parsed successfully - count: %d", rules.size());

        // Counted a success only once the answer turned out to be usable. Counting it on the 200
        // alone would let the same request be both a success and an httpFailed.
        trackApiSendSuccess();

        return rules;
    }

    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
        std::vector<SensorType> sensorTypes;

        if (httpFailed != nullptr)
        {
            *httpFailed = false;
        }

        if (_moduleConfig == nullptr || _settings == nullptr)
        {
            log("NET: getSensorTypesList - ModuleConfig or Settings not available");
            if (httpFailed != nullptr)
            {
                *httpFailed = true;
            }
            return sensorTypes;
        }

        const char *moduleUid = _moduleConfig->getModuleUid();
        
        snprintf(_urlBuffer, sizeof(_urlBuffer),
                 "%s/api/v1/Hardware/sensor_models?token=%s&grow_director_uid=%s",
                 _serverUrl, _token, moduleUid);

        log("NET: GET %.*s", HttpLog::loggablePathLength(_urlBuffer), _urlBuffer);

        // One attempt, for the same reason getRuleList() makes one: the outbox owns the retry
        // budget for the catalog, and the one caller it does not drive
        // (ModuleCommandExecutionUseCase, ScanSensors) retries at its own call site.
        //
        // The answer is read as it arrives and never held whole. A catalog is ~1.4 KB per sensor
        // type, so getString() asks for one heap block the size of the account's catalog - twelve
        // types are 16 KB, and the module has to have that free, contiguous, on top of the types it
        // is about to build out of it and the catalog it still holds. Read this way the transient
        // is one sensor type, whatever the account owns.
        _httpClient->setTimeout(30000);
        _httpClient->begin(_urlBuffer);

        int httpResponseCode = _httpClient->GET();

        if (httpResponseCode <= 0)
        {
            log("NET: HTTP Response Error code: %d", httpResponseCode);
        }

        if (httpResponseCode != 200)
        {
            _httpClient->end();
            logFetch("/api/v1/Hardware/sensor_models", httpResponseCode, 0);
            if (httpFailed != nullptr)
            {
                *httpFailed = true;
            }
            trackApiSendFailure();
            return sensorTypes;
        }

        if (_jsonConverter == nullptr)
        {
            _httpClient->end();
            log("NET: getSensorTypesList - 200 with no usable body");
            if (httpFailed != nullptr)
            {
                *httpFailed = true;
            }
            return sensorTypes;
        }

        // An empty catalog from a server that answered is an answer - it is what
        // handleEmptyServerResponse() in SyncSensorTypesUseCase exists to deal with - while a 200
        // whose body this module cannot use is not a statement that the account has no sensor
        // models. The reader keeps the two apart the way the whole-body parse did.
        SensorTypeStreamReader reader(sensorTypes);
        const bool bodyRead = _httpClient->readBodyInto(reader);
        _httpClient->end();

        const JsonListParse parsed = bodyRead ? reader.finish() : JsonListParse::Failed;
        logFetch("/api/v1/Hardware/sensor_models", httpResponseCode, reader.bytesFed());
        log("NET: getSensorTypesList - sensor types: %d", (int)reader.objectCount());

        // A model the module cannot read is left out and the rest of the catalog stands. Silence
        // here would make a catalog that is quietly one model short look like the account's.
        if (reader.skippedCount() > 0)
        {
            log("NET: getSensorTypesList - %d sensor types could not be read and were skipped",
                (int)reader.skippedCount());
        }

        if (parsed == JsonListParse::Failed)
        {
            if (reader.bytesFed() == 0)
            {
                log("NET: getSensorTypesList - 200 with no usable body");
            }
            else
            {
                log("NET: Failed to parse sensor types JSON");
                log("NET: getSensorTypesList - Response (first %d chars): %s...",
                    (int)strlen(reader.prefix()), reader.prefix());
            }
            sensorTypes.clear();
            if (httpFailed != nullptr)
            {
                *httpFailed = true;
            }
        }
        else
        {
            log("NET: Sensor types parsed successfully - count: %d", sensorTypes.size());
            trackApiSendSuccess();
        }

        return sensorTypes;
    }

    ModuleDeletionQueryResult queryModuleDeletion() override
    {
        std::lock_guard<std::recursive_mutex> apiLock(_apiMutex);
        ModuleDeletionQueryResult result;

        if (init() == false)
        {
            log("NET: queryModuleDeletion - Initialization failed");
            result.queryFailed = true;
            return result;
        }

        const char *url = buildUrl("/api/v1/HardWare/is_deleted");
        log("NET: Checking if module is deleted at: %.*s", HttpLog::loggablePathLength(url), url);

        if (_httpClient == nullptr)
        {
            log("NET: queryModuleDeletion - HTTP client not available");
            result.queryFailed = true;
            return result;
        }
        _httpClient->end();

        byte tries = 0;
        int httpResponseCode = 0;
        byte forbiddenCount = 0;
        bool got200 = false;
        std::string responseBody;

        while (tries < DELETION_QUERY_MAX_ATTEMPTS && httpResponseCode != 200)
        {
            _httpClient->setTimeout(DELETION_QUERY_READ_TIMEOUT_MS);
            _httpClient->begin(url);

            httpResponseCode = _httpClient->GET();

            if (httpResponseCode > 0)
            {
                log("NET: HTTP Response code: %d", httpResponseCode);

                if (httpResponseCode == 403)
                {
                    forbiddenCount++;
                }

                if (httpResponseCode == 200)
                {
                    got200 = true;
                    responseBody = _httpClient->getString();
                    log("NET: HTTP Response: %s", responseBody.c_str());
                }
            }
            else
            {
                log("NET: HTTP Response Error code: %d", httpResponseCode);
            }

            _httpClient->end();
            delay(DELETION_QUERY_RETRY_PAUSE_MS);
            tries++;
        }

        result.received200 = got200;
        result.allTries403 = (!got200 && forbiddenCount == 3);
        result.queryFailed = (httpResponseCode <= 0);
        result.finalHttpCode = httpResponseCode;

        if (got200 && !responseBody.empty())
        {
            size_t isDeletedPos = responseBody.find("\"isDeleted\"");
            if (isDeletedPos != std::string::npos)
            {
                size_t truePos = responseBody.find("true", isDeletedPos);
                size_t falsePos = responseBody.find("false", isDeletedPos);

                if (truePos != std::string::npos && (falsePos == std::string::npos || truePos < falsePos))
                {
                    result.isDeletedFlag = 1;
                }
                else if (falsePos != std::string::npos)
                {
                    result.isDeletedFlag = 0;
                }
            }
        }

        if (got200)
        {
            trackApiSendSuccess();
        }
        else
        {
            trackApiSendFailure();
        }

        log("NET: queryModuleDeletion result received200=%d allTries403=%d isDeletedFlag=%d queryFailed=%d code=%d",
            result.received200 ? 1 : 0, result.allTries403 ? 1 : 0, result.isDeletedFlag,
            result.queryFailed ? 1 : 0, result.finalHttpCode);

        return result;
    }

    // Implementations of network diagnostic counter methods
    int getApiSendFails() const override { return _apiSendFails; }
    void resetApiSendFails() override { _apiSendFails = 0; }
    
    float getApiSendFailRatio() const override { return _apiSendFailRatio; }
    void resetApiSendFailRatio() override { _apiSendFailRatio = 0.0f; }
    
    int getCountOffline() const override { return _countOffline; }
    void resetCountOffline() override { _countOffline = 0; }
};
