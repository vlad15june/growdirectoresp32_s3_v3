#pragma once

#include <Arduino.h>

class IRestAPIService {
public:
    virtual ~IRestAPIService() = default;

    // Connection methods
    virtual bool isModuleOnline() = 0;
    
    // Structure update methods
    virtual void sendToServerModuleStructure() = 0;
    
    // Device and rule state methods
    virtual void sendRuleStateCommand(String msg) = 0;
    virtual bool sendByUidRuleStateCommand(String uid, String msg) = 0;
    virtual void sendFirmwareVersion() = 0;
    virtual void sendDeviceCommandExecutionResult(String msg) = 0;
    virtual void sendAnalogInputDeviceCommandExecutionResult(String msg) = 0;
    virtual bool sendModuleCommandExecutionResult(String msg) = 0;
};
