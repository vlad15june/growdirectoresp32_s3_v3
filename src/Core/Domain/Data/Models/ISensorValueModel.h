#pragma once

#include <vector>
#include <string>

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
