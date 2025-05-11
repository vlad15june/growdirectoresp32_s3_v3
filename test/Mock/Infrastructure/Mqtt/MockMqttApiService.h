#ifndef MOCK_MQTT_API_SERVICE_H
#define MOCK_MQTT_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IMqttApiService.h"

class MockMqttApiService : public IMqttApiService {
public:
    bool isModuleOnlineCalled = false;
    bool sendToServerModuleStructureCalled = false;
    bool sendDiagnosticDataCalled = false;
    
    const ModuleStructure* lastModuleStructure = nullptr;
    DiagnosticDto lastDiagnosticData;
    bool isModuleOnlineReturnValue = true;
    bool sendDiagnosticDataReturnValue = true;
    
    void reset() {
        isModuleOnlineCalled = false;
        sendToServerModuleStructureCalled = false;
        sendDiagnosticDataCalled = false;
        lastModuleStructure = nullptr;
        isModuleOnlineReturnValue = true;
        sendDiagnosticDataReturnValue = true;
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
};

#endif // MOCK_MQTT_API_SERVICE_H
