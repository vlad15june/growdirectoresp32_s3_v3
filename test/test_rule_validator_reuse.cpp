// The rule validators as long-lived, stateless strategies.
//
// RuleValidator keeps one validator instance per rule type for its whole life instead of
// building one on the heap per call: validation runs for every rule on every evaluation tick.
// That trade is only sound while the validators stay stateless - every call answered from the
// rule alone, exactly as a freshly constructed validator would answer. These tests pin both
// sides of the trade: interleaved verdicts do not contaminate each other in either direction,
// and repeated validation calls take nothing from the heap.

#include <unity.h>

#include <cstdio>

#include "Application/Services/Rules/RuleValidator.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Sim/HostAllocationCounter.h"

namespace {

Rule validScheduleRule() {
    Rule rule;
    rule.uid = "rule-schedule-valid-aaaaaaaa-bbbb-00001";
    rule.deviceUid = "device-schedule-aaaaaaaa-bbbb-cccc-001";
    rule.ruleType = RuleType::SCHEDULE;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.schedule.push_back(ScheduleItem(1609459200, 3600));
    return rule;
}

Rule invalidScheduleRule() {
    Rule rule = validScheduleRule();
    rule.uid = "rule-schedule-broken-aaaaaaaa-bbbb-0002";
    rule.schedule.clear();  // a schedule rule without schedule items is malformed
    return rule;
}

}  // namespace

void test_a_shared_validator_answers_every_call_as_if_freshly_constructed(void) {
    RuleValidator validator;
    Rule valid = validScheduleRule();
    Rule invalid = invalidScheduleRule();

    // Interleave both directions repeatedly: a validator that keeps any residue of a previous
    // verdict fails on one of these steps.
    for (int round = 0; round < 3; ++round) {
        RuleValidationResult badResult = validator.validateRule(invalid);
        TEST_ASSERT_FALSE_MESSAGE(badResult.isValid,
            "a schedule rule without items must stay invalid on every repeated call");
        TEST_ASSERT_EQUAL_STRING("wrong_format", badResult.errorType.c_str());

        RuleValidationResult goodResult = validator.validateRule(valid);
        TEST_ASSERT_TRUE_MESSAGE(goodResult.isValid,
            "a valid rule must stay valid right after an invalid one was rejected");
        TEST_ASSERT_EQUAL_STRING("", goodResult.errorType.c_str());
    }
}

void test_execution_validation_of_a_rule_takes_nothing_from_the_heap(void) {
    RuleValidator validator;
    Rule rule = validScheduleRule();

    // Warm-up: whatever one call lazily builds, it may build once.
    for (int i = 0; i < 3; ++i) {
        validator.validateRuleExecution(rule);
    }

    // Tracing prints nothing while the loop stays allocation-free; a regression prints the
    // first backtraces and a closing count of the rest.
    HostAllocationCounter::reset();
    HostAllocationCounter::setTrace(true);
    for (int i = 0; i < 100; ++i) {
        RuleValidationResult result = validator.validateRuleExecution(rule);
        TEST_ASSERT_TRUE(result.isValid);
    }
    HostAllocationCounter::setTrace(false);
    unsigned long long allocations = HostAllocationCounter::newCount();

    char message[128];
    snprintf(message, sizeof(message),
             "100 execution validations made %llu heap allocations, expected 0", allocations);
    TEST_ASSERT_EQUAL_MESSAGE(0, static_cast<unsigned>(allocations), message);
}

extern "C" void run_rule_validator_reuse_tests(void) {
    RUN_TEST(test_a_shared_validator_answers_every_call_as_if_freshly_constructed);
    RUN_TEST(test_execution_validation_of_a_rule_takes_nothing_from_the_heap);
}
