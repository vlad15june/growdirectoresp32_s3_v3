#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"

class IRestAPIService {
public:
    virtual ~IRestAPIService() = default;

    virtual bool init() = 0;

    virtual bool isModuleOnline() = 0;
    
    virtual void sendToServerModuleStructure(const ModuleStructure& moduleStructure) = 0;
    
    virtual void sendRuleStateCommand(const char* msg) = 0;
    virtual bool sendByUidRuleStateCommand(const char* uid, const char* msg) = 0;
    virtual void sendFirmwareVersion() = 0;
    virtual void sendDeviceCommandExecutionResult(const char* msg) = 0;
    virtual void sendAnalogInputDeviceCommandExecutionResult(const char* msg) = 0;
    virtual bool sendModuleCommandExecutionResult(const char* msg) = 0;
    
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
    
    // Network diagnostic counter methods - getters and reset methods only
    virtual int getApiSendFails() const = 0;
    virtual void resetApiSendFails() = 0;
    
    virtual float getApiSendFailRatio() const = 0;
    virtual void resetApiSendFailRatio() = 0;
    
    virtual int getCountOffline() const = 0;
    virtual void resetCountOffline() = 0;
};
