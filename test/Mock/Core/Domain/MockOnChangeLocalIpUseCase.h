#ifndef MOCK_ON_CHANGE_LOCAL_IP_USE_CASE_H
#define MOCK_ON_CHANGE_LOCAL_IP_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"

class MockOnChangeLocalIpUseCase : public IOnChangeLocalIpUseCase {
public:
    // Track method calls
    bool performCalled = false;
    int performCallCount = 0;
    
    // Implementation of the interface method
    void perform() override {
        performCalled = true;
        performCallCount++;
    }
    
    // Reset state for fresh test
    void reset() {
        performCalled = false;
        performCallCount = 0;
    }
    
    // Constructor
    MockOnChangeLocalIpUseCase() {
        reset();
    }
};

#endif // MOCK_ON_CHANGE_LOCAL_IP_USE_CASE_H
