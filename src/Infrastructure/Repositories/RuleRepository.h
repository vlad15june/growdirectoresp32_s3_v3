#pragma once

#include "Repository.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsRuleDao.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>

/**
 * @brief Implementation of rule repository using SPIFFS storage
 */
class RuleRepository : public Repository<Rule>, public IRuleRepository {

public:
    /**
     * @brief How many rules this module keeps, at most.
     *
     * The registry's half of RuleLimits::MAX_RULES_PER_MODULE, where the number and the reasoning
     * behind it live: the same limit bounds what the transport builds out of a server's answer, so
     * a rule that reaches the merge is a rule there is room for.
     *
     * What it costs here is one runtime slot per rule, reserved with the repository, plus the rule
     * set itself when the module is actually holding that many.
     */
    static const size_t MAX_RULES = RuleLimits::MAX_RULES_PER_MODULE;

private:
    ISpiffsRuleDao* ruleDao;
    INTPTimeAdapter* _ntpTimeAdapter;

    /** @brief The part of a rule that says how the rule is doing right now. */
    struct RuleRuntimeState {
        RuleState state;
        std::string errorType;
        std::string modeReason;

        RuleRuntimeState() : state(RuleState::UNSYNCHRONIZED) {}

        bool sameAs(const RuleRuntimeState& other) const {
            return state == other.state && errorType == other.errorType &&
                   modeReason == other.modeReason;
        }
    };

    /** @brief One rule that is not doing what the stored copy of it says; empty uid = free. */
    struct RuleStateOverlaySlot {
        std::string uid;
        RuleRuntimeState runtimeState;
    };

    // The rules whose runtime state differs from the stored one. state/errorType/modeReason
    // describe how a rule is doing at this moment, not what the rule is: the evaluation tick
    // moves them whenever the sensors a rule watches go stale and again when they answer, and the
    // next tick after a reboot establishes them again from the same sensor readings. Keeping them
    // here is what stops a rule whose sensor comes and goes from rewriting the whole rule file,
    // and dropping the cache with it, on every transition.
    //
    // A fixed table, not a growing one: the slots are reserved with the repository, so a
    // transition allocates no node and the table cannot grow with a rule set that arrives from
    // the server. (The strings inside a slot do allocate - a uid is 36 characters and misses the
    // small-string buffer.) Only rules that disagree with their stored copy take a slot, so the
    // table is empty on a settled module; a slot is freed when the rule is saved, removed or
    // comes back to what the file says.
    //
    // One slot per rule the registry will hold - MAX_RULES - so every rule can differ from its
    // stored copy at once: a whole bus going quiet puts every sensor-bearing rule in error in the
    // same tick, and that must cost no writes either.
    static const size_t STATE_OVERLAY_SLOTS = MAX_RULES;
    RuleStateOverlaySlot stateOverlay[STATE_OVERLAY_SLOTS];
    uint32_t stateOverlayOverflows = 0;

    // Rules the registry had no room for. A refusal costs the extra rule and nothing else - the
    // rules the module already runs stay - and it is never silent: it is logged here and reported
    // to the server, which reads the registry when it builds the acknowledgement.
    uint32_t ruleOverflows = 0;

    // Counts the runtime transitions recorded above. Unlike the stored entities, they move
    // nothing in the base repository - no write, no cache invalidation - so they need their own
    // term in rulesRevision(): a reader that keeps a snapshot of the rule set has to re-take it
    // when a rule changes state, and the snapshot lives outside this class where it cannot be
    // corrected in place.
    uint32_t stateOverlayRevision = 0;

    /** @brief Runtime part of a rule as it is stored. */
    static RuleRuntimeState runtimeStateOf(const Rule& rule) {
        RuleRuntimeState runtimeState;
        runtimeState.state = rule.state;
        runtimeState.errorType = rule.errorType;
        runtimeState.modeReason = rule.modeReason;
        return runtimeState;
    }

    /** @brief The slot holding the runtime state of a rule, or nullptr when it has none. */
    RuleStateOverlaySlot* slotFor(const std::string& uid) {
        for (size_t i = 0; i < STATE_OVERLAY_SLOTS; i++) {
            if (!stateOverlay[i].uid.empty() && stateOverlay[i].uid == uid) {
                return &stateOverlay[i];
            }
        }
        return nullptr;
    }

    const RuleStateOverlaySlot* slotFor(const std::string& uid) const {
        return const_cast<RuleRepository*>(this)->slotFor(uid);
    }

    /**
     * @brief Runtime part of a stored rule.
     *
     * Answers by value: the caller goes on to change the overlay, and a reference into the
     * repository cache would not be guaranteed to survive that.
     *
     * @return false when no rule with this uid is stored
     */
    bool storedRuntimeState(const char* uid, RuleRuntimeState& storedState) {
        const std::unordered_map<std::string, Rule>& rules = loadedEntities();
        auto it = rules.find(uid);
        if (it == rules.end()) {
            return false;
        }
        storedState = runtimeStateOf(it->second);
        return true;
    }

    /** @brief The runtime state the module currently holds for a stored rule. */
    RuleRuntimeState liveRuntimeState(const std::string& uid,
                                      const RuleRuntimeState& storedState) const {
        const RuleStateOverlaySlot* slot = slotFor(uid);
        return slot ? slot->runtimeState : storedState;
    }

    /** @brief Put the live runtime state on a rule leaving the repository. */
    void applyStateOverlay(Rule& rule) const {
        const RuleStateOverlaySlot* slot = slotFor(rule.uid);
        if (slot) {
            rule.state = slot->runtimeState.state;
            rule.errorType = slot->runtimeState.errorType;
            rule.modeReason = slot->runtimeState.modeReason;
        }
    }

    void applyStateOverlay(std::vector<Rule>& rules) const {
        for (auto& rule : rules) {
            applyStateOverlay(rule);
        }
    }

    static void freeSlot(RuleStateOverlaySlot& slot) {
        slot.uid.clear();
        slot.runtimeState = RuleRuntimeState();
    }

    /**
     * @brief Record a runtime state without touching storage.
     * @param uid Rule the state belongs to
     * @param storedState The runtime state as the stored rule carries it
     * @param liveState The runtime state the module now holds
     * @return false when every slot is taken by another rule
     */
    bool setStateOverlay(const std::string& uid, const RuleRuntimeState& storedState,
                         const RuleRuntimeState& liveState) {
        RuleStateOverlaySlot* slot = slotFor(uid);
        if (slot) {
            if (liveState.sameAs(storedState)) {
                freeSlot(*slot);
            } else {
                slot->runtimeState = liveState;
            }
            ++stateOverlayRevision;
            return true;
        }

        for (int attempt = 0; attempt < 2; attempt++) {
            for (size_t i = 0; i < STATE_OVERLAY_SLOTS; i++) {
                if (stateOverlay[i].uid.empty()) {
                    stateOverlay[i].uid = uid;
                    stateOverlay[i].runtimeState = liveState;
                    ++stateOverlayRevision;
                    return true;
                }
            }
            // Every slot is taken. One of them may name a rule the file no longer has, which is
            // what a rule set replaced behind the repository's back leaves behind.
            if (attempt == 0) {
                pruneStateOverlay();
            }
        }

        ++stateOverlayOverflows;
        log("RULE_STATE_OVERLAY full uid=%s stored=%u", uid.c_str(), stateOverlayOverflows);
        return false;
    }

    /** @brief Forget the runtime state of a rule whose stored copy has just been written. */
    void dropStateOverlay(const std::string& uid) {
        RuleStateOverlaySlot* slot = slotFor(uid);
        if (slot) {
            freeSlot(*slot);
        }
    }

    void clearStateOverlay() {
        for (size_t i = 0; i < STATE_OVERLAY_SLOTS; i++) {
            freeSlot(stateOverlay[i]);
        }
    }

    /**
     * @brief Free the slots that no longer name a stored rule.
     *
     * Every removal made through this repository frees its slot directly, so this has something
     * to do only when the rule file changed behind the repository's back (a write straight into
     * the DAO, then refreshCache()). It frees the slots whose uid the file no longer has, and
     * says nothing about the slots whose uid is still there.
     */
    void pruneStateOverlay() {
        const std::unordered_map<std::string, Rule>& rules = loadedEntities();
        for (size_t i = 0; i < STATE_OVERLAY_SLOTS; i++) {
            if (!stateOverlay[i].uid.empty() && rules.find(stateOverlay[i].uid) == rules.end()) {
                freeSlot(stateOverlay[i]);
            }
        }
    }

    /**
     * @brief Store a runtime state, the way every one of them was stored before the table.
     *
     * The last resort for a transition with no slot. The table has a slot for every rule the
     * stored format can hold, so on a device this is unreachable; it exists because that ceiling
     * comes from a document size rather than from a declared limit, and a transition must never
     * be dropped on the floor if the ceiling ever moves. It is not free - the file has no
     * spelling for RuleState::ERROR, so a rule that goes this way stops matching either branch of
     * the evaluation tick until the next server sync, which is what every rule did before this
     * table existed.
     */
    bool storeRuntimeState(const char* uid, const RuleRuntimeState& runtimeState) {
        Rule rule;
        if (!ruleDao->getRule(uid, rule)) {
            return false;
        }
        rule.state = runtimeState.state;
        rule.errorType = runtimeState.errorType;
        rule.modeReason = runtimeState.modeReason;
        return save(rule);
    }

    void log(const char* format, ...) const {
        if (!logger || !debugSystem || !debugSystem->isFeatureEnabled(DebugFeatures::RULE_REPOSITORY_DEBUG)) {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        IDebugFeature* feature = debugSystem->getFeature(DebugFeatures::RULE_REPOSITORY_DEBUG);
        if (feature) {
            logger->println(DebugFeatures::RULE_REPOSITORY_DEBUG, buffer, feature->getLogChannel());
        } else {
            logger->println(DebugFeatures::RULE_REPOSITORY_DEBUG, buffer);
        }
    }

public:
    ~RuleRepository() noexcept override = default;

    /**
     * @brief Constructor for RuleRepository
     * @param dao Data access object for Rule entities
     * @param debugSystem Debug system for logging (optional)
     * @param logger Logger for output (optional)
     * @param clock What a failed read is retried by (optional; see FAILED_READ_RETRY_MS)
     */
    RuleRepository(ISpiffsRuleDao* dao, INTPTimeAdapter* ntpTimeAdapter = nullptr, IDebugSystem *debugSystem = nullptr, ILogger *logger = nullptr,
                   IArduinoAdapter* clock = nullptr)
        : Repository<Rule>(dao, debugSystem, logger, clock), ruleDao(dao), _ntpTimeAdapter(ntpTimeAdapter) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
    }

    // IRepository<Rule> implementations 
    Utils::Optional<Rule> get(const char* uid) override {
        Utils::Optional<Rule> rule = Repository<Rule>::get(uid);
        if (rule.has_value()) {
            applyStateOverlay(rule.value());
        }
        return rule;
    }

    std::vector<Rule> getAll() override {
        std::vector<Rule> rules = Repository<Rule>::getAll();
        applyStateOverlay(rules);
        return rules;
    }

    // A whole rule is being stored, runtime state included: that state is what the rule is now
    // known to be, so the runtime one has nothing left to say.
    bool save(const Rule& entity) override {
        bool result = Repository<Rule>::save(entity);
        if (result) {
            dropStateOverlay(entity.uid);
        }
        return result;
    }

    int saveAll(const std::vector<Rule>& entities) override {
        int saved = Repository<Rule>::saveAll(entities);
        if (saved > 0) {
            // saveAll replaces the rule set: every uid in it was just written, and every uid that
            // is not in it no longer exists.
            clearStateOverlay();
        }
        return saved;
    }

    bool remove(const char* uid) override {
        bool result = Repository<Rule>::remove(uid);
        if (result && uid) {
            dropStateOverlay(uid);
        }
        return result;
    }

    int removeAll() override {
        int removed = Repository<Rule>::removeAll();
        clearStateOverlay();
        return removed;
    }

    int count() override {
        return Repository<Rule>::count();
    }

    /** @brief Rules the registry has refused for lack of room since the module started. */
    uint32_t refusedRuleCount() const { return ruleOverflows; }

    bool exists(const char* uid) override {
        return Repository<Rule>::exists(uid);
    }

    bool clear() override {
        bool result = Repository<Rule>::clear();
        if (result) {
            clearStateOverlay();
        }
        return result;
    }

    /**
     * @brief Rule-set revision, backed by the base repository's cache generation
     *
     * The generation is bumped inside invalidateCache(), the single funnel every write path
     * of Repository<Rule> goes through - save, saveAll, remove, removeAll, clear,
     * refreshCache - and the mode updater and synchronizeRules below all mutate through those
     * methods. Deriving the revision from it therefore needs no per-mutator wiring that could
     * be forgotten.
     *
     * Runtime state transitions are the one change that does not go through a write path: they
     * are recorded in stateOverlay, and their own counter is added here. Both terms only ever
     * grow, so their sum changes on every change of either.
     *
     * Shifted by one: the interface reserves 0 for "no revision available - treat every call
     * as a change", while the generation itself is 0 for as long as nothing was written
     * through the repository - a registry read from storage at boot stays at generation 0
     * until the first write, possibly for the module's whole uptime when it runs offline.
     * Answering the reserved 0 for that entire time would make the caller re-copy the
     * registry on every evaluation tick, which is precisely the churn the revision exists to
     * remove. The shift keeps every distinct generation distinct. (At the 2^32 wrap-around
     * one generation maps onto the reserved 0; the caller then re-reads until the next write
     * - a needless re-read, never staleness.)
     */
    uint32_t rulesRevision() override {
        return Repository<Rule>::generation() + stateOverlayRevision + 1;
    }

    /**
     * @brief Get rules for a specific device
     * @param deviceUid Device unique identifier
     * @return Vector of rules associated with the device
     */
    std::vector<Rule> getRulesByDeviceUid(const char* deviceUid) override {
        if (!deviceUid) return {};

        std::vector<Rule> rules;
        ruleDao->getRulesByDeviceUid(deviceUid, rules);
        applyStateOverlay(rules);
        return rules;
    }

    /**
     * @brief Get rules of a specific type
     * @param type Rule type to filter by
     * @return Vector of rules of the specified type
     */
    std::vector<Rule> getRulesByType(RuleType type) override {
        std::vector<Rule> rules;
        ruleDao->getRulesByType(type, rules);
        applyStateOverlay(rules);
        return rules;
    }

    /**
     * @brief Get rules by sensor UID
     * @param sensorUid Sensor unique identifier
     * @return Vector of rules associated with the sensor
     */
    std::vector<Rule> getRulesBySensorUid(const char* sensorUid) override {
        if (!sensorUid) return {};

        // Get all rules and filter by sensor UID
        std::vector<Rule> allRules = getAll();
        std::vector<Rule> sensorRules;

        for (const auto& rule : allRules) {
            if (rule.sensorUid == sensorUid) {
                sensorRules.push_back(rule);
            }
        }

        return sensorRules;
    }

    /**
     * @brief Get active rules by type
     * @param type Rule type to filter by
     * @return Vector of active rules of the specified type
     */
    std::vector<Rule> getActiveRulesByType(RuleType type) override {
        std::vector<Rule> activeRules = getActiveRules();
        std::vector<Rule> typedRules;

        for (const auto& rule : activeRules) {
            if (rule.ruleType == type) {
                typedRules.push_back(rule);
            }
        }

        return typedRules;
    }

    /**
     * @brief Get rules that need synchronization
     * @return Vector of rules that are not synchronized
     */
    std::vector<Rule> getRulesNeedingSynchronization() override {
        // Get all rules first
        std::vector<Rule> allRules = getAll();
        std::vector<Rule> unsyncedRules;
        
        // Filter rules that need synchronization
        for (const auto& rule : allRules) {
            if (rule.state != RuleState::SYNCHRONIZED) {
                unsyncedRules.push_back(rule);
            }
        }
        
        return unsyncedRules;
    }

    /**
     * @brief Update the state of a rule
     *
     * The state is how the rule is doing, not what it is: it is recorded in RAM, because
     * storing it would serialize and rewrite the whole rule file, and drop the cache the
     * evaluation tick reads, for a value the next tick establishes again anyway. Readers get it
     * through the same methods as before.
     *
     * @param uid Rule unique identifier
     * @param state New state for the rule
     * @return true if successful, false otherwise
     */
    bool updateRuleState(const char* uid, RuleState state) override {
        if (!uid) return false;

        RuleRuntimeState storedState;
        if (!storedRuntimeState(uid, storedState)) {
            return false;
        }

        RuleRuntimeState nextState = liveRuntimeState(uid, storedState);
        if (nextState.state == state) {
            return true;
        }
        nextState.state = state;

        if (setStateOverlay(uid, storedState, nextState)) {
            return true;
        }
        return storeRuntimeState(uid, nextState);
    }

    bool updateRuleError(const char* uid, RuleState state, const char* errorType, const char* modeReason) override {
        if (!uid) return false;

        RuleRuntimeState storedState;
        if (!storedRuntimeState(uid, storedState)) {
            return false;
        }

        RuleRuntimeState nextState;
        nextState.state = state;
        nextState.errorType = errorType ? errorType : "";
        nextState.modeReason = modeReason ? modeReason : "";

        if (nextState.sameAs(liveRuntimeState(uid, storedState))) {
            return true;
        }

        if (setStateOverlay(uid, storedState, nextState)) {
            return true;
        }
        return storeRuntimeState(uid, nextState);
    }

    /**
     * @brief Update the mode of a rule
     *
     * The mode is what the rule was told to do - by the user through the server, or by a
     * protective shutdown that hands the rule back to the user - and it belongs in storage: a
     * rule held for confirmation has to stay held across a reboot. The rule is written whole,
     * with the runtime state the module currently holds, which is what the file carried before
     * that state could stay behind in RAM.
     *
     * @param uid Rule unique identifier
     * @param mode New mode for the rule
     * @param reason Reason for the mode change (optional)
     * @return true if successful, false otherwise
     */
    bool updateRuleMode(const char* uid, RuleMode mode, const char* reason = nullptr) override {
        if (!uid) return false;

        Rule rule;
        if (!ruleDao->getRule(uid, rule)) {
            return false;
        }

        applyStateOverlay(rule);
        rule.mode = mode;

        if (reason) {
            rule.modeReason = reason;
        } else {
            rule.modeReason = "";
        }

        return save(rule);
    }

    /**
     * @brief Synchronize rules with cloud/server
     * @param rules Vector of rules to sync
     * @return Number of successfully synchronized rules
     */
    int synchronizeRules(const std::vector<Rule>& rules, bool isFullSnapshot = false, const std::set<std::string>& snapshotUids = {}) override {
        if (rules.empty()) return 0;

        // The merge starts from the rules this repository holds and the file is written from it, so
        // it is not run on a cache a failed read left: every stored rule the incoming set does not
        // name would be dropped from the file, and a rule the server deleted would leave without
        // going through DELETING, where the evaluation tick switches off a device it had on.
        // Refused, the synchronization stores nothing and reports nothing as taken; the next one
        // tries again.
        if (!cacheMayBeWritten()) {
            log("RULE_SYNC refused: the stored rules did not come back whole");
            return 0;
        }

        // The rules as they are stored, without the runtime state the module holds on top of them.
        // The merge decides what a rule *is* - its configuration - and how a rule is *doing* is
        // not in a server snapshot at all: it is established by the evaluation tick out of the
        // sensors, it lives in the runtime table, and nothing below reads it or writes it. That is
        // what keeps a routine re-sync from moving a rule whose sensor is gone out of the state
        // the tick can recover it from.
        std::vector<Rule> existingRules = Repository<Rule>::getAll();

        // Create a map of existing rules by UID for quick lookup
        std::unordered_map<std::string, Rule*> existingRuleMap;
        for (auto& rule : existingRules) {
            existingRuleMap[rule.uid] = &rule;
        }

        int syncCount = 0;
        int storedCount = static_cast<int>(existingRules.size());
        std::vector<Rule> updatedRules;

        // Per entry of updatedRules: true when it is the module's own stored rule, written back
        // unchanged. The runtime state the module holds describes that rule and stays; every other
        // entry is the server's copy replacing what was there, and the runtime state of what it
        // replaced goes with it - the same rule as an explicit save().
        std::vector<bool> keptFromStorage;

        // Process incoming rules
        for (const auto& incomingRule : rules) {
            // Skip rules without UIDs
            if (incomingRule.uid.empty()) {
                continue;
            }

            auto it = existingRuleMap.find(incomingRule.uid);

            // The registry is full and this rule is not one of the rules in it. What is refused is
            // the rule that does not fit, never the rules the module is already running: those go
            // through the merge below as they always do, edits included. The refusal is counted
            // here and reaches the server through the acknowledgement, which is built by reading
            // this registry - a rule that is not in it is not reported as running.
            if (it == existingRuleMap.end() && storedCount >= static_cast<int>(MAX_RULES)) {
                ++ruleOverflows;
                log("RULE_REGISTRY full uid=%s stored=%d refused=%u", incomingRule.uid.c_str(),
                    storedCount, ruleOverflows);
                continue;
            }

            if (incomingRule.state == RuleState::DELETING) {
                Rule updatedRule = incomingRule;
                updatedRule.state = RuleState::DELETING;
                updatedRules.push_back(updatedRule);
                keptFromStorage.push_back(false);
                if (it == existingRuleMap.end()) {
                    storedCount++;
                } else {
                    existingRuleMap.erase(it);
                }
                syncCount++;
                continue;
            }

            if (it != existingRuleMap.end()) {
                // Rule exists - check if we need to update it
                Rule* existingRule = it->second;

                log("RULE_MERGE uid=%s existState=%d errType=%s existVer=%d inVer=%d inMode=%d existMode=%d",
                        existingRule->uid.c_str(), static_cast<int>(existingRule->state),
                        existingRule->errorType.c_str(), existingRule->version, incomingRule.version,
                        static_cast<int>(incomingRule.mode), static_cast<int>(existingRule->mode));

                // The stored rule is configuration, and the stored format has no spelling for
                // RuleState::ERROR - the converter writes it as "Unsynchronized" - so this branch
                // is not reached from storage. It is left as it stands: what it guards against, a
                // re-sync declaring a rule healthy while its sensor is missing, cannot happen here
                // either, because the state the sensors decide is not in this merge at all.
                if (existingRule->state == RuleState::ERROR &&
                    existingRule->errorType == "sensor_unreachable" &&
                    incomingRule.version <= existingRule->version) {
                    log("RULE_MERGE uid=%s -> PRESERVE_ERR", existingRule->uid.c_str());
                    updatedRules.push_back(*existingRule);
                    keptFromStorage.push_back(true);
                    existingRuleMap.erase(it);
                    syncCount++;
                    continue;
                }

                // Only update if version is newer or state needs synchronization
                if (incomingRule.version > existingRule->version ||
                    existingRule->state == RuleState::UNSYNCHRONIZED) {

                    log("RULE_MERGE uid=%s -> FORCE_SYNC_VER_OR_UNSYNC", existingRule->uid.c_str());

                    Rule updatedRule = incomingRule;
                    updatedRule.state = RuleState::SYNCHRONIZED;
                    updatedRules.push_back(updatedRule);
                    keptFromStorage.push_back(false);
                    existingRuleMap.erase(it);
                    syncCount++;
                } else if (incomingRule.mode != existingRule->mode) {
                    log("RULE_MERGE uid=%s -> FORCE_SYNC_MODE", existingRule->uid.c_str());
                    Rule updatedRule = incomingRule;
                    updatedRule.state = RuleState::SYNCHRONIZED;
                    updatedRules.push_back(updatedRule);
                    keptFromStorage.push_back(false);
                    existingRuleMap.erase(it);
                    syncCount++;
                } else {
                    log("RULE_MERGE uid=%s -> KEEP_EXISTING", existingRule->uid.c_str());
                    updatedRules.push_back(*existingRule);
                    keptFromStorage.push_back(true);
                    existingRuleMap.erase(it);
                    syncCount++;
                }
            } else {
                // New rule
                Rule newRule = incomingRule;
                newRule.state = RuleState::SYNCHRONIZED;
                updatedRules.push_back(newRule);
                keptFromStorage.push_back(false);
                storedCount++;
                syncCount++;
            }
        }
        
        for (auto& pair : existingRuleMap) {
            if (isFullSnapshot && snapshotUids.find(pair.first) == snapshotUids.end()) {
                Rule deletingRule = *pair.second;
                deletingRule.state = RuleState::DELETING;
                updatedRules.push_back(deletingRule);
                keptFromStorage.push_back(false);
            } else {
                updatedRules.push_back(*pair.second);
                keptFromStorage.push_back(true);
            }
        }

        // Save all rules. Through the base repository, not this class's saveAll: that one releases
        // the whole runtime table, and here only the rules the merge actually replaced have lost
        // the state the table holds for them. A rule the server deleted is one of those - its
        // entry is now DELETING and must be read as DELETING, or the purge never happens - while a
        // rule written back exactly as it was is not: its sensor is still missing, and the state
        // saying so is what the next tick recovers from.
        if (Repository<Rule>::saveAll(updatedRules) == static_cast<int>(updatedRules.size())) {
            for (size_t i = 0; i < updatedRules.size(); i++) {
                if (!keptFromStorage[i]) {
                    dropStateOverlay(updatedRules[i].uid);
                }
            }
            return syncCount;
        }

        return 0;
    }

    /**
     * @brief Get all active rules that are in ON mode
     *
     * Rules being deleted and rules held by a protective shutdown are left out. The two calendar
     * arrays are combined with OR, the same way EvaluateRulesUseCase::isRuleActiveNow combines
     * them: the rule is kept when today matches daysOfWeek or daysOfMonth, and is dropped only
     * when neither matches. An empty array carries no calendar restriction of its own. daysOfWeek
     * holds tm_wday values, where 0 is Sunday; daysOfMonth holds tm_mday values.
     *
     * @return Vector of active rules
     */
    std::vector<Rule> getActiveRules() override {
        // Get all rules first
        std::vector<Rule> allRules = getAll();
        std::vector<Rule> activeRules;
        
        // Current time information
        struct tm timeinfo = {0};
        if (_ntpTimeAdapter) {
            // Use NTP time adapter to get current time
            time_t now = _ntpTimeAdapter->getEpochTime();
            localtime_r(&now, &timeinfo);
        } else {
            // Fallback to system time if NTP adapter not available
            time_t now;
            time(&now);
            localtime_r(&now, &timeinfo);
        }
        
        // Filter rules that are active
        for (const auto& rule : allRules) {
            bool isRuleOn = false;
            
            bool inProtectiveShutdown =
                rule.mode == RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM ||
                (rule.state == RuleState::ERROR && rule.errorType == "protective_shutdown");

            if (rule.state == RuleState::DELETING || inProtectiveShutdown) {
                continue;
            }
            
            if (rule.mode == RuleMode::ON) {
                if (!rule.daysOfWeek.empty() || !rule.daysOfMonth.empty()) {
                    int today = timeinfo.tm_wday;
                    int dayOfMonth = timeinfo.tm_mday;

                    bool dayMatches =
                        std::find(rule.daysOfWeek.begin(), rule.daysOfWeek.end(), today) != rule.daysOfWeek.end();
                    if (!dayMatches) {
                        dayMatches =
                            std::find(rule.daysOfMonth.begin(), rule.daysOfMonth.end(), dayOfMonth) != rule.daysOfMonth.end();
                    }
                    if (!dayMatches) {
                        continue;
                    }
                }

                isRuleOn = true;
            }
            
            // If the rule is active, add it to our result
            if (isRuleOn) {
                activeRules.push_back(rule);
            }
        }
        
        return activeRules;
    }
};
