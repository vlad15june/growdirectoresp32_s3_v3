#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"

class IRestAPIService {
public:
    virtual ~IRestAPIService() = default;

    // Connection methods
    virtual bool isModuleOnline() = 0;
    
    // Structure update methods
    virtual void sendToServerModuleStructure(const ModuleStructure& moduleStructure) = 0;
    
    // Device and rule state methods
    virtual void sendRuleStateCommand(const char* msg) = 0;
    virtual bool sendByUidRuleStateCommand(const char* uid, const char* msg) = 0;
    virtual void sendFirmwareVersion() = 0;
    virtual void sendDeviceCommandExecutionResult(const char* msg) = 0;
    virtual void sendAnalogInputDeviceCommandExecutionResult(const char* msg) = 0;
    virtual bool sendModuleCommandExecutionResult(const char* msg) = 0;
};
