#include <unity.h>

#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Core/Utils/WindArc.h"

static int verdict(RuleDirection dir, double value, double low, double high, bool prevOn) {
    return static_cast<int>(evaluateSensorHysteresis(dir, value, low, high, prevOn));
}

static const int V_ON = static_cast<int>(SensorVerdict::TURN_ON);
static const int V_OFF = static_cast<int>(SensorVerdict::TURN_OFF);

static void test_isInsideArc_normal_arc(void) {
    TEST_ASSERT_TRUE(Utils::isInsideArc(45, 22.5, 67.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(10, 22.5, 67.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(90, 22.5, 67.5));
    TEST_ASSERT_TRUE(Utils::isInsideArc(22.5, 22.5, 67.5));
    TEST_ASSERT_TRUE(Utils::isInsideArc(67.5, 22.5, 67.5));
}

static void test_isInsideArc_wrap_arc_north(void) {
    TEST_ASSERT_TRUE(Utils::isInsideArc(350, 292.5, 67.5));
    TEST_ASSERT_TRUE(Utils::isInsideArc(0, 292.5, 67.5));
    TEST_ASSERT_TRUE(Utils::isInsideArc(45, 292.5, 67.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(90, 292.5, 67.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(200, 292.5, 67.5));
}

static void test_isInsideArc_single_rhumb_north(void) {
    TEST_ASSERT_TRUE(Utils::isInsideArc(0, 337.5, 22.5));
    TEST_ASSERT_TRUE(Utils::isInsideArc(350, 337.5, 22.5));
    TEST_ASSERT_TRUE(Utils::isInsideArc(10, 337.5, 22.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(45, 337.5, 22.5));
}

static void test_isInsideArc_reversed_bounds_selects_opposite_arc(void) {
    TEST_ASSERT_TRUE(Utils::isInsideArc(180, 22.5, 337.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(0, 22.5, 337.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(350, 22.5, 337.5));
}

static void test_isInsideArc_degenerate_low_equals_high(void) {
    TEST_ASSERT_TRUE(Utils::isInsideArc(22.5, 22.5, 22.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(22.6, 22.5, 22.5));
    TEST_ASSERT_FALSE(Utils::isInsideArc(0, 22.5, 22.5));
}

static void test_between_hysteresis_membership_ignores_previously_on(void) {
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::BETWEEN, 45, 292.5, 67.5, false));
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::BETWEEN, 90, 292.5, 67.5, true));
}

static void test_getRuleDirectionFromString_recognizes_between(void) {
    TEST_ASSERT_EQUAL(RuleDirection::BETWEEN, getRuleDirectionFromString("between"));
}

static void test_getRuleDirectionName_between(void) {
    TEST_ASSERT_EQUAL_STRING("between", getRuleDirectionName(RuleDirection::BETWEEN));
}

extern "C" void run_rule_wind_between_tests(void) {
    RUN_TEST(test_isInsideArc_normal_arc);
    RUN_TEST(test_isInsideArc_wrap_arc_north);
    RUN_TEST(test_isInsideArc_single_rhumb_north);
    RUN_TEST(test_isInsideArc_reversed_bounds_selects_opposite_arc);
    RUN_TEST(test_isInsideArc_degenerate_low_equals_high);
    RUN_TEST(test_between_hysteresis_membership_ignores_previously_on);
    RUN_TEST(test_getRuleDirectionFromString_recognizes_between);
    RUN_TEST(test_getRuleDirectionName_between);
}
