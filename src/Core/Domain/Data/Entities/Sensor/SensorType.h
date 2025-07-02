#pragma once

#include <string>
#include <vector>
#include "ModbusRegisterConfig.h"
#include "I2CRegisterConfig.h"

enum class SensorProtocol {
    ANALOG_PROTOCOL,
    ANALOG_BNC_PROTOCOL,
    ANALOG_EC2CH_PROTOCOL,
    ANALOG_PH_PROTOCOL,
    I2C_PROTOCOL,
    MODBUS_PROTOCOL
};

class SensorType {
public:
    int id;
    bool canScan;
    SensorProtocol protocol;
    bool isSupported;

    //NOTE: from sensorTypeTable - need or not????
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
    
    
    
    std::string i2cAddress;
    std::string chipType;
    std::vector<I2CRegisterConfig> i2cRegisterConfig;
    
    int modbusDeviceAddress;
    int baudRate;
    std::vector<ModbusRegisterConfig> modbusRegisterConfig;

    SensorType() : 
        id(0),
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
        precisionValue(0.0f),
        protocol(SensorProtocol::ANALOG_PROTOCOL),
        modbusDeviceAddress(1),
        baudRate(9600) {}

    bool isAnalogSensor() const { return protocol == SensorProtocol::ANALOG_PROTOCOL; }
    bool isAnalogBncSensor() const { return protocol == SensorProtocol::ANALOG_BNC_PROTOCOL; }
    bool isAnalogEc2chSensor() const { return protocol == SensorProtocol::ANALOG_EC2CH_PROTOCOL; }
    bool isAnalogPhSensor() const { return protocol == SensorProtocol::ANALOG_PH_PROTOCOL; }
    bool isI2CSensor() const { return protocol == SensorProtocol::I2C_PROTOCOL; }
    bool isModbusSensor() const { return protocol == SensorProtocol::MODBUS_PROTOCOL; }
    
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
        if (protocolStr == "Analog") {
            protocol = SensorProtocol::ANALOG_PROTOCOL;
        } else if (protocolStr == "AnalogBnc") {
            protocol = SensorProtocol::ANALOG_BNC_PROTOCOL;
        } else if (protocolStr == "AnalogEc2ch") {
            protocol = SensorProtocol::ANALOG_EC2CH_PROTOCOL;
        } else if (protocolStr == "AnalogPh") {
            protocol = SensorProtocol::ANALOG_PH_PROTOCOL;
        } else if (protocolStr == "I2C") {
            protocol = SensorProtocol::I2C_PROTOCOL;
        } else if (protocolStr == "ModBus") {
            protocol = SensorProtocol::MODBUS_PROTOCOL;
        }
    }
};
