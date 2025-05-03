#ifndef MOCK_LOOP_SERVICE_FACTORY_H
#define MOCK_LOOP_SERVICE_FACTORY_H

#include "Application/Interfaces/LoopServices/ILoopServiceFactory.h"
#include "MockLoopService.h"
#include <functional>

class MockLoopServiceFactory : public ILoopServiceFactory {
public:
    // Track method calls
    bool createLoopServiceCalled = false;
    int createLoopServiceCallCount = 0;
    ModuleModeEnum lastModuleMode;
    
    // Mock behaviors
    bool shouldThrowException = false;
    std::string exceptionMessage = "Test exception";
    
    // Mock return values
    std::unique_ptr<ILoopService> mockLoopService;
    
    // Override the interface method
    std::unique_ptr<ILoopService> createLoopService(ModuleModeEnum moduleMode) override {
        createLoopServiceCalled = true;
        createLoopServiceCallCount++;
        lastModuleMode = moduleMode;
        
        // Optionally throw an exception based on configured behavior
        if (shouldThrowException) {
            shouldThrowException = false; // Reset after throwing
            throw std::invalid_argument(exceptionMessage);
        }
        
        // If a specific mock service was set, return it
        if (mockLoopService) {
            auto service = std::move(mockLoopService);
            return service;
        }
        
        // Otherwise, create a new MockLoopService
        return std::unique_ptr<ILoopService>(new MockLoopService());
    }
    
    // Helper methods for tests
    void setMockLoopService(std::unique_ptr<ILoopService> service) {
        mockLoopService = std::move(service);
    }
    
    void configureMockToThrowException(bool shouldThrow = true, const std::string& message = "Test exception") {
        shouldThrowException = shouldThrow;
        exceptionMessage = message;
    }
    
    // Reset tracking state for tests
    void reset() {
        createLoopServiceCalled = false;
        createLoopServiceCallCount = 0;
        shouldThrowException = false;
        exceptionMessage = "Test exception";
        mockLoopService.reset();
    }
    
    // Constructor and destructor
    MockLoopServiceFactory() = default;
    ~MockLoopServiceFactory() override = default;
};

#endif // MOCK_LOOP_SERVICE_FACTORY_H
