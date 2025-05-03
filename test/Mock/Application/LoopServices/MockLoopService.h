#ifndef MOCK_LOOP_SERVICE_H
#define MOCK_LOOP_SERVICE_H

#include "Application/Interfaces/LoopServices/ILoopService.h"

class MockLoopService : public ILoopService {
public:
    // Track method calls
    bool loopCalled = false;
    int loopCallCount = 0;
    
    // Override the interface method
    void loop() override {
        loopCalled = true;
        loopCallCount++;
    }
    
    // Reset tracking state for tests
    void reset() {
        loopCalled = false;
        loopCallCount = 0;
    }
    
    // Constructor and destructor
    MockLoopService() = default;
    ~MockLoopService() override = default;
};

#endif // MOCK_LOOP_SERVICE_H
