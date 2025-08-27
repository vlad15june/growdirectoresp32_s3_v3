#ifndef MOCK_HYBRID_API_SERVICE_H
#define MOCK_HYBRID_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include <vector>

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
    bool getDeviceCommandCalled = false;
    bool sendCommandResultCalled = false;
    bool getRuleListCalled = false;
    bool getModuleCommandCalled = false;
    
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
    bool sendCommandResultReturnValue = true;
    DeviceCommandDto deviceCommandReturnValue;
    std::vector<DeviceCommandDto> deviceCommandsReturnValue;
    std::vector<Rule> ruleListReturnValue;
    std::vector<ModuleCommandDto> moduleCommandsReturnValue;
    
    // Parameter storage
    ApiPriority currentApiPriority = ApiPriority::REST_PREFERRED;
    ApiPriority lastSetApiPriority = ApiPriority::UNDEFINED;
    const ModuleStructure* lastModuleStructure = nullptr;
    ApiPriority lastModuleStructurePriority = ApiPriority::UNDEFINED;
    const char* lastRuleStateCommand = nullptr;
    ApiPriority lastRuleStateCommandPriority = ApiPriority::UNDEFINED;
    const char* lastRuleUid = nullptr;
    const char* lastByUidRuleStateCommand = nullptr;
    DeviceCommandUpdateDto lastDeviceCommandUpdate;
    const char* lastAnalogInputDeviceCommandResult = nullptr;
    const char* lastModuleCommandResult = nullptr;
    DiagnosticDto lastDiagnosticData;
    UpdateDto lastUpdateData;
    CommandResultDto lastCommandResult;
    ApiPriority lastDiagnosticDataPriority = ApiPriority::UNDEFINED;
    ApiPriority lastUpdateDataPriority = ApiPriority::UNDEFINED;
    ApiPriority lastCommandResultPriority = ApiPriority::UNDEFINED;
    ModuleCommandUpdateDto lastModuleCommandUpdate;
    
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
        getDeviceCommandCalled = false;
        sendCommandResultCalled = false;
        getRuleListCalled = false;
        getModuleCommandCalled = false;
        
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
        sendCommandResultReturnValue = true;
        deviceCommandReturnValue = DeviceCommandDto();
        deviceCommandsReturnValue.clear();
        ruleListReturnValue.clear();
        moduleCommandsReturnValue.clear();
        
        currentApiPriority = ApiPriority::REST_PREFERRED;
        lastSetApiPriority = ApiPriority::UNDEFINED;
        lastModuleStructure = nullptr;
        lastModuleStructurePriority = ApiPriority::UNDEFINED;
        lastRuleStateCommand = nullptr;
        lastRuleUid = nullptr;
        lastByUidRuleStateCommand = nullptr;
        lastAnalogInputDeviceCommandResult = nullptr;
        lastModuleCommandResult = nullptr;
        lastDiagnosticDataPriority = ApiPriority::UNDEFINED;
        lastUpdateDataPriority = ApiPriority::UNDEFINED;
        lastCommandResultPriority = ApiPriority::UNDEFINED;
        lastRuleStateCommandPriority = ApiPriority::UNDEFINED;
        lastModuleCommandUpdate = ModuleCommandUpdateDto();
        
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
    
    bool sendRuleStateCommand(const char* msg, ApiPriority priority = ApiPriority::UNDEFINED) override {
        sendRuleStateCommandCalled = true;
        lastRuleStateCommand = msg;
        lastRuleStateCommandPriority = priority;
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
    
    bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& commandUpdate) override {
        sendDeviceCommandExecutionResultCalled = true;
        lastDeviceCommandUpdate = commandUpdate;
        return sendDeviceCommandExecutionResultReturnValue;
    }
    
    bool sendAnalogInputDeviceCommandExecutionResult(const char* msg) override {
        sendAnalogInputDeviceCommandExecutionResultCalled = true;
        lastAnalogInputDeviceCommandResult = msg;
        return sendAnalogInputDeviceCommandExecutionResultReturnValue;
    }
    
    bool sendModuleCommandExecutionResult(const ModuleCommandUpdateDto& commandUpdate) override {
        sendModuleCommandExecutionResultCalled = true;
        lastModuleCommandUpdate = commandUpdate;
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
    
    bool sendCommandResult(const CommandResultDto& commandResult, ApiPriority priority = ApiPriority::UNDEFINED) override {
        sendCommandResultCalled = true;
        lastCommandResult = commandResult;
        lastCommandResultPriority = priority;
        return sendCommandResultReturnValue;
    }
    
    std::vector<DeviceCommandDto> getDeviceCommand() override {
        getDeviceCommandCalled = true;
        return deviceCommandsReturnValue;
    }
    
    std::vector<Rule> getRuleList() override {
        getRuleListCalled = true;
        return ruleListReturnValue;
    }
    
    std::vector<ModuleCommandDto> getModuleCommand() override {
        getModuleCommandCalled = true;
        return moduleCommandsReturnValue;
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
