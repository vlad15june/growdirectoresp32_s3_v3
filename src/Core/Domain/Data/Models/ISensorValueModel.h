#pragma once

#include <vector>
#include <string>
#include <cstdint>

/**
 * @brief Interface for sensor value model management
 */
class ISensorValueModel {
public:
    virtual ~ISensorValueModel() = default;
    
    // Initialization
    virtual bool init() = 0;
    
    // Single sensor value operations
    virtual float getSensorValue(const std::string& sensorUid) const = 0;
    virtual void setSensorValue(const std::string& sensorUid, float value) = 0;
    // Whether a single value has been stored for this uid. getSensorValue() answers 0 either way.
    virtual bool hasSensorValue(const std::string& sensorUid) const = 0;
    virtual void setLastReadTime(const std::string& sensorUid, uint32_t time) = 0;
    virtual uint32_t getLastReadTime(const std::string& sensorUid) const = 0;
    
    // Channel value operations
    virtual float getChannelValue(const std::string& sensorUid, const std::string& channelName) const = 0;
    virtual void setChannelValue(const std::string& sensorUid, const std::string& channelName, float value) = 0;
    virtual std::vector<std::pair<std::string, float>> getChannelValues(const std::string& sensorUid) const = 0;
    virtual bool hasChannelValues(const std::string& sensorUid) const = 0;
    virtual void clearChannelValues(const std::string& sensorUid) = 0;
    virtual void clearAllChannelValues() = 0;
    
    // Multi-channel sensor management
    virtual void setSensorMultiChannel(const std::string& sensorUid, bool isMultiChannel) = 0;
    virtual bool isSensorMultiChannel(const std::string& sensorUid) const = 0;
    
    // Sensor management
    virtual void removeSensor(const std::string& sensorUid) = 0;
    virtual std::vector<std::string> getSensorsWithChannelValues() const = 0;
};
