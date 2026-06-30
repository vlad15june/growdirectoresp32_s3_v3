#ifndef MOCK_MQTT_API_SERVICE_H
#define MOCK_MQTT_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IMqttApiService.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"


class MockMqttApiService : public IMqttApiService {
public:
    bool isModuleOnlineCalled = false;
    bool sendToServerModuleStructureCalled = false;
    bool sendDiagnosticDataCalled = false;
    bool sendModuleUpdateDataCalled = false;
    bool sendDeviceCommandExecutionResultCalled = false;
    bool sendCommandResultCalled = false;
    bool sendRuleStateCommandCalled = false;
    
    const ModuleStructure* lastModuleStructure = nullptr;
    DiagnosticDto lastDiagnosticData;
    UpdateDto lastUpdateData;
    DeviceCommandUpdateDto lastDeviceCommandUpdate;
    CommandResultDto lastCommandResult;
    bool isModuleOnlineReturnValue = true;
    bool sendDiagnosticDataReturnValue = true;
    bool sendModuleUpdateDataReturnValue = true;
    bool sendToServerModuleStructureReturnValue = true;
    bool sendDeviceCommandExecutionResultReturnValue = true;
    bool sendCommandResultReturnValue = true;
    bool sendRuleStateCommandReturnValue = true;
    const char* lastRuleStateCommand = nullptr;
    
    void reset() {
        isModuleOnlineCalled = false;
        sendToServerModuleStructureCalled = false;
        sendDiagnosticDataCalled = false;
        sendModuleUpdateDataCalled = false;
        sendDeviceCommandExecutionResultCalled = false;
        sendCommandResultCalled = false;
        sendRuleStateCommandCalled = false;
        lastModuleStructure = nullptr;
        lastRuleStateCommand = nullptr;
        isModuleOnlineReturnValue = true;
        sendDiagnosticDataReturnValue = true;
        sendModuleUpdateDataReturnValue = true;
        sendToServerModuleStructureReturnValue = true;
        sendDeviceCommandExecutionResultReturnValue = true;
        sendCommandResultReturnValue = true;
        sendRuleStateCommandReturnValue = true;
    }
    
    // IMqttApiService interface implementation
    bool isModuleOnline() override {
        isModuleOnlineCalled = true;
        return isModuleOnlineReturnValue;
    }
    
    bool sendToServerModuleStructure(const ModuleStructure& moduleStructure) override {
        sendToServerModuleStructureCalled = true;
        lastModuleStructure = &moduleStructure;
        return sendToServerModuleStructureReturnValue;
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
    
    bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& commandUpdate) override {
        sendDeviceCommandExecutionResultCalled = true;
        lastDeviceCommandUpdate = commandUpdate;
        return sendDeviceCommandExecutionResultReturnValue;
    }
    
    bool sendCommandResult(const CommandResultDto& commandResult) override {
        sendCommandResultCalled = true;
        lastCommandResult = commandResult;
        return sendCommandResultReturnValue;
    }

    bool sendRawIoResultCalled = false;
    RawIoResultDto lastRawIoResult;
    bool sendRawIoResultReturnValue = true;

    bool sendRawIoResult(const RawIoResultDto& result) override {
        sendRawIoResultCalled = true;
        lastRawIoResult = result;
        return sendRawIoResultReturnValue;
    }

    bool sendRuleStateCommand(const char* msg) override {
        sendRuleStateCommandCalled = true;
        lastRuleStateCommand = msg;
        return sendRuleStateCommandReturnValue;
    }

    bool sendByUidRuleStateCommandCalled = false;
    const char* lastByUidRuleStateUid = nullptr;
    const char* lastByUidRuleStateCommand = nullptr;
    bool sendByUidRuleStateCommandReturnValue = true;

    bool sendByUidRuleStateCommand(const char* uid, const char* msg) override {
        sendByUidRuleStateCommandCalled = true;
        lastByUidRuleStateUid = uid;
        lastByUidRuleStateCommand = msg;
        return sendByUidRuleStateCommandReturnValue;
    }

    bool sendRuleSensorOfflineEventCalled = false;
    std::string lastRuleSensorOfflineEvent;
    bool sendRuleSensorOfflineEventReturnValue = true;

    bool sendRuleSensorOfflineEvent(const char* msg) override {
        sendRuleSensorOfflineEventCalled = true;
        lastRuleSensorOfflineEvent = msg ? msg : "";
        return sendRuleSensorOfflineEventReturnValue;
    }
};

#endif // MOCK_MQTT_API_SERVICE_H
