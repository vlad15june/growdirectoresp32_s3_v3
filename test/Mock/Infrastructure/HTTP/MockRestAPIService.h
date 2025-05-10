#ifndef MOCK_REST_API_SERVICE_H
#define MOCK_REST_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IRestAPIService.h"

class MockRestAPIService : public IRestAPIService {
public:
    bool initCalled = false;
    bool isModuleOnlineCalled = false;
    bool sendToServerModuleStructureCalled = false;
    bool sendRuleStateCommandCalled = false;
    bool sendByUidRuleStateCommandCalled = false;
    bool sendFirmwareVersionCalled = false;
    bool sendDeviceCommandExecutionResultCalled = false;
    bool sendAnalogInputDeviceCommandExecutionResultCalled = false;
    bool sendModuleCommandExecutionResultCalled = false;
    bool sendDiagnosticDataCalled = false;
    
    const ModuleStructure* lastModuleStructure = nullptr;
    const char* lastRuleStateCommand = nullptr;
    const char* lastDeviceCommandResult = nullptr;
    DiagnosticDto lastDiagnosticData;
    bool sendDiagnosticDataReturnValue = true;
    
    void reset() {
        initCalled = false;
        isModuleOnlineCalled = false;
        sendToServerModuleStructureCalled = false;
        sendRuleStateCommandCalled = false;
        sendByUidRuleStateCommandCalled = false;
        sendFirmwareVersionCalled = false;
        sendDeviceCommandExecutionResultCalled = false;
        sendAnalogInputDeviceCommandExecutionResultCalled = false;
        sendModuleCommandExecutionResultCalled = false;
        sendDiagnosticDataCalled = false;
        sendDiagnosticDataReturnValue = true;
        
        lastModuleStructure = nullptr;
        lastRuleStateCommand = nullptr;
        lastDeviceCommandResult = nullptr;
    }
    
    // IRestAPIService interface implementation
    bool init() override {
        initCalled = true;
        return true;
    }
    
    bool isModuleOnline() override {
        isModuleOnlineCalled = true;
        return true;
    }
    
    void sendToServerModuleStructure(const ModuleStructure& moduleStructure) override {
        sendToServerModuleStructureCalled = true;
        lastModuleStructure = &moduleStructure;
    }
    
    void sendRuleStateCommand(const char* msg) override {
        sendRuleStateCommandCalled = true;
        lastRuleStateCommand = msg;
    }
    
    bool sendByUidRuleStateCommand(const char* uid, const char* msg) override {
        sendByUidRuleStateCommandCalled = true;
        lastRuleStateCommand = msg;
        return true;
    }
    
    void sendFirmwareVersion() override {
        sendFirmwareVersionCalled = true;
    }
    
    void sendDeviceCommandExecutionResult(const char* msg) override {
        sendDeviceCommandExecutionResultCalled = true;
        lastDeviceCommandResult = msg;
    }
    
    void sendAnalogInputDeviceCommandExecutionResult(const char* msg) override {
        sendAnalogInputDeviceCommandExecutionResultCalled = true;
        lastDeviceCommandResult = msg;
    }
    
    bool sendModuleCommandExecutionResult(const char* msg) override {
        sendModuleCommandExecutionResultCalled = true;
        lastDeviceCommandResult = msg;
        return true;
    }
    
    bool sendDiagnosticData(const DiagnosticDto& diagnosticData) override {
        sendDiagnosticDataCalled = true;
        lastDiagnosticData = diagnosticData;
        return sendDiagnosticDataReturnValue;
    }
};

#endif // MOCK_REST_API_SERVICE_H
