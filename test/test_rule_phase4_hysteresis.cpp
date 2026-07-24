#include <unity.h>

#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"

static int verdict(RuleDirection dir, double value, double low, double high, bool prevOn) {
    return static_cast<int>(evaluateSensorHysteresis(dir, value, low, high, prevOn));
}

static const int V_ON = static_cast<int>(SensorVerdict::TURN_ON);
static const int V_OFF = static_cast<int>(SensorVerdict::TURN_OFF);
static const int V_HOLD = static_cast<int>(SensorVerdict::HOLD);
static const int V_INERT = static_cast<int>(SensorVerdict::INERT);

static void test_down_turns_on_above_high_when_off(void) {
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::DOWN, 26.2, 24.0, 26.0, false));
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::DOWN, 26.0, 24.0, 26.0, false));
}

static void test_down_turns_off_below_low_when_on(void) {
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::DOWN, 23.8, 24.0, 26.0, true));
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::DOWN, 24.0, 24.0, 26.0, true));
}

static void test_down_deadband_holds(void) {
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::DOWN, 25.0, 24.0, 26.0, true));
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::DOWN, 25.0, 24.0, 26.0, false));
}

static void test_down_on_zone_while_on_holds(void) {
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::DOWN, 27.0, 24.0, 26.0, true));
}

static void test_down_off_zone_while_off_holds(void) {
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::DOWN, 23.0, 24.0, 26.0, false));
}

static void test_up_turns_on_below_low_when_off(void) {
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::UP, 23.9, 24.0, 26.0, false));
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::UP, 24.0, 24.0, 26.0, false));
}

static void test_up_turns_off_above_high_when_on(void) {
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::UP, 26.1, 24.0, 26.0, true));
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::UP, 26.0, 24.0, 26.0, true));
}

static void test_up_deadband_holds(void) {
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::UP, 25.0, 24.0, 26.0, true));
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::UP, 25.0, 24.0, 26.0, false));
}

static void test_up_on_zone_while_on_holds(void) {
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::UP, 23.0, 24.0, 26.0, true));
}

static void test_unknown_is_inert(void) {
    TEST_ASSERT_EQUAL_INT(V_INERT, verdict(RuleDirection::UNKNOWN, 25.0, 24.0, 26.0, false));
    TEST_ASSERT_EQUAL_INT(V_INERT, verdict(RuleDirection::UNKNOWN, 30.0, 24.0, 26.0, true));
}

static void test_no_spurious_turn_off_at_boot(void) {
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::DOWN, 27.0, 24.0, 26.0, false));
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::UP, 23.0, 24.0, 26.0, false));
}

static void test_down_inverted_bounds_turns_on_above_max_when_off(void) {
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::DOWN, 26.2, 26.0, 24.0, false));
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::DOWN, 26.0, 26.0, 24.0, false));
}

static void test_down_inverted_bounds_turns_off_below_min_when_on(void) {
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::DOWN, 23.8, 26.0, 24.0, true));
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::DOWN, 24.0, 26.0, 24.0, true));
}

static void test_down_inverted_bounds_deadband_holds(void) {
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::DOWN, 25.0, 26.0, 24.0, true));
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::DOWN, 25.0, 26.0, 24.0, false));
}

static void test_up_inverted_bounds_turns_on_below_min_when_off(void) {
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::UP, 23.9, 26.0, 24.0, false));
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::UP, 24.0, 26.0, 24.0, false));
}

static void test_up_inverted_bounds_turns_off_above_max_when_on(void) {
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::UP, 26.1, 26.0, 24.0, true));
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::UP, 26.0, 26.0, 24.0, true));
}

static void test_up_inverted_bounds_deadband_holds(void) {
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::UP, 25.0, 26.0, 24.0, true));
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::UP, 25.0, 26.0, 24.0, false));
}

static void test_gdv1_3753_ph_down_inverted_bounds_does_not_turn_on(void) {
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::DOWN, 2729.0, 2812.021973, 2664.710938, false));
    TEST_ASSERT_EQUAL_INT(V_HOLD, verdict(RuleDirection::DOWN, 2701.0, 2812.021973, 2664.710938, false));
}

static void test_gdv1_3753_ph_down_inverted_bounds_switches_at_extremes(void) {
    TEST_ASSERT_EQUAL_INT(V_ON, verdict(RuleDirection::DOWN, 2850.0, 2812.021973, 2664.710938, false));
    TEST_ASSERT_EQUAL_INT(V_OFF, verdict(RuleDirection::DOWN, 2600.0, 2812.021973, 2664.710938, true));
}

extern "C" void run_rule_phase4_hysteresis_tests(void) {
    RUN_TEST(test_down_turns_on_above_high_when_off);
    RUN_TEST(test_down_turns_off_below_low_when_on);
    RUN_TEST(test_down_deadband_holds);
    RUN_TEST(test_down_on_zone_while_on_holds);
    RUN_TEST(test_down_off_zone_while_off_holds);
    RUN_TEST(test_up_turns_on_below_low_when_off);
    RUN_TEST(test_up_turns_off_above_high_when_on);
    RUN_TEST(test_up_deadband_holds);
    RUN_TEST(test_up_on_zone_while_on_holds);
    RUN_TEST(test_unknown_is_inert);
    RUN_TEST(test_no_spurious_turn_off_at_boot);
    RUN_TEST(test_down_inverted_bounds_turns_on_above_max_when_off);
    RUN_TEST(test_down_inverted_bounds_turns_off_below_min_when_on);
    RUN_TEST(test_down_inverted_bounds_deadband_holds);
    RUN_TEST(test_up_inverted_bounds_turns_on_below_min_when_off);
    RUN_TEST(test_up_inverted_bounds_turns_off_above_max_when_on);
    RUN_TEST(test_up_inverted_bounds_deadband_holds);
    RUN_TEST(test_gdv1_3753_ph_down_inverted_bounds_does_not_turn_on);
    RUN_TEST(test_gdv1_3753_ph_down_inverted_bounds_switches_at_extremes);
}
