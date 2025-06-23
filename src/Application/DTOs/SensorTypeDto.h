#pragma once

#include <string>

class SensorTypeDto {
public:
    int id;
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
    std::string protocol;
    std::string i2cAddress;
    std::string chipType;
    std::string i2cRegisterConfig;
    int modbusDeviceAddress;
    int baudRate;
    std::string modbusRegisterConfig;

    SensorTypeDto() : 
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
        modbusDeviceAddress(1),
        baudRate(9600) {}
};
