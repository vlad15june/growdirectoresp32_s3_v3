#ifndef MOCK_MQTT_API_SERVICE_H
#define MOCK_MQTT_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IMqttApiService.h"
#include "Application/DTOs/UpdateDto.h"


class MockMqttApiService : public IMqttApiService {
public:
    bool isModuleOnlineCalled = false;
    bool sendToServerModuleStructureCalled = false;
    bool sendDiagnosticDataCalled = false;
    bool sendModuleUpdateDataCalled = false;
    
    const ModuleStructure* lastModuleStructure = nullptr;
    DiagnosticDto lastDiagnosticData;
    UpdateDto lastUpdateData;
    bool isModuleOnlineReturnValue = true;
    bool sendDiagnosticDataReturnValue = true;
    bool sendModuleUpdateDataReturnValue = true;
    
    void reset() {
        isModuleOnlineCalled = false;
        sendToServerModuleStructureCalled = false;
        sendDiagnosticDataCalled = false;
        sendModuleUpdateDataCalled = false;
        lastModuleStructure = nullptr;
        isModuleOnlineReturnValue = true;
        sendDiagnosticDataReturnValue = true;
        sendModuleUpdateDataReturnValue = true;
    }
    
    // IMqttApiService interface implementation
    bool isModuleOnline() override {
        isModuleOnlineCalled = true;
        return isModuleOnlineReturnValue;
    }
    
    void sendToServerModuleStructure(const ModuleStructure& moduleStructure) override {
        sendToServerModuleStructureCalled = true;
        lastModuleStructure = &moduleStructure;
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
};

#endif // MOCK_MQTT_API_SERVICE_H
