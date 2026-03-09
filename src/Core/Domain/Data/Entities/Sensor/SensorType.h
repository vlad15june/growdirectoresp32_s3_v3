#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "ModbusRegisterConfig.h"
#include "I2CRegisterConfig.h"
#include "AnalogPinConfig.h"

enum class SensorProtocol {
    ANALOG_PROTOCOL,
    ANALOG_BNC_PROTOCOL,
    ANALOG_EC2CH_PROTOCOL,
    ANALOG_PH_PROTOCOL,
    I2C_PROTOCOL,
    MODBUS_PROTOCOL
};

/**
 * @brief Individual sensor type definition (used in sensorTypes array)
 */
class SubSensorType {
public:
    std::string name;
    std::string displayName;
    bool isAnalog;
    bool canCalibrate;
    int validCalibrationPeriodDays;
    int calibrationTimeInSeconds;
    float minValue;
    float maxValue;
    float limitMinValue;
    float limitMaxValue;
    std::string unit;
    float step;
    float precisionPercent;
    float precisionValue;
    std::string createdAt;
    std::string updatedAt;

    SubSensorType() : 
        isAnalog(false),
        canCalibrate(false),
        validCalibrationPeriodDays(0),
        calibrationTimeInSeconds(0),
        minValue(0.0f),
        maxValue(0.0f),
        limitMinValue(0.0f),
        limitMaxValue(0.0f),
        step(0.0f),
        precisionPercent(0.0f),
        precisionValue(0.0f) {}
};

class SensorType {
public:
    // Core identification
    std::string uid;
    std::string name;
    std::string displayName;
    bool canScan;
    SensorProtocol protocol;
    bool isSupported;
    
    // Timestamps
    std::string createdAt;
    std::string updatedAt;
    
    // Array of individual sensor types
    std::vector<SubSensorType> sensorTypes;
    
    // Communication protocol settings
    std::string i2cAddress;
    std::string chipType;
    std::vector<I2CRegisterConfig> i2cRegisterConfig;
    
    int modbusDeviceAddress;
    int baudRate;
    std::vector<ModbusRegisterConfig> modbusRegisterConfig;
    
    std::vector<AnalogPinConfig> analogPinConfig;

    // Scan priority - higher value means higher priority; sensor types with higher priority are scanned first
    int priorityForScan;

    // Legacy compatibility - keeping id for backward compatibility
    int id; 

    SensorType() : 
        canScan(true),
        protocol(SensorProtocol::ANALOG_PROTOCOL),
        isSupported(true),
        modbusDeviceAddress(1),
        baudRate(9600),
        priorityForScan(0),
        id(0) {}

    bool isAnalogSensor() const { return protocol == SensorProtocol::ANALOG_PROTOCOL; }
    bool isAnalogBncSensor() const { return protocol == SensorProtocol::ANALOG_BNC_PROTOCOL; }
    bool isAnalogEc2chSensor() const { return protocol == SensorProtocol::ANALOG_EC2CH_PROTOCOL; }
    bool isAnalogPhSensor() const { return protocol == SensorProtocol::ANALOG_PH_PROTOCOL; }
    bool isI2CSensor() const { return protocol == SensorProtocol::I2C_PROTOCOL; }
    bool isModbusSensor() const { return protocol == SensorProtocol::MODBUS_PROTOCOL; }
    
    // Helper methods for the new structure
    bool hasAnalogSensorTypes() const {
        for (const auto& sensorType : sensorTypes) {
            if (sensorType.isAnalog) return true;
        }
        return false;
    }
    
    bool hasCalibratableSensorTypes() const {
        for (const auto& sensorType : sensorTypes) {
            if (sensorType.canCalibrate) return true;
        }
        return false;
    }
    
    SubSensorType* getSensorTypeByName(const std::string& sensorTypeName) {
        for (auto& sensorType : sensorTypes) {
            if (sensorType.name == sensorTypeName) {
                return &sensorType;
            }
        }
        return nullptr;
    }
    
    const SubSensorType* getSensorTypeByName(const std::string& sensorTypeName) const {
        for (const auto& sensorType : sensorTypes) {
            if (sensorType.name == sensorTypeName) {
                return &sensorType;
            }
        }
        return nullptr;
    }
    
    std::string getProtocolString() const {
        switch (protocol) {
            case SensorProtocol::ANALOG_PROTOCOL: return "Analog";
            case SensorProtocol::ANALOG_BNC_PROTOCOL: return "AnalogBnc";
            case SensorProtocol::ANALOG_EC2CH_PROTOCOL: return "AnalogEc2ch";
            case SensorProtocol::ANALOG_PH_PROTOCOL: return "AnalogPh";
            case SensorProtocol::I2C_PROTOCOL: return "I2C";
            case SensorProtocol::MODBUS_PROTOCOL: return "ModBus";
            default: return "Unknown";
        }
    }
    
    void setProtocolFromString(const std::string& protocolStr) {
        std::string lowerProtocol = protocolStr;
        std::transform(lowerProtocol.begin(), lowerProtocol.end(), lowerProtocol.begin(), ::tolower);
        
        if (lowerProtocol == "analog") {
            protocol = SensorProtocol::ANALOG_PROTOCOL;
        } else if (lowerProtocol == "analogbnc") {
            protocol = SensorProtocol::ANALOG_BNC_PROTOCOL;
        } else if (lowerProtocol == "analogec2ch") {
            protocol = SensorProtocol::ANALOG_EC2CH_PROTOCOL;
        } else if (lowerProtocol == "analogph") {
            protocol = SensorProtocol::ANALOG_PH_PROTOCOL;
        } else if (lowerProtocol == "i2c") {
            protocol = SensorProtocol::I2C_PROTOCOL;
        } else if (lowerProtocol == "modbus") {
            protocol = SensorProtocol::MODBUS_PROTOCOL;
        } else {
            protocol = SensorProtocol::ANALOG_PROTOCOL;
        }
    }
};
