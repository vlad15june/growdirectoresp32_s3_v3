#include <unity.h>
#include <string>

#include "Core/Domain/UseCases/Sensors/SensorRecoveryUseCase.h"

namespace {

const std::string UID_A = "84:F7:03:FE:88:2C.00";
const std::string UID_B = "84:F7:03:FE:88:2C.01";

// INV-2: disconnected < threshold and recovers -> plain READ, no power cycle
void test_below_threshold_reads(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 45000, UID_A));
}

// INV-2 boundary: exactly threshold is not stuck yet
void test_at_threshold_reads(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 60000, UID_A));
}

// INV-2/INV-3: disconnected > threshold -> POWER_OFF
void test_over_threshold_powers_off(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 61000, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::POWER_OFF_PHASE == recovery.getPhase(0));
}

// INV-3: next tick after power off -> POWER_ON (no read), exactly one tick each
void test_power_off_then_power_on(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 61000, UID_A));
    recovery.observe(UID_A, true, 61000);
    TEST_ASSERT_TRUE(RecoveryAction::POWER_ON == recovery.decide(0, 76000, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::POWER_ON_PHASE == recovery.getPhase(0));
}

// INV-4: after power on, poll; first successful read returns to NORMAL
void test_full_recovery_returns_to_normal(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);

    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 61000, UID_A));
    recovery.observe(UID_A, true, 61000);

    TEST_ASSERT_TRUE(RecoveryAction::POWER_ON == recovery.decide(0, 76000, UID_A));
    recovery.observe(UID_A, true, 76000);

    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 91000, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::POLLING == recovery.getPhase(0));

    // first successful read clears the disconnected timestamp
    recovery.observe(UID_A, false, 91000);

    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 106000, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::NORMAL == recovery.getPhase(0));
}

// INV-5: no recovery within poll window -> cycle repeats (POWER_OFF again)
void test_poll_window_timeout_repeats_cycle(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);

    recovery.decide(0, 61000, UID_A);  // POWER_OFF
    recovery.observe(UID_A, true, 61000);
    recovery.decide(0, 76000, UID_A);  // POWER_ON
    recovery.observe(UID_A, true, 76000);
    recovery.decide(0, 91000, UID_A);  // READ, enters POLLING at 91000
    recovery.observe(UID_A, true, 91000);

    // within window -> keep reading
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 106000, UID_A));
    recovery.observe(UID_A, true, 106000);

    // past window (now - phaseStart > 60000) -> restart cycle
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 152000, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::POWER_OFF_PHASE == recovery.getPhase(0));
}

// INV-6: actuating port A does not affect a healthy sensor on port B
void test_ports_are_independent(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);  // port 0 stuck
    // UID_B on port 1 never disconnected

    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 61000, UID_A));
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(1, 61000, UID_B));
    TEST_ASSERT_TRUE(RecoveryPhase::NORMAL == recovery.getPhase(1));
}

// INV-8: fresh instance (reboot) has no state -> plain READ
void test_fresh_instance_reads(void)
{
    SensorRecoveryUseCase recovery;
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 999999, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::NORMAL == recovery.getPhase(0));
}

// A recovered sensor that later disconnects again re-arms the cycle
void test_recovered_then_disconnect_rearms(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, false, 0);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 1000, UID_A));

    recovery.observe(UID_A, true, 2000);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 50000, UID_A));
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 63000, UID_A));
}

} // namespace

extern "C" int run_sensor_recovery_tests(void)
{
    RUN_TEST(test_below_threshold_reads);
    RUN_TEST(test_at_threshold_reads);
    RUN_TEST(test_over_threshold_powers_off);
    RUN_TEST(test_power_off_then_power_on);
    RUN_TEST(test_full_recovery_returns_to_normal);
    RUN_TEST(test_poll_window_timeout_repeats_cycle);
    RUN_TEST(test_ports_are_independent);
    RUN_TEST(test_fresh_instance_reads);
    RUN_TEST(test_recovered_then_disconnect_rearms);
    return 0;
}
