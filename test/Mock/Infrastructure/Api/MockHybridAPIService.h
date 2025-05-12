#ifndef MOCK_HYBRID_API_SERVICE_H
#define MOCK_HYBRID_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IHybridAPIService.h"

class MockHybridAPIService : public IHybridAPIService {
public:
    // Tracking flags for method calls
    bool setApiPriorityCalled = false;
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
    bool sendModuleUpdateDataCalled = false;
    bool resetApiStatisticsCalled = false;
    
    // Return values
    bool initReturnValue = true;
    bool isModuleOnlineReturnValue = true;
    bool sendModuleStructureReturnValue = true;
    bool sendRuleStateCommandReturnValue = true;
    bool sendByUidRuleStateCommandReturnValue = true;
    bool sendFirmwareVersionReturnValue = true;
    bool sendDeviceCommandExecutionResultReturnValue = true;
    bool sendAnalogInputDeviceCommandExecutionResultReturnValue = true;
    bool sendModuleCommandExecutionResultReturnValue = true;
    bool sendDiagnosticDataReturnValue = true;
    bool sendModuleUpdateDataReturnValue = true;
    
    // Parameter storage
    ApiPriority currentApiPriority = ApiPriority::REST_PREFERRED;
    ApiPriority lastSetApiPriority = ApiPriority::UNDEFINED;
    const ModuleStructure* lastModuleStructure = nullptr;
    ApiPriority lastModuleStructurePriority = ApiPriority::UNDEFINED;
    const char* lastRuleStateCommand = nullptr;
    const char* lastRuleUid = nullptr;
    const char* lastByUidRuleStateCommand = nullptr;
    const char* lastDeviceCommandResult = nullptr;
    const char* lastAnalogInputDeviceCommandResult = nullptr;
    const char* lastModuleCommandResult = nullptr;
    DiagnosticDto lastDiagnosticData;
    UpdateDto lastUpdateData;
    ApiPriority lastDiagnosticDataPriority = ApiPriority::UNDEFINED;
    ApiPriority lastUpdateDataPriority = ApiPriority::UNDEFINED;
    
    // Statistics
    int restApiSendFailsValue = 0;
    int mqttApiSendFailsValue = 0;
    float apiSendFailRatioValue = 0.0f;
    
    void reset() {
        setApiPriorityCalled = false;
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
        sendModuleUpdateDataCalled = false;
        resetApiStatisticsCalled = false;
        
        initReturnValue = true;
        isModuleOnlineReturnValue = true;
        sendModuleStructureReturnValue = true;
        sendRuleStateCommandReturnValue = true;
        sendByUidRuleStateCommandReturnValue = true;
        sendFirmwareVersionReturnValue = true;
        sendDeviceCommandExecutionResultReturnValue = true;
        sendAnalogInputDeviceCommandExecutionResultReturnValue = true;
        sendModuleCommandExecutionResultReturnValue = true;
        sendDiagnosticDataReturnValue = true;
        sendModuleUpdateDataReturnValue = true;
        
        currentApiPriority = ApiPriority::REST_PREFERRED;
        lastSetApiPriority = ApiPriority::UNDEFINED;
        lastModuleStructure = nullptr;
        lastModuleStructurePriority = ApiPriority::UNDEFINED;
        lastRuleStateCommand = nullptr;
        lastRuleUid = nullptr;
        lastByUidRuleStateCommand = nullptr;
        lastDeviceCommandResult = nullptr;
        lastAnalogInputDeviceCommandResult = nullptr;
        lastModuleCommandResult = nullptr;
        lastDiagnosticDataPriority = ApiPriority::UNDEFINED;
        lastUpdateDataPriority = ApiPriority::UNDEFINED;
        
        restApiSendFailsValue = 0;
        mqttApiSendFailsValue = 0;
        apiSendFailRatioValue = 0.0f;
    }
    
    // IHybridAPIService implementation
    void setApiPriority(ApiPriority priority) override {
        setApiPriorityCalled = true;
        lastSetApiPriority = priority;
        currentApiPriority = priority;
    }
    
    ApiPriority getApiPriority() const override {
        return currentApiPriority;
    }
    
    bool init() override {
        initCalled = true;
        return initReturnValue;
    }
    
    bool isModuleOnline() override {
        isModuleOnlineCalled = true;
        return isModuleOnlineReturnValue;
    }
    
    bool sendToServerModuleStructure(const ModuleStructure& moduleStructure, ApiPriority priority = ApiPriority::UNDEFINED) override {
        sendToServerModuleStructureCalled = true;
        lastModuleStructure = &moduleStructure;
        lastModuleStructurePriority = priority;
        return sendModuleStructureReturnValue;
    }
    
    bool sendRuleStateCommand(const char* msg) override {
        sendRuleStateCommandCalled = true;
        lastRuleStateCommand = msg;
        return sendRuleStateCommandReturnValue;
    }
    
    bool sendByUidRuleStateCommand(const char* uid, const char* msg) override {
        sendByUidRuleStateCommandCalled = true;
        lastRuleUid = uid;
        lastByUidRuleStateCommand = msg;
        return sendByUidRuleStateCommandReturnValue;
    }
    
    bool sendFirmwareVersion() override {
        sendFirmwareVersionCalled = true;
        return sendFirmwareVersionReturnValue;
    }
    
    bool sendDeviceCommandExecutionResult(const char* msg) override {
        sendDeviceCommandExecutionResultCalled = true;
        lastDeviceCommandResult = msg;
        return sendDeviceCommandExecutionResultReturnValue;
    }
    
    bool sendAnalogInputDeviceCommandExecutionResult(const char* msg) override {
        sendAnalogInputDeviceCommandExecutionResultCalled = true;
        lastAnalogInputDeviceCommandResult = msg;
        return sendAnalogInputDeviceCommandExecutionResultReturnValue;
    }
    
    bool sendModuleCommandExecutionResult(const char* msg) override {
        sendModuleCommandExecutionResultCalled = true;
        lastModuleCommandResult = msg;
        return sendModuleCommandExecutionResultReturnValue;
    }
    
    bool sendDiagnosticData(const DiagnosticDto& diagnosticData, ApiPriority priority = ApiPriority::UNDEFINED) override {
        sendDiagnosticDataCalled = true;
        lastDiagnosticData = diagnosticData;
        lastDiagnosticDataPriority = priority;
        return sendDiagnosticDataReturnValue;
    }
    
    bool sendModuleUpdateData(const UpdateDto& updateData, ApiPriority priority = ApiPriority::UNDEFINED) override {
        sendModuleUpdateDataCalled = true;
        lastUpdateData = updateData;
        lastUpdateDataPriority = priority;
        return sendModuleUpdateDataReturnValue;
    }
    
    int getRestApiSendFails() const override {
        return restApiSendFailsValue;
    }
    
    int getMqttApiSendFails() const override {
        return mqttApiSendFailsValue;
    }
    
    float getApiSendFailRatio() const override {
        return apiSendFailRatioValue;
    }
    
    void resetApiStatistics() override {
        resetApiStatisticsCalled = true;
        restApiSendFailsValue = 0;
        mqttApiSendFailsValue = 0;
        apiSendFailRatioValue = 0.0f;
    }
};

#endif // MOCK_HYBRID_API_SERVICE_H
