#ifndef RULE_EVALUATION_RESULT_H
#define RULE_EVALUATION_RESULT_H

#include <string>
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/RuleEnums.h"

/**
 * @brief Result of rule evaluation containing all necessary data for device control
 */
class RuleEvaluationResult {
public:
    // Device state information
    DeviceStateEnum state = DEVICE_STATE_OFF;
    uint32_t durationOn = 0;
    uint32_t timeOn = 0;
    uint32_t timeOff = 0;
    uint32_t timeResquestOn = 0;  // Time when request was made
    
    // Sensor information
    std::string sensorUid;
    float sensorValue = 0.0f;
    uint32_t sensorValueTime = 0;
    
    // Target values
    float valueTarget = 0.0f;
    float valueLow = 0.0f;
    float valueHigh = 0.0f;
    
    // Rule information
    std::string ruleUid;
    RuleType ruleType = RuleType::SENSOR;
    int ruleVersion = 0;
    float sensorValueLow = 0.0f;
    float sensorValueHigh = 0.0f;
    std::string ruleScheduleStartAt;
    int ruleScheduleDuration = 0;
    
    // Control flags
    bool needToSwitchBack = false;
    int mixTime = 0;
    
    // Rule execution tracking
    std::string ruleSensorStartAt;
    float ruleSensorValueStart = 0.0f;
    std::string ruleSensorStopAt;
    float ruleSensorValueStop = 0.0f;
    std::string ruleStopReason;
    std::string switchedAt;
    
    // Advanced rule control
    bool isRuleTurnOnDevice = false;
    int timeToSetPoint = 0;
    int cyclesToSetPoint = 0;
    int currentCycle = 0;
    uint32_t delayStartTime = 0;
    
    RuleEvaluationResult() = default;
    
    /**
     * @brief Constructor with basic rule information
     */
    RuleEvaluationResult(const std::string& ruleUid, RuleType ruleType, int ruleVersion)
        : ruleUid(ruleUid), ruleType(ruleType), ruleVersion(ruleVersion) {}
    
    /**
     * @brief Check if the evaluation result indicates device should be on
     */
    bool isDeviceOn() const {
        return state == DEVICE_STATE_ON || 
               state == DEVICE_STATE_STEP_ON ||
               state == DEVICE_STATE_DELAY_ON ||
               state == DEVICE_STATE_REQUEST_ON ||
               state == DEVICE_STATE_REQUEST_STEP_ON;
    }
    
    /**
     * @brief Check if the evaluation result indicates a delay state
     */
    bool isDelayState() const {
        return state == DEVICE_STATE_DELAY_ON || state == DEVICE_STATE_DELAY_OFF;
    }
    
    /**
     * @brief Check if the evaluation result indicates a step rule state
     */
    bool isStepState() const {
        return state == DEVICE_STATE_STEP_ON || 
               state == DEVICE_STATE_STEP_WAITING ||
               state == DEVICE_STATE_REQUEST_STEP_ON;
    }
};

#endif // RULE_EVALUATION_RESULT_H
