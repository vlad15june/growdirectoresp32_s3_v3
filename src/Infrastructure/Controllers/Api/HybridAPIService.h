#ifndef HYBRID_API_SERVICE_H
#define HYBRID_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/API/IMqttApiService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Core/Domain/Data/Entities/Rule.h"

/**
 * Implementation of hybrid API service that can use both REST and MQTT APIs
 * with fallback capability
 */
class HybridAPIService : public IHybridAPIService
{
private:
    IRestAPIService *_restApiService;
    IMqttApiService *_mqttApiService;
    ILogger *_logger;
    IDebugSystem *_debugSystem;
    IModuleConfig *_moduleConfig;

    ApiPriority _apiPriority;

    // Statistics
    int _restApiSendFails;
    int _mqttApiSendFails;
    float _apiSendFailRatio;

    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr &&
            (_debugSystem->isFeatureEnabled(DebugFeatures::NET_DEBUG) ||
             _debugSystem->isFeatureEnabled(DebugFeatures::MQTT_DEBUG)))
        {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::NET_DEBUG, buffer);
        }
    }

    /**
     * Helper to track API send success/failure for the fallback mechanism
     */
    void trackApiSendFailure(bool isRest)
    {
        if (isRest)
        {
            _restApiSendFails++;
        }
        else
        {
            _mqttApiSendFails++;
        }
        _apiSendFailRatio = (_restApiSendFails + _mqttApiSendFails) / 2.0f;

        log("HYBRID API: Tracked %s failure - REST fails: %d, MQTT fails: %d, Ratio: %.2f",
            isRest ? "REST" : "MQTT", _restApiSendFails, _mqttApiSendFails, _apiSendFailRatio);
    }

public:
    HybridAPIService(
        IRestAPIService *restApiService,
        IMqttApiService *mqttApiService,
        IModuleConfig *moduleConfig = nullptr,
        ILogger *logger = nullptr,
        IDebugSystem *debugSystem = nullptr,
        ApiPriority initialPriority = ApiPriority::REST_PREFERRED) : _restApiService(restApiService),
                                                                     _mqttApiService(mqttApiService),
                                                                     _logger(logger),
                                                                     _debugSystem(debugSystem),
                                                                     _moduleConfig(moduleConfig),
                                                                     _apiPriority(initialPriority),
                                                                     _restApiSendFails(0),
                                                                     _mqttApiSendFails(0),
                                                                     _apiSendFailRatio(0.0f)
    {
    }

    ~HybridAPIService() override = default;

    void setApiPriority(ApiPriority priority) override
    {
        _apiPriority = priority;
        log("HYBRID API: Priority set to %d", static_cast<int>(priority));
    }

    ApiPriority getApiPriority() const override
    {
        return _apiPriority;
    }

    bool init() override
    {
        bool restInitSuccess = false;

        // Always try to initialize REST API if available
        if (_restApiService != nullptr)
        {
            restInitSuccess = _restApiService->init();
            log("HYBRID API: REST API initialization %s", restInitSuccess ? "successful" : "failed");
        }

        // For MQTT API, just check if it's available
        bool mqttAvailable = (_mqttApiService != nullptr);
        log("HYBRID API: MQTT API is %s", mqttAvailable ? "available" : "not available");

        // Return success if at least one API is available
        return restInitSuccess || mqttAvailable;
    }

    bool isModuleOnline() override
    {
        bool restOnline = false;
        bool mqttOnline = false;

        // Check REST API online status if available
        if (_restApiService != nullptr)
        {
            restOnline = _restApiService->isModuleOnline();
            log("HYBRID API: REST API online status: %s", restOnline ? "online" : "offline");
        }

        // Check MQTT API online status if available
        if (_mqttApiService != nullptr)
        {
            mqttOnline = _mqttApiService->isModuleOnline();
            log("HYBRID API: MQTT API online status: %s", mqttOnline ? "online" : "offline");
        }

        // Module is online if either API is online
        bool isOnline = restOnline || mqttOnline;
        log("HYBRID API: Module is %s", isOnline ? "online" : "offline");
        return isOnline;
    }

    bool sendToServerModuleStructure(const ModuleStructure &moduleStructure, ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiPriority effectivePriority = (priority == ApiPriority::UNDEFINED) ? _apiPriority : priority;
        bool success = false;

        switch (effectivePriority)
        {
        case ApiPriority::REST_ONLY:
            if (_restApiService != nullptr)
            {
                success = _restApiService->sendToServerModuleStructure(moduleStructure);
                if (!success)
                    trackApiSendFailure(true);
            }
            else
            {
                log("HYBRID API: REST_ONLY mode but REST API is not available");
            }
            break;

        case ApiPriority::MQTT_ONLY:
            if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendToServerModuleStructure(moduleStructure);
                if (!success)
                    trackApiSendFailure(false);
            }
            else
            {
                log("HYBRID API: MQTT_ONLY mode but MQTT API is not available");
            }
            break;

        case ApiPriority::REST_PREFERRED:
            if (_restApiService != nullptr)
            {
                success = _restApiService->sendToServerModuleStructure(moduleStructure);
                if (!success && _mqttApiService != nullptr)
                {
                    trackApiSendFailure(true);
                    log("HYBRID API: REST failed, trying MQTT for module structure");
                    success = _mqttApiService->sendToServerModuleStructure(moduleStructure);
                    if (!success)
                        trackApiSendFailure(false);
                }
            }
            else if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendToServerModuleStructure(moduleStructure);
                if (!success)
                    trackApiSendFailure(false);
            }
            break;

        case ApiPriority::MQTT_PREFERRED:
            if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendToServerModuleStructure(moduleStructure);
                if (!success && _restApiService != nullptr)
                {
                    trackApiSendFailure(false);
                    log("HYBRID API: MQTT failed, trying REST for module structure");
                    success = _restApiService->sendToServerModuleStructure(moduleStructure);
                    if (!success)
                        trackApiSendFailure(true);
                }
            }
            else if (_restApiService != nullptr)
            {
                success = _restApiService->sendToServerModuleStructure(moduleStructure);
                if (!success)
                    trackApiSendFailure(true);
            }
            break;

        case ApiPriority::BOTH:
        {
            bool restSuccess = false;
            bool mqttSuccess = false;
            if (_restApiService != nullptr)
            {
                restSuccess = _restApiService->sendToServerModuleStructure(moduleStructure);
                if (!restSuccess)
                    trackApiSendFailure(true);
            }
            if (_mqttApiService != nullptr)
            {
                mqttSuccess = _mqttApiService->sendToServerModuleStructure(moduleStructure);
                if (!mqttSuccess)
                    trackApiSendFailure(false);
            }
            success = restSuccess || mqttSuccess;
            break;
        }
        default:
            break;
        }
        return success;
    }

    bool sendRuleStateCommand(const char *msg) override
    {
        if (_restApiService == nullptr)
        {
            log("HYBRID API: sendRuleStateCommand - REST API not available");
            return false;
        }
        bool success = _restApiService->sendRuleStateCommand(msg);
        if (!success)
            trackApiSendFailure(true);
        return success;
    }

    bool sendByUidRuleStateCommand(const char *uid, const char *msg) override
    {
        if (_restApiService == nullptr)
        {
            log("HYBRID API: sendByUidRuleStateCommand - REST API not available");
            return false;
        }
        bool success = _restApiService->sendByUidRuleStateCommand(uid, msg);
        if (!success)
            trackApiSendFailure(true);
        return success;
    }

    bool sendFirmwareVersion() override
    {
        if (_restApiService == nullptr)
        {
            log("HYBRID API: sendFirmwareVersion - REST API not available");
            return false;
        }
        bool success = _restApiService->sendFirmwareVersion();
        if (!success)
            trackApiSendFailure(true);
        return success;
    }

    bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& commandUpdate) override
    {
        ApiPriority effectivePriority = _apiPriority;
        bool success = false;

        switch (effectivePriority)
        {
        case ApiPriority::REST_ONLY:
            if (_restApiService != nullptr)
            {
                success = _restApiService->sendDeviceCommandExecutionResult(commandUpdate);
                if (!success)
                    trackApiSendFailure(true);
            }
            else
            {
                log("HYBRID API: REST_ONLY mode but REST API is not available");
            }
            break;

        case ApiPriority::MQTT_ONLY:
            if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendDeviceCommandExecutionResult(commandUpdate);
                if (!success)
                    trackApiSendFailure(false);
            }
            else
            {
                log("HYBRID API: MQTT_ONLY mode but MQTT API is not available");
            }
            break;

        case ApiPriority::REST_PREFERRED:
            if (_restApiService != nullptr)
            {
                success = _restApiService->sendDeviceCommandExecutionResult(commandUpdate);
                if (!success && _mqttApiService != nullptr)
                {
                    trackApiSendFailure(true);
                    log("HYBRID API: REST failed, trying MQTT for device command result");
                    success = _mqttApiService->sendDeviceCommandExecutionResult(commandUpdate);
                    if (!success)
                        trackApiSendFailure(false);
                }
            }
            else if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendDeviceCommandExecutionResult(commandUpdate);
                if (!success)
                    trackApiSendFailure(false);
            }
            break;

        case ApiPriority::MQTT_PREFERRED:
            if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendDeviceCommandExecutionResult(commandUpdate);
                if (!success && _restApiService != nullptr)
                {
                    trackApiSendFailure(false);
                    log("HYBRID API: MQTT failed, trying REST for device command result");
                    success = _restApiService->sendDeviceCommandExecutionResult(commandUpdate);
                    if (!success)
                        trackApiSendFailure(true);
                }
            }
            else if (_restApiService != nullptr)
            {
                success = _restApiService->sendDeviceCommandExecutionResult(commandUpdate);
                if (!success)
                    trackApiSendFailure(true);
            }
            break;

        case ApiPriority::BOTH:
        {
            bool restSuccess = false;
            bool mqttSuccess = false;
            if (_restApiService != nullptr)
            {
                restSuccess = _restApiService->sendDeviceCommandExecutionResult(commandUpdate);
                if (!restSuccess)
                    trackApiSendFailure(true);
            }
            if (_mqttApiService != nullptr)
            {
                mqttSuccess = _mqttApiService->sendDeviceCommandExecutionResult(commandUpdate);
                if (!mqttSuccess)
                    trackApiSendFailure(false);
            }
            success = restSuccess || mqttSuccess;
            break;
        }
        default:
            break;
        }

        log("HYBRID API: Device command execution result sent - success: %s", success ? "true" : "false");
        return success;
    }

    bool sendAnalogInputDeviceCommandExecutionResult(const char *msg) override
    {
        if (_restApiService == nullptr)
        {
            log("HYBRID API: sendAnalogInputDeviceCommandExecutionResult - REST API not available");
            return false;
        }
        bool success = _restApiService->sendAnalogInputDeviceCommandExecutionResult(msg);
        if (!success)
            trackApiSendFailure(true);
        return success;
    }

    bool sendModuleCommandExecutionResult(const char *msg) override
    {
        if (_restApiService == nullptr)
        {
            log("HYBRID API: sendModuleCommandExecutionResult - REST API not available");
            return false;
        }
        bool success = _restApiService->sendModuleCommandExecutionResult(msg);
        if (!success)
            trackApiSendFailure(true);
        return success;
    }

    bool sendDiagnosticData(const DiagnosticDto &diagnosticData, ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiPriority effectivePriority = (priority == ApiPriority::UNDEFINED) ? _apiPriority : priority;
        bool success = false;

        switch (effectivePriority)
        {
        case ApiPriority::REST_ONLY:
            if (_restApiService != nullptr)
            {
                success = _restApiService->sendDiagnosticData(diagnosticData);
                if (!success)
                    trackApiSendFailure(true);
            }
            else
            {
                log("HYBRID API: REST_ONLY mode but REST API is not available");
            }
            break;

        case ApiPriority::MQTT_ONLY:
            if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendDiagnosticData(diagnosticData);
                if (!success)
                    trackApiSendFailure(false);
            }
            else
            {
                log("HYBRID API: MQTT_ONLY mode but MQTT API is not available");
            }
            break;

        case ApiPriority::REST_PREFERRED:
            if (_restApiService != nullptr)
            {
                success = _restApiService->sendDiagnosticData(diagnosticData);
                if (!success && _mqttApiService != nullptr)
                {
                    trackApiSendFailure(true);
                    log("HYBRID API: REST failed, trying MQTT for diagnostic data");
                    success = _mqttApiService->sendDiagnosticData(diagnosticData);
                    if (!success)
                        trackApiSendFailure(false);
                }
            }
            else if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendDiagnosticData(diagnosticData);
                if (!success)
                    trackApiSendFailure(false);
            }
            break;

        case ApiPriority::MQTT_PREFERRED:
            if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendDiagnosticData(diagnosticData);
                if (!success && _restApiService != nullptr)
                {
                    trackApiSendFailure(false);
                    log("HYBRID API: MQTT failed, trying REST for diagnostic data");
                    success = _restApiService->sendDiagnosticData(diagnosticData);
                    if (!success)
                        trackApiSendFailure(true);
                }
            }
            else if (_restApiService != nullptr)
            {
                success = _restApiService->sendDiagnosticData(diagnosticData);
                if (!success)
                    trackApiSendFailure(true);
            }
            break;

        case ApiPriority::BOTH:
        {
            bool restSuccess = false;
            bool mqttSuccess = false;
            if (_restApiService != nullptr)
            {
                restSuccess = _restApiService->sendDiagnosticData(diagnosticData);
                if (!restSuccess)
                    trackApiSendFailure(true);
            }
            if (_mqttApiService != nullptr)
            {
                mqttSuccess = _mqttApiService->sendDiagnosticData(diagnosticData);
                if (!mqttSuccess)
                    trackApiSendFailure(false);
            }
            success = restSuccess || mqttSuccess;
            break;
        }
        default:
            break;
        }
        return success;
    }

    bool sendModuleUpdateData(const UpdateDto &updateData, ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiPriority effectivePriority = (priority == ApiPriority::UNDEFINED) ? _apiPriority : priority;
        bool success = false;

        switch (effectivePriority)
        {
        case ApiPriority::REST_ONLY:
            if (_restApiService != nullptr)
            {
                success = _restApiService->sendModuleUpdateData(updateData);
                if (!success)
                    trackApiSendFailure(true);
            }
            else
            {
                log("HYBRID API: REST_ONLY mode but REST API is not available");
            }
            break;

        case ApiPriority::MQTT_ONLY:
            if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendModuleUpdateData(updateData);
                if (!success)
                    trackApiSendFailure(false);
            }
            else
            {
                log("HYBRID API: MQTT_ONLY mode but MQTT API is not available");
            }
            break;

        case ApiPriority::REST_PREFERRED:
            if (_restApiService != nullptr)
            {
                success = _restApiService->sendModuleUpdateData(updateData);
                if (!success && _mqttApiService != nullptr)
                {
                    trackApiSendFailure(true);
                    log("HYBRID API: REST failed, trying MQTT for update data");
                    success = _mqttApiService->sendModuleUpdateData(updateData);
                    if (!success)
                        trackApiSendFailure(false);
                }
            }
            else if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendModuleUpdateData(updateData);
                if (!success)
                    trackApiSendFailure(false);
            }
            break;

        case ApiPriority::MQTT_PREFERRED:
            if (_mqttApiService != nullptr)
            {
                success = _mqttApiService->sendModuleUpdateData(updateData);
                if (!success && _restApiService != nullptr)
                {
                    trackApiSendFailure(false);
                    log("HYBRID API: MQTT failed, trying REST for update data");
                    success = _restApiService->sendModuleUpdateData(updateData);
                    if (!success)
                        trackApiSendFailure(true);
                }
            }
            else if (_restApiService != nullptr)
            {
                success = _restApiService->sendModuleUpdateData(updateData);
                if (!success)
                    trackApiSendFailure(true);
            }
            break;

        case ApiPriority::BOTH:
        {
            bool restSuccess = false;
            bool mqttSuccess = false;
            if (_restApiService != nullptr)
            {
                restSuccess = _restApiService->sendModuleUpdateData(updateData);
                if (!restSuccess)
                    trackApiSendFailure(true);
            }
            if (_mqttApiService != nullptr)
            {
                mqttSuccess = _mqttApiService->sendModuleUpdateData(updateData);
                if (!mqttSuccess)
                    trackApiSendFailure(false);
            }
            success = restSuccess || mqttSuccess;
            break;
        }
        default:
            break;
        }
        return success;
    }

    std::vector<DeviceCommandDto> getDeviceCommand() override
    {
        std::vector<DeviceCommandDto> commands;
        
        // This method always uses REST API only, regardless of priority setting
        if (_restApiService != nullptr)
        {
            commands = _restApiService->getDeviceCommand();
            log("HYBRID API: Get device commands via REST - command count: %d", commands.size());
        }
        else
        {
            log("HYBRID API: REST API service not available for getDeviceCommand");
        }
        
        return commands;
    }

    std::vector<Rule> getRuleList() override
    {
        std::vector<Rule> rules;
        
        // This method always uses REST API only, regardless of priority setting
        if (_restApiService != nullptr)
        {
            rules = _restApiService->getRuleList();
            log("HYBRID API: Get rule list via REST - rule count: %d", rules.size());
        }
        else
        {
            log("HYBRID API: REST API service not available for getRuleList");
        }
        
        return rules;
    }

    int getRestApiSendFails() const override
    {
        return _restApiSendFails;
    }

    int getMqttApiSendFails() const override
    {
        return _mqttApiSendFails;
    }

    float getApiSendFailRatio() const override
    {
        return _apiSendFailRatio;
    }

    void resetApiStatistics() override
    {
        _restApiSendFails = 0;
        _mqttApiSendFails = 0;
        _apiSendFailRatio = 0.0f;

        // Also reset the underlying API service statistics if available
        if (_restApiService != nullptr)
        {
            _restApiService->resetApiSendFails();
            _restApiService->resetApiSendFailRatio();
            _restApiService->resetCountOffline();
        }

        log("HYBRID API: Statistics reset");
    }
};

#endif // HYBRID_API_SERVICE_H
