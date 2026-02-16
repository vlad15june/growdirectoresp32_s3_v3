#pragma once

#include <string>
#include <vector>

/**
 * @brief DTO for individual sensor type within a sensor device
 */
class SubSensorTypeDto {
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

    SubSensorTypeDto() : 
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

/**
 * @brief DTO for sensor type device
 */
class SensorTypeDto {
public:
    std::string uid;
    std::string name;
    std::string displayName;
    bool canScan;
    std::string protocol;
    bool isSupported;
    
    std::string createdAt;
    std::string updatedAt;
    
    std::vector<SubSensorTypeDto> sensorTypes;
    
    std::string i2cAddress;
    std::string chipType;
    std::string i2cRegisterConfig;
    int modbusDeviceAddress;
    int baudRate;
    std::string modbusRegisterConfig;
    std::string analogPinConfig;

    // Legacy fields for backward compatibility
    int id;

    SensorTypeDto() : 
        canScan(true),
        isSupported(true),
        modbusDeviceAddress(1),
        baudRate(9600),
        id(0) {}
};
