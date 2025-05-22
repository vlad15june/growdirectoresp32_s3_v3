#ifndef MOCK_DEVICE_SERVICE_H
#define MOCK_DEVICE_SERVICE_H

#include "Application/Services/Devices/IDeviceService.h"

class MockDeviceService : public IDeviceService {
private:
    bool initCalled = false;
    bool initSuccess = true;
    int initCallCount = 0;

public:
    MockDeviceService() = default;
    
    ~MockDeviceService() override = default;
    
    // IDeviceService implementation
    bool init() override {
        initCalled = true;
        initCallCount++;
        return initSuccess;
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
    
    void reset() {
        initCalled = false;
        initSuccess = true;
        initCallCount = 0;
    }
};

#endif // MOCK_DEVICE_SERVICE_H
