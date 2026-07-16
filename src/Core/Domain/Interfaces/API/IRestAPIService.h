#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Data/Models/ModuleDeletionQueryResult.h"
#include <vector>

class IRestAPIService {
public:
    virtual ~IRestAPIService() = default;

    virtual bool init() = 0;

    virtual bool isModuleOnline() = 0;
    
    virtual bool sendToServerModuleStructure(const ModuleStructure& moduleStructure) = 0;
    
    virtual bool sendRuleStateCommand(const char* msg) = 0;
    virtual bool sendByUidRuleStateCommand(const char* uid, const char* msg) = 0;
    virtual bool sendFirmwareVersion() = 0;
    virtual bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& commandUpdate) = 0;
    virtual bool sendAnalogInputDeviceCommandExecutionResult(const char* msg) = 0;
    virtual bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& commandUpdates) = 0;
    
    /**
     * @brief Get pending device commands from server
     * @return vector of DeviceCommandDto containing the command details, empty if no commands or error
     */
    virtual std::vector<DeviceCommandDto> getDeviceCommand(bool* httpFailed = nullptr) = 0;
    
    /**
     * @brief Get rule list from server
     * @return vector of Rule containing the rule details, empty if no rules or error
     */
    virtual std::vector<Rule> getRuleList() = 0;
    
    /**
     * @brief Get sensor types list from server
     * @return vector of SensorType containing the sensor type definitions, empty if no sensor types or error
     */
    virtual std::vector<SensorType> getSensorTypesList() = 0;
    
    /**
     * @brief Sends diagnostic data to the server
     * @param diagnosticData The diagnostic data to send
     * @return true if the data was sent successfully, false otherwise
     */
    virtual bool sendDiagnosticData(const DiagnosticDto& diagnosticData) = 0;
    
    /**
     * @brief Sends module update data to the server
     * @param updateData The update data to send
     * @return true if the data was sent successfully, false otherwise
     */
    virtual bool sendModuleUpdateData(const UpdateDto& updateData) = 0;
    
    /**
     * @brief Get pending module commands from server
     * @return vector of ModuleCommandDto containing the command details, empty if no commands or error
     */
    virtual std::vector<ModuleCommandDto> getModuleCommand() = 0;
    
    /**
     * @brief Query the server for the module deletion status (REST only)
     *
     * Performs the /is_deleted request with the existing retry budget and returns
     * the raw transport facts (final HTTP code, whether every retry returned 403,
     * the parsed isDeleted flag as tri-state, transport failure). It does NOT decide
     * whether the module is deleted — that policy lives in CheckModuleDeletionUseCase.
     * @return ModuleDeletionQueryResult with the raw outcome of the query
     */
    virtual ModuleDeletionQueryResult queryModuleDeletion() = 0;
    
    // Network diagnostic counter methods - getters and reset methods only
    virtual int getApiSendFails() const = 0;
    virtual void resetApiSendFails() = 0;
    
    virtual float getApiSendFailRatio() const = 0;
    virtual void resetApiSendFailRatio() = 0;
    
    virtual int getCountOffline() const = 0;
    virtual void resetCountOffline() = 0;
};
