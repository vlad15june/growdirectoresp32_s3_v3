#ifndef EVALUATE_RULES_USE_CASE_H
#define EVALUATE_RULES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/Rules/IEvaluateRulesUseCase.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Application/Services/Rules/RuleValidator.h"
#include "Core/Domain/Data/Models/RuleEvaluationResult.h"
#include "Core/Domain/Interfaces/Rules/IRuleStateNotifier.h"
#include "Core/Utils/WindArc.h"
#include <map>
#include <set>
#include <cstdarg>
#include <cstdio>

enum class SensorVerdict { TURN_ON, TURN_OFF, HOLD, INERT };

enum class FreshnessVerdict { FRESH, STALE, WAITING };

inline SensorVerdict evaluateSensorHysteresis(RuleDirection direction, double value, double valueLow, double valueHigh, bool previouslyOn) {
    if (direction == RuleDirection::UNKNOWN) {
        return SensorVerdict::INERT;
    }

    if (direction == RuleDirection::BETWEEN) {
        return Utils::isInsideArc(value, valueLow, valueHigh) ? SensorVerdict::TURN_ON : SensorVerdict::TURN_OFF;
    }

    double lo = (valueLow < valueHigh) ? valueLow : valueHigh;
    double hi = (valueLow > valueHigh) ? valueLow : valueHigh;

    if (direction == RuleDirection::DOWN) {
        if (value >= hi) {
            return previouslyOn ? SensorVerdict::HOLD : SensorVerdict::TURN_ON;
        }
        if (value <= lo) {
            return previouslyOn ? SensorVerdict::TURN_OFF : SensorVerdict::HOLD;
        }
        return SensorVerdict::HOLD;
    }

    if (value <= lo) {
        return previouslyOn ? SensorVerdict::HOLD : SensorVerdict::TURN_ON;
    }
    if (value >= hi) {
        return previouslyOn ? SensorVerdict::TURN_OFF : SensorVerdict::HOLD;
    }
    return SensorVerdict::HOLD;
}

inline bool combineConditionsDnf(const std::vector<RuleCondition>& conditions,
                                 const std::vector<bool>& condFired,
                                 bool conditionOr) {
    bool hasJoinOp = false;
    for (size_t i = 1; i < conditions.size(); ++i) {
        if (conditions[i].joinOp != RuleJoinOp::UNSET) {
            hasJoinOp = true;
            break;
        }
    }

    if (!hasJoinOp) {
        if (condFired.empty()) {
            return false;
        }
        if (conditionOr) {
            for (bool f : condFired) {
                if (f) return true;
            }
            return false;
        }
        for (bool f : condFired) {
            if (!f) return false;
        }
        return true;
    }

    bool result = false;
    bool group = true;
    for (size_t i = 0; i < condFired.size(); ++i) {
        bool f = condFired[i];
        RuleJoinOp op = (i == 0) ? RuleJoinOp::UNSET : conditions[i].joinOp;
        if (i == 0) {
            group = f;
        } else if (op == RuleJoinOp::OR) {
            result = result || group;
            group = f;
        } else {
            group = group && f;
        }
    }
    result = result || group;
    return result;
}

struct DelayDecision {
    DeviceStateEnum newState;
    const char* ruleStopReason;
    bool armTimer;
    unsigned long newDeadline;
    bool clearTimer;
};

inline DelayDecision decideDelayTransition(DeviceStateEnum current, bool targetOn,
                                           int delayOnSec, int delayOffSec,
                                           unsigned long now, unsigned long deadline) {
    DelayDecision d{current, "", false, 0, false};

    if (current == DEVICE_STATE_DELAY_ON) {
        if (!targetOn) {
            d.newState = DEVICE_STATE_OFF;
            d.ruleStopReason = "unstable_sensor";
            d.clearTimer = true;
        } else if (now >= deadline) {
            d.newState = DEVICE_STATE_REQUEST_ON;
            d.clearTimer = true;
        } else {
            d.newState = DEVICE_STATE_DELAY_ON;
        }
        return d;
    }

    if (current == DEVICE_STATE_DELAY_OFF) {
        if (targetOn) {
            d.newState = DEVICE_STATE_REQUEST_ON;
            d.ruleStopReason = "unstable_sensor";
            d.clearTimer = true;
        } else if (now >= deadline) {
            d.newState = DEVICE_STATE_OFF;
            d.ruleStopReason = "setpoint_reach";
            d.clearTimer = true;
        } else {
            d.newState = DEVICE_STATE_DELAY_OFF;
        }
        return d;
    }

    bool committedOn = (current == DEVICE_STATE_ON || current == DEVICE_STATE_REQUEST_ON);
    if (committedOn) {
        if (targetOn) {
            d.newState = current;
        } else if (delayOffSec > 0) {
            d.newState = DEVICE_STATE_DELAY_OFF;
            d.armTimer = true;
            d.newDeadline = now + (unsigned long)delayOffSec * 1000UL;
        } else {
            d.newState = DEVICE_STATE_OFF;
            d.ruleStopReason = "setpoint_reach";
        }
        return d;
    }

    if (!targetOn) {
        d.newState = DEVICE_STATE_OFF;
    } else if (delayOnSec > 0) {
        d.newState = DEVICE_STATE_DELAY_ON;
        d.armTimer = true;
        d.newDeadline = now + (unsigned long)delayOnSec * 1000UL;
    } else {
        d.newState = DEVICE_STATE_REQUEST_ON;
    }
    return d;
}

/**
 * Use case for evaluating rules and executing their actions
 */
class EvaluateRulesUseCase : public IEvaluateRulesUseCase {
private:
    IDeviceModel* _deviceModel;
    IRuleRepository* _ruleRepository;
    ISensorValueModel* _sensorValueModel;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IArduinoAdapter* _arduinoAdapter;
    INTPTimeAdapter* _ntpTimeAdapter;
    IRuleStateNotifier* _ruleStateNotifier;
    RuleValidator _ruleValidator;

    // Rule state tracking
    std::map<std::string, RuleEvaluationResult> _ruleActiveStates;
    std::map<std::string, RuleEvaluationResult> _lastEvaluationResults;
    std::map<std::string, unsigned long> _ruleLastEvaluated;
    std::map<std::string, unsigned long> _delayTimers;
    std::map<std::string, RuleEvaluationResult> _deviceRuleContext;
    std::map<std::string, int> _deletePurgeRetries;
    std::map<std::string, unsigned long> _sensorWaitSince;

    static const int DELETING_PURGE_MAX_RETRIES = 3;
    static const unsigned long SENSOR_FRESHNESS_WINDOW_MS = 300000;

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
            IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::RULE_DEBUG);
            if (feature) {
                _logger->println(DebugFeatures::RULE_DEBUG, buffer, feature->getLogChannel());
            } else {
                _logger->println(DebugFeatures::RULE_DEBUG, buffer);
            }
        }
    }

    void logSerial(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::RULE_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            _logger->println(DebugFeatures::RULE_DEBUG, buffer, MqttLoggerMode::SerialOnly);
        }
    }

    static bool conditionsHaveJoinOp(const std::vector<RuleCondition>& conditions) {
        for (size_t i = 1; i < conditions.size(); ++i) {
            if (conditions[i].joinOp != RuleJoinOp::UNSET) {
                return true;
            }
        }
        return false;
    }

    static std::string conditionsOpsString(const std::vector<RuleCondition>& conditions) {
        std::string ops;
        for (size_t i = 0; i < conditions.size(); ++i) {
            if (i > 0) {
                ops += ",";
            }
            if (i == 0 || conditions[i].joinOp == RuleJoinOp::UNSET) {
                ops += "-";
            } else {
                ops += getRuleJoinOpName(conditions[i].joinOp);
            }
        }
        return ops;
    }

    void purgeDeletingRules() {
        if (!_ruleRepository) {
            return;
        }

        std::vector<Rule> allRules = _ruleRepository->getAll();
        for (const auto& rule : allRules) {
            if (rule.state != RuleState::DELETING) {
                continue;
            }

            log("EVALUATE_RULES: RULE_DELETE mark uid=%s deviceUid=%s",
                rule.uid.c_str(), rule.deviceUid.c_str());

            auto activeIt = _ruleActiveStates.find(rule.uid);
            bool wasActive = activeIt != _ruleActiveStates.end() &&
                             activeIt->second.state != DEVICE_STATE_OFF;
            if (wasActive && _deviceModel) {
                RuleEvaluationResult off(rule.uid, rule.ruleType, rule.version);
                off.workflowUid = rule.workflowUid;
                off.workflowVersion = rule.workflowVersion;
                off.state = DEVICE_STATE_OFF;
                off.isRuleTurnOnDevice = false;
                off.ruleStopReason = "rule_manualy_deleted";
                off.sensorUid = rule.sensorUid;
                off.sensorValue = static_cast<float>(getSensorValue(rule.sensorUid));
                off.sensorValueLow = rule.valueLow;
                off.sensorValueHigh = rule.valueHigh;
                off.ruleSensorStopAt = sensorEventTimestamp();
                off.switchedAt = sensorEventTimestamp();
                executeDeviceAction(rule.deviceUid, off);

                log("EVALUATE_RULES: RULE_DELETE device_off uid=%s deviceUid=%s reason=rule_manualy_deleted",
                    rule.uid.c_str(), rule.deviceUid.c_str());
            }

            _ruleActiveStates.erase(rule.uid);
            _lastEvaluationResults.erase(rule.uid);
            _ruleLastEvaluated.erase(rule.uid);
            _delayTimers.erase(rule.uid);

            if (_ruleRepository->remove(rule.uid.c_str())) {
                _deletePurgeRetries.erase(rule.uid);
                log("EVALUATE_RULES: RULE_DELETE purged uid=%s", rule.uid.c_str());
            } else {
                int retries = ++_deletePurgeRetries[rule.uid];
                if (retries <= DELETING_PURGE_MAX_RETRIES) {
                    log("EVALUATE_RULES: RULE_DELETE purge_fail uid=%s retry=%d", rule.uid.c_str(), retries);
                }
                if (retries == DELETING_PURGE_MAX_RETRIES) {
                    log("EVALUATE_RULES: RULE_DELETE purge_giveup uid=%s", rule.uid.c_str());
                }
            }
        }
    }

    void deactivateDroppedRules(const std::set<std::string>& activeUids) {
        if (!_ruleRepository) {
            return;
        }

        std::vector<Rule> allRules = _ruleRepository->getAll();
        for (const auto& rule : allRules) {
            if (rule.mode != RuleMode::OFF) {
                continue;
            }
            if (activeUids.count(rule.uid)) {
                continue;
            }

            auto it = _ruleActiveStates.find(rule.uid);
            bool wasOn = it != _ruleActiveStates.end() && it->second.isDeviceOn();
            if (!wasOn) {
                continue;
            }

            log("EVALUATE_RULES: RULE_DEACTIVATED uid=%s deviceUid=%s reason=rule_manualy_off",
                rule.uid.c_str(), rule.deviceUid.c_str());

            RuleEvaluationResult off(rule.uid, rule.ruleType, rule.version);
            off.workflowUid = rule.workflowUid;
            off.workflowVersion = rule.workflowVersion;
            off.state = DEVICE_STATE_OFF;
            off.isRuleTurnOnDevice = false;
            off.ruleStopReason = "rule_manualy_off";
            off.sensorUid = rule.sensorUid;
            off.sensorValue = static_cast<float>(getSensorValue(rule.sensorUid));
            off.sensorValueLow = rule.valueLow;
            off.sensorValueHigh = rule.valueHigh;
            off.ruleSensorStopAt = sensorEventTimestamp();
            off.switchedAt = sensorEventTimestamp();
            executeDeviceAction(rule.deviceUid, off);

            _ruleActiveStates.erase(rule.uid);
            _lastEvaluationResults.erase(rule.uid);
            _ruleLastEvaluated.erase(rule.uid);
            _delayTimers.erase(rule.uid);
        }
    }

    void deactivateInactiveCyclingRules(const std::set<std::string>& activeUids) {
        if (!_ruleRepository || !_deviceModel) {
            return;
        }

        std::vector<Rule> allRules = _ruleRepository->getAll();
        for (const auto& rule : allRules) {
            if (rule.ruleType != RuleType::LOOP && rule.ruleType != RuleType::STEP) {
                continue;
            }
            if (activeUids.count(rule.uid)) {
                continue;
            }

            Device* device = _deviceModel->getDeviceByUid(rule.deviceUid.c_str());
            if (!device || !isStepCycleState(device->state)) {
                continue;
            }
            if (!device->ruleUid || rule.uid != device->ruleUid) {
                continue;
            }

            RuleEvaluationResult off(rule.uid, rule.ruleType, rule.version);
            off.workflowUid = rule.workflowUid;
            off.workflowVersion = rule.workflowVersion;
            off.state = DEVICE_STATE_OFF;
            off.isRuleTurnOnDevice = false;
            off.ruleStopReason = "rule_off_by_date";
            off.sensorUid = rule.sensorUid;
            off.sensorValueLow = rule.valueLow;
            off.sensorValueHigh = rule.valueHigh;
            off.switchedAt = sensorEventTimestamp();
            executeDeviceAction(rule.deviceUid, off);

            _ruleActiveStates.erase(rule.uid);
            _lastEvaluationResults.erase(rule.uid);
            _ruleLastEvaluated.erase(rule.uid);

            log("EVALUATE_RULES: %s_STOP uid=%s deviceUid=%s reason=rule_off_by_date",
                rule.ruleType == RuleType::LOOP ? "LOOP" : "STEP",
                rule.uid.c_str(), rule.deviceUid.c_str());
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
        
        RuleEvaluationResult merged = evaluationResult;
        if (merged.state == DEVICE_STATE_OFF) {
            auto prevIt = _deviceRuleContext.find(deviceUid);
            if (prevIt != _deviceRuleContext.end()) {
                const RuleEvaluationResult& prev = prevIt->second;
                if (merged.ruleSensorStartAt.empty()) merged.ruleSensorStartAt = prev.ruleSensorStartAt;
                if (merged.ruleSensorValueStart == 0.0f) merged.ruleSensorValueStart = prev.ruleSensorValueStart;
                if (merged.ruleScheduleStartAt.empty()) merged.ruleScheduleStartAt = prev.ruleScheduleStartAt;
                if (merged.ruleScheduleDuration == 0) merged.ruleScheduleDuration = prev.ruleScheduleDuration;
                if (merged.cyclesToSetPoint == 0) merged.cyclesToSetPoint = prev.cyclesToSetPoint;
                if (merged.sensorUid.empty()) merged.sensorUid = prev.sensorUid;
                if (merged.sensorValueLow == 0.0f) merged.sensorValueLow = prev.sensorValueLow;
                if (merged.sensorValueHigh == 0.0f) merged.sensorValueHigh = prev.sensorValueHigh;
            }
        }
        _deviceRuleContext[deviceUid] = merged;
        const RuleEvaluationResult& ctx = _deviceRuleContext[deviceUid];

        device->state = ctx.state;
        device->durationOn = ctx.durationOn;
        device->timeOn = ctx.timeOn;
        device->timeOff = ctx.timeOff;
        device->needToSwitchBack = ctx.needToSwitchBack;
        device->mixTime = ctx.mixTime;

        device->workByRule = true;
        device->isAuto = ctx.isRuleTurnOnDevice;
        device->ruleUid = ctx.ruleUid.c_str();
        device->ruleType = ctx.ruleType;
        device->ruleVersion = ctx.ruleVersion;
        device->workflowUid = ctx.workflowUid.c_str();
        device->workflowVersion = ctx.workflowVersion;
        device->sensorUid = ctx.sensorUid.c_str();
        device->sensorValue = ctx.sensorValue;
        device->sensorValueTime = ctx.sensorValueTime;
        device->sensorValueLow = ctx.sensorValueLow;
        device->sensorValueHigh = ctx.sensorValueHigh;
        device->ruleSensorStartAt = ctx.ruleSensorStartAt.c_str();
        device->ruleSensorValueStart = ctx.ruleSensorValueStart;
        device->ruleSensorStopAt = ctx.ruleSensorStopAt.c_str();
        device->ruleSensorValueStop = ctx.ruleSensorValueStop;
        device->ruleStopReason = ctx.ruleStopReason.c_str();
        device->ruleScheduleStartAt = ctx.ruleScheduleStartAt.c_str();
        device->ruleScheduleDuration = ctx.ruleScheduleDuration;
        device->switchedAt = ctx.switchedAt.c_str();

        if (evaluationResult.state == DEVICE_STATE_OFF && !ctx.ruleStopReason.empty()) {
            device->needReportingRuleUid = true;
        }

        if (device->deviceType != DeviceTypeEnum::DEVICE_TYPE_ANALOG) {
            DeviceDigitalOut* digitalDevice = static_cast<DeviceDigitalOut*>(device);
            digitalDevice->isRuleTurnOnDevice = ctx.isRuleTurnOnDevice;
            digitalDevice->timeToSetPoint = ctx.timeToSetPoint;
            digitalDevice->cyclesToSetPoint = ctx.cyclesToSetPoint;
            digitalDevice->currentCycle = ctx.currentCycle;
        }

        log("EVALUATE_RULES: Executing device action - device %s, state %s, ruleUid %s",
            deviceUid.c_str(), getDeviceStateName(evaluationResult.state), ctx.ruleUid.c_str());

        log("EVALUATE_RULES: WF_ATTR deviceUid=%s ruleUid=%s workflowUid=%s workflowVersion=%d",
            deviceUid.c_str(), ctx.ruleUid.c_str(), ctx.workflowUid.c_str(), evaluationResult.workflowVersion);

        return true;
    }

public:
    EvaluateRulesUseCase(
        IDeviceModel* deviceModel,
        IRuleRepository* ruleRepository,
        IArduinoAdapter* arduinoAdapter,
        INTPTimeAdapter* ntpTimeAdapter,
        ISensorValueModel* sensorValueModel,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        IRuleStateNotifier* ruleStateNotifier = nullptr
    ) : _deviceModel(deviceModel),
        _ruleRepository(ruleRepository),
        _sensorValueModel(sensorValueModel),
        _logger(logger),
        _debugSystem(debugSystem),
        _arduinoAdapter(arduinoAdapter),
        _ntpTimeAdapter(ntpTimeAdapter),
        _ruleStateNotifier(ruleStateNotifier),
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
        
        if (!_sensorValueModel) {
            log("EVALUATE_RULES: Sensor value model not available");
            return false;
        }

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

        logSerial("EVALUATE_RULES: Starting rule evaluation cycle at %lu ms, last= %lu, elapsed= %lu", now, _lastEvaluationTime, elapsed);
        
        _lastEvaluationTime = now;
        
        if (!_ruleRepository) {
            log("EVALUATE_RULES: Rule repository not available in loop");
            return;
        }

        purgeDeletingRules();

        // Get active rules from repository
        std::vector<Rule> activeRules = _ruleRepository->getActiveRules();
        
        logSerial("EVALUATE_RULES: Starting rule evaluation cycle - %d active rules",
            static_cast<int>(activeRules.size()));
        
        // Evaluate all active rules
        std::set<std::string> activeUids;
        for (const auto& rule : activeRules) {
            activeUids.insert(rule.uid);
            if (rule.state == RuleState::SYNCHRONIZED) {
                FreshnessVerdict freshness = ruleSensorFreshness(rule, now);
                if (freshness == FreshnessVerdict::STALE) {
                    transitionToSensorUnreachable(rule);
                } else if (freshness == FreshnessVerdict::FRESH) {
                    if (!maybeProtectiveShutdown(rule, now) && !maybeStepProtectiveShutdown(rule, now)) {
                        executeRuleActions(rule);
                    }
                }
            } else if (rule.state == RuleState::ERROR &&
                       rule.errorType == "sensor_unreachable") {
                if (ruleSensorFreshness(rule, now) == FreshnessVerdict::FRESH) {
                    recoverFromSensorUnreachable(rule);
                }
            }
        }

        deactivateDroppedRules(activeUids);
        deactivateInactiveCyclingRules(activeUids);
    }

    RuleEvaluationResult evaluateRule(const Rule& rule) override {
        logSerial("EVALUATE_RULES: Evaluating rule %s", rule.uid.c_str());
        
        // Create result with basic rule information
        RuleEvaluationResult result(rule.uid, rule.ruleType, rule.version);
        result.workflowUid = rule.workflowUid;
        result.workflowVersion = rule.workflowVersion;
        
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

        if (!isRuleActiveNow(rule, result)) {
            return result;
        }

        // Fill sensor information
        if (!rule.sensorUid.empty()) {
            result.sensorUid = rule.sensorUid;
            result.sensorValue = getSensorValue(rule.sensorUid);
            if (_arduinoAdapter) {
                result.sensorValueTime = _arduinoAdapter->millis(); //TODO: check, may be need real tile
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
            case RuleType::SENSOR: {
                DeviceStateEnum currentState = _ruleActiveStates[rule.uid].state;
                bool committedOn = (currentState == DEVICE_STATE_ON ||
                                    currentState == DEVICE_STATE_REQUEST_ON ||
                                    currentState == DEVICE_STATE_DELAY_OFF);
                SensorVerdict verdict = evaluateSensorRule(rule, committedOn);
                bool targetOn;
                switch (verdict) {
                    case SensorVerdict::TURN_ON:
                        targetOn = true;
                        break;
                    case SensorVerdict::TURN_OFF:
                    case SensorVerdict::INERT:
                        targetOn = false;
                        break;
                    case SensorVerdict::HOLD:
                    default:
                        targetOn = committedOn;
                        break;
                }
                applyDelay(rule, targetOn, currentState, result);
                conditionMet = targetOn;
                resultState = result.state;
                break;
            }
            case RuleType::STEP:
                evaluateStepRule(rule, result);
                conditionMet = result.isRuleTurnOnDevice;
                resultState = result.state;
                break;
            case RuleType::AVERAGE_SENSOR: {
                DeviceStateEnum currentState = _ruleActiveStates[rule.uid].state;
                bool committedOn = (currentState == DEVICE_STATE_ON ||
                                    currentState == DEVICE_STATE_REQUEST_ON ||
                                    currentState == DEVICE_STATE_DELAY_OFF);
                SensorVerdict verdict = evaluateAverageSensorRule(rule, committedOn, result);
                bool targetOn;
                switch (verdict) {
                    case SensorVerdict::TURN_ON:
                        targetOn = true;
                        break;
                    case SensorVerdict::TURN_OFF:
                    case SensorVerdict::INERT:
                        targetOn = false;
                        break;
                    case SensorVerdict::HOLD:
                    default:
                        targetOn = committedOn;
                        break;
                }
                applyDelay(rule, targetOn, currentState, result);
                conditionMet = targetOn;
                resultState = result.state;
                break;
            }
            case RuleType::LOOP:
                evaluateLoopRule(rule, result);
                conditionMet = result.isRuleTurnOnDevice;
                resultState = result.state;
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
        
        logSerial("EVALUATE_RULES: Rule %s condition %s, state: %s",
            rule.uid.c_str(), conditionMet ? "MET" : "NOT MET", getDeviceStateName(result.state));
        return result;
    }

    bool executeRuleActions(const Rule& rule) override {
        logSerial("EVALUATE_RULES: Executing actions for rule %s", rule.uid.c_str());
        
        RuleEvaluationResult currentState = _ruleActiveStates[rule.uid];
        RuleEvaluationResult evaluationResult = evaluateRule(rule);
        
        if (currentState.state == evaluationResult.state) {
            return true; // No change needed
        }

        evaluationResult.switchedAt = sensorEventTimestamp();
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
    bool isRuleActiveNow(const Rule& rule, RuleEvaluationResult& result) {
        bool hasWindow = (rule.startDayTime != 0 || rule.endDayTime != 0);
        bool hasDays = (!rule.daysOfWeek.empty() || !rule.daysOfMonth.empty());
        if (!hasWindow && !hasDays) {
            return true;
        }

        if (!_ntpTimeAdapter) {
            log("EVALUATE_RULES: NTP time adapter not available for activity window of rule %s", rule.uid.c_str());
            result.state = DEVICE_STATE_OFF;
            result.isRuleTurnOnDevice = false;
            result.ruleStopReason = "time_not_available_error";
            return false;
        }

        time_t currentTime = _ntpTimeAdapter->getEpochTime();
        struct tm timeinfo;
        if (!gmtime_r(&currentTime, &timeinfo)) {
            log("EVALUATE_RULES: Failed to get current time for activity window of rule %s", rule.uid.c_str());
            result.state = DEVICE_STATE_OFF;
            result.isRuleTurnOnDevice = false;
            result.ruleStopReason = "time_not_available_error";
            return false;
        }

        if (hasWindow) {
            long nowSec = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
            long startSec = static_cast<long>(rule.startDayTime);
            long endSec = static_cast<long>(rule.endDayTime);
            bool windowOpen;
            if (endSec >= startSec) {
                windowOpen = (nowSec >= startSec && nowSec <= endSec);
            } else {
                windowOpen = (nowSec >= startSec || nowSec <= endSec);
            }
            if (!windowOpen) {
                log("EVALUATE_RULES: Rule %s outside operation time window (now=%ld start=%ld end=%ld)",
                    rule.uid.c_str(), nowSec, startSec, endSec);
                result.state = DEVICE_STATE_OFF;
                result.isRuleTurnOnDevice = false;
                result.ruleStopReason = "time_reach";
                return false;
            }
        }

        if (hasDays) {
            bool dayMatches = false;
            for (int day : rule.daysOfWeek) {
                if (day == timeinfo.tm_wday) { dayMatches = true; break; }
            }
            if (!dayMatches) {
                for (int day : rule.daysOfMonth) {
                    if (day == timeinfo.tm_mday) { dayMatches = true; break; }
                }
            }
            if (!dayMatches) {
                log("EVALUATE_RULES: Rule %s not active on current day (wday=%d mday=%d)",
                    rule.uid.c_str(), timeinfo.tm_wday, timeinfo.tm_mday);
                result.state = DEVICE_STATE_OFF;
                result.isRuleTurnOnDevice = false;
                result.ruleStopReason = "rule_off_by_date";
                return false;
            }
        }

        return true;
    }

    RuleEvaluationResult evaluateScheduleRule(const Rule& rule, RuleEvaluationResult& result) {
        log("EVALUATE_RULES: Evaluating schedule rule %s", rule.uid.c_str());
        
        #ifdef DEBUG
        log("EVALUATE_RULES: Rule data - uid: %s, version: %d, deviceUid: %s", 
            rule.uid.c_str(), rule.version, rule.deviceUid.c_str());
        log("EVALUATE_RULES: Rule type: %d, mode: %d, state: %d, protectiveShutdown: %s", 
            static_cast<int>(rule.ruleType), static_cast<int>(rule.mode), 
            static_cast<int>(rule.state), rule.protectiveShutdown ? "true" : "false");
        log("EVALUATE_RULES: Mode reason: %s", rule.modeReason.c_str());
        log("EVALUATE_RULES: SensorUid: %s, direction: %d", 
            rule.sensorUid.c_str(), static_cast<int>(rule.direction));
        log("EVALUATE_RULES: ValueTarget: %.2f, cyclesToSetPoint: %d, workingTime: %d, waitingTime: %d", 
            rule.valueTarget, rule.cyclesToSetPoint, rule.workingTime, rule.waitingTime);
        log("EVALUATE_RULES: ValueLow: %.2f, valueHigh: %.2f, timeToSetPoint: %d", 
            rule.valueLow, rule.valueHigh, rule.timeToSetPoint);
        log("EVALUATE_RULES: IsAdvanced: %s, conditionOr: %s, delayOn: %d, delayOff: %d", 
            rule.isAdvanced ? "true" : "false", rule.conditionOr ? "true" : "false", 
            rule.delayOn, rule.delayOff);
        log("EVALUATE_RULES: StartDayTime: %ld, endDayTime: %ld", 
            static_cast<long>(rule.startDayTime), static_cast<long>(rule.endDayTime));
        log("EVALUATE_RULES: DaysOfWeek count: %d, daysOfMonth count: %d", 
            static_cast<int>(rule.daysOfWeek.size()), static_cast<int>(rule.daysOfMonth.size()));
        log("EVALUATE_RULES: Conditions count: %d, advancedRuleValues count: %d", 
            static_cast<int>(rule.conditions.size()), static_cast<int>(rule.advancedRuleValues.size()));
        log("EVALUATE_RULES: Schedule items count: %d", static_cast<int>(rule.schedule.size()));
        
        // Log schedule items details
        for (size_t i = 0; i < rule.schedule.size(); ++i) {
            log("EVALUATE_RULES: Schedule[%d] - startAt: %ld, duration: %d", 
                static_cast<int>(i), static_cast<long>(rule.schedule[i].startAt), rule.schedule[i].duration);
        }
        
        // Log days of week if any
        if (!rule.daysOfWeek.empty()) {
            char daysBuffer[64] = {0};
            int offset = 0;
            for (size_t i = 0; i < rule.daysOfWeek.size() && offset < 60; ++i) {
                offset += snprintf(daysBuffer + offset, 64 - offset, "%d%s", 
                    rule.daysOfWeek[i], (i < rule.daysOfWeek.size() - 1) ? "," : "");
            }
            log("EVALUATE_RULES: DaysOfWeek: [%s]", daysBuffer);
        }
        
        // Log days of month if any
        if (!rule.daysOfMonth.empty()) {
            char daysBuffer[128] = {0};
            int offset = 0;
            for (size_t i = 0; i < rule.daysOfMonth.size() && offset < 120; ++i) {
                offset += snprintf(daysBuffer + offset, 128 - offset, "%d%s", 
                    rule.daysOfMonth[i], (i < rule.daysOfMonth.size() - 1) ? "," : "");
            }
            log("EVALUATE_RULES: DaysOfMonth: [%s]", daysBuffer);
        }
        #endif

        if (!_ntpTimeAdapter) {
            log("EVALUATE_RULES: NTP time adapter not available for schedule rule");
            result.state = DEVICE_STATE_OFF;
            result.isRuleTurnOnDevice = false;
            result.ruleStopReason = "time_not_available_error";
            return result;
        }
        
        // Get current time
        time_t currentTime = _ntpTimeAdapter->getEpochTime();
        struct tm timeinfo;
        if (!gmtime_r(&currentTime, &timeinfo)) {
            log("EVALUATE_RULES: Failed to get current time for schedule rule");
            result.state = DEVICE_STATE_OFF;
            result.isRuleTurnOnDevice = false;
            result.ruleStopReason = "time_not_available_error";
            return result;
        }
        
        logSerial("EVALUATE_RULES: LocalTime = %d:%d", timeinfo.tm_hour, timeinfo.tm_min);
        
        if (rule.schedule.empty()) {
            log("EVALUATE_RULES: No schedule items in rule %s", rule.uid.c_str());
            result.state = DEVICE_STATE_OFF;
            result.isRuleTurnOnDevice = false;
            return result;
        }
        
        logSerial("EVALUATE_RULES: rules[%s][schedule].size= %d", rule.uid.c_str(), static_cast<int>(rule.schedule.size()));
        
        bool isSchRuleOn = false;
        uint32_t durationOn = 0;
        struct tm whenRuleShouldStarted = {0};
        int ruleScheduleDuration = 0;
        char buffer[150];
        buffer[0] = 0;
        
        for (size_t si = 0; si < rule.schedule.size(); ++si) {
            const auto& scheduleItem = rule.schedule[si];
            
            logSerial("EVALUATE_RULES: schedule[%d][duration]= %d", static_cast<int>(si), scheduleItem.duration);
            
            // Convert startAt time_t to tm structure
            struct tm startAt = {0};
            if (!gmtime_r(&scheduleItem.startAt, &startAt)) {
                logSerial("EVALUATE_RULES: Failed to parse startAt time for schedule item %d", static_cast<int>(si));
                continue;
            }
            
            logSerial("EVALUATE_RULES: startAt = %d:%d", startAt.tm_hour, startAt.tm_min);
            
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
                logSerial("EVALUATE_RULES: RULE_ON: whenRuleShouldStarted=%s", buffer);
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
                logSerial("EVALUATE_RULES: RULE_ON: whenRuleShouldStarted=%s", buffer);
                break;
            }

            logSerial("EVALUATE_RULES: RULE_ON: totalSecondWhenStart = %d, totalSecondWhenEnd = %d, totalSecondNow = %d, duration in rule = %d",
                totalSecondWhenStart, totalSecondWhenEnd, totalSecondNow, duration);
        }
        
        if (isSchRuleOn) {
            logSerial("EVALUATE_RULES: RULE_ON: uid = %s, current time = %d:%d; rule time = %d:%d ; duration = %d",
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
            result.ruleStopReason = "time_reach";
        }

        return result;
    }

    std::string sensorEventTimestamp() {
        if (!_ntpTimeAdapter) {
            return "";
        }
        time_t currentTime = _ntpTimeAdapter->getEpochTime();
        struct tm timeinfo;
        if (!gmtime_r(&currentTime, &timeinfo)) {
            return "";
        }
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        return std::string(buffer);
    }

    SensorVerdict evaluateSensorRule(const Rule& rule, bool committedOn) {
        if (!rule.conditions.empty()) {
            std::vector<bool> condFired;
            condFired.reserve(rule.conditions.size());
            for (const auto& cond : rule.conditions) {
                bool condOn;
                if (cond.sensorUids.empty()) {
                    logSerial("EVALUATE_RULES: SENSOR_COND uid=%s reason=no_sensor_uid on=0", rule.uid.c_str());
                    condOn = false;
                } else {
                    double condValue = getSensorValue(cond.sensorUids[0]);
                    SensorVerdict condVerdict = evaluateSensorHysteresis(cond.direction, condValue, cond.valueLow, cond.valueHigh, committedOn);
                    switch (condVerdict) {
                        case SensorVerdict::TURN_ON:
                            condOn = true;
                            break;
                        case SensorVerdict::TURN_OFF:
                        case SensorVerdict::INERT:
                            condOn = false;
                            break;
                        case SensorVerdict::HOLD:
                        default:
                            condOn = committedOn;
                            break;
                    }
                    logSerial("EVALUATE_RULES: SENSOR_COND uid=%s sensor=%s dir=%s value=%.2f low=%.2f high=%.2f on=%d",
                        rule.uid.c_str(), cond.sensorUids[0].c_str(), getRuleDirectionName(cond.direction),
                        condValue, cond.valueLow, cond.valueHigh, condOn ? 1 : 0);
                }
                condFired.push_back(condOn);
            }
            bool targetOn = combineConditionsDnf(rule.conditions, condFired, rule.conditionOr);
            logSerial("EVALUATE_RULES: SENSOR_COND uid=%s mode=%s ops=%s nConds=%d targetOn=%d",
                rule.uid.c_str(),
                conditionsHaveJoinOp(rule.conditions) ? "dnf" : "legacy",
                conditionsOpsString(rule.conditions).c_str(),
                static_cast<int>(rule.conditions.size()), targetOn ? 1 : 0);
            return targetOn ? SensorVerdict::TURN_ON : SensorVerdict::TURN_OFF;
        }

        if (rule.sensorUid.empty()) {
            log("EVALUATE_RULES: SENSOR_EVAL inert uid=%s reason=no_sensor_uid", rule.uid.c_str());
            return SensorVerdict::INERT;
        }

        double sensorValue = getSensorValue(rule.sensorUid);

        unsigned long freshNow = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        uint32_t freshLast = _sensorValueModel ? _sensorValueModel->getLastReadTime(rule.sensorUid) : 0;
        unsigned long freshAge = (freshNow >= (unsigned long)freshLast) ? (freshNow - (unsigned long)freshLast) : 0UL;
        log("EVALUATE_RULES: SENSOR_EVAL uid=%s dir=%s value=%.2f low=%.2f high=%.2f prevOn=%d sensor=%s last=%u age=%lu now=%lu",
            rule.uid.c_str(), getRuleDirectionName(rule.direction), sensorValue,
            rule.valueLow, rule.valueHigh, committedOn ? 1 : 0,
            rule.sensorUid.c_str(), freshLast,
            freshAge, freshNow);

        if (rule.direction == RuleDirection::BETWEEN) {
            log("EVALUATE_RULES: SENSOR_EVAL_BETWEEN uid=%s value=%.2f low=%.2f high=%.2f wrap=%d",
                rule.uid.c_str(), sensorValue, rule.valueLow, rule.valueHigh,
                (rule.valueLow > rule.valueHigh) ? 1 : 0);

            SensorVerdict verdict = evaluateSensorHysteresis(rule.direction, sensorValue, rule.valueLow, rule.valueHigh, committedOn);

            if (verdict == SensorVerdict::TURN_ON) {
                log("EVALUATE_RULES: SENSOR_EVAL_BETWEEN turn_on uid=%s value=%.2f", rule.uid.c_str(), sensorValue);
            } else {
                log("EVALUATE_RULES: SENSOR_EVAL_BETWEEN turn_off uid=%s value=%.2f", rule.uid.c_str(), sensorValue);
            }

            return verdict;
        }

        return evaluateSensorHysteresis(rule.direction, sensorValue, rule.valueLow, rule.valueHigh, committedOn);
    }

    void applyDelay(const Rule& rule, bool targetOn, DeviceStateEnum currentState, RuleEvaluationResult& result) {
        unsigned long now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        unsigned long deadline = 0;
        auto it = _delayTimers.find(rule.uid);
        if (it != _delayTimers.end()) {
            deadline = it->second;
        }

        DelayDecision d = decideDelayTransition(currentState, targetOn, rule.delayOn, rule.delayOff, now, deadline);

        result.state = d.newState;
        result.isRuleTurnOnDevice = targetOn;
        if (d.ruleStopReason[0] != '\0') {
            result.ruleStopReason = d.ruleStopReason;
        }
        if (d.armTimer) {
            _delayTimers[rule.uid] = d.newDeadline;
        }
        if (d.clearTimer) {
            _delayTimers.erase(rule.uid);
        }

        bool wasOn = (currentState == DEVICE_STATE_ON || currentState == DEVICE_STATE_REQUEST_ON);
        bool nowOn = (d.newState == DEVICE_STATE_ON || d.newState == DEVICE_STATE_REQUEST_ON);
        if (nowOn && !wasOn) {
            result.timeOn = now;
            result.ruleSensorStartAt = sensorEventTimestamp();
            result.ruleSensorValueStart = result.sensorValue;
        }
        if (d.newState == DEVICE_STATE_OFF && currentState != DEVICE_STATE_OFF) {
            result.ruleSensorStopAt = sensorEventTimestamp();
            result.ruleSensorValueStop = result.sensorValue;
        }

        logDelayTransition(rule, currentState, d, now, deadline);
    }

    void logDelayTransition(const Rule& rule, DeviceStateEnum currentState,
                            const DelayDecision& d, unsigned long now, unsigned long deadline) {
        bool nowOn = (d.newState == DEVICE_STATE_ON || d.newState == DEVICE_STATE_REQUEST_ON);

        if (currentState == DEVICE_STATE_OFF && d.newState == DEVICE_STATE_DELAY_ON) {
            log("EVALUATE_RULES: SENSOR_DELAY arm_on uid=%s delayOn=%d deadline=%lu",
                rule.uid.c_str(), rule.delayOn, d.newDeadline);
        } else if ((currentState == DEVICE_STATE_ON || currentState == DEVICE_STATE_REQUEST_ON) &&
                   d.newState == DEVICE_STATE_DELAY_OFF) {
            log("EVALUATE_RULES: SENSOR_DELAY arm_off uid=%s delayOff=%d deadline=%lu",
                rule.uid.c_str(), rule.delayOff, d.newDeadline);
        } else if (currentState == DEVICE_STATE_DELAY_ON && nowOn) {
            log("EVALUATE_RULES: SENSOR_DELAY commit_on uid=%s now=%lu", rule.uid.c_str(), now);
        } else if (currentState == DEVICE_STATE_DELAY_OFF && d.newState == DEVICE_STATE_OFF) {
            log("EVALUATE_RULES: SENSOR_DELAY commit_off uid=%s now=%lu reason=setpoint_reach",
                rule.uid.c_str(), now);
        } else if (currentState == DEVICE_STATE_DELAY_ON && d.newState == DEVICE_STATE_OFF) {
            log("EVALUATE_RULES: SENSOR_DELAY cancel uid=%s from=delayOn reason=unstable_sensor",
                rule.uid.c_str());
        } else if (currentState == DEVICE_STATE_DELAY_OFF && nowOn) {
            log("EVALUATE_RULES: SENSOR_DELAY cancel uid=%s from=delayOff reason=unstable_sensor",
                rule.uid.c_str());
        } else if (currentState == DEVICE_STATE_DELAY_ON && d.newState == DEVICE_STATE_DELAY_ON) {
            log("EVALUATE_RULES: SENSOR_DELAY hold uid=%s state=delayOn remain=%lu",
                rule.uid.c_str(), deadline > now ? deadline - now : 0UL);
        } else if (currentState == DEVICE_STATE_DELAY_OFF && d.newState == DEVICE_STATE_DELAY_OFF) {
            log("EVALUATE_RULES: SENSOR_DELAY hold uid=%s state=delayOff remain=%lu",
                rule.uid.c_str(), deadline > now ? deadline - now : 0UL);
        }
    }
    
    static bool isStepCycleState(DeviceStateEnum s) {
        return s == DEVICE_STATE_REQUEST_STEP_ON ||
               s == DEVICE_STATE_STEP_ON ||
               s == DEVICE_STATE_STEP_WAITING;
    }

    double computeMean(const std::string& ruleUid, const std::vector<std::string>& uids) {
        double sum = 0.0;
        int count = 0;
        for (const auto& uid : uids) {
            if (uid.empty()) {
                continue;
            }
            sum += getSensorValue(uid);
            count++;
        }
        double mean = count > 0 ? sum / count : 0.0;
        logSerial("EVALUATE_RULES: AVG_MEAN uid=%s n=%d mean=%.2f", ruleUid.c_str(), count, mean);
        return mean;
    }

    static bool conditionFires(double value, RuleDirection direction, double target) {
        if (direction == RuleDirection::DOWN) {
            return value >= target;
        }
        if (direction == RuleDirection::UP) {
            return value <= target;
        }
        return false;
    }

    bool evaluateConditionSet(const Rule& rule) {
        if (!rule.conditions.empty()) {
            std::vector<bool> condFired;
            condFired.reserve(rule.conditions.size());
            for (const auto& cond : rule.conditions) {
                bool fires;
                if (cond.direction == RuleDirection::BETWEEN) {
                    double value = cond.sensorUids.empty() ? 0.0 : getSensorValue(cond.sensorUids[0]);
                    fires = !cond.sensorUids.empty() && Utils::isInsideArc(value, cond.valueLow, cond.valueHigh);
                } else {
                    double mean = computeMean(rule.uid, cond.sensorUids);
                    double target = (cond.valueTarget != 0.0)
                                    ? cond.valueTarget
                                    : (cond.direction == RuleDirection::DOWN ? cond.valueHigh : cond.valueLow);
                    fires = conditionFires(mean, cond.direction, target);
                }
                condFired.push_back(fires);
            }
            bool firing = combineConditionsDnf(rule.conditions, condFired, rule.conditionOr);
            logSerial("EVALUATE_RULES: STEP_COND uid=%s mode=%s ops=%s nConds=%d firing=%d",
                rule.uid.c_str(),
                conditionsHaveJoinOp(rule.conditions) ? "dnf" : "legacy",
                conditionsOpsString(rule.conditions).c_str(),
                static_cast<int>(rule.conditions.size()), firing ? 1 : 0);
            return firing;
        }

        if (rule.direction == RuleDirection::BETWEEN) {
            double value = getSensorValue(rule.sensorUid);
            bool firing = Utils::isInsideArc(value, rule.valueLow, rule.valueHigh);
            logSerial("EVALUATE_RULES: STEP_COND uid=%s mode=single firing=%d", rule.uid.c_str(), firing ? 1 : 0);
            return firing;
        }

        double value = getSensorValue(rule.sensorUid);
        bool firing = conditionFires(value, rule.direction, rule.valueTarget);
        logSerial("EVALUATE_RULES: STEP_COND uid=%s mode=single firing=%d", rule.uid.c_str(), firing ? 1 : 0);
        return firing;
    }

    SensorVerdict evaluateAverageSensorRule(const Rule& rule, bool committedOn, RuleEvaluationResult& result) {
        std::vector<std::string> uids = rule.conditions.empty()
            ? std::vector<std::string>()
            : rule.conditions[0].sensorUids;

        if (uids.empty()) {
            log("EVALUATE_RULES: AVG_EVAL inert uid=%s reason=no_sensors", rule.uid.c_str());
            return SensorVerdict::INERT;
        }

        double mean = computeMean(rule.uid, uids);
        result.sensorUid = uids[0];
        result.sensorValue = static_cast<float>(mean);
        result.sensorValueLow = rule.valueLow;
        result.sensorValueHigh = rule.valueHigh;

        SensorVerdict verdict = evaluateSensorHysteresis(rule.direction, mean, rule.valueLow, rule.valueHigh, committedOn);

        log("EVALUATE_RULES: AVG_EVAL uid=%s n=%d mean=%.2f dir=%s low=%.2f high=%.2f prevOn=%d",
            rule.uid.c_str(), static_cast<int>(uids.size()), mean, getRuleDirectionName(rule.direction),
            rule.valueLow, rule.valueHigh, committedOn ? 1 : 0);

        return verdict;
    }

    void evaluateLoopRule(const Rule& rule, RuleEvaluationResult& result) {
        Device* device = _deviceModel ? _deviceModel->getDeviceByUid(rule.deviceUid.c_str()) : nullptr;
        DeviceStateEnum deviceState = device ? device->state : DEVICE_STATE_OFF;
        bool running = isStepCycleState(deviceState);

        if (!running) {
            result.state = DEVICE_STATE_REQUEST_STEP_ON;
            result.isRuleTurnOnDevice = true;
            result.durationOn = static_cast<uint32_t>(rule.workingTime) * 1000;
            result.mixTime = rule.waitingTime * 1000;
            result.needToSwitchBack = true;
            result.cyclesToSetPoint = 0;
            result.currentCycle = 1;
            log("EVALUATE_RULES: LOOP_START uid=%s work=%d wait=%d",
                rule.uid.c_str(), rule.workingTime, rule.waitingTime);
            return;
        }

        if (device && device->deviceType != DeviceTypeEnum::DEVICE_TYPE_ANALOG) {
            static_cast<DeviceDigitalOut*>(device)->isRuleTurnOnDevice = true;
        }
        result.state = _ruleActiveStates[rule.uid].state;
        result.isRuleTurnOnDevice = true;
        log("EVALUATE_RULES: LOOP_RUN uid=%s", rule.uid.c_str());
    }

    void evaluateStepRule(const Rule& rule, RuleEvaluationResult& result) {
        Device* device = _deviceModel ? _deviceModel->getDeviceByUid(rule.deviceUid.c_str()) : nullptr;
        DeviceStateEnum deviceState = device ? device->state : DEVICE_STATE_OFF;
        DeviceDigitalOut* digitalDevice = nullptr;
        int currentCycle = 0;
        if (device && device->deviceType != DeviceTypeEnum::DEVICE_TYPE_ANALOG) {
            digitalDevice = static_cast<DeviceDigitalOut*>(device);
            currentCycle = digitalDevice->currentCycle;
        }

        bool running = isStepCycleState(deviceState);
        bool firing = evaluateConditionSet(rule);

        if (!running) {
            if (firing) {
                result.state = DEVICE_STATE_REQUEST_STEP_ON;
                result.isRuleTurnOnDevice = true;
                result.durationOn = static_cast<uint32_t>(rule.workingTime) * 1000;
                result.mixTime = rule.waitingTime * 1000;
                result.needToSwitchBack = true;
                result.cyclesToSetPoint = rule.cyclesToSetPoint;
                result.currentCycle = 1;
                result.sensorValueLow = rule.valueLow;
                result.sensorValueHigh = rule.valueHigh;
                if (!rule.sensorUid.empty()) {
                    result.sensorUid = rule.sensorUid;
                    result.sensorValue = static_cast<float>(getSensorValue(rule.sensorUid));
                }
                result.ruleSensorStartAt = sensorEventTimestamp();
                result.ruleSensorValueStart = result.sensorValue;
                log("EVALUATE_RULES: STEP_START uid=%s cyclesCap=%d", rule.uid.c_str(), rule.cyclesToSetPoint);
            } else {
                result.state = DEVICE_STATE_OFF;
                result.isRuleTurnOnDevice = false;
            }
            return;
        }

        result.state = _ruleActiveStates[rule.uid].state;
        result.currentCycle = currentCycle;
        result.cyclesToSetPoint = rule.cyclesToSetPoint;

        if (!firing) {
            if (digitalDevice) {
                digitalDevice->isRuleTurnOnDevice = false;
            }
            if (device) {
                device->ruleStopReason = "setpoint_reach";
            }
            result.isRuleTurnOnDevice = false;
            result.ruleStopReason = "setpoint_reach";
            log("EVALUATE_RULES: STEP_STOP uid=%s reason=setpoint_reach cycle=%d", rule.uid.c_str(), currentCycle);
        } else if (rule.cyclesToSetPoint > 0 && currentCycle >= rule.cyclesToSetPoint) {
            if (digitalDevice) {
                digitalDevice->isRuleTurnOnDevice = false;
            }
            if (device) {
                device->ruleStopReason = "time_reach";
            }
            result.isRuleTurnOnDevice = false;
            result.ruleStopReason = "time_reach";
            log("EVALUATE_RULES: STEP_STOP uid=%s reason=time_reach cycle=%d/%d",
                rule.uid.c_str(), currentCycle, rule.cyclesToSetPoint);
        } else {
            if (digitalDevice) {
                digitalDevice->isRuleTurnOnDevice = true;
            }
            result.isRuleTurnOnDevice = true;
            log("EVALUATE_RULES: STEP_RUN uid=%s cycle=%d/%d firing=1",
                rule.uid.c_str(), currentCycle, rule.cyclesToSetPoint);
        }
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
    
    void logSensorValue(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::RULE_SENSOR_VALUE_DEBUG)) {
            char buffer[256];
            int prefixLen = snprintf(buffer, sizeof(buffer), "%s: ", DebugFeatures::RULE_SENSOR_VALUE_DEBUG);
            va_list args;
            va_start(args, format);
            vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, format, args);
            va_end(args);

            IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::RULE_SENSOR_VALUE_DEBUG);
            if (feature) {
                _logger->println(DebugFeatures::RULE_SENSOR_VALUE_DEBUG, buffer, feature->getLogChannel());
            } else {
                _logger->println(DebugFeatures::RULE_SENSOR_VALUE_DEBUG, buffer);
            }
        }
    }

    double getSensorValue(const std::string& sensorUid) {
        double value = _sensorValueModel ? _sensorValueModel->getSensorValue(sensorUid) : 0.0;
        logSensorValue("read sensorUid=%s value=%.2f", sensorUid.c_str(), value);
        return value;
    }

    std::vector<std::string> collectSensorUids(const Rule& rule) {
        std::vector<std::string> uids;
        if (!rule.sensorUid.empty()) {
            uids.push_back(rule.sensorUid);
        }
        for (const auto& cond : rule.conditions) {
            for (const auto& s : cond.sensorUids) {
                if (!s.empty()) {
                    uids.push_back(s);
                }
            }
        }
        return uids;
    }

    FreshnessVerdict ruleSensorFreshness(const Rule& rule, unsigned long now) {
        std::vector<std::string> uids = collectSensorUids(rule);
        if (uids.empty()) {
            return FreshnessVerdict::FRESH;
        }

        bool anyNeverSeen = false;
        bool anyStale = false;
        for (const auto& uid : uids) {
            uint32_t last = _sensorValueModel ? _sensorValueModel->getLastReadTime(uid) : 0;
            if (last == 0) {
                anyNeverSeen = true;
                log("EVALUATE_RULES: FRESHNESS uid=%s sensor=%s last=0 age=NA now=%lu verdict=never_seen",
                    rule.uid.c_str(), uid.c_str(), now);
            } else {
                unsigned long age = (now >= (unsigned long)last) ? (now - (unsigned long)last) : 0UL;
                if (age > SENSOR_FRESHNESS_WINDOW_MS) {
                    anyStale = true;
                    log("EVALUATE_RULES: FRESHNESS uid=%s sensor=%s last=%u age=%lu now=%lu verdict=stale",
                        rule.uid.c_str(), uid.c_str(), last, age, now);
                }
            }
        }

        if (anyStale) {
            _sensorWaitSince.erase(rule.uid);
            return FreshnessVerdict::STALE;
        }

        if (anyNeverSeen) {
            auto it = _sensorWaitSince.find(rule.uid);
            if (it == _sensorWaitSince.end()) {
                _sensorWaitSince[rule.uid] = now;
                log("EVALUATE_RULES: SENSOR_WAIT uid=%s sensor=%s waitSince=%lu",
                    rule.uid.c_str(), uids[0].c_str(), now);
                return FreshnessVerdict::WAITING;
            }
            if ((unsigned long)(now - it->second) < SENSOR_FRESHNESS_WINDOW_MS) {
                return FreshnessVerdict::WAITING;
            }
            return FreshnessVerdict::STALE;
        }

        _sensorWaitSince.erase(rule.uid);
        return FreshnessVerdict::FRESH;
    }

    bool maybeProtectiveShutdown(const Rule& rule, unsigned long now) {
        if (!rule.protectiveShutdown) {
            return false;
        }
        if (rule.ruleType != RuleType::SENSOR && rule.ruleType != RuleType::AVERAGE_SENSOR) {
            return false;
        }
        if (rule.timeToSetPoint <= 0) {
            return false;
        }

        auto it = _ruleActiveStates.find(rule.uid);
        if (it == _ruleActiveStates.end() || !it->second.isDeviceOn()) {
            return false;
        }

        unsigned long onSince = it->second.timeOn;
        if (onSince == 0) {
            return false;
        }

        unsigned long elapsedSec = (now >= onSince) ? (now - onSince) / 1000UL : 0UL;
        if (elapsedSec <= (unsigned long)rule.timeToSetPoint) {
            return false;
        }

        triggerProtectiveShutdown(rule, elapsedSec);
        return true;
    }

    bool maybeStepProtectiveShutdown(const Rule& rule, unsigned long now) {
        if (!rule.protectiveShutdown) {
            return false;
        }
        if (rule.ruleType != RuleType::STEP) {
            return false;
        }
        if (rule.cyclesToSetPoint <= 0) {
            return false;
        }

        Device* device = _deviceModel ? _deviceModel->getDeviceByUid(rule.deviceUid.c_str()) : nullptr;
        if (!device || device->deviceType == DeviceTypeEnum::DEVICE_TYPE_ANALOG) {
            return false;
        }

        if (device->ruleUid && std::string(device->ruleUid).length() > 0 &&
            rule.uid != std::string(device->ruleUid)) {
            return false;
        }

        DeviceDigitalOut* digitalDevice = static_cast<DeviceDigitalOut*>(device);
        if (digitalDevice->currentCycle < rule.cyclesToSetPoint) {
            return false;
        }

        if (device->state == DEVICE_STATE_REQUEST_STEP_ON) {
            return false;
        }
        if (device->state == DEVICE_STATE_STEP_ON &&
            now >= device->timeOn &&
            (now - device->timeOn) < device->durationOn) {
            return false;
        }

        triggerProtectiveShutdown(rule, 0, "time_reach");
        return true;
    }

    void triggerProtectiveShutdown(const Rule& rule, unsigned long elapsedSec,
                                   const char* source = "time_to_setpoint") {
        Device* device = _deviceModel ? _deviceModel->getDeviceByUid(rule.deviceUid.c_str()) : nullptr;
        DeviceDigitalOut* digitalDevice = nullptr;
        int currentCycle = 0;
        if (device && device->deviceType != DeviceTypeEnum::DEVICE_TYPE_ANALOG) {
            digitalDevice = static_cast<DeviceDigitalOut*>(device);
            currentCycle = digitalDevice->currentCycle;
        }

        log("EVALUATE_RULES: PROTECTIVE_SHUTDOWN uid=%s deviceUid=%s source=%s cycles=%d/%d timeToSetPoint=%d elapsed=%lu",
            rule.uid.c_str(), rule.deviceUid.c_str(), source, currentCycle, rule.cyclesToSetPoint,
            rule.timeToSetPoint, elapsedSec);

        if (_ruleRepository) {
            _ruleRepository->updateRuleError(rule.uid.c_str(), RuleState::ERROR,
                                             "protective_shutdown", "protective_shutdown");
            _ruleRepository->updateRuleMode(rule.uid.c_str(),
                                            RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM,
                                            "protective_shutdown");
        }

        if (_ruleStateNotifier) {
            _ruleStateNotifier->notifyProtectiveShutdown(rule);
        }

        RuleEvaluationResult off(rule.uid, rule.ruleType, rule.version);
        off.workflowUid = rule.workflowUid;
        off.workflowVersion = rule.workflowVersion;
        off.state = DEVICE_STATE_OFF;
        off.isRuleTurnOnDevice = false;
        off.ruleStopReason = "protective_shutdown";
        off.sensorUid = rule.sensorUid;
        off.sensorValueLow = rule.valueLow;
        off.sensorValueHigh = rule.valueHigh;
        off.switchedAt = sensorEventTimestamp();
        executeDeviceAction(rule.deviceUid, off);

        if (digitalDevice) {
            digitalDevice->currentCycle = 0;
        }

        _ruleActiveStates.erase(rule.uid);
        _lastEvaluationResults.erase(rule.uid);
        _ruleLastEvaluated.erase(rule.uid);
        _delayTimers.erase(rule.uid);
    }

    void transitionToSensorUnreachable(const Rule& rule) {
        std::vector<std::string> uids = collectSensorUids(rule);
        std::string sensorUid = uids.empty() ? "" : uids[0];

        if (_ruleRepository) {
            _ruleRepository->updateRuleError(rule.uid.c_str(), RuleState::ERROR,
                                             "sensor_unreachable", rule.modeReason.c_str());
        }
        log("EVALUATE_RULES: SENSOR_STALE uid=%s sensor=%s", rule.uid.c_str(), sensorUid.c_str());
        if (_ruleStateNotifier) {
            _ruleStateNotifier->notifySensorUnreachable(rule, sensorUid);
        }

        auto activeIt = _ruleActiveStates.find(rule.uid);
        if (activeIt != _ruleActiveStates.end() && activeIt->second.isDeviceOn()) {
            RuleEvaluationResult off(rule.uid, rule.ruleType, rule.version);
            off.workflowUid = rule.workflowUid;
            off.workflowVersion = rule.workflowVersion;
            off.state = DEVICE_STATE_OFF;
            off.isRuleTurnOnDevice = false;
            off.ruleStopReason = "sensor_unreachable";
            off.sensorUid = rule.sensorUid;
            off.sensorValueLow = rule.valueLow;
            off.sensorValueHigh = rule.valueHigh;
            off.switchedAt = sensorEventTimestamp();
            executeDeviceAction(rule.deviceUid, off);

            log("EVALUATE_RULES: SENSOR_STALE off uid=%s deviceUid=%s reason=sensor_unreachable switchedAt=%s",
                rule.uid.c_str(), rule.deviceUid.c_str(), off.switchedAt.c_str());

            _ruleActiveStates.erase(rule.uid);
            _lastEvaluationResults.erase(rule.uid);
            _ruleLastEvaluated.erase(rule.uid);
        }

        _delayTimers.erase(rule.uid);
    }

    void recoverFromSensorUnreachable(const Rule& rule) {
        if (_ruleRepository) {
            _ruleRepository->updateRuleError(rule.uid.c_str(), RuleState::SYNCHRONIZED, "", "");
        }
        _sensorWaitSince.erase(rule.uid);
        log("EVALUATE_RULES: SENSOR_RECOVER uid=%s", rule.uid.c_str());
        if (_ruleStateNotifier) {
            _ruleStateNotifier->notifyRuleRecovered(rule);
        }
    }
};

#endif // EVALUATE_RULES_USE_CASE_H
