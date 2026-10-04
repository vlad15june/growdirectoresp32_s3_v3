#pragma once

#include <string>
#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Interfaces/Rules/IRuleStateNotifier.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Application/DTOs/RuleStateUpdateDto.h"
#include "Application/DTOs/RuleSensorOfflineEventDto.h"
#include <cstdarg>
#include <cstdio>

class RuleStateReportingService : public IRuleStateNotifier {
private:
    IHybridAPIService* _hybridApiService;
    IJsonConverter* _jsonConverter;
    ISettings* _settings;
    IModuleConfig* _moduleConfig;
    INTPTimeAdapter* _ntpTimeAdapter;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IOutbox* _outbox = nullptr;

    // Every report this service makes is an event: a rule changed state, and a later report about
    // a different rule does not make this one redundant. So it is submitted with the never-drop
    // policy and refused rather than collapsed if the outbox has no room.
    //
    // This service is driven from EvaluateRulesUseCase, inside the control loop. Before the
    // outbox each of these three notifications was a REST POST with a 30 s timeout made from that
    // loop, which is why the loop could stall on a rule state change even with every periodic
    // send moved elsewhere.
    //
    // What this service does NOT yet do is consume its completions. A submission refused because
    // the ring is full is logged and dropped here, and an event that exhausted its four attempts
    // is never heard about at all - onOutboxCompletion() is not implemented. So the second half
    // of the never-drop promise ("the producer keeps its state and retries") is not kept by this
    // producer. That is the
    // piece that finishes the policy, and it needs somewhere to keep the pending report.
    bool submitOrSend(OutboxKind kind, const std::string& uid, const std::string& json) {
        if (_outbox != nullptr) {
            if (kind == OutboxKind::RULE_SENSOR_OFFLINE) {
                return _outbox->submitRuleSensorOffline(json) != IOutbox::kRejected;
            }
            return _outbox->submitRuleStateByUid(uid, json) != IOutbox::kRejected;
        }

        if (kind == OutboxKind::RULE_SENSOR_OFFLINE) {
            return _hybridApiService->sendRuleSensorOfflineEvent(json.c_str());
        }
        return _hybridApiService->sendByUidRuleStateCommand(uid.c_str(), json.c_str());
    }

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

    const char* ruleModeWire(RuleMode mode) {
        return mode == RuleMode::ON ? "On" : (mode == RuleMode::OFF ? "Off" : "WaitingForUserConfirm");
    }

    std::string reportTimestamp() {
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

    std::string accountUid() {
        if (!_settings) {
            return "";
        }
        char buffer[256];
        buffer[0] = '\0';
        _settings->getAccountUid(buffer);
        return std::string(buffer);
    }

    void sendPerUidReport(const Rule& rule, const char* state, const char* errorType) {
        if (!_hybridApiService || !_jsonConverter) {
            return;
        }
        RuleStateDto dto(rule.uid, state, ruleModeWire(rule.mode), rule.modeReason, errorType, rule.version);
        dto.accountUid = accountUid();
        std::string json = _jsonConverter->ruleStateUpdateSingleToJson(dto, reportTimestamp());
        submitOrSend(OutboxKind::RULE_STATE_BY_UID, rule.uid, json);
    }

public:
    RuleStateReportingService(
        IHybridAPIService* hybridApiService,
        IJsonConverter* jsonConverter,
        ISettings* settings,
        IModuleConfig* moduleConfig,
        INTPTimeAdapter* ntpTimeAdapter,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr
    ) : _hybridApiService(hybridApiService),
        _jsonConverter(jsonConverter),
        _settings(settings),
        _moduleConfig(moduleConfig),
        _ntpTimeAdapter(ntpTimeAdapter),
        _logger(logger),
        _debugSystem(debugSystem) {}

    /** @brief Report rule state changes through the outbox instead of from the control loop. */
    void setOutbox(IOutbox* outbox) { _outbox = outbox; }

    void notifySensorUnreachable(const Rule& rule, const std::string& sensorUid) override {
        log("EVALUATE_RULES: SENSOR_UNREACHABLE_REPORT uid=%s errorType=sensor_unreachable", rule.uid.c_str());
        sendPerUidReport(rule, "Error", "sensor_unreachable");

        if (!_hybridApiService || !_jsonConverter) {
            return;
        }
        std::string moduleUid = _moduleConfig ? _moduleConfig->getModuleUid() : "";
        char accountUidBuffer[256];
        accountUidBuffer[0] = '\0';
        if (_settings) {
            _settings->getAccountUid(accountUidBuffer);
        }
        RuleSensorOfflineEventDto dto(moduleUid, accountUidBuffer, sensorUid, rule.uid, rule.deviceUid);
        std::string json = _jsonConverter->ruleSensorOfflineEventToJson(dto);
        bool ok = submitOrSend(OutboxKind::RULE_SENSOR_OFFLINE, rule.uid, json);
        log("EVALUATE_RULES: SENSOR_OFFLINE_EVENT uid=%s sensor=%s device=%s ok=%d",
            rule.uid.c_str(), sensorUid.c_str(), rule.deviceUid.c_str(), ok ? 1 : 0);
        if (!ok) {
            log("EVALUATE_RULES: SENSOR_OFFLINE_EVENT_fail uid=%s sensor=%s",
                rule.uid.c_str(), sensorUid.c_str());
        }
    }

    void notifyRuleRecovered(const Rule& rule) override {
        log("EVALUATE_RULES: SENSOR_RECOVER_REPORT uid=%s", rule.uid.c_str());
        sendPerUidReport(rule, "Synchronized", "");
    }

    void notifyProtectiveShutdown(const Rule& rule) override {
        log("EVALUATE_RULES: PROTECTIVE_SHUTDOWN_REPORT uid=%s errorType=protective_shutdown", rule.uid.c_str());
        if (!_hybridApiService || !_jsonConverter) {
            return;
        }
        RuleStateDto dto(rule.uid, "Error", "WaitingForUserConfirm", "protective_shutdown",
                         "protective_shutdown", rule.version);
        dto.accountUid = accountUid();
        std::string json = _jsonConverter->ruleStateUpdateSingleToJson(dto, reportTimestamp());
        submitOrSend(OutboxKind::RULE_STATE_BY_UID, rule.uid, json);
    }
};
