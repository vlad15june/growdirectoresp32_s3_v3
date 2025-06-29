#pragma once

#include <string>
#include <vector>
#include "ModbusRegisterConfig.h"
#include "I2CRegisterConfig.h"
#include "SensorType.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"

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
    int pin;
    bool isActive;
    bool isError;
    SensorStatusEnum status;
    
    int modbusDeviceAddress;
    int baudRate;
    std::vector<ModbusRegisterConfig> modbusRegisterConfigs;
    
    std::string i2cAddress;
    std::string chipType;
    std::vector<I2CRegisterConfig> i2cRegisterConfigs;
    
    float currentValue;
    uint32_t lastReadTime;
    std::vector<std::pair<std::string, float>> channelValues;
    
    // Analog sensor calibration values
    float multiplier = 1.0f;
    float offset = 0.0f;

    int sensorTypeId = 0;

    SensorEntity() : isActive(true), isError(false), modbusDeviceAddress(1), baudRate(9600), 
               currentValue(0.0f), lastReadTime(0), multiplier(1.0f), offset(0.0f), sensorTypeId(0), 
               status(SENSOR_STATUS_NEW) {}

    bool isModbusSensor() const { return protocol == "ModBus"; }
    bool isI2CSensor() const { return protocol == "I2C"; }
    bool isAtlasSensor() const { return model.find("atlas_") == 0; }
    bool isMultiChannel() const { return !modbusRegisterConfigs.empty() || !i2cRegisterConfigs.empty(); }
    
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
