#ifndef MOCK_REST_API_SERVICE_H
#define MOCK_REST_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include <vector>

class MockRestAPIService : public IRestAPIService {
public:
    // Tracking flags for method calls
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
    bool getDeviceCommandCalled = false;
    bool getRuleListCalled = false;
    bool getModuleCommandCalled = false;
    
    // Return values
    bool isModuleOnlineReturnValue = true;
    bool sendToServerModuleStructureReturnValue = true;
    bool sendRuleStateCommandReturnValue = true;
    bool sendFirmwareVersionReturnValue = true;
    bool sendDeviceCommandExecutionResultReturnValue = true;
    bool sendAnalogInputDeviceCommandExecutionResultReturnValue = true;
    bool sendModuleCommandExecutionResultReturnValue = true;
    
    const ModuleStructure* lastModuleStructure = nullptr;
    const char* lastRuleStateCommand = nullptr;
    const char* lastDeviceCommandResult = nullptr;
    DiagnosticDto lastDiagnosticData;
    UpdateDto lastUpdateData;
    DeviceCommandUpdateDto lastDeviceCommandUpdate;
    ModuleCommandUpdateDto lastModuleCommandUpdate;
    bool sendDiagnosticDataReturnValue = true;
    bool sendModuleUpdateDataReturnValue = true;
    std::vector<DeviceCommandDto> deviceCommandsReturnValue;
    std::vector<Rule> ruleListReturnValue;
    std::vector<ModuleCommandDto> moduleCommandsReturnValue;
    
    int networkFails = 0;
    int apiSendFails = 0;
    float apiSendFailRatio = 0.0f;
    int countOffline = 0;
    
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
        sendModuleUpdateDataCalled = false;
        sendDiagnosticDataReturnValue = true;
        sendModuleUpdateDataReturnValue = true;
        isModuleOnlineReturnValue = true;
        sendToServerModuleStructureReturnValue = true;
        sendRuleStateCommandReturnValue = true;
        sendFirmwareVersionReturnValue = true;
        sendDeviceCommandExecutionResultReturnValue = true;
        sendAnalogInputDeviceCommandExecutionResultReturnValue = true;
        sendModuleCommandExecutionResultReturnValue = true;
        getDeviceCommandCalled = false;
        getRuleListCalled = false;
        getModuleCommandCalled = false;
        deviceCommandsReturnValue.clear();
        ruleListReturnValue.clear();
        moduleCommandsReturnValue.clear();
        
        lastModuleStructure = nullptr;
        lastRuleStateCommand = nullptr;
        lastDeviceCommandResult = nullptr;
        lastDeviceCommandUpdate = DeviceCommandUpdateDto();
        lastModuleCommandUpdate = ModuleCommandUpdateDto();
        
        networkFails = 0;
        apiSendFails = 0;
        apiSendFailRatio = 0.0f;
        countOffline = 0;
    }
    
    bool init() override {
        initCalled = true;
        return true;
    }
    
    bool isModuleOnline() override {
        isModuleOnlineCalled = true;
        return isModuleOnlineReturnValue;
    }
    
    bool sendToServerModuleStructure(const ModuleStructure& moduleStructure) override {
        sendToServerModuleStructureCalled = true;
        lastModuleStructure = &moduleStructure;
        return sendToServerModuleStructureReturnValue;
    }
    
    bool sendRuleStateCommand(const char* msg) override {
        sendRuleStateCommandCalled = true;
        lastRuleStateCommand = msg;
        return sendRuleStateCommandReturnValue;
    }
    
    bool sendByUidRuleStateCommand(const char* uid, const char* msg) override {
        sendByUidRuleStateCommandCalled = true;
        lastRuleStateCommand = msg;
        return true;
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
        lastDeviceCommandResult = msg;
        return sendAnalogInputDeviceCommandExecutionResultReturnValue;
    }
    
    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& commandUpdates) override {
        sendModuleCommandExecutionResultCalled = true;
        if (!commandUpdates.empty()) {
            lastModuleCommandUpdate = commandUpdates[0];
        }
        return sendModuleCommandExecutionResultReturnValue;
    }
    
    bool sendDiagnosticData(const DiagnosticDto& diagnosticData) override {
        sendDiagnosticDataCalled = true;
        lastDiagnosticData = diagnosticData;
        return sendDiagnosticDataReturnValue;
    }
    
    bool sendModuleUpdateData(const UpdateDto& updateData) override {
        sendModuleUpdateDataCalled = true;
        lastUpdateData = updateData;
        return sendModuleUpdateDataReturnValue;
    }
    
    bool getDeviceCommandHttpFailed = false;

    std::vector<DeviceCommandDto> getDeviceCommand(bool* httpFailed = nullptr) override {
        getDeviceCommandCalled = true;
        if (httpFailed != nullptr)
            *httpFailed = getDeviceCommandHttpFailed;
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
    
    std::vector<SensorType> getSensorTypesList() override {
        return std::vector<SensorType>();
    }
    
    int getApiSendFails() const override { return apiSendFails; }
    void resetApiSendFails() override { apiSendFails = 0; }
    
    float getApiSendFailRatio() const override { return apiSendFailRatio; }
    void resetApiSendFailRatio() override { apiSendFailRatio = 0.0f; }
    
    int getCountOffline() const override { return countOffline; }
    void resetCountOffline() override { countOffline = 0; }

    ModuleDeletionQueryResult queryModuleDeletionResult;
    ModuleDeletionQueryResult queryModuleDeletion() override { return queryModuleDeletionResult; }
};

#endif // MOCK_REST_API_SERVICE_H
