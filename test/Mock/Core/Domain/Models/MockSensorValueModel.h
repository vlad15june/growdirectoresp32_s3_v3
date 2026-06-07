#ifndef MOCK_SENSOR_VALUE_MODEL_H
#define MOCK_SENSOR_VALUE_MODEL_H

#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include <map>
#include <vector>
#include <string>

class MockSensorValueModel : public ISensorValueModel {
public:
    std::map<std::string, float> values;
    std::map<std::string, uint32_t> lastReadTimes;

    int setSensorValueCallCount = 0;
    int setLastReadTimeCallCount = 0;
    std::string lastSetUid;
    float lastSetValue = 0.0f;
    uint32_t lastSetLastReadTime = 0;

    bool init() override {
        return true;
    }

    float getSensorValue(const std::string& sensorUid) const override {
        auto it = values.find(sensorUid);
        return it != values.end() ? it->second : 0.0f;
    }

    void setSensorValue(const std::string& sensorUid, float value) override {
        values[sensorUid] = value;
        setSensorValueCallCount++;
        lastSetUid = sensorUid;
        lastSetValue = value;
    }

    void setLastReadTime(const std::string& sensorUid, uint32_t time) override {
        lastReadTimes[sensorUid] = time;
        setLastReadTimeCallCount++;
        lastSetLastReadTime = time;
    }

    uint32_t getLastReadTime(const std::string& sensorUid) const override {
        auto it = lastReadTimes.find(sensorUid);
        return it != lastReadTimes.end() ? it->second : 0;
    }

    float getChannelValue(const std::string&, const std::string&) const override { return 0.0f; }
    void setChannelValue(const std::string&, const std::string&, float) override {}
    std::vector<std::pair<std::string, float>> getChannelValues(const std::string&) const override { return {}; }
    bool hasChannelValues(const std::string&) const override { return false; }
    void clearChannelValues(const std::string&) override {}
    void clearAllChannelValues() override {}
    void setSensorMultiChannel(const std::string&, bool) override {}
    bool isSensorMultiChannel(const std::string&) const override { return false; }
    void removeSensor(const std::string&) override {}
    std::vector<std::string> getSensorsWithChannelValues() const override { return {}; }
};

#endif // MOCK_SENSOR_VALUE_MODEL_H
