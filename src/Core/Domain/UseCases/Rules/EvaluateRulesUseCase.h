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
#include "Core/Domain/Enums/DeviceStateEnum.h"
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
    std::map<std::string, DeviceStateEnum> _ruleActiveStates;
    std::map<std::string, DeviceStateEnum> _lastEvaluationResults;
    std::map<std::string, unsigned long> _ruleLastEvaluated;
    std::map<std::string, unsigned long> _delayTimers;
    
    // Evaluation interval in milliseconds
    const int EVALUATION_INTERVAL_MS = 5000;
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
    
    bool evaluateScheduleRule(const Rule& rule);
    bool evaluateSensorRule(const Rule& rule);
    bool evaluateStepRule(const Rule& rule);
    bool evaluateTimeConditions(const Rule& rule);
    double getSensorValue(const std::string& sensorUid);
    bool executeDeviceAction(const std::string& deviceUid, DeviceStateEnum deviceState) {
        if (!_deviceModel) {
            log("EVALUATE_RULES: Device model not available");
            return false;
        }
        
        Device* device = _deviceModel->getDeviceByUid(deviceUid.c_str());
        if (!device) {
            log("EVALUATE_RULES: Device %s not found", deviceUid.c_str());
            return false;
        }
        
        // Execute device action based on device state
        log("EVALUATE_RULES: Executing device action - device %s, state %s", 
            deviceUid.c_str(), getDeviceStateName(deviceState));
            device->state = deviceState;
        
        return true; // Placeholder - implement actual device control
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
            return;
        }
        
        unsigned long now = _arduinoAdapter->millis();
        unsigned long elapsed = now - _lastEvaluationTime;
        
        if (elapsed < EVALUATION_INTERVAL_MS) {
            return;
        }
        
        _lastEvaluationTime = now;
        
        if (!_ruleRepository) {
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
    
    DeviceStateEnum evaluateRule(const Rule& rule) override {
        log("EVALUATE_RULES: Evaluating rule %s", rule.uid.c_str());
        
        // Skip if rule is not active
        if (rule.mode != RuleMode::ON) {
            log("EVALUATE_RULES: Rule %s is not in ON mode", rule.uid.c_str());
            return DEVICE_STATE_OFF;
        }
        
        // Validate rule first
        auto validationResult = _ruleValidator.validateRuleExecution(rule);
        if (!validationResult.isValid) {
            log("EVALUATE_RULES: Rule %s validation failed: %s", rule.uid.c_str(), validationResult.errorMessage.c_str());
            return DEVICE_STATE_OFF;
        }
        
        bool conditionMet = false;
        DeviceStateEnum resultState = DEVICE_STATE_OFF;
        
        switch (rule.ruleType) {
            case RuleType::SCHEDULE:
                conditionMet = evaluateScheduleRule(rule);
                resultState = conditionMet ? DEVICE_STATE_ON : DEVICE_STATE_OFF;
                break;
            case RuleType::SENSOR:
                conditionMet = evaluateSensorRule(rule);
                resultState = conditionMet ? DEVICE_STATE_ON : DEVICE_STATE_OFF;
                break;
            case RuleType::STEP:
                conditionMet = evaluateStepRule(rule);
                if (conditionMet) {
                    // For step rules, we might need different states based on the step phase
                    resultState = DEVICE_STATE_STEP_ON;
                } else {
                    resultState = DEVICE_STATE_OFF;
                }
                break;
            default:
                log("EVALUATE_RULES: Unsupported rule type for rule %s", rule.uid.c_str());
                return DEVICE_STATE_OFF;
        }
        
        // Store evaluation result
        _lastEvaluationResults[rule.uid] = resultState;
        if (_arduinoAdapter) {
            _ruleLastEvaluated[rule.uid] = _arduinoAdapter->millis();
        }
        
        // Handle delays
        if (conditionMet && rule.delayOn > 0) {
            auto delayKey = rule.uid + "_on";
            if (_delayTimers.find(delayKey) == _delayTimers.end()) {
                if (_arduinoAdapter) {
                    _delayTimers[delayKey] = _arduinoAdapter->millis();
                }
                log("EVALUATE_RULES: Starting delay ON timer for rule %s", rule.uid.c_str());
                return DEVICE_STATE_DELAY_ON;
            } else {
                unsigned long elapsed = _arduinoAdapter ? (_arduinoAdapter->millis() - _delayTimers[delayKey]) : 0;
                if (elapsed < (rule.delayOn * 1000)) { // Convert seconds to milliseconds
                    return DEVICE_STATE_DELAY_ON;
                }
                _delayTimers.erase(delayKey);
            }
        }
        
        if (!conditionMet && rule.delayOff > 0) {
            auto delayKey = rule.uid + "_off";
            if (_delayTimers.find(delayKey) == _delayTimers.end()) {
                if (_arduinoAdapter) {
                    _delayTimers[delayKey] = _arduinoAdapter->millis();
                }
                log("EVALUATE_RULES: Starting delay OFF timer for rule %s", rule.uid.c_str());
                return DEVICE_STATE_DELAY_OFF;
            } else {
                unsigned long elapsed = _arduinoAdapter ? (_arduinoAdapter->millis() - _delayTimers[delayKey]) : 0;
                if (elapsed < (rule.delayOff * 1000)) { // Convert seconds to milliseconds
                    return DEVICE_STATE_DELAY_OFF;
                }
                _delayTimers.erase(delayKey);
            }
        }
        
        log("EVALUATE_RULES: Rule %s condition %s, state: %s", 
            rule.uid.c_str(), conditionMet ? "MET" : "NOT MET", getDeviceStateName(resultState));
        return resultState;
    }
    
    bool executeRuleActions(const Rule& rule) override {
        log("EVALUATE_RULES: Executing actions for rule %s", rule.uid.c_str());
        
        DeviceStateEnum currentState = _ruleActiveStates[rule.uid];
        DeviceStateEnum shouldBeActive = evaluateRule(rule);
        
        if (currentState == shouldBeActive) {
            return true; // No change needed
        }
        
        bool success = executeDeviceAction(rule.deviceUid, shouldBeActive);
        if (success) {
            _ruleActiveStates[rule.uid] = shouldBeActive;
            log("EVALUATE_RULES: Rule %s action executed - device state %s", 
                rule.uid.c_str(), getDeviceStateName(shouldBeActive));
        } else {
            log("EVALUATE_RULES: Failed to execute action for rule %s", rule.uid.c_str());
        }
        
        return success;
    }
    
    bool isRuleActive(const std::string& ruleUid) const override {
        auto it = _ruleActiveStates.find(ruleUid);
        if (it != _ruleActiveStates.end()) {
            // Rule is considered "active" if it's in any state other than OFF or NONE
            DeviceStateEnum state = it->second;
            return state != DEVICE_STATE_OFF && state != DEVICE_STATE_NONE;
        }
        return false;
    }
    
    DeviceStateEnum getLastEvaluationResult(const std::string& ruleUid) const override {
        auto it = _lastEvaluationResults.find(ruleUid);
        return it != _lastEvaluationResults.end() ? it->second : DEVICE_STATE_OFF;
    }
};

#endif // EVALUATE_RULES_USE_CASE_H
