#pragma once

#include "IRestAPIService.h"
#include "IMqttApiService.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include <vector>

/**
 * Enum that defines API priority for sending data
 */
enum class ApiPriority {
    UNDEFINED,       // Use the current class setting
    REST_ONLY,       // Only use REST API
    MQTT_ONLY,       // Only use MQTT API
    REST_PREFERRED,  // Try REST first, fallback to MQTT
    MQTT_PREFERRED,  // Try MQTT first, fallback to REST
    BOTH             // Send through both APIs
};

/**
 * Interface for a hybrid API service that can use either REST or MQTT
 * to communicate with the server, with fallback options.
 */
class IHybridAPIService {
public:
    virtual ~IHybridAPIService() = default;

    /**
     * Set the API priority mode for communication
     * @param priority The API priority mode to use
     */
    virtual void setApiPriority(ApiPriority priority) = 0;
    
    /**
     * Get the current API priority mode
     * @return The current API priority mode
     */
    virtual ApiPriority getApiPriority() const = 0;

    /**
     * Initialize the API service
     * @return true if initialization was successful, false otherwise
     */
    virtual bool init() = 0;

    /**
     * Check if the module is online and can communicate with the server
     * @return true if online, false otherwise
     */
    virtual bool isModuleOnline() = 0;
    
    /**
     * Send module structure to the server using the configured API priority
     * @param moduleStructure The module structure to send
     * @param priority Optional API priority to use for this call (overrides default)
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendToServerModuleStructure(const ModuleStructure& moduleStructure, ApiPriority priority = ApiPriority::UNDEFINED) = 0;
    
    /**
     * Send rule state command to the server
     * @param msg The message containing the rule state
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendRuleStateCommand(const char* msg, ApiPriority priority = ApiPriority::UNDEFINED) = 0;
    
    /**
     * Send rule state command for a specific rule UID
     * @param uid The rule UID
     * @param msg The message containing the rule state
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendByUidRuleStateCommand(const char* uid, const char* msg) = 0;
    
    /**
     * Send firmware version information to the server
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendFirmwareVersion() = 0;
    
    /**
     * Send device command execution result to the server
     * @param commandUpdate The device command update containing the execution result
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& commandUpdate) = 0;
    
    /**
     * Send analog input device command execution result to the server
     * @param msg The message containing the execution result
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendAnalogInputDeviceCommandExecutionResult(const char* msg) = 0;
    
    /**
     * Send module command execution result to the server
     * @param msg The message containing the execution result
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendModuleCommandExecutionResult(const char* msg) = 0;
    
    /**
     * Send diagnostic data to the server
     * @param diagnosticData The diagnostic data to send
     * @param priority Optional API priority to use for this call (overrides default)
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendDiagnosticData(const DiagnosticDto& diagnosticData, ApiPriority priority = ApiPriority::UNDEFINED) = 0;
    
    /**
     * @brief Send module update data to the server
     * @param updateData The update data to send
     * @param priority Optional API priority to use for this call (overrides default)
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendModuleUpdateData(const UpdateDto& updateData, ApiPriority priority = ApiPriority::UNDEFINED) = 0;
    
    /**
     * Send command result to the server
     * @param commandResult The command result containing status and command type
     * @param priority Optional API priority to use for this call (overrides default)
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendCommandResult(const CommandResultDto& commandResult, ApiPriority priority = ApiPriority::UNDEFINED) = 0;
    
    /**
     * Get statistics about API usage and performance
     */
    virtual int getRestApiSendFails() const = 0;
    virtual int getMqttApiSendFails() const = 0;
    virtual float getApiSendFailRatio() const = 0;
    
    /**
     * Reset API statistics
     */
    virtual void resetApiStatistics() = 0;

    /**
     * Get device commands from the server (REST only)
     * @return vector of DeviceCommandDto containing the command details, empty if no commands or error
     */
    virtual std::vector<DeviceCommandDto> getDeviceCommand() = 0;
    
    /**
     * Get rule list from the server (REST only)
     * @return vector of Rule containing the rule details, empty if no rules or error
     */
    virtual std::vector<Rule> getRuleList() = 0;

    /**
     * Get module commands from the server (REST only)
     * @return vector of ModuleCommandDto containing the command details, empty if no commands or error
     */
    virtual std::vector<ModuleCommandDto> getModuleCommand() = 0;
};
