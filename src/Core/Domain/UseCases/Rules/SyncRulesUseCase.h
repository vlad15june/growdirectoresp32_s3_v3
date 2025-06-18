#ifndef SYNC_RULES_USE_CASE_H
#define SYNC_RULES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Application/Services/Rules/RuleValidator.h"

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
    
    bool syncRulesFromServer();
    bool validateAndProcessRules(const std::vector<Rule>& rules, std::vector<Rule>& validRules);
    std::string generateRuleStateResponse(const std::vector<Rule>& rules, const std::vector<RuleValidationResult>& validationResults);

public:
    SyncRulesUseCase(
        IHybridAPIService* apiService,
        IRuleRepository* ruleRepository,
        IArduinoAdapter* arduinoAdapter,
        INTPTimeAdapter* ntpTimeAdapter,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr
    ) : _apiService(apiService),
        _ruleRepository(ruleRepository),
        _logger(logger),
        _debugSystem(debugSystem),
        _arduinoAdapter(arduinoAdapter),
        _ntpTimeAdapter(ntpTimeAdapter),
        _ruleValidator(logger, debugSystem),
        _lastSyncTime(0) {
        
        if (_arduinoAdapter) {
            _lastSyncTime = _arduinoAdapter->millis() - SYNC_INTERVAL_MS;
        }
    }
    
    ~SyncRulesUseCase() override = default;
    
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
    
    bool sendRuleStateUpdate(const std::string& ruleUid, RuleState state, const std::string& errorType) override {
        log("SYNC_RULES: Sending rule state update for %s - state: %d", ruleUid.c_str(), static_cast<int>(state));
        
        if (!_apiService) {
            log("SYNC_RULES: API service not available for state update");
            return false;
        }
        
        // Create JSON message for rule state
        std::string stateStr;
        switch (state) {
            case RuleState::SYNCHRONIZED: stateStr = "Synchronized"; break;
            case RuleState::ERROR: stateStr = "Error"; break;
            case RuleState::DELETING: stateStr = "Deleting"; break;
            default: stateStr = "Unsynchronized"; break;
        }
        
        char jsonMsg[512];
        unsigned long currentTime = _ntpTimeAdapter ? _ntpTimeAdapter->getEpochTime() : 0;
        
        if (errorType.empty()) {
            snprintf(jsonMsg, sizeof(jsonMsg),
                "{\"rules\":[{\"uid\":\"%s\",\"state\":\"%s\"}],\"timestamp\":%lu}",
                ruleUid.c_str(), stateStr.c_str(), currentTime);
        } else {
            snprintf(jsonMsg, sizeof(jsonMsg),
                "{\"rules\":[{\"uid\":\"%s\",\"state\":\"%s\",\"errorType\":\"%s\"}],\"timestamp\":%lu}",
                ruleUid.c_str(), stateStr.c_str(), errorType.c_str(), currentTime);
        }
        
        return _apiService->sendByUidRuleStateCommand(ruleUid.c_str(), jsonMsg);
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
            log("SYNC_RULES: No rules received from server");
            return true; // Empty response is valid
        }
        
        log("SYNC_RULES: Received %d rules from server", static_cast<int>(serverRules.size()));
        
        // Validate rules
        std::vector<RuleValidationResult> validationResults = _ruleValidator.validateRules(serverRules);
        
        // Filter valid rules
        std::vector<Rule> validRules;
        validRules.reserve(serverRules.size());
        
        for (size_t i = 0; i < serverRules.size(); ++i) {
            const auto& rule = serverRules[i];
            const auto& result = validationResults[i];
            
            if (result.isValid || rule.state == RuleState::DELETING) {
                validRules.push_back(rule);
            } else {
                log("SYNC_RULES: Rule %s validation failed: %s", rule.uid.c_str(), result.errorMessage.c_str());
                // Send error state back to server
                sendRuleStateUpdate(rule.uid, RuleState::ERROR, result.errorType);
            }
        }
        
        // Synchronize with repository
        int syncedCount = _ruleRepository->synchronizeRules(validRules);
        log("SYNC_RULES: Synchronized %d rules successfully", syncedCount);
        
        // Generate and send rule state response
        std::string stateResponse = generateRuleStateResponse(serverRules, validationResults);
        if (!stateResponse.empty()) {
            _apiService->sendRuleStateCommand(stateResponse.c_str());
        }
        
        return true;
    }
    
    bool validateAndProcessRules(const std::vector<Rule>& rules, std::vector<Rule>& validRules) {
        log("SYNC_RULES: Validating %d rules", static_cast<int>(rules.size()));
        
        std::vector<RuleValidationResult> validationResults = _ruleValidator.validateRules(rules);
        
        validRules.clear();
        validRules.reserve(rules.size());
        
        for (size_t i = 0; i < rules.size(); ++i) {
            const auto& rule = rules[i];
            const auto& result = validationResults[i];
            
            if (result.isValid || rule.state == RuleState::DELETING) {
                validRules.push_back(rule);
            } else {
                log("SYNC_RULES: Rule %s validation failed: %s", rule.uid.c_str(), result.errorMessage.c_str());
                // Send error state back to server
                sendRuleStateUpdate(rule.uid, RuleState::ERROR, result.errorType);
            }
        }
        
        // Generate and send rule state response
        std::string stateResponse = generateRuleStateResponse(rules, validationResults);
        if (!stateResponse.empty()) {
            _apiService->sendRuleStateCommand(stateResponse.c_str());
        }
        
        log("SYNC_RULES: Validated %d/%d rules successfully", 
            static_cast<int>(validRules.size()), static_cast<int>(rules.size()));
        
        return true;
    }
    
    std::string generateRuleStateResponse(const std::vector<Rule>& rules, const std::vector<RuleValidationResult>& validationResults) {
        if (rules.empty()) {
            return "";
        }
        
        std::string response = "{\"rules\":[";
        bool first = true;
        
        for (size_t i = 0; i < rules.size(); ++i) {
            const auto& rule = rules[i];
            const auto& result = validationResults[i];
            
            if (!first) {
                response += ",";
            }
            first = false;
            
            response += "{\"uid\":\"" + rule.uid + "\"";
            
            if (result.isValid || rule.state == RuleState::DELETING) {
                response += ",\"state\":\"Synchronized\"";
            } else {
                response += ",\"state\":\"Error\"";
                if (!result.errorType.empty()) {
                    response += ",\"errorType\":\"" + result.errorType + "\"";
                }
            }
            
            response += "}";
        }
        
        unsigned long currentTime = _ntpTimeAdapter ? _ntpTimeAdapter->getEpochTime() : 0;
        response += "],\"timestamp\":" + std::to_string(currentTime) + "}";
        return response;
    }
};

#endif // SYNC_RULES_USE_CASE_H
