#ifndef MOCK_REMOTE_SENSOR_RECEIVER_H
#define MOCK_REMOTE_SENSOR_RECEIVER_H

#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"

class MockRemoteSensorReceiver : public IRemoteSensorReceiver {
public:
    int initCallCount = 0;
    int onRuleSetChangedCallCount = 0;

    bool init() override {
        initCallCount++;
        return true;
    }

    void onRuleSetChanged() override {
        onRuleSetChangedCallCount++;
    }
};

#endif // MOCK_REMOTE_SENSOR_RECEIVER_H
