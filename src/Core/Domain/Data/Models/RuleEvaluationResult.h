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
    std::string workflowUid;
    int workflowVersion = 0;
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
     * @brief Reinitialize in place to what the three-argument constructor would produce
     *
     * Field-by-field assignment instead of `*this = RuleEvaluationResult(...)` so the strings
     * keep their allocated buffers: rule evaluation runs every second and reuses one instance,
     * and a reset that frees and re-allocates would put that churn back. Every field of the
     * class must be listed here - when adding a field above, add its default to this method.
     */
    void resetForRule(const std::string& forRuleUid, RuleType forRuleType, int forRuleVersion) {
        state = DEVICE_STATE_OFF;
        durationOn = 0;
        timeOn = 0;
        timeOff = 0;
        timeResquestOn = 0;
        sensorUid.clear();
        sensorValue = 0.0f;
        sensorValueTime = 0;
        valueTarget = 0.0f;
        valueLow = 0.0f;
        valueHigh = 0.0f;
        ruleUid = forRuleUid;
        ruleType = forRuleType;
        ruleVersion = forRuleVersion;
        workflowUid.clear();
        workflowVersion = 0;
        sensorValueLow = 0.0f;
        sensorValueHigh = 0.0f;
        ruleScheduleStartAt.clear();
        ruleScheduleDuration = 0;
        needToSwitchBack = false;
        mixTime = 0;
        ruleSensorStartAt.clear();
        ruleSensorValueStart = 0.0f;
        ruleSensorStopAt.clear();
        ruleSensorValueStop = 0.0f;
        ruleStopReason.clear();
        switchedAt.clear();
        isRuleTurnOnDevice = false;
        timeToSetPoint = 0;
        cyclesToSetPoint = 0;
        currentCycle = 0;
        delayStartTime = 0;
    }
    
    /**
     * @brief Check if the device is currently powered by this rule
     *
     * Uses the shared predicate from DeviceStateEnum.h, i.e. the states in which the relay is
     * actually closed - including DEVICE_STATE_DELAY_OFF and excluding DEVICE_STATE_DELAY_ON.
     */
    bool isDeviceOn() const {
        return isDeviceStatePowered(state);
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
