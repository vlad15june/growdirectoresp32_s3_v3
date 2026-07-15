#include <unity.h>
#include <vector>

#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"

static std::vector<RuleCondition> makeConditions(const std::vector<RuleJoinOp>& ops) {
    std::vector<RuleCondition> conditions;
    for (RuleJoinOp op : ops) {
        RuleCondition cond;
        cond.joinOp = op;
        conditions.push_back(cond);
    }
    return conditions;
}

static void test_dnf_all_and_equals_all(void) {
    auto conditions = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::AND, RuleJoinOp::AND});

    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, true, true}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {true, false, true}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {false, false, false}, false));
}

static void test_dnf_all_or_equals_any(void) {
    auto conditions = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::OR, RuleJoinOp::OR});

    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {false, false, true}, false));
    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, false, false}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {false, false, false}, false));
}

static void test_dnf_a_and_b_or_c(void) {
    auto conditions = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::AND, RuleJoinOp::OR});

    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, true, false}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {true, false, false}, false));
    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {false, false, true}, false));
    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, true, true}, false));
}

static void test_dnf_a_or_b_and_c(void) {
    auto conditions = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::OR, RuleJoinOp::AND});

    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, false, false}, false));
    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {false, true, true}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {false, true, false}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {false, false, true}, false));
}

static void test_dnf_three_groups(void) {
    auto conditions = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::AND, RuleJoinOp::OR,
                                      RuleJoinOp::OR, RuleJoinOp::AND});

    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, true, false, false, false}, false));
    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {false, false, true, false, false}, false));
    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {false, false, false, true, true}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {true, false, false, true, false}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {false, false, false, false, false}, false));
}

static void test_dnf_joinop_overrides_condition_or(void) {
    auto conditions = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::AND, RuleJoinOp::OR});

    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, true, false}, true));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {true, false, false}, true));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {true, false, false}, false));
}

static void test_dnf_missing_joinop_treated_as_and(void) {
    auto conditions = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::UNSET, RuleJoinOp::OR});

    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, true, false}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {true, false, false}, false));
    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {false, false, true}, false));
}

static void test_dnf_no_joinop_falls_back_to_condition_or(void) {
    auto conditions = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::UNSET, RuleJoinOp::UNSET});

    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {false, false, true}, true));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {false, false, true}, false));
    TEST_ASSERT_TRUE(combineConditionsDnf(conditions, {true, true, true}, false));
    TEST_ASSERT_FALSE(combineConditionsDnf(conditions, {true, true, false}, false));
}

static void test_dnf_order_dependence(void) {
    auto andThenOr = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::AND, RuleJoinOp::OR});
    auto orThenAnd = makeConditions({RuleJoinOp::UNSET, RuleJoinOp::OR, RuleJoinOp::AND});

    std::vector<bool> fired = {true, false, false};
    bool r1 = combineConditionsDnf(andThenOr, fired, false);
    bool r2 = combineConditionsDnf(orThenAnd, fired, false);
    TEST_ASSERT_FALSE(r1);
    TEST_ASSERT_TRUE(r2);
    TEST_ASSERT_NOT_EQUAL(r1, r2);
}

extern "C" void run_rule_dnf_combine_tests(void) {
    RUN_TEST(test_dnf_all_and_equals_all);
    RUN_TEST(test_dnf_all_or_equals_any);
    RUN_TEST(test_dnf_a_and_b_or_c);
    RUN_TEST(test_dnf_a_or_b_and_c);
    RUN_TEST(test_dnf_three_groups);
    RUN_TEST(test_dnf_joinop_overrides_condition_or);
    RUN_TEST(test_dnf_missing_joinop_treated_as_and);
    RUN_TEST(test_dnf_no_joinop_falls_back_to_condition_or);
    RUN_TEST(test_dnf_order_dependence);
}
