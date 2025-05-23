#pragma once

#include <cstdint>
#include <string>
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/DirectionEnum.h"

// Base Device class
class Device {
public:
    const char* deviceUid;                     // Unique identifier for the device (previously devicePinNum)
    bool isError;                              // Error state flag
    DeviceStateEnum state;                     // Current state (replaced const char* with enum)
    DeviceTypeEnum deviceType;                 // Type of device (digital, analog, etc.)
    uint32_t timeOn;                           // Timestamp when turned on
    uint32_t timeOff;                          // Timestamp when turned off
    uint32_t durationOn;                       // Duration to stay on
    uint32_t durationOff;                      // Duration to stay off
    const char* createdDate;                   // Creation timestamp
    const char* switchedAt;                    // Last switch timestamp
    
    bool isAuto;                               // Flag for automatic control
    bool isManual;                             // Flag for manual control
    
    const char* sensorUid;                     // Associated sensor UID
    float sensorValue;                         // Current sensor value
    uint32_t sensorValueTime;                  // Timestamp of last sensor reading
    
    float valueTarget;                         // Target value
    float valueLow;                            // Lower threshold
    float valueHigth;                          // Upper threshold
    DirectionEnum direction;                   // Direction (up/down) - now using enum
    bool range;                                // Range flag
    
    const char* ruleUid;                       // Associated rule UID
    const char* ruleType;                      // Rule type
    int ruleVersion;                           // Rule version
    float sensorValueLow;                      // Rule sensor lower threshold
    float sensorValueHigh;                     // Rule sensor upper threshold
    const char* ruleScheduleStartAt;           // Scheduled start time
    int ruleScheduleDuration;                  // Schedule duration
    bool workByRule;                           // Flag for rule-based operation
    long commandId;                            // Command ID
    bool workManual;                           // Manual operation flag
    
    bool needToSwitchBack;                     // Flag to switch back
    int mixTime;                               // Mix time value
    
    const char* ruleSensorStartAt;             // Rule sensor start timestamp
    float ruleSensorValueStart;                // Rule sensor start value
    const char* ruleSensorStopAt;              // Rule sensor stop timestamp
    float ruleSensorValueStop;                 // Rule sensor stop value
    const char* ruleStopReason;                // Rule stop reason
};

// Digital Output Device class
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

// Analog Output Device class
class DeviceAnalogOut : public Device {
public:
    float analogPinValue;
    float analogPinValueWhen5pers;
    float analogPinValueWhen10pers;
    float analogPinValueWhenTarget;
};
