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
     * @param httpFailed Optional: set to true when the request itself failed, as opposed to the
     *        server answering that this module has no rules. Both cases return an empty vector,
     *        and a caller that retries has to be able to tell them apart. Same shape as
     *        getDeviceCommand() above, for the same reason.
     * @param readWhole Optional: set to true when every rule the answer carried was built into
     *        the vector. A rule can be left out without the request failing - unreadable, larger
     *        than this module accepts, or past the number of rules it keeps - and the difference
     *        decides whether the answer may be treated as the complete set of rules this module
     *        has. It may not: the merge retires every stored rule an authoritative snapshot does
     *        not name, and a rule missing because the module could not take it is not a rule the
     *        server deleted. False when the request failed.
     * @return vector of Rule containing the rule details, empty if no rules or error
     */
    virtual std::vector<Rule> getRuleList(bool* httpFailed = nullptr, bool* readWhole = nullptr) = 0;
    
    /**
     * @brief Get sensor types list from server
     * @param httpFailed Optional: set to true when the request itself failed, as opposed to the
     *        server answering that this account has no sensor models. Same shape and same reason as
     *        getDeviceCommand() above.
     * @return vector of SensorType containing the sensor type definitions, empty if no sensor types or error
     */
    virtual std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) = 0;
    
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
     * @param httpFailed Optional: set to true when the request itself failed, as opposed to the
     *        server answering that this module has no commands. Same shape and same reason as
     *        getDeviceCommand() above.
     * @return vector of ModuleCommandDto containing the command details, empty if no commands or error
     */
    virtual std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) = 0;
    
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
