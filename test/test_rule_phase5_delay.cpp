#include <unity.h>

#include <string>
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"

static void test_delay_on_arm(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_OFF, true, 30, 0, 1000, 0);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_ON, d.newState);
    TEST_ASSERT_TRUE(d.armTimer);
    TEST_ASSERT_EQUAL_UINT32(31000, d.newDeadline);
    TEST_ASSERT_FALSE(d.clearTimer);
    TEST_ASSERT_EQUAL_STRING("", d.ruleStopReason);
}

static void test_delay_on_hold(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_DELAY_ON, true, 30, 0, 20000, 31000);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_ON, d.newState);
    TEST_ASSERT_FALSE(d.armTimer);
    TEST_ASSERT_FALSE(d.clearTimer);
}

static void test_delay_on_commit(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_DELAY_ON, true, 30, 0, 31000, 31000);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, d.newState);
    TEST_ASSERT_TRUE(d.clearTimer);
    TEST_ASSERT_EQUAL_STRING("", d.ruleStopReason);
}

static void test_delay_on_commit_on_late_tick(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_DELAY_ON, true, 30, 0, 99999, 31000);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, d.newState);
    TEST_ASSERT_TRUE(d.clearTimer);
}

static void test_delay_on_cancel(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_DELAY_ON, false, 30, 0, 20000, 31000);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, d.newState);
    TEST_ASSERT_EQUAL_STRING("unstable_sensor", d.ruleStopReason);
    TEST_ASSERT_TRUE(d.clearTimer);
}

static void test_delay_off_arm(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_REQUEST_ON, false, 0, 30, 1000, 0);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_OFF, d.newState);
    TEST_ASSERT_TRUE(d.armTimer);
    TEST_ASSERT_EQUAL_UINT32(31000, d.newDeadline);
}

static void test_delay_off_hold(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_DELAY_OFF, false, 0, 30, 20000, 31000);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_OFF, d.newState);
    TEST_ASSERT_FALSE(d.armTimer);
    TEST_ASSERT_FALSE(d.clearTimer);
}

static void test_delay_off_commit(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_DELAY_OFF, false, 0, 30, 31000, 31000);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, d.newState);
    TEST_ASSERT_EQUAL_STRING("setpoint_reach", d.ruleStopReason);
    TEST_ASSERT_TRUE(d.clearTimer);
}

static void test_delay_off_cancel(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_DELAY_OFF, true, 0, 30, 20000, 31000);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, d.newState);
    TEST_ASSERT_EQUAL_STRING("unstable_sensor", d.ruleStopReason);
    TEST_ASSERT_TRUE(d.clearTimer);
}

static void test_delay_on_zero_immediate(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_OFF, true, 0, 0, 1000, 0);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, d.newState);
    TEST_ASSERT_FALSE(d.armTimer);
    TEST_ASSERT_EQUAL_STRING("", d.ruleStopReason);
}

static void test_delay_off_zero_immediate(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_REQUEST_ON, false, 0, 0, 1000, 0);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, d.newState);
    TEST_ASSERT_FALSE(d.armTimer);
    TEST_ASSERT_EQUAL_STRING("setpoint_reach", d.ruleStopReason);
}

static void test_steady_off_stays_off(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_OFF, false, 30, 30, 1000, 0);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, d.newState);
    TEST_ASSERT_FALSE(d.armTimer);
    TEST_ASSERT_FALSE(d.clearTimer);
}

static void test_steady_on_stays_on(void) {
    DelayDecision d = decideDelayTransition(DEVICE_STATE_REQUEST_ON, true, 30, 30, 1000, 0);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, d.newState);
    TEST_ASSERT_FALSE(d.armTimer);
    TEST_ASSERT_FALSE(d.clearTimer);
}

extern "C" void run_rule_phase5_delay_tests(void) {
    RUN_TEST(test_delay_on_arm);
    RUN_TEST(test_delay_on_hold);
    RUN_TEST(test_delay_on_commit);
    RUN_TEST(test_delay_on_commit_on_late_tick);
    RUN_TEST(test_delay_on_cancel);
    RUN_TEST(test_delay_off_arm);
    RUN_TEST(test_delay_off_hold);
    RUN_TEST(test_delay_off_commit);
    RUN_TEST(test_delay_off_cancel);
    RUN_TEST(test_delay_on_zero_immediate);
    RUN_TEST(test_delay_off_zero_immediate);
    RUN_TEST(test_steady_off_stays_off);
    RUN_TEST(test_steady_on_stays_on);
}
