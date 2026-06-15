#pragma once

#include <string>
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

    void sendPerUidReport(const Rule& rule, const char* state, const char* errorType) {
        if (!_hybridApiService || !_jsonConverter) {
            return;
        }
        RuleStateDto dto(rule.uid, state, ruleModeWire(rule.mode), rule.modeReason, errorType, rule.version);
        std::string json = _jsonConverter->ruleStateUpdateSingleToJson(dto, reportTimestamp());
        _hybridApiService->sendByUidRuleStateCommand(rule.uid.c_str(), json.c_str());
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
        bool ok = _hybridApiService->sendRuleSensorOfflineEvent(json.c_str());
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
};
