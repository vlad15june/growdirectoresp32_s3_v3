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
    
    void reset() {
        isModuleOnlineCalled = false;
        sendToServerModuleStructureCalled = false;
        sendDiagnosticDataCalled = false;
        sendModuleUpdateDataCalled = false;
        sendDeviceCommandExecutionResultCalled = false;
        sendCommandResultCalled = false;
        lastModuleStructure = nullptr;
        isModuleOnlineReturnValue = true;
        sendDiagnosticDataReturnValue = true;
        sendModuleUpdateDataReturnValue = true;
        sendToServerModuleStructureReturnValue = true;
        sendDeviceCommandExecutionResultReturnValue = true;
        sendCommandResultReturnValue = true;
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
};

#endif // MOCK_MQTT_API_SERVICE_H
