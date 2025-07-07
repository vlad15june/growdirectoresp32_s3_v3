#ifndef MOCK_SYNC_RULES_USE_CASE_H
#define MOCK_SYNC_RULES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include <ctime>

class MockSyncRulesUseCase : public ISyncRulesUseCase {
public:
    // Tracking flags for method calls
    bool initCalled = false;
    bool loopCalled = false;
    bool forceSyncCalled = false;
    bool getLastSyncStatusCalled = false;
    bool getLastSyncTimeCalled = false;
    
    // Return values
    bool initReturnValue = true;
    bool forceSyncReturnValue = true;
    bool lastSyncStatusReturnValue = true;
    time_t lastSyncTimeReturnValue = 0;
    
    // Call counters
    int loopCallCount = 0;
    int forceSyncCallCount = 0;
    
    void reset() {
        initCalled = false;
        loopCalled = false;
        forceSyncCalled = false;
        getLastSyncStatusCalled = false;
        getLastSyncTimeCalled = false;
        
        initReturnValue = true;
        forceSyncReturnValue = true;
        lastSyncStatusReturnValue = true;
        lastSyncTimeReturnValue = 0;
        
        loopCallCount = 0;
        forceSyncCallCount = 0;
    }
    
    // ISyncRulesUseCase implementation
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
    
    bool getLastSyncStatus() const override {
        const_cast<MockSyncRulesUseCase*>(this)->getLastSyncStatusCalled = true;
        return lastSyncStatusReturnValue;
    }
    
    time_t getLastSyncTime() const override {
        const_cast<MockSyncRulesUseCase*>(this)->getLastSyncTimeCalled = true;
        return lastSyncTimeReturnValue;
    }
};

#endif // MOCK_SYNC_RULES_USE_CASE_H
