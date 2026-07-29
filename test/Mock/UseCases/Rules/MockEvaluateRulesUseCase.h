#ifndef MOCK_EVALUATE_RULES_USE_CASE_H
#define MOCK_EVALUATE_RULES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/Rules/IEvaluateRulesUseCase.h"

class MockEvaluateRulesUseCase : public IEvaluateRulesUseCase {
private:
    bool _willTurnOnWithinMinutes = false;
    int _lastMinutes = 0;

public:
    bool init() override {
        return true;
    }

    void loop() override {}

    RuleEvaluationResult evaluateRule(const Rule& rule) override {
        (void)rule;
        return RuleEvaluationResult();
    }

    bool executeRuleActions(const Rule& rule) override {
        (void)rule;
        return true;
    }

    bool isRuleActive(const std::string& ruleUid) const override {
        (void)ruleUid;
        return false;
    }

    RuleEvaluationResult getLastEvaluationResult(const std::string& ruleUid) const override {
        (void)ruleUid;
        return RuleEvaluationResult();
    }

    bool willAnyRuleTurnDeviceOnWithinMinutes(int minutes) override {
        _lastMinutes = minutes;
        return _willTurnOnWithinMinutes;
    }

    void setWillTurnOnWithinMinutes(bool value) {
        _willTurnOnWithinMinutes = value;
    }

    int getLastMinutes() const {
        return _lastMinutes;
    }

    void reset() {
        _willTurnOnWithinMinutes = false;
        _lastMinutes = 0;
    }
};

#endif // MOCK_EVALUATE_RULES_USE_CASE_H
