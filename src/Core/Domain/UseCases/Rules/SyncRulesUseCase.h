#ifndef SYNC_RULES_USE_CASE_H
#define SYNC_RULES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Application/Services/Rules/RuleValidator.h"
#include "Application/DTOs/RuleStateUpdateDto.h"
#include <set>
#include <cstdarg>
#include <cstdio>

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
    ISettings* _settings = nullptr;
    IOutbox* _outbox = nullptr;
    RuleValidator _ruleValidator;

    // The rule-list fetch this use case is waiting for. kRejected means "none in flight", which
    // is also what stops loop() asking again every 10 ms while the first ask is still travelling.
    uint32_t _requestTicket = IOutbox::kRejected;

    // Sync timing
    const int SYNC_INTERVAL_MS = 300000; // 5 minutes
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
            IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::RULE_SYNC_DEBUG);
            if (feature) {
                _logger->println(DebugFeatures::RULE_SYNC_DEBUG, buffer, feature->getLogChannel());
            } else {
                _logger->println(DebugFeatures::RULE_SYNC_DEBUG, buffer);
            }
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

    void setSettings(ISettings* settings) {
        _settings = settings;
    }

    /**
     * Fetch rules through the outbox instead of blocking the control loop on a GET.
     *
     * Rule sync is not a send, so it does not go into an outbox of outgoing messages - it goes
     * into the same queue of *requests*, and the answer comes back on the same completion queue,
     * with the rules left in the outbox's response slot. Without an outbox this use case keeps
     * its previous synchronous behaviour.
     */
    void setOutbox(IOutbox* outbox) {
        _outbox = outbox;
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

        if (_outbox != nullptr) {
            if (_requestTicket != IOutbox::kRejected) {
                // A fetch is already travelling. Asking again would only collapse into the same
                // slot in the outbox, but it would also reset the interval, so the module would
                // keep re-arming a request it is already waiting for.
                return;
            }

            const uint32_t ticket = _outbox->submitRuleListRequest();
            if (ticket == IOutbox::kRejected) {
                // Nothing was consumed - not the force flag, not the interval - so the next
                // iteration tries again.
                return;
            }

            _forceSyncRequested = false;
            _lastSyncTime = now;
            _requestTicket = ticket;
            log("SYNC_RULES: Rule list requested through the outbox, ticket %u", (unsigned)ticket);
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

    void onOutboxCompletion(const OutboxCompletion& completion) override {
        if (completion.kind != OutboxKind::RULE_LIST || _outbox == nullptr) {
            return;
        }

        if (_requestTicket == IOutbox::kRejected || completion.ticket != _requestTicket) {
            return;
        }

        _requestTicket = IOutbox::kRejected;

        if (!completion.delivered) {
            // Nothing is applied. This is the guard that matters: without it a failed fetch
            // would be recorded as a successful synchronization against a response the module
            // never understood. Measured: an *empty* list does not
            // delete anything - it goes to handleEmptyServerResponse() and the local rules stay.
            // A DELETING sweep needs a short but valid snapshot (RuleRepository::synchronizeRules()), which
            // is why the list is never shortened either.
            // "delivered" here means the server answered 200 and the body parsed - see
            // RestAPIService::getRuleList(), which reports httpFailed for both a bad status and a
            // body this module could not read.
            _lastSyncStatus = false;
            log("SYNC_RULES: Rule list fetch failed after %u attempts - keeping the rules the "
                "module already has", (unsigned)completion.attempts);
            return;
        }

        std::vector<Rule> serverRules;
        if (!_outbox->takeRuleListResponse(serverRules)) {
            _lastSyncStatus = false;
            log("SYNC_RULES: Rule list reported delivered but the response slot was empty");
            return;
        }

        bool syncSuccess;
        if (serverRules.empty()) {
            log("SYNC_RULES: No rules received from server, checking local storage");
            syncSuccess = handleEmptyServerResponse();
        } else {
            log("SYNC_RULES: Received %d rules from server, answer read whole: %d",
                static_cast<int>(serverRules.size()), completion.responseReadWhole ? 1 : 0);
            syncSuccess = processServerRules(serverRules, completion.responseReadWhole);
        }

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
        bool readWhole = false;
        std::vector<Rule> serverRules = _apiService->getRuleList(nullptr, &readWhole);

        if (serverRules.empty()) {
            log("SYNC_RULES: No rules received from server, checking local storage");
            return handleEmptyServerResponse();
        }

        log("SYNC_RULES: Received %d rules from server, answer read whole: %d",
            static_cast<int>(serverRules.size()), readWhole ? 1 : 0);
        return processServerRules(serverRules, readWhole);
    }
    
    /**
     * @param answerReadWhole false when the answer carried rules the module did not take. Such an
     *        answer is a set of rules to apply, never the set of rules the account has: the merge
     *        retires what an authoritative snapshot does not name, and a rule left out because
     *        this module could not read it, could not fit it or would not keep it is not a rule
     *        the server deleted. Deletion does not depend on this - the server sends it as a rule
     *        in state DELETING, which travels as an element like any other.
     */
    bool processServerRules(const std::vector<Rule>& serverRules, bool answerReadWhole) {
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
        
        std::set<std::string> snapshotUids;
        for (const auto& rule : serverRules) {
            if (!rule.uid.empty()) {
                snapshotUids.insert(rule.uid);
            }
        }
        if (answerReadWhole) {
            for (const auto& localRule : _ruleRepository->getAll()) {
                if (!localRule.uid.empty() && snapshotUids.find(localRule.uid) == snapshotUids.end()) {
                    log("SYNC_RULES: RULE_SNAPSHOT absent uid=%s -> DELETING", localRule.uid.c_str());
                }
            }
        } else {
            log("SYNC_RULES: RULE_SNAPSHOT incomplete - applying %d rules and retiring none",
                static_cast<int>(validRules.size()));
        }

        // Store valid rules in repository.
        //
        // A snapshot the module has nothing to store from is a completed exchange, not a failed
        // one: the server answered, the module read the answer, and every rule of it is reported
        // below with the reason it was not taken. What makes a synchronization fail is the module
        // ending up with a rule set that is not the server's, which here is a refused write.
        bool syncSuccess = true;
        if (!validRules.empty()) {
            int syncedCount =
                _ruleRepository->synchronizeRules(validRules, answerReadWhole, snapshotUids);
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

        if (_settings) {
            char accountUidBuffer[256];
            accountUidBuffer[0] = '\0';
            _settings->getAccountUid(accountUidBuffer);
            stateUpdate.accountUid = accountUidBuffer;
        }
        
        bool hasErrors = false;

        for (size_t i = 0; i < rules.size(); ++i) {
            const auto& rule = rules[i];
            const auto& result = validationResults[i];

            // The rule as the module holds it at this moment - storage plus the runtime state kept
            // in RAM - and not as the snapshot described it. The merge has already run, so this is
            // what the module will act on until the next one, and it is what the server has to be
            // told: the two differ exactly where it matters. A rule whose sensor stopped answering
            // is in error here and "Synchronized" in the snapshot; a rule the registry had no room
            // for is not here at all.
            Utils::Optional<Rule> held = _ruleRepository ? _ruleRepository->get(rule.uid.c_str())
                                                         : Utils::nullopt<Rule>();

            std::string ruleState = "Synchronized";
            std::string errorType;
            RuleMode mode = rule.mode;
            std::string modeReason = rule.modeReason;

            if (!result.isValid) {
                // A rule this module cannot read is reported on what the validator said about it;
                // whatever the module holds under that uid is an older rule, not this one.
                ruleState = "Error";
                errorType = result.errorType;
                hasErrors = true;
            } else if (!held.has_value()) {
                // Valid, and the module is not running it: the registry had no room. Reported with
                // the error type the backend uses for a rule a module did not take, so that it is
                // not acknowledged as if it were running.
                ruleState = "Error";
                errorType = "sync_failed";
                hasErrors = true;
            } else {
                mode = held.value().mode;
                modeReason = held.value().modeReason;
                if (held.value().state == RuleState::ERROR) {
                    ruleState = "Error";
                    errorType = held.value().errorType;
                }
            }

            std::string ruleMode = mode == RuleMode::ON ? "On" :
                                  mode == RuleMode::OFF ? "Off" : "WaitingForUserConfirm";

            // The version stays the one the server sent. It is what the backend matches the
            // acknowledgement against - a report naming any other version is discarded there - and
            // after the merge it is the version the module holds anyway.
            stateUpdate.addRuleState(rule.uid, ruleState, ruleMode, modeReason, errorType,
                                     rule.version);

            log("SYNC_RULES: RULE_ACK item uid=%s version=%d state=%s workflowUid=%s workflowVersion=%d",
                rule.uid.c_str(), rule.version, ruleState.c_str(), rule.workflowUid.c_str(), rule.workflowVersion);
        }

        stateUpdate.status = hasErrors ? "Error" : "Success";

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

        if (_outbox != nullptr) {
            // An event, not state: this acknowledges a specific snapshot the server sent, and a
            // later one does not make it redundant. It is refused rather than dropped if the
            // outbox has no room, and the refusal is visible here as a false return.
            return _outbox->submitRuleStateBulk(jsonMsg) != IOutbox::kRejected;
        }

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
