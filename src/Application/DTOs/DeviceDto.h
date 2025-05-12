#pragma once

#include <string>

/**
 * @brief Data Transfer Object for device information
 */
class DeviceDto {
public:
    std::string uid;                    // Device unique identifier
    std::string state;                  // Device state
    std::string state_hw;               // Hardware state ("on" or "off")
    std::string switchedAt;             // Time when device was switched
    bool isAuto;                        // Whether device is in auto mode
    bool isManual;                      // Whether device is in manual mode
    std::string ruleUid;                // Unique identifier of the rule
    std::string ruleType;               // Type of rule
    bool isRuleTurnOnDevice;            // Whether rule turns on the device
    std::string ruleSensorStopAt;       // Time when rule sensor stopped
    float ruleSensorValueStop;          // Sensor value at stop
    std::string ruleStopReason;         // Reason for rule stop
    std::string ruleSensorStartAt;      // Time when rule sensor started
    int ruleVersion;                    // Rule version
    std::string sensorUid;              // Sensor unique identifier
    float sensorValueLow;               // Low threshold for sensor value
    float sensorValueHigh;              // High threshold for sensor value
    float sensorValue;                  // Current sensor value
    float ruleSensorValueStart;         // Sensor value at start
    uint32_t timeToSetPoint;            // Time to reach set point
    int cyclesToSetPoint;               // Cycles to reach set point
    int currentCycle;                   // Current cycle
    std::string ruleScheduleStartAt;    // Schedule start time
    uint32_t ruleScheduleDuration;      // Schedule duration
    uint32_t secondsToSwitchBack;       // Seconds until switch back
    uint32_t secondsEnabledFor;         // Seconds device is enabled for
    uint32_t secondsDisabledFor;        // Seconds device is disabled for
    std::string commandId;              // Command identifier for manual control
    
    DeviceDto() : 
        isAuto(false),
        isManual(false),
        isRuleTurnOnDevice(false),
        ruleSensorValueStop(0.0f),
        ruleVersion(0),
        sensorValueLow(0.0f),
        sensorValueHigh(0.0f),
        sensorValue(0.0f),
        ruleSensorValueStart(0.0f),
        timeToSetPoint(0),
        cyclesToSetPoint(0),
        currentCycle(0),
        ruleScheduleDuration(0),
        secondsToSwitchBack(0),
        secondsEnabledFor(0),
        secondsDisabledFor(0) {}
};
