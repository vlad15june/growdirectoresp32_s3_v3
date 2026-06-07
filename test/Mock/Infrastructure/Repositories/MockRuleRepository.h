#ifndef MOCK_RULE_REPOSITORY_H
#define MOCK_RULE_REPOSITORY_H

#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include <set>

class MockRuleRepository : public IRuleRepository {
public:
    std::vector<Rule> rules;

    int synchronizeReturnValue = 0;
    int synchronizeCallCount = 0;
    std::vector<Rule> lastSynchronizeRules;
    bool lastIsFullSnapshot = false;
    std::set<std::string> lastSnapshotUids;

    bool removeReturnValue = true;
    std::vector<std::string> removedUids;

    Utils::Optional<Rule> get(const char* uid) override {
        for (const auto& rule : rules) {
            if (rule.uid == uid) {
                return Utils::Optional<Rule>(rule);
            }
        }
        return Utils::Optional<Rule>();
    }

    std::vector<Rule> getAll() override { return rules; }
    bool save(const Rule&) override { return true; }
    int saveAll(const std::vector<Rule>&) override { return 0; }
    bool remove(const char* uid) override { removedUids.push_back(uid ? uid : ""); return removeReturnValue; }
    int removeAll() override { return 0; }
    int count() override { return static_cast<int>(rules.size()); }
    bool exists(const char*) override { return false; }
    bool clear() override { return true; }

    std::vector<Rule> getRulesByType(RuleType) override { return {}; }
    std::vector<Rule> getRulesByDeviceUid(const char*) override { return {}; }
    std::vector<Rule> getRulesBySensorUid(const char*) override { return {}; }
    std::vector<Rule> getActiveRules() override { return rules; }
    std::vector<Rule> getActiveRulesByType(RuleType) override { return {}; }
    std::vector<Rule> getRulesNeedingSynchronization() override { return {}; }
    bool updateRuleState(const char*, RuleState) override { return true; }
    bool updateRuleError(const char*, RuleState, const char*, const char*) override { return true; }
    bool updateRuleMode(const char*, RuleMode, const char*) override { return true; }
    int synchronizeRules(const std::vector<Rule>& r, bool isFullSnapshot = false, const std::set<std::string>& snapshotUids = {}) override {
        synchronizeCallCount++;
        lastSynchronizeRules = r;
        lastIsFullSnapshot = isFullSnapshot;
        lastSnapshotUids = snapshotUids;
        return synchronizeReturnValue;
    }
};

#endif // MOCK_RULE_REPOSITORY_H
