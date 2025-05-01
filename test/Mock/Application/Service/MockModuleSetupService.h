#ifndef MOCK_MODULE_SETUP_SERVICE_H
#define MOCK_MODULE_SETUP_SERVICE_H

#include "Application/Services/ModuleSetupService.h"

class MockModuleSetupService : public IModuleSetupService {
public:
    // Track method calls
    bool setupModuleCalled = false;
    int setupModuleCallCount = 0;
    
    // Override the interface method
    void setupModule() override {
        setupModuleCalled = true;
        setupModuleCallCount++;
    }
    
    // Reset tracking state for tests
    void reset() {
        setupModuleCalled = false;
        setupModuleCallCount = 0;
    }
    
    // Constructor and destructor
    MockModuleSetupService() = default;
    ~MockModuleSetupService() override = default;
};

#endif // MOCK_MODULE_SETUP_SERVICE_H
