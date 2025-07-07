#pragma once

#include <vector>
#include <string>

/**
 * @brief Class to hold sensor value data for both single and multi-channel sensors
 */
class SensorValue {
public:
    std::string sensorUid;
    float currentValue;
    uint32_t lastReadTime;
    std::vector<std::pair<std::string, float>> channelValues;
    bool isMultiChannel;
    
    /**
     * @brief Constructor
     */
    SensorValue() : currentValue(0.0f), lastReadTime(0), isMultiChannel(false) {}
    
    /**
     * @brief Constructor with sensor UID
     * @param uid Sensor UID
     */
    SensorValue(const std::string& uid) : sensorUid(uid), currentValue(0.0f), lastReadTime(0), isMultiChannel(false) {}
    
    /**
     * @brief Get channel value by name
     * @param channelName Channel name
     * @return Channel value, currentValue if not found and not multi-channel
     */
    float getChannelValue(const std::string& channelName) const {
        if (!isMultiChannel) {
            return currentValue;
        }
        
        for (const auto& pair : channelValues) {
            if (pair.first == channelName) {
                return pair.second;
            }
        }
        return 0.0f;
    }
    
    /**
     * @brief Set channel value
     * @param channelName Channel name
     * @param value Channel value
     */
    void setChannelValue(const std::string& channelName, float value) {
        if (!isMultiChannel) {
            currentValue = value;
            return;
        }
        
        for (auto& pair : channelValues) {
            if (pair.first == channelName) {
                pair.second = value;
                return;
            }
        }
        
        channelValues.push_back({channelName, value});
    }
    
    /**
     * @brief Set as multi-channel sensor
     */
    void setMultiChannel(bool multiChannel) {
        isMultiChannel = multiChannel;
        if (!multiChannel) {
            channelValues.clear();
        }
    }
    
    /**
     * @brief Check if has channel values
     * @return True if has channel values, false otherwise
     */
    bool hasChannelValues() const {
        return isMultiChannel && !channelValues.empty();
    }
    
    /**
     * @brief Clear all channel values
     */
    void clearChannelValues() {
        channelValues.clear();
    }
    
    /**
     * @brief Get all channel values
     * @return Vector of channel name-value pairs
     */
    const std::vector<std::pair<std::string, float>>& getChannelValues() const {
        return channelValues;
    }
};
