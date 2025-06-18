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
#include <map>

/**
 * Use case for evaluating rules and executing their actions
 */
class EvaluateRulesUseCase : public IEvaluateRulesUseCase {
private:
    IDeviceModel* _deviceModel;
    // ISensorModel* _sensorModel;
    IRuleRepository* _ruleRepository;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IArduinoAdapter* _arduinoAdapter;
    INTPTimeAdapter* _ntpTimeAdapter;
    RuleValidator _ruleValidator;
    
    // Rule state tracking
    std::map<std::string, bool> _ruleActiveStates;
    std::map<std::string, bool> _lastEvaluationResults;
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
    bool executeDeviceAction(const std::string& deviceUid, bool turnOn) {
        if (!_deviceModel) {
            log("EVALUATE_RULES: Device model not available");
            return false;
        }
        
        Device* device = _deviceModel->getDeviceByUid(deviceUid.c_str());
        if (!device) {
            log("EVALUATE_RULES: Device %s not found", deviceUid.c_str());
            return false;
        }
        
        // Execute device action based on device type
        // This would depend on your device implementation
        log("EVALUATE_RULES: Executing device action - device %s, turn %s", 
            deviceUid.c_str(), turnOn ? "ON" : "OFF");
        
        return true; // Placeholder - implement actual device control
    }

public:
    EvaluateRulesUseCase(
        IDeviceModel* deviceModel,
        // ISensorModel* sensorModel,
        IRuleRepository* ruleRepository,
        IArduinoAdapter* arduinoAdapter,
        INTPTimeAdapter* ntpTimeAdapter,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr
    ) : _deviceModel(deviceModel),
        // _sensorModel(sensorModel),
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
    
    bool evaluateRule(const Rule& rule) override {
        log("EVALUATE_RULES: Evaluating rule %s", rule.uid.c_str());
        
        // Skip if rule is not active
        if (rule.mode != RuleMode::ON) {
            log("EVALUATE_RULES: Rule %s is not in ON mode", rule.uid.c_str());
            return false;
        }
        
        // Validate rule first
        auto validationResult = _ruleValidator.validateRuleExecution(rule);
        if (!validationResult.isValid) {
            log("EVALUATE_RULES: Rule %s validation failed: %s", rule.uid.c_str(), validationResult.errorMessage.c_str());
            return false;
        }
        
        bool conditionMet = false;
        
        switch (rule.ruleType) {
            case RuleType::SCHEDULE:
                conditionMet = evaluateScheduleRule(rule);
                break;
            case RuleType::SENSOR:
                conditionMet = evaluateSensorRule(rule);
                break;
            case RuleType::STEP:
                conditionMet = evaluateStepRule(rule);
                break;
            default:
                log("EVALUATE_RULES: Unsupported rule type for rule %s", rule.uid.c_str());
                return false;
        }
        
        // Store evaluation result
        _lastEvaluationResults[rule.uid] = conditionMet;
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
                return false;
            } else {
                unsigned long elapsed = _arduinoAdapter ? (_arduinoAdapter->millis() - _delayTimers[delayKey]) : 0;
                if (elapsed < (rule.delayOn * 1000)) { // Convert seconds to milliseconds
                    return false;
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
                return _ruleActiveStates[rule.uid]; // Keep current state during delay
            } else {
                unsigned long elapsed = _arduinoAdapter ? (_arduinoAdapter->millis() - _delayTimers[delayKey]) : 0;
                if (elapsed < (rule.delayOff * 1000)) { // Convert seconds to milliseconds
                    return _ruleActiveStates[rule.uid]; // Keep current state during delay
                }
                _delayTimers.erase(delayKey);
            }
        }
        
        log("EVALUATE_RULES: Rule %s condition %s", rule.uid.c_str(), conditionMet ? "MET" : "NOT MET");
        return conditionMet;
    }
    
    bool executeRuleActions(const Rule& rule) override {
        log("EVALUATE_RULES: Executing actions for rule %s", rule.uid.c_str());
        
        bool currentlyActive = _ruleActiveStates[rule.uid];
        bool shouldBeActive = evaluateRule(rule);
        
        if (currentlyActive == shouldBeActive) {
            return true; // No change needed
        }
        
        bool success = executeDeviceAction(rule.deviceUid, shouldBeActive);
        if (success) {
            _ruleActiveStates[rule.uid] = shouldBeActive;
            log("EVALUATE_RULES: Rule %s action executed - device %s", 
                rule.uid.c_str(), shouldBeActive ? "ON" : "OFF");
        } else {
            log("EVALUATE_RULES: Failed to execute action for rule %s", rule.uid.c_str());
        }
        
        return success;
    }
    
    bool isRuleActive(const std::string& ruleUid) const override {
        auto it = _ruleActiveStates.find(ruleUid);
        return it != _ruleActiveStates.end() ? it->second : false;
    }
    
    bool getLastEvaluationResult(const std::string& ruleUid) const override {
        auto it = _lastEvaluationResults.find(ruleUid);
        return it != _lastEvaluationResults.end() ? it->second : false;
    }
};

#endif // EVALUATE_RULES_USE_CASE_H
