#ifndef RULE_VALIDATOR_H
#define RULE_VALIDATOR_H

#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <string>
#include <vector>
#include <memory>
#include <regex>

struct RuleValidationResult {
    bool isValid = true;
    std::string errorType;
    std::string errorMessage;
    
    RuleValidationResult() = default;
    RuleValidationResult(bool valid, const std::string& error = "", const std::string& message = "")
        : isValid(valid), errorType(error), errorMessage(message) {}
};

// Base rule validator strategy
class BaseRuleValidator {
protected:
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    
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

public:
    BaseRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : _logger(logger), _debugSystem(debugSystem) {}
    
    virtual ~BaseRuleValidator() = default;
    
    virtual RuleValidationResult validate(const Rule& rule) {
        // Base validation - check required fields
        if (rule.uid.empty()) {
            return RuleValidationResult(false, "missing_uid", "Rule UID is required");
        }
        
        if (rule.deviceUid.empty()) {
            return RuleValidationResult(false, "missing_device_uid", "Device UID is required");
        }
        
        return RuleValidationResult(true);
    }
    
    virtual RuleValidationResult validateExecution(const Rule& rule) {
        return RuleValidationResult(true);
    }
};

// Unknown rule validator
class UnknownRuleValidator : public BaseRuleValidator {
public:
    UnknownRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        // Only valid if rule is being deleted
        if (rule.state == RuleState::DELETING) {
            return RuleValidationResult(true);
        }
        
        log("RULE_VALIDATION: Unknown rule type for rule %s", rule.uid.c_str());
        return RuleValidationResult(false, "wrong_format", "Unknown rule type");
    }
};

// Schedule rule validator
class ScheduleRuleValidator : public BaseRuleValidator {
public:
    ScheduleRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto baseResult = BaseRuleValidator::validate(rule);
        if (!baseResult.isValid) {
            return baseResult;
        }
        
        if (rule.schedule.empty()) {
            log("RULE_VALIDATION: Schedule rule %s has no schedule items", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Schedule rule must have schedule items");
        }
        
        return RuleValidationResult(true);
    }
};

// Sensor rule validator
class SensorRuleValidator : public BaseRuleValidator {
public:
    SensorRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto baseResult = BaseRuleValidator::validate(rule);
        if (!baseResult.isValid) {
            return baseResult;
        }
        
        if (rule.sensorUid.empty()) {
            log("RULE_VALIDATION: Sensor rule %s has no sensor UID", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Sensor rule must have sensor UID");
        }
        
        if (rule.valueLow == 0.0 && rule.valueHigh == 0.0) {
            log("RULE_VALIDATION: Sensor rule %s has no value thresholds", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Sensor rule must have value thresholds");
        }
        
        if (rule.direction == RuleDirection::UNKNOWN) {
            log("RULE_VALIDATION: Sensor rule %s has no direction", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Sensor rule must have direction");
        }
        
        return RuleValidationResult(true);
    }
};

// Step rule validator
class StepRuleValidator : public BaseRuleValidator {
public:
    StepRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto baseResult = BaseRuleValidator::validate(rule);
        if (!baseResult.isValid) {
            return baseResult;
        }
        
        if (rule.sensorUid.empty()) {
            log("RULE_VALIDATION: Step rule %s has no sensor UID", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Step rule must have sensor UID");
        }
        
        if (rule.valueTarget == 0.0) {
            log("RULE_VALIDATION: Step rule %s has no target value", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Step rule must have target value");
        }
        
        if (rule.direction == RuleDirection::UNKNOWN) {
            log("RULE_VALIDATION: Step rule %s has no direction", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Step rule must have direction");
        }
        
        if (rule.workingTime <= 0 || rule.waitingTime <= 0) {
            log("RULE_VALIDATION: Step rule %s has invalid working/waiting time", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Step rule must have valid working and waiting times");
        }
        
        return RuleValidationResult(true);
    }
};

// EC rule validator
class ECRuleValidator : public BaseRuleValidator {
public:
    ECRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto baseResult = BaseRuleValidator::validate(rule);
        if (!baseResult.isValid) {
            return baseResult;
        }
        
        if (rule.sensorUid.empty()) {
            log("RULE_VALIDATION: EC rule %s has no sensor UID", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "EC rule must have sensor UID");
        }
        
        if (rule.valueLow == 0.0 && rule.valueHigh == 0.0) {
            log("RULE_VALIDATION: EC rule %s has no value thresholds", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "EC rule must have value thresholds");
        }
        
        if (rule.direction == RuleDirection::UNKNOWN) {
            log("RULE_VALIDATION: EC rule %s has no direction", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "EC rule must have direction");
        }
        
        // EC rules need mixTime and dosingTime
        if (rule.workingTime <= 0 || rule.waitingTime <= 0) {
            log("RULE_VALIDATION: EC rule %s has no mixing/dosing time", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "EC rule must have mixing and dosing times");
        }
        
        return RuleValidationResult(true);
    }
};

// PH rule validator (same as EC)
class PHRuleValidator : public ECRuleValidator {
public:
    PHRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : ECRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto result = ECRuleValidator::validate(rule);
        if (!result.isValid) {
            // Update error messages for PH context
            if (result.errorMessage.find("EC rule") != std::string::npos) {
                result.errorMessage = std::regex_replace(result.errorMessage, std::regex("EC rule"), "PH rule");
            }
        }
        return result;
    }
};

// Dimmer Auto rule validator
class DimmerAutoRuleValidator : public BaseRuleValidator {
public:
    DimmerAutoRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto baseResult = BaseRuleValidator::validate(rule);
        if (!baseResult.isValid) {
            return baseResult;
        }
        
        if (rule.sensorUid.empty()) {
            log("RULE_VALIDATION: Dimmer Auto rule %s has no sensor UID", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Dimmer Auto rule must have sensor UID");
        }
        
        if (rule.startDayTime == 0 || rule.endDayTime == 0) {
            log("RULE_VALIDATION: Dimmer Auto rule %s has no start/end day time", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Dimmer Auto rule must have start and end day times");
        }
        
        return RuleValidationResult(true);
    }
};

// Dimmer Manual rule validator
class DimmerManualRuleValidator : public BaseRuleValidator {
public:
    DimmerManualRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto baseResult = BaseRuleValidator::validate(rule);
        if (!baseResult.isValid) {
            return baseResult;
        }
        
        if (rule.startDayTime == 0 || rule.endDayTime == 0) {
            log("RULE_VALIDATION: Dimmer Manual rule %s has no start/end day time", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Dimmer Manual rule must have start and end day times");
        }
        
        return RuleValidationResult(true);
    }
};

// Loop rule validator
class LoopRuleValidator : public BaseRuleValidator {
public:
    LoopRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto baseResult = BaseRuleValidator::validate(rule);
        if (!baseResult.isValid) {
            return baseResult;
        }
        
        if (rule.waitingTime <= 0 || rule.workingTime <= 0) {
            log("RULE_VALIDATION: Loop rule %s has invalid waiting/working time", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Loop rule must have valid waiting and working times");
        }
        
        return RuleValidationResult(true);
    }
};

// Average Sensor rule validator
class AverageSensorRuleValidator : public BaseRuleValidator {
public:
    AverageSensorRuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : BaseRuleValidator(logger, debugSystem) {}
    
    RuleValidationResult validate(const Rule& rule) override {
        auto baseResult = BaseRuleValidator::validate(rule);
        if (!baseResult.isValid) {
            return baseResult;
        }
        
        // For average sensor rules, we need multiple sensor UIDs
        if (rule.conditions.empty() || rule.conditions[0].sensorUids.empty()) {
            log("RULE_VALIDATION: Average Sensor rule %s has no sensor UIDs", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Average Sensor rule must have sensor UIDs");
        }
        
        if (rule.valueLow == 0.0 && rule.valueHigh == 0.0) {
            log("RULE_VALIDATION: Average Sensor rule %s has no value thresholds", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Average Sensor rule must have value thresholds");
        }
        
        if (rule.direction == RuleDirection::UNKNOWN) {
            log("RULE_VALIDATION: Average Sensor rule %s has no direction", rule.uid.c_str());
            return RuleValidationResult(false, "wrong_format", "Average Sensor rule must have direction");
        }
        
        return RuleValidationResult(true);
    }
};

// Rule validator factory
class RuleValidatorFactory {
private:
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    
public:
    RuleValidatorFactory(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : _logger(logger), _debugSystem(debugSystem) {}
    
    BaseRuleValidator* createValidator(RuleType ruleType) {
        switch (ruleType) {
            case RuleType::SCHEDULE:
                return new ScheduleRuleValidator(_logger, _debugSystem);
            case RuleType::SENSOR:
                return new SensorRuleValidator(_logger, _debugSystem);
            case RuleType::EC:
                return new ECRuleValidator(_logger, _debugSystem);
            case RuleType::PH:
                return new PHRuleValidator(_logger, _debugSystem);
            case RuleType::DIMMER_AUTO:
                return new DimmerAutoRuleValidator(_logger, _debugSystem);
            case RuleType::DIMMER_MANUAL:
                return new DimmerManualRuleValidator(_logger, _debugSystem);
            case RuleType::LOOP:
                return new LoopRuleValidator(_logger, _debugSystem);
            case RuleType::STEP:
                return new StepRuleValidator(_logger, _debugSystem);
            case RuleType::AVERAGE_SENSOR:
                return new AverageSensorRuleValidator(_logger, _debugSystem);
            default:
                return new UnknownRuleValidator(_logger, _debugSystem);
        }
    }
};

// Main rule validator service
class RuleValidator {
private:
    RuleValidatorFactory _factory;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    
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

public:
    RuleValidator(ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : _factory(logger, debugSystem), _logger(logger), _debugSystem(debugSystem) {}
    
    // Validate a single rule
    RuleValidationResult validateRule(const Rule& rule) {
        log("RULE_VALIDATION: Validating rule %s of type %d", rule.uid.c_str(), static_cast<int>(rule.ruleType));
        
        // Handle deleting rules
        if (rule.state == RuleState::DELETING) {
            log("RULE_VALIDATION: Rule %s is being deleted - validation passed", rule.uid.c_str());
            return RuleValidationResult(true);
        }
        
        BaseRuleValidator* validator = _factory.createValidator(rule.ruleType);
        RuleValidationResult result = validator->validate(rule);
        delete validator; // Clean up
        
        if (!result.isValid) {
            log("RULE_VALIDATION: Rule %s validation failed: %s", rule.uid.c_str(), result.errorMessage.c_str());
        } else {
            log("RULE_VALIDATION: Rule %s validation passed", rule.uid.c_str());
        }
        
        return result;
    }
    
    // Validate multiple rules
    std::vector<RuleValidationResult> validateRules(const std::vector<Rule>& rules) {
        std::vector<RuleValidationResult> results;
        results.reserve(rules.size());
        
        log("RULE_VALIDATION: Validating %d rules", static_cast<int>(rules.size()));
        
        for (const auto& rule : rules) {
            results.push_back(validateRule(rule));
        }
        
        return results;
    }
    
    // Check if all rules are valid
    bool areAllRulesValid(const std::vector<Rule>& rules) {
        auto results = validateRules(rules);
        
        for (const auto& result : results) {
            if (!result.isValid) {
                return false;
            }
        }
        
        return true;
    }
    
    // Validate rule execution (runtime validation)
    RuleValidationResult validateRuleExecution(const Rule& rule) {
        log("RULE_VALIDATION: Validating rule execution for %s", rule.uid.c_str());
        
        BaseRuleValidator* validator = _factory.createValidator(rule.ruleType);
        RuleValidationResult result = validator->validateExecution(rule);
        delete validator; // Clean up
        
        return result;
    }
};

#endif // RULE_VALIDATOR_H
