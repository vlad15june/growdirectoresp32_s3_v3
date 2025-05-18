#ifndef MOCK_MODULE_SETUP_SERVICE_H
#define MOCK_MODULE_SETUP_SERVICE_H

#include "Application/Services/ModuleSetupService.h"
#include "Mock/Application/LoopServices/MockLoopService.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"

class MockModuleSetupService : public IModuleSetupService {
public:
    // Track method calls
    bool setupModuleCalled = false;
    int setupModuleCallCount = 0;
    bool getLoopServiceCalled = false;
    int getLoopServiceCallCount = 0;
    bool setupMqttConnectionCalled = false;
    int setupMqttConnectionCallCount = 0;
    
    // Mock return values
    std::unique_ptr<ILoopService> mockLoopService;
    ModuleModeEnum returnedModuleMode = ModuleModeEnum::USER_CAN_CONFIGURE;
    
    // Add reference to MqttManager
    IMqttManager* mqttManager = nullptr;
    
    // Override the interface methods
    void setupModule() override {
        setupModuleCalled = true;
        setupModuleCallCount++;
    }
    
    std::unique_ptr<ILoopService> getLoopService() override {
        getLoopServiceCalled = true;
        getLoopServiceCallCount++;
        
        // If a specific mock service was set, return it
        if (mockLoopService) {
            return std::move(mockLoopService);
        }
        
        // Otherwise, create a new MockLoopService
        return std::unique_ptr<ILoopService>(new MockLoopService());
    }
    
    // Expose setupMqttConnection for testing
    void setupMqttConnection() {
        setupMqttConnectionCalled = true;
        setupMqttConnectionCallCount++;
        
        // Mock implementation - just delegate to the mqtt manager if available
        if (mqttManager) {
            mqttManager->connect();
        }
    }
    
    // Helper methods for tests
    void setMockLoopService(std::unique_ptr<ILoopService> service) {
        mockLoopService = std::move(service);
    }
    
    void setMqttManager(IMqttManager* manager) {
        mqttManager = manager;
    }
    
    // Reset tracking state for tests
    void reset() {
        setupModuleCalled = false;
        setupModuleCallCount = 0;
        getLoopServiceCalled = false;
        getLoopServiceCallCount = 0;
        setupMqttConnectionCalled = false;
        setupMqttConnectionCallCount = 0;
        mockLoopService.reset();
        returnedModuleMode = ModuleModeEnum::USER_CAN_CONFIGURE;
        mqttManager = nullptr;
    }
    
    // Constructor and destructor
    MockModuleSetupService() = default;
    ~MockModuleSetupService() override = default;
};

#endif // MOCK_MODULE_SETUP_SERVICE_H
