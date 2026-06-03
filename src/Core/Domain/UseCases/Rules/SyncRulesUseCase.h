#ifndef SYNC_RULES_USE_CASE_H
#define SYNC_RULES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Application/Services/Rules/RuleValidator.h"
#include "Application/DTOs/RuleStateUpdateDto.h"

/**
 * Use case for synchronizing rules with the server
 */
class SyncRulesUseCase : public ISyncRulesUseCase {
private:
    IHybridAPIService* _apiService;
    IRuleRepository* _ruleRepository;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IArduinoAdapter* _arduinoAdapter;
    INTPTimeAdapter* _ntpTimeAdapter;
    IJsonConverter* _jsonConverter;
    IRemoteSensorReceiver* _remoteSensorReceiver = nullptr;
    RuleValidator _ruleValidator;

    // Sync timing
    const int SYNC_INTERVAL_MS = 30000; // 30 seconds
    unsigned long _lastSyncTime;
    bool _lastSyncStatus = false;
    time_t _lastSuccessfulSyncTimestamp = 0;
    bool _forceSyncRequested = false;
    
    void log(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::RULE_SYNC_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            _logger->println(DebugFeatures::RULE_SYNC_DEBUG, buffer);
        }
    }
    
    // Enhanced sync methods
    bool processMqttRuleUpdates(const std::vector<Rule>& mqttRules);
    bool validateAndStoreRules(const std::vector<Rule>& rules);
    
    // Rule comparison and merging
    bool shouldUpdateRule(const Rule& existingRule, const Rule& newRule);
    Rule mergeRuleUpdates(const Rule& existingRule, const Rule& newRule);

public:
    SyncRulesUseCase(
        IHybridAPIService* apiService,
        IRuleRepository* ruleRepository,
        IArduinoAdapter* arduinoAdapter,
        INTPTimeAdapter* ntpTimeAdapter,
        IJsonConverter* jsonConverter = nullptr,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr
    ) : _apiService(apiService),
        _ruleRepository(ruleRepository),
        _logger(logger),
        _debugSystem(debugSystem),
        _arduinoAdapter(arduinoAdapter),
        _ntpTimeAdapter(ntpTimeAdapter),
        _jsonConverter(jsonConverter),
        _ruleValidator(logger, debugSystem),
        _lastSyncTime(0) {
        
        if (_arduinoAdapter) {
            _lastSyncTime = _arduinoAdapter->millis() - SYNC_INTERVAL_MS;
        }
    }
    
    ~SyncRulesUseCase() override = default;

    void setRemoteSensorReceiver(IRemoteSensorReceiver* remoteSensorReceiver) {
        _remoteSensorReceiver = remoteSensorReceiver;
    }

    bool init() override {
        log("SYNC_RULES: Initializing rule sync use case");
        
        if (!_apiService) {
            log("SYNC_RULES: API service not available");
            return false;
        }
        
        if (!_ruleRepository) {
            log("SYNC_RULES: Rule repository not available");
            return false;
        }
        
        if (!_arduinoAdapter) {
            log("SYNC_RULES: Arduino adapter not available");
            return false;
        }
        
        if (!_ntpTimeAdapter) {
            log("SYNC_RULES: NTP time adapter not available");
            return false;
        }
        
        log("SYNC_RULES: Rule sync use case initialized successfully");
        return true;
    }
    
    void loop() override {
        if (!_arduinoAdapter) {
            return;
        }
        
        unsigned long now = _arduinoAdapter->millis();
        unsigned long elapsed = now - _lastSyncTime;
        
        bool shouldSync = _forceSyncRequested || (elapsed >= SYNC_INTERVAL_MS);
        
        if (!shouldSync) {
            return;
        }
        
        _forceSyncRequested = false;
        _lastSyncTime = now;
        
        log("SYNC_RULES: Starting rule synchronization");
        
        bool syncSuccess = syncRulesFromServer();
        _lastSyncStatus = syncSuccess;
        
        if (syncSuccess) {
            if (_ntpTimeAdapter) {
                _lastSuccessfulSyncTimestamp = _ntpTimeAdapter->getEpochTime();
            }
            log("SYNC_RULES: Rule synchronization completed successfully");
        } else {
            log("SYNC_RULES: Rule synchronization failed");
        }
    }
    
    bool forceSync() override {
        log("SYNC_RULES: Force sync requested");
        _forceSyncRequested = true;
        return true;
    }

    bool getLastSyncStatus() const override {
        return _lastSyncStatus;
    }
    
    time_t getLastSyncTime() const override {
        return _lastSuccessfulSyncTimestamp;
    }

private:
    bool syncRulesFromServer() {
        if (!_apiService || !_ruleRepository) {
            return false;
        }
        
        log("SYNC_RULES: Fetching rules from server");
        std::vector<Rule> serverRules = _apiService->getRuleList();
        
        if (serverRules.empty()) {
            log("SYNC_RULES: No rules received from server, checking local storage");
            return handleEmptyServerResponse();
        }
        
        log("SYNC_RULES: Received %d rules from server", static_cast<int>(serverRules.size()));
        return processServerRules(serverRules);
    }
    
    bool processServerRules(const std::vector<Rule>& serverRules) {
        // Validate incoming rules
        std::vector<RuleValidationResult> validationResults = _ruleValidator.validateRules(serverRules);
        
        // Separate valid and invalid rules
        std::vector<Rule> validRules;
        
        for (size_t i = 0; i < serverRules.size(); ++i) {
            const auto& rule = serverRules[i];
            const auto& result = validationResults[i];
            
            if (result.isValid || rule.state == RuleState::DELETING) {
                validRules.push_back(rule);
            } else {
                log("SYNC_RULES: Rule %s validation failed: %s", rule.uid.c_str(), result.errorMessage.c_str());
            }
        }
        
        // Store valid rules in repository
        bool syncSuccess = false;
        if (!validRules.empty()) {
            int syncedCount = _ruleRepository->synchronizeRules(validRules);
            syncSuccess = (syncedCount > 0);
            log("SYNC_RULES: Synchronized %d/%d rules successfully", syncedCount, static_cast<int>(validRules.size()));
            if (_remoteSensorReceiver) {
                _remoteSensorReceiver->onRuleSetChanged();
            }
        }
        
        // Generate and send rule state update for ALL rules (valid and invalid)
        RuleStateUpdateDto stateUpdate = generateRuleStateUpdate(serverRules, validationResults);
        if (stateUpdate.hasRules()) {
            sendRuleStateUpdate(stateUpdate);
        }
        
        trackSyncAttempt(syncSuccess);
        return syncSuccess;
    }
        
    RuleStateUpdateDto generateRuleStateUpdate(const std::vector<Rule>& rules, const std::vector<RuleValidationResult>& validationResults) {
        RuleStateUpdateDto stateUpdate;
        
        if (_ntpTimeAdapter) {
            // Convert epoch time to ISO string format
            time_t currentTime = _ntpTimeAdapter->getEpochTime();
            struct tm* timeinfo = gmtime(&currentTime);
            char timestamp[32];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", timeinfo);
            stateUpdate.timestamp = timestamp;
        }
        
        bool hasErrors = false;
        for (const auto& result : validationResults) {
            if (!result.isValid) {
                hasErrors = true;
                break;
            }
        }
        
        stateUpdate.status = hasErrors ? "Error" : "Success";
        
        for (size_t i = 0; i < rules.size(); ++i) {
            const auto& rule = rules[i];
            const auto& result = validationResults[i];
            
            std::string ruleState = result.isValid ? "Synchronized" : "Error";
            std::string ruleMode = rule.mode == RuleMode::ON ? "On" : 
                                  rule.mode == RuleMode::OFF ? "Off" : "WaitingForUserConfirm";
            
            stateUpdate.addRuleState(rule.uid, ruleState, ruleMode, rule.modeReason, 
                                   result.isValid ? "" : result.errorType);
        }
        
        return stateUpdate;
    }
    
    bool sendRuleStateUpdate(const RuleStateUpdateDto& stateUpdate) {
        if (!_apiService || !stateUpdate.hasRules()) {
            return false;
        }
        
        if (!_jsonConverter) {
            log("SYNC_RULES: JSON converter not available for rule state update");
            return false;
        }
        
        std::string jsonMsg = _jsonConverter->ruleStateUpdateToJson(stateUpdate);
        
        log("SYNC_RULES: Sending rule state update: %s", jsonMsg.c_str());
        return _apiService->sendRuleStateCommand(jsonMsg.c_str());
    }
    
    bool handleEmptyServerResponse() {
        std::vector<Rule> localRules = _ruleRepository->getAll();
        
        if (!localRules.empty()) {
            log("SYNC_RULES: Using %d rules from local storage", static_cast<int>(localRules.size()));
            return true;
        }
        
        log("SYNC_RULES: No rules available locally or from server");
        return true;
    }
    
    bool handleSyncFailure() {
        log("SYNC_RULES: Sync failure, implementing recovery strategy");
        
        // Check if we've been failing for too long (5 minutes)
        static unsigned long lastSuccessfulSync = 0;
        unsigned long now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        
        if (_lastSyncStatus && _lastSuccessfulSyncTimestamp > 0) {
            lastSuccessfulSync = now;
        }
        
        if (now - lastSuccessfulSync > 300000) { // 5 minutes
            log("SYNC_RULES: Failed to sync for 5 minutes, requesting restart");
            // This would trigger a system restart if configured
            return false;
        }
        
        return true;
    }
    
    void trackSyncAttempt(bool success) {
        if (success) {
            if (_ntpTimeAdapter) {
                _lastSuccessfulSyncTimestamp = _ntpTimeAdapter->getEpochTime();
            }
        }
        _lastSyncStatus = success;
    }
};

#endif // SYNC_RULES_USE_CASE_H
