#ifndef MOCK_SYNC_SENSOR_TYPES_USE_CASE_H
#define MOCK_SYNC_SENSOR_TYPES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include <ctime>

class MockSyncSensorTypesUseCase : public ISyncSensorTypesUseCase {
public:
    // Tracking flags for method calls
    bool initCalled = false;
    bool loopCalled = false;
    bool forceSyncCalled = false;
    bool syncNowCalled = false;
    bool getLastSyncStatusCalled = false;
    bool getLastSyncTimeCalled = false;
    
    // Return values
    bool initReturnValue = true;
    bool forceSyncReturnValue = true;
    bool syncNowReturnValue = true;
    bool lastSyncStatusReturnValue = true;
    time_t lastSyncTimeReturnValue = 0;
    
    // Call counters
    int loopCallCount = 0;
    int forceSyncCallCount = 0;
    int syncNowCallCount = 0;
    
    void reset() {
        initCalled = false;
        loopCalled = false;
        forceSyncCalled = false;
        syncNowCalled = false;
        getLastSyncStatusCalled = false;
        getLastSyncTimeCalled = false;
        
        initReturnValue = true;
        forceSyncReturnValue = true;
        syncNowReturnValue = true;
        lastSyncStatusReturnValue = true;
        lastSyncTimeReturnValue = 0;
        
        loopCallCount = 0;
        forceSyncCallCount = 0;
        syncNowCallCount = 0;
    }
    
    // ISyncSensorTypesUseCase implementation
    bool init() override {
        initCalled = true;
        return initReturnValue;
    }
    
    void loop() override {
        loopCalled = true;
        loopCallCount++;
    }
    
    bool forceSync() override {
        forceSyncCalled = true;
        forceSyncCallCount++;
        return forceSyncReturnValue;
    }

    bool syncNow() override {
        syncNowCalled = true;
        syncNowCallCount++;
        return syncNowReturnValue;
    }
    
    bool getLastSyncStatus() const override {
        const_cast<MockSyncSensorTypesUseCase*>(this)->getLastSyncStatusCalled = true;
        return lastSyncStatusReturnValue;
    }
    
    time_t getLastSyncTime() const override {
        const_cast<MockSyncSensorTypesUseCase*>(this)->getLastSyncTimeCalled = true;
        return lastSyncTimeReturnValue;
    }
};

#endif // MOCK_SYNC_SENSOR_TYPES_USE_CASE_H
