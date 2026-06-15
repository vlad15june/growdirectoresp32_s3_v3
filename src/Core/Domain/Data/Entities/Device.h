#pragma once

#include <string>
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/RuleEnums.h"

class Device {
public:
    const char* deviceUid;
    bool isError;
    DeviceStateEnum state;
    DeviceTypeEnum deviceType;
    uint32_t timeOn;
    uint32_t timeOff;
    uint32_t durationOn;
    uint32_t durationOff;
    const char* createdDate;
    const char* switchedAt;
    std::string switchedAtStorage;

    bool isAuto;
    bool isManual;
    
    const char* sensorUid;
    float sensorValue;
    uint32_t sensorValueTime;
    
    float valueTarget;
    float valueLow;
    float valueHigth;
    RuleDirection direction;
    bool range;
    
    const char* ruleUid;
    RuleType ruleType;
    int ruleVersion;
    const char* workflowUid;
    int workflowVersion;
    float sensorValueLow;
    float sensorValueHigh;
    const char* ruleScheduleStartAt;
    int ruleScheduleDuration;
    bool workByRule;
    long commandId;
    bool workManual;
    
    bool needToSwitchBack;
    int mixTime;
    
    const char* ruleSensorStartAt;
    float ruleSensorValueStart;
    const char* ruleSensorStopAt;
    float ruleSensorValueStop;
    const char* ruleStopReason;
};

class DeviceDigitalOut : public Device {
public:
    bool isRuleTurnOnDevice;
    int timeToSetPoint;
    int cyclesToSetPoint;
    int currentCycle;
    uint32_t delayStartTime;
    
    bool isResquestOn;
    uint32_t timeResquestOn;
};

class DeviceAnalogOut : public Device {
public:
    float analogPinValue;
    float analogPinValueWhen5pers;
    float analogPinValueWhen10pers;
    float analogPinValueWhenTarget;
};
