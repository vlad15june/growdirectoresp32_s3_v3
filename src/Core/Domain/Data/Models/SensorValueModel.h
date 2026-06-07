#pragma once

#include <map>
#include <vector>
#include <string>
#include <mutex>
#include "ISensorValueModel.h"
#include "Core/Domain/Data/Entities/Sensor/SensorValue.h"

/**
 * @brief Implementation of the sensor value model
 */
class SensorValueModel : public ISensorValueModel {
private:
    std::map<std::string, SensorValue> sensorValues;
    mutable std::mutex valuesMutex;

public:
    /**
     * @brief Constructor
     */
    SensorValueModel() = default;
    
    /**
     * @brief Destructor
     */
    ~SensorValueModel() override = default;
    
    /**
     * @brief Initialize the sensor value model
     * @return True if initialization was successful, false otherwise
     */
    bool init() override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        sensorValues.clear();
        return true;
    }
    
    /**
     * @brief Get channel value for a specific sensor and channel
     * @param sensorUid Sensor UID
     * @param channelName Channel name
     * @return Channel value, 0.0f if not found
     */
    float getChannelValue(const std::string& sensorUid, const std::string& channelName) const override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto sensorIt = sensorValues.find(sensorUid);
        if (sensorIt != sensorValues.end()) {
            return sensorIt->second.getChannelValue(channelName);
        }
        return 0.0f;
    }
    
    /**
     * @brief Set channel value for a specific sensor and channel
     * @param sensorUid Sensor UID
     * @param channelName Channel name
     * @param value Channel value
     */
    void setChannelValue(const std::string& sensorUid, const std::string& channelName, float value) override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto& sensorValue = sensorValues[sensorUid];
        if (sensorValue.sensorUid.empty()) {
            sensorValue.sensorUid = sensorUid;
        }
        sensorValue.setChannelValue(channelName, value);
    }
    
    /**
     * @brief Get all channel values for a sensor
     * @param sensorUid Sensor UID
     * @return Vector of channel name-value pairs
     */
    std::vector<std::pair<std::string, float>> getChannelValues(const std::string& sensorUid) const override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto sensorIt = sensorValues.find(sensorUid);
        if (sensorIt != sensorValues.end()) {
            return sensorIt->second.getChannelValues();
        }
        return {};
    }
    
    /**
     * @brief Check if sensor has channel values
     * @param sensorUid Sensor UID
     * @return True if sensor has channel values, false otherwise
     */
    bool hasChannelValues(const std::string& sensorUid) const override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto sensorIt = sensorValues.find(sensorUid);
        return sensorIt != sensorValues.end() && sensorIt->second.hasChannelValues();
    }
    
    /**
     * @brief Clear all channel values for a sensor
     * @param sensorUid Sensor UID
     */
    void clearChannelValues(const std::string& sensorUid) override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto sensorIt = sensorValues.find(sensorUid);
        if (sensorIt != sensorValues.end()) {
            sensorIt->second.clearChannelValues();
        }
    }
    
    /**
     * @brief Clear all channel values for all sensors
     */
    void clearAllChannelValues() override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        sensorValues.clear();
    }
    
    /**
     * @brief Remove sensor from the model
     * @param sensorUid Sensor UID
     */
    void removeSensor(const std::string& sensorUid) override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        sensorValues.erase(sensorUid);
    }
    
    /**
     * @brief Get list of sensors that have channel values
     * @return Vector of sensor UIDs
     */
    std::vector<std::string> getSensorsWithChannelValues() const override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        std::vector<std::string> sensorUids;
        for (const auto& pair : sensorValues) {
            if (pair.second.hasChannelValues()) {
                sensorUids.push_back(pair.first);
            }
        }
        return sensorUids;
    }
    
    /**
     * @brief Get single sensor value
     * @param sensorUid Sensor UID
     * @return Sensor value, 0.0f if not found
     */
    float getSensorValue(const std::string& sensorUid) const override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto sensorIt = sensorValues.find(sensorUid);
        if (sensorIt != sensorValues.end()) {
            return sensorIt->second.currentValue;
        }
        return 0.0f;
    }
    
    /**
     * @brief Set single sensor value
     * @param sensorUid Sensor UID
     * @param value Sensor value
     */
    void setSensorValue(const std::string& sensorUid, float value) override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto& sensorValue = sensorValues[sensorUid];
        if (sensorValue.sensorUid.empty()) {
            sensorValue.sensorUid = sensorUid;
        }
        sensorValue.currentValue = value;
    }

    void setLastReadTime(const std::string& sensorUid, uint32_t time) override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto& sensorValue = sensorValues[sensorUid];
        if (sensorValue.sensorUid.empty()) {
            sensorValue.sensorUid = sensorUid;
        }
        sensorValue.lastReadTime = time;
    }

    uint32_t getLastReadTime(const std::string& sensorUid) const override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto sensorIt = sensorValues.find(sensorUid);
        if (sensorIt != sensorValues.end()) {
            return sensorIt->second.lastReadTime;
        }
        return 0;
    }
    
    /**
     * @brief Set sensor as multi-channel
     * @param sensorUid Sensor UID
     * @param isMultiChannel True if multi-channel, false otherwise
     */
    void setSensorMultiChannel(const std::string& sensorUid, bool isMultiChannel) override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto& sensorValue = sensorValues[sensorUid];
        if (sensorValue.sensorUid.empty()) {
            sensorValue.sensorUid = sensorUid;
        }
        sensorValue.setMultiChannel(isMultiChannel);
    }
    
    /**
     * @brief Check if sensor is multi-channel
     * @param sensorUid Sensor UID
     * @return True if multi-channel, false otherwise
     */
    bool isSensorMultiChannel(const std::string& sensorUid) const override {
        std::lock_guard<std::mutex> lock(valuesMutex);
        auto sensorIt = sensorValues.find(sensorUid);
        return sensorIt != sensorValues.end() && sensorIt->second.isMultiChannel;
    }
};
