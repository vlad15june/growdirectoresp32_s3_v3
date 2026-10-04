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

    bool hasSensorValue(const std::string& sensorUid) const override {
        return values.find(sensorUid) != values.end();
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

    // Channel values are kept per sensor in insertion order, like the real SensorValue
    std::map<std::string, std::vector<std::pair<std::string, float>>> channelValues;
    std::map<std::string, bool> multiChannelFlags;

    int setChannelValueCallCount = 0;
    std::string lastSetChannelUid;
    float lastSetChannelValue = 0.0f;

    float getChannelValue(const std::string& sensorUid, const std::string& channelName) const override {
        auto it = channelValues.find(sensorUid);
        if (it == channelValues.end()) return 0.0f;
        for (const auto& pair : it->second) {
            if (pair.first == channelName) return pair.second;
        }
        return 0.0f;
    }

    void setChannelValue(const std::string& sensorUid, const std::string& channelName, float value) override {
        setChannelValueCallCount++;
        lastSetChannelUid = channelName;
        lastSetChannelValue = value;

        auto& channels = channelValues[sensorUid];
        for (auto& pair : channels) {
            if (pair.first == channelName) {
                pair.second = value;
                return;
            }
        }
        channels.push_back({channelName, value});
    }

    std::vector<std::pair<std::string, float>> getChannelValues(const std::string& sensorUid) const override {
        auto it = channelValues.find(sensorUid);
        return it != channelValues.end() ? it->second : std::vector<std::pair<std::string, float>>();
    }

    bool hasChannelValues(const std::string& sensorUid) const override {
        auto it = channelValues.find(sensorUid);
        return it != channelValues.end() && !it->second.empty();
    }

    void clearChannelValues(const std::string& sensorUid) override {
        channelValues.erase(sensorUid);
    }

    void clearAllChannelValues() override {
        channelValues.clear();
    }

    void setSensorMultiChannel(const std::string& sensorUid, bool isMultiChannel) override {
        multiChannelFlags[sensorUid] = isMultiChannel;
    }

    bool isSensorMultiChannel(const std::string& sensorUid) const override {
        auto it = multiChannelFlags.find(sensorUid);
        return it != multiChannelFlags.end() && it->second;
    }

    void removeSensor(const std::string& sensorUid) override {
        values.erase(sensorUid);
        lastReadTimes.erase(sensorUid);
        channelValues.erase(sensorUid);
        multiChannelFlags.erase(sensorUid);
    }

    std::vector<std::string> getSensorsWithChannelValues() const override {
        std::vector<std::string> result;
        for (const auto& entry : channelValues) {
            if (!entry.second.empty()) result.push_back(entry.first);
        }
        return result;
    }

    void reset() {
        values.clear();
        lastReadTimes.clear();
        channelValues.clear();
        multiChannelFlags.clear();
        setSensorValueCallCount = 0;
        setLastReadTimeCallCount = 0;
        setChannelValueCallCount = 0;
        lastSetUid.clear();
        lastSetChannelUid.clear();
        lastSetValue = 0.0f;
        lastSetChannelValue = 0.0f;
        lastSetLastReadTime = 0;
    }
};

#endif // MOCK_SENSOR_VALUE_MODEL_H
