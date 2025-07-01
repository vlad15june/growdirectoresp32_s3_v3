#ifndef EVALUATE_RULES_USE_CASE_H
#define EVALUATE_RULES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/Rules/IEvaluateRulesUseCase.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Application/Services/Rules/RuleValidator.h"
#include "Core/Domain/Data/Models/RuleEvaluationResult.h"
#include <map>

/**
 * Use case for evaluating rules and executing their actions
 */
class EvaluateRulesUseCase : public IEvaluateRulesUseCase {
private:
    IDeviceModel* _deviceModel;
    IRuleRepository* _ruleRepository;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IArduinoAdapter* _arduinoAdapter;
    INTPTimeAdapter* _ntpTimeAdapter;
    RuleValidator _ruleValidator;
    
    // Rule state tracking
    std::map<std::string, RuleEvaluationResult> _ruleActiveStates;
    std::map<std::string, RuleEvaluationResult> _lastEvaluationResults;
    std::map<std::string, unsigned long> _ruleLastEvaluated;
    std::map<std::string, unsigned long> _delayTimers;
    
    // Evaluation interval in milliseconds
    const int EVALUATION_INTERVAL_MS = 1000;
    unsigned long _lastEvaluationTime;
    
    void log(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::RULE_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            _logger->println(DebugFeatures::RULE_DEBUG, buffer);
        }
    }
    
    bool executeDeviceAction(const std::string& deviceUid, const RuleEvaluationResult& evaluationResult) {
        if (!_deviceModel) {
            log("EVALUATE_RULES: Device model not available");
            return false;
        }
        
        Device* device = _deviceModel->getDeviceByUid(deviceUid.c_str());
        if (!device) {
            log("EVALUATE_RULES: Device %s not found", deviceUid.c_str());
            return false;
        }
        
        // Execute device action based on evaluation result
        device->state = evaluationResult.state;
        device->durationOn = evaluationResult.durationOn;
        device->timeOn = evaluationResult.timeOn;
        device->timeOff = evaluationResult.timeOff;
        device->needToSwitchBack = evaluationResult.needToSwitchBack;
        device->mixTime = evaluationResult.mixTime;
        
        log("EVALUATE_RULES: Executing device action - device %s, state %s", 
            deviceUid.c_str(), getDeviceStateName(evaluationResult.state));
        
        return true;
    }

public:
    EvaluateRulesUseCase(
        IDeviceModel* deviceModel,
        IRuleRepository* ruleRepository,
        IArduinoAdapter* arduinoAdapter,
        INTPTimeAdapter* ntpTimeAdapter,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr
    ) : _deviceModel(deviceModel),
        _ruleRepository(ruleRepository),
        _logger(logger),
        _debugSystem(debugSystem),
        _arduinoAdapter(arduinoAdapter),
        _ntpTimeAdapter(ntpTimeAdapter),
        _ruleValidator(logger, debugSystem),
        _lastEvaluationTime(0) {
        
        if (_arduinoAdapter) {
            _lastEvaluationTime = _arduinoAdapter->millis();
        }
    }
    
    ~EvaluateRulesUseCase() override = default;
    
    bool init() override {
        log("EVALUATE_RULES: Initializing rule evaluation use case");
        
        if (!_deviceModel) {
            log("EVALUATE_RULES: Device model not available");
            return false;
        }
        
        // if (!_sensorModel) {
        //     log("EVALUATE_RULES: Sensor model not available");
        //     return false;
        // }
        
        if (!_ruleRepository) {
            log("EVALUATE_RULES: Rule repository not available");
            return false;
        }
        
        if (!_arduinoAdapter) {
            log("EVALUATE_RULES: Arduino adapter not available");
            return false;
        }
        
        if (!_ntpTimeAdapter) {
            log("EVALUATE_RULES: NTP time adapter not available");
            return false;
        }
        
        if (_arduinoAdapter) {
            _lastEvaluationTime = _arduinoAdapter->millis();
        }
        
        log("EVALUATE_RULES: Rule evaluation use case initialized successfully");
        return true;
    }
    
    void loop() override {
        if (!_arduinoAdapter) {
            log("EVALUATE_RULES: Arduino adapter not available in loop");
            return;
        }
        
        unsigned long now = _arduinoAdapter->millis();
        unsigned long elapsed = now - _lastEvaluationTime;
        
        if (elapsed < EVALUATION_INTERVAL_MS) {            
            return;
        }

        log("EVALUATE_RULES: Starting rule evaluation cycle at %lu ms", now);
        
        _lastEvaluationTime = now;
        
        if (!_ruleRepository) {
            log("EVALUATE_RULES: Rule repository not available in loop");
            return;
        }
        
        // Get active rules from repository
        std::vector<Rule> activeRules = _ruleRepository->getActiveRules();
        
        log("EVALUATE_RULES: Starting rule evaluation cycle - %d active rules", 
            static_cast<int>(activeRules.size()));
        
        // Evaluate all active rules
        for (const auto& rule : activeRules) {
            if (rule.state == RuleState::SYNCHRONIZED) {
                executeRuleActions(rule);
            }
        }
    }
    
    RuleEvaluationResult evaluateRule(const Rule& rule) override {
        log("EVALUATE_RULES: Evaluating rule %s", rule.uid.c_str());
        
        // Create result with basic rule information
        RuleEvaluationResult result(rule.uid, rule.ruleType, rule.version);
        
        // Skip if rule is not active
        if (rule.mode != RuleMode::ON) {
            log("EVALUATE_RULES: Rule %s is not in ON mode", rule.uid.c_str());
            result.state = DEVICE_STATE_OFF;
            return result;
        }
        
        // Validate rule first
        auto validationResult = _ruleValidator.validateRuleExecution(rule);
        if (!validationResult.isValid) {
            log("EVALUATE_RULES: Rule %s validation failed: %s", rule.uid.c_str(), validationResult.errorMessage.c_str());
            result.state = DEVICE_STATE_OFF;
            result.ruleStopReason = validationResult.errorMessage;
            return result;
        }
        
        // Fill sensor information
        if (!rule.sensorUid.empty()) {
            result.sensorUid = rule.sensorUid;
            result.sensorValue = getSensorValue(rule.sensorUid);
            if (_arduinoAdapter) {
                result.sensorValueTime = _arduinoAdapter->millis();
            }
        }
        
        // Get device to access current cycle for step rules
        Device* device = nullptr;
        if (_deviceModel) {
            device = _deviceModel->getDeviceByUid(rule.deviceUid.c_str());
        }
        
        // Fill rule-specific values
        switch (rule.ruleType) {
            case RuleType::STEP:
                result.valueTarget = rule.valueTarget;
                result.cyclesToSetPoint = rule.cyclesToSetPoint;
                // Get current cycle from device if available
                if (device && device->deviceType != DeviceTypeEnum::DEVICE_TYPE_ANALOG) {
                    DeviceDigitalOut* digitalDevice = static_cast<DeviceDigitalOut*>(device);
                    result.currentCycle = digitalDevice->currentCycle;
                } else {
                    result.currentCycle = 0;
                }
                result.sensorValueLow = rule.valueLow;
                result.sensorValueHigh = rule.valueHigh;
                break;
            case RuleType::SENSOR:
                result.valueLow = rule.valueLow;
                result.valueHigh = rule.valueHigh;
                result.sensorValueLow = rule.valueLow;
                result.sensorValueHigh = rule.valueHigh;
                result.timeToSetPoint = rule.timeToSetPoint;
                break;
            case RuleType::SCHEDULE:
                result.ruleScheduleDuration = 0; // Will be set based on active schedule
                break;
        }
        
        bool conditionMet = false;
        DeviceStateEnum resultState = DEVICE_STATE_OFF; //TODO: resultState - not used, remove it?
        
        switch (rule.ruleType) {
            case RuleType::SCHEDULE:
                result = evaluateScheduleRule(rule, result);
                conditionMet = result.isRuleTurnOnDevice;
                resultState = result.state;
                break;
            case RuleType::SENSOR:
                conditionMet = evaluateSensorRule(rule);
                resultState = conditionMet ? DEVICE_STATE_ON : DEVICE_STATE_OFF;
                result.isRuleTurnOnDevice = conditionMet;
                break;
            case RuleType::STEP:
                conditionMet = evaluateStepRule(rule);
                if (conditionMet) {
                    resultState = DEVICE_STATE_STEP_ON;
                    result.isRuleTurnOnDevice = true;
                } else {
                    resultState = DEVICE_STATE_STEP_WAITING;
                    result.isRuleTurnOnDevice = false;
                }
                break;
            default:
                log("EVALUATE_RULES: Unsupported rule type for rule %s", rule.uid.c_str());
                result.state = DEVICE_STATE_OFF;
                return result;
        }                
        
        // Store evaluation result
        _lastEvaluationResults[rule.uid] = result;
        if (_arduinoAdapter) {
            _ruleLastEvaluated[rule.uid] = _arduinoAdapter->millis();
        }
        
        log("EVALUATE_RULES: Rule %s condition %s, state: %s", 
            rule.uid.c_str(), conditionMet ? "MET" : "NOT MET", getDeviceStateName(result.state));
        return result;
    }

    bool executeRuleActions(const Rule& rule) override {
        log("EVALUATE_RULES: Executing actions for rule %s", rule.uid.c_str());
        
        RuleEvaluationResult currentState = _ruleActiveStates[rule.uid];
        RuleEvaluationResult evaluationResult = evaluateRule(rule);
        
        if (currentState.state == evaluationResult.state) {
            return true; // No change needed
        }
        
        bool success = executeDeviceAction(rule.deviceUid, evaluationResult);
        if (success) {
            _ruleActiveStates[rule.uid] = evaluationResult;
            log("EVALUATE_RULES: Rule %s action executed - device state %s", 
                rule.uid.c_str(), getDeviceStateName(evaluationResult.state));
        } else {
            log("EVALUATE_RULES: Failed to execute action for rule %s", rule.uid.c_str());
        }
        
        return success;
    }
    
    bool isRuleActive(const std::string& ruleUid) const override {
        auto it = _ruleActiveStates.find(ruleUid);
        if (it != _ruleActiveStates.end()) {
            return it->second.isDeviceOn();
        }
        return false;
    }
    
    RuleEvaluationResult getLastEvaluationResult(const std::string& ruleUid) const override {
        auto it = _lastEvaluationResults.find(ruleUid);
        if (it != _lastEvaluationResults.end()) {
            return it->second;
        }
        return RuleEvaluationResult();
    }

private:
    RuleEvaluationResult evaluateScheduleRule(const Rule& rule, RuleEvaluationResult& result) {
        log("EVALUATE_RULES: Evaluating schedule rule %s", rule.uid.c_str());
        
        if (!_ntpTimeAdapter) {
            log("EVALUATE_RULES: NTP time adapter not available for schedule rule");
            result.state = DEVICE_STATE_OFF;
            result.isRuleTurnOnDevice = false;
            return result;
        }
        
        // Get current time
        time_t currentTime = _ntpTimeAdapter->getEpochTime();
        struct tm timeinfo;
        if (!gmtime_r(&currentTime, &timeinfo)) {
            log("EVALUATE_RULES: Failed to get current time for schedule rule");
            result.state = DEVICE_STATE_OFF;
            result.isRuleTurnOnDevice = false;
            return result;
        }
        
        log("EVALUATE_RULES: LocalTime = %d:%d", timeinfo.tm_hour, timeinfo.tm_min);
        
        if (rule.schedule.empty()) {
            log("EVALUATE_RULES: No schedule items in rule %s", rule.uid.c_str());
            result.state = DEVICE_STATE_OFF;
            result.isRuleTurnOnDevice = false;
            return result;
        }
        
        log("EVALUATE_RULES: rules[%s][schedule].size= %d", rule.uid.c_str(), static_cast<int>(rule.schedule.size()));
        
        bool isSchRuleOn = false;
        uint32_t durationOn = 0;
        struct tm whenRuleShouldStarted = {0};
        int ruleScheduleDuration = 0;
        char buffer[150];
        buffer[0] = 0;
        
        for (size_t si = 0; si < rule.schedule.size(); ++si) {
            const auto& scheduleItem = rule.schedule[si];
            
            log("EVALUATE_RULES: schedule[%d][duration]= %d", static_cast<int>(si), scheduleItem.duration);
            
            // Convert startAt time_t to tm structure
            struct tm startAt = {0};
            if (!gmtime_r(&scheduleItem.startAt, &startAt)) {
                log("EVALUATE_RULES: Failed to parse startAt time for schedule item %d", static_cast<int>(si));
                continue;
            }
            
            log("EVALUATE_RULES: startAt = %d:%d", startAt.tm_hour, startAt.tm_min);
            
            uint32_t duration = scheduleItem.duration;
            uint32_t totalSecondWhenStart = startAt.tm_hour * 3600 + startAt.tm_min * 60;
            uint32_t totalSecondWhenEnd = startAt.tm_hour * 3600 + startAt.tm_min * 60 + duration;
            uint32_t totalSecondNow = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
            
            // Check if current time is within schedule today
            if (totalSecondWhenStart <= totalSecondNow && totalSecondNow < totalSecondWhenEnd) {
                isSchRuleOn = true;
                durationOn = totalSecondWhenEnd - totalSecondNow;
                ruleScheduleDuration = scheduleItem.duration;
                whenRuleShouldStarted = timeinfo;
                whenRuleShouldStarted.tm_hour = startAt.tm_hour;
                whenRuleShouldStarted.tm_min = startAt.tm_min;
                whenRuleShouldStarted.tm_sec = 0;
                strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &whenRuleShouldStarted);
                log("EVALUATE_RULES: RULE_ON: whenRuleShouldStarted=%s", buffer);
                break;
            }
            
            // Check if current time is within schedule from yesterday (cross-midnight schedules)
            struct tm yesterday = timeinfo;
            yesterday.tm_mday--;
            mktime(&yesterday);
            uint32_t totalSecondNowNextDay = totalSecondNow + 24 * 3600;
            if (totalSecondWhenStart <= totalSecondNowNextDay && totalSecondNowNextDay < totalSecondWhenEnd) {
                isSchRuleOn = true;
                durationOn = totalSecondWhenEnd - totalSecondNowNextDay;
                ruleScheduleDuration = scheduleItem.duration;
                whenRuleShouldStarted = yesterday;
                whenRuleShouldStarted.tm_hour = startAt.tm_hour;
                whenRuleShouldStarted.tm_min = startAt.tm_min;
                whenRuleShouldStarted.tm_sec = 0;
                strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &whenRuleShouldStarted);
                log("EVALUATE_RULES: RULE_ON: whenRuleShouldStarted=%s", buffer);
                break;
            }
            
            log("EVALUATE_RULES: RULE_ON: totalSecondWhenStart = %d, totalSecondWhenEnd = %d, totalSecondNow = %d, duration in rule = %d", 
                totalSecondWhenStart, totalSecondWhenEnd, totalSecondNow, duration);
        }
        
        if (isSchRuleOn) {
            log("EVALUATE_RULES: RULE_ON: uid = %s, current time = %d:%d; rule time = %d:%d ; duration = %d", 
                rule.uid.c_str(), timeinfo.tm_hour, timeinfo.tm_min, 
                whenRuleShouldStarted.tm_hour, whenRuleShouldStarted.tm_min, durationOn);
            
            result.isRuleTurnOnDevice = true;
            result.state = DEVICE_STATE_REQUEST_ON;
            result.durationOn = durationOn * 1000; // Convert to milliseconds
            if (_arduinoAdapter) {
                result.timeOn = _arduinoAdapter->millis();
                result.timeResquestOn = _arduinoAdapter->millis();
            }
            result.needToSwitchBack = true;
            result.mixTime = 0;
            result.ruleScheduleStartAt = buffer;
            result.ruleScheduleDuration = ruleScheduleDuration;
        } else {
            result.isRuleTurnOnDevice = false;
            result.state = DEVICE_STATE_OFF;
        }
        
        return result;
    }
    
    bool evaluateSensorRule(const Rule& rule) {
        log("EVALUATE_RULES: Evaluating sensor rule %s", rule.uid.c_str());
        
        if (rule.sensorUid.empty()) {
            log("EVALUATE_RULES: Sensor rule %s has no sensor UID", rule.uid.c_str());
            return false;
        }
        
        double sensorValue = getSensorValue(rule.sensorUid);
        
        // Check if sensor value is within the rule's range
        if (sensorValue >= rule.valueLow && sensorValue <= rule.valueHigh) {
            log("EVALUATE_RULES: Sensor rule %s condition MET - value %.2f in range [%.2f, %.2f]", 
                rule.uid.c_str(), sensorValue, rule.valueLow, rule.valueHigh);
            return true;
        } else {
            log("EVALUATE_RULES: Sensor rule %s condition NOT MET - value %.2f outside range [%.2f, %.2f]", 
                rule.uid.c_str(), sensorValue, rule.valueLow, rule.valueHigh);
            return false;
        }
    }
    
    bool evaluateStepRule(const Rule& rule) {
        log("EVALUATE_RULES: Evaluating step rule %s", rule.uid.c_str());
        
        if (rule.sensorUid.empty()) {
            log("EVALUATE_RULES: Step rule %s has no sensor UID", rule.uid.c_str());
            return false;
        }
        
        double sensorValue = getSensorValue(rule.sensorUid);
        
        // Get device to check current cycle
        Device* device = nullptr;
        if (_deviceModel) {
            device = _deviceModel->getDeviceByUid(rule.deviceUid.c_str());
        }
        
        int currentCycle = 0;
        if (device && device->deviceType != DeviceTypeEnum::DEVICE_TYPE_ANALOG) {
            DeviceDigitalOut* digitalDevice = static_cast<DeviceDigitalOut*>(device);
            currentCycle = digitalDevice->currentCycle;
        }
        
        // Check if we've reached the target cycles and sensor value is within range
        bool cyclesReached = currentCycle >= rule.cyclesToSetPoint;
        bool valueInRange = sensorValue >= rule.valueLow && sensorValue <= rule.valueHigh;
        
        log("EVALUATE_RULES: Step rule %s - cycles: %d/%d, value: %.2f [%.2f-%.2f], condition: %s", 
            rule.uid.c_str(), currentCycle, rule.cyclesToSetPoint, sensorValue, 
            rule.valueLow, rule.valueHigh, (cyclesReached && valueInRange) ? "MET" : "NOT MET");
        
        return cyclesReached && valueInRange;
    }
    
    bool evaluateTimeConditions(const Rule& rule) {
        log("EVALUATE_RULES: Evaluating time conditions for rule %s", rule.uid.c_str());
        
        if (!_ntpTimeAdapter) {
            log("EVALUATE_RULES: NTP time adapter not available for time conditions");
            return false;
        }
        
        // Get current time
        time_t currentTime = _ntpTimeAdapter->getEpochTime();
        struct tm timeinfo;
        if (!gmtime_r(&currentTime, &timeinfo)) {
            log("EVALUATE_RULES: Failed to get current time for rule evaluation");
            return false;
        }
        
        // For now, just return true as time conditions would depend on specific rule implementation
        // This would need to be expanded based on specific time-based rule requirements
        return true;
    }
    
    double getSensorValue(const std::string& sensorUid) {
        log("EVALUATE_RULES: Getting sensor value for %s", sensorUid.c_str());
        
        // TODO: This would need to be implemented to get actual sensor values
        // For now, return a default value
        // This would typically interact with a sensor service or sensor model
        
        log("EVALUATE_RULES: Sensor value retrieval not yet implemented, returning default value");
        return 0.0;
    }
};

#endif // EVALUATE_RULES_USE_CASE_H
