#ifndef MOCK_DEVICE_SERVICE_H
#define MOCK_DEVICE_SERVICE_H

#include "Application/Services/Devices/IDeviceService.h"

class MockDeviceService : public IDeviceService {
private:
    bool initCalled = false;
    bool initSuccess = true;
    int initCallCount = 0;
    
    bool loopCalled = false;
    int loopCallCount = 0;

public:
    MockDeviceService() = default;
    
    ~MockDeviceService() override = default;
    
    // IDeviceService implementation
    bool init() override {
        initCalled = true;
        initCallCount++;
        return initSuccess;
    }
    
    // Implement the required loop method
    void loop() override {
        loopCalled = true;
        loopCallCount++;
    }

    // Helper methods for testing
    void setInitSuccess(bool success) {
        initSuccess = success;
    }
    
    bool wasInitCalled() const {
        return initCalled;
    }
    
    int getInitCallCount() const {
        return initCallCount;
    }
    
    bool wasLoopCalled() const {
        return loopCalled;
    }
    
    int getLoopCallCount() const {
        return loopCallCount;
    }

    void reset() {
        initCalled = false;
        initSuccess = true;
        initCallCount = 0;
        loopCalled = false;
        loopCallCount = 0;
    }
};

#endif // MOCK_DEVICE_SERVICE_H
