#pragma once

#include <string>
#include <vector>
#include "ModbusRegisterConfig.h"
#include "I2CRegisterConfig.h"

/**
 * @brief Base sensor entity class
 */
class SensorEntity {
public:
    std::string uid;
    std::string sensorType;
    std::string model;
    std::string protocol;
    std::string createdAt;
    bool isActive;
    bool isError;
    
    int modbusDeviceAddress;
    int baudRate;
    std::vector<ModbusRegisterConfig> modbusRegisterConfigs;
    
    std::string i2cAddress;
    std::string chipType;
    std::vector<std::pair<std::string, I2CRegisterConfig>> i2cRegisterConfigs;
    
    float currentValue;
    uint32_t lastReadTime;
    std::vector<std::pair<std::string, float>> channelValues;

    SensorEntity() : isActive(true), isError(false), modbusDeviceAddress(1), baudRate(9600), 
               currentValue(0.0f), lastReadTime(0) {}

    bool isModbusSensor() const { return protocol == "RS485"; }
    bool isI2CSensor() const { return protocol == "I2C"; }
    bool isAtlasSensor() const { return model.find("atlas_") == 0; }
    bool isMultiChannel() const { return !channelValues.empty(); }
    
    float getChannelValue(const std::string& channelName) const {
        for (const auto& pair : channelValues) {
            if (pair.first == channelName) {
                return pair.second;
            }
        }
        return currentValue;
    }
    
    void setChannelValue(const std::string& channelName, float value) {
        for (auto& pair : channelValues) {
            if (pair.first == channelName) {
                pair.second = value;
                return;
            }
        }
        channelValues.push_back({channelName, value});
    }
};
