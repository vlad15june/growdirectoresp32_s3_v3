#ifndef MOCK_RULE_STATE_NOTIFIER_H
#define MOCK_RULE_STATE_NOTIFIER_H

#include "Core/Domain/Interfaces/Rules/IRuleStateNotifier.h"
#include <string>

class MockRuleStateNotifier : public IRuleStateNotifier {
public:
    bool sensorUnreachableCalled = false;
    bool ruleRecoveredCalled = false;
    bool protectiveShutdownCalled = false;
    int sensorUnreachableCount = 0;
    int ruleRecoveredCount = 0;
    int protectiveShutdownCount = 0;
    std::string lastUnreachableRuleUid;
    std::string lastUnreachableSensorUid;
    std::string lastRecoveredRuleUid;
    std::string lastProtectiveShutdownRuleUid;

    void notifySensorUnreachable(const Rule& rule, const std::string& sensorUid) override {
        sensorUnreachableCalled = true;
        sensorUnreachableCount++;
        lastUnreachableRuleUid = rule.uid;
        lastUnreachableSensorUid = sensorUid;
    }

    void notifyRuleRecovered(const Rule& rule) override {
        ruleRecoveredCalled = true;
        ruleRecoveredCount++;
        lastRecoveredRuleUid = rule.uid;
    }

    void notifyProtectiveShutdown(const Rule& rule) override {
        protectiveShutdownCalled = true;
        protectiveShutdownCount++;
        lastProtectiveShutdownRuleUid = rule.uid;
    }
};

#endif // MOCK_RULE_STATE_NOTIFIER_H
