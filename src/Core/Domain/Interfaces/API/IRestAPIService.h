#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"

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
};
