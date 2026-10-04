#include <unity.h>
#include <string>

#include "Core/Domain/UseCases/Sensors/PortWarmUpWatch.h"
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

// A stretch the port could not answer in is left out of the silence: 20 s of misses, then 70 s
// dark or coming up, and the sensor has been silent for 20 s as far as the recovery is concerned.
void test_the_dark_is_left_out_of_the_silence(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    recovery.leaveOutOfTheSilence(0, UID_A, 20000, 90000, 90000);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 90000, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::NORMAL == recovery.getPhase(0));
}

// Only the dark part is left out: 30 s of silence before the stretch and 31 s after it are 61 s,
// and that is a power cycle.
void test_silence_on_both_sides_of_the_dark_still_counts(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    recovery.leaveOutOfTheSilence(0, UID_A, 30000, 70000, 70000);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 70000, UID_A));
    recovery.observe(UID_A, true, 70000);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 100000, UID_A));
    recovery.observe(UID_A, true, 100000);
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 101000, UID_A));
}

// A stretch that ended before the silence began leaves nothing out, and one the silence began in
// the middle of leaves out only what came after it.
void test_only_the_dark_the_silence_ran_through_is_left_out(void)
{
    SensorRecoveryUseCase before;
    before.observe(UID_A, true, 20000);
    before.leaveOutOfTheSilence(0, UID_A, 0, 10000, 20000);
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == before.decide(0, 81000, UID_A));

    SensorRecoveryUseCase inside;
    inside.observe(UID_A, true, 20000);
    inside.leaveOutOfTheSilence(0, UID_A, 0, 30000, 30000);
    // 10 s of the stretch after the silence began: 61 s after it began is 51 s of silence.
    TEST_ASSERT_TRUE(RecoveryAction::READ == inside.decide(0, 81000, UID_A));
    inside.observe(UID_A, true, 81000);
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == inside.decide(0, 91000, UID_A));
}

// In POLLING the dark is left out of the polling window, and the sensor stays stuck until a read
// answers: a dark stretch neither retries the power cycle straight away nor ends the recovery.
void test_the_polling_window_counts_only_a_port_that_could_answer(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    recovery.decide(0, 61000, UID_A);  // POWER_OFF
    recovery.observe(UID_A, true, 61000);
    recovery.decide(0, 76000, UID_A);  // POWER_ON
    recovery.observe(UID_A, true, 76000);
    recovery.decide(0, 91000, UID_A);  // READ, POLLING from 91000
    recovery.observe(UID_A, true, 91000);

    recovery.leaveOutOfThePollingWindow(0, 100000, 170000, 170000);
    // 9 s of the polling window before the dark: no retry yet, and not recovered either.
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 170000, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::POLLING == recovery.getPhase(0));
    recovery.observe(UID_A, true, 170000);
    // 9 s before the dark and 52 s after it: past the window.
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 222000, UID_A));
}

// Out of NORMAL, "no longer stuck" means a read has answered, and the dark does not stand in for
// one: a silence that began inside the polling phase, and was mostly dark, does not end the
// recovery.
void test_a_dark_stretch_does_not_end_a_recovery_nothing_answered(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    recovery.decide(0, 61000, UID_A);  // POWER_OFF
    // The flag the power cycle is told is the one the loop last applied, and it may still be down.
    recovery.observe(UID_A, false, 61000);
    recovery.decide(0, 76000, UID_A);  // POWER_ON
    recovery.observe(UID_A, false, 76000);
    recovery.decide(0, 91000, UID_A);  // READ, POLLING from 91000
    recovery.observe(UID_A, true, 91000);

    recovery.leaveOutOfTheSilence(0, UID_A, 100000, 170000, 170000);
    recovery.leaveOutOfThePollingWindow(0, 100000, 170000, 170000);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, 170000, UID_A));
    TEST_ASSERT_TRUE_MESSAGE(RecoveryPhase::POLLING == recovery.getPhase(0),
                             "a dark stretch took the port out of recovery with no read answering");
}

// The port is back, but the line was busy and nobody has asked the sensor since: 35 s of silence
// before the stretch and 25 s after it are past the threshold, and still the sensor is asked once
// before it is power-cycled - once, not again.
void test_after_the_dark_the_sensor_is_asked_once_before_its_silence_cycles_it(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 45000);
    recovery.leaveOutOfTheSilence(0, UID_A, 80000, 155000, 156000);
    TEST_ASSERT_TRUE_MESSAGE(RecoveryAction::READ == recovery.decide(0, 180500, UID_A),
                             "the recovery power-cycled a sensor nobody asked since its port came "
                             "back");
    TEST_ASSERT_TRUE(RecoveryPhase::NORMAL == recovery.getPhase(0));
    recovery.observe(UID_A, true, 180500);
    TEST_ASSERT_TRUE_MESSAGE(RecoveryAction::POWER_OFF == recovery.decide(0, 195500, UID_A),
                             "one read after the dark deferred the power cycle by more than itself");
}

// The same for the polling window: past it once the stretch is left out, and the port is still read
// once before the cycle is retried - and a sensor that answers that read ends the recovery.
void test_after_the_dark_the_port_is_asked_once_before_the_cycle_is_retried(void)
{
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, 0);
    recovery.decide(0, 61000, UID_A);  // POWER_OFF
    recovery.decide(0, 76000, UID_A);  // POWER_ON
    recovery.decide(0, 91000, UID_A);  // READ, POLLING from 91000
    recovery.observe(UID_A, true, 91000);

    recovery.leaveOutOfThePollingWindow(0, 100000, 170000, 170000);
    // 9 s of the window before the dark and 53 s after it, none of them asked.
    TEST_ASSERT_TRUE_MESSAGE(RecoveryAction::READ == recovery.decide(0, 223000, UID_A),
                             "the recovery retried the power cycle of a port nobody asked since it "
                             "came back");
    recovery.observe(UID_A, true, 223000);
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, 238000, UID_A));

    SensorRecoveryUseCase answers;
    answers.observe(UID_A, true, 0);
    answers.decide(0, 61000, UID_A);
    answers.decide(0, 76000, UID_A);
    answers.decide(0, 91000, UID_A);
    answers.observe(UID_A, true, 91000);
    answers.leaveOutOfThePollingWindow(0, 100000, 170000, 170000);
    TEST_ASSERT_TRUE(RecoveryAction::READ == answers.decide(0, 223000, UID_A));
    answers.observe(UID_A, false, 223000);
    TEST_ASSERT_TRUE(RecoveryAction::READ == answers.decide(0, 238000, UID_A));
    TEST_ASSERT_TRUE(RecoveryPhase::NORMAL == answers.getPhase(0));
}

// Ages, not moments: a dark stretch across the wrap of the clock is left out as it would be anywhere
// else.
void test_the_dark_is_left_out_across_the_wrap_of_the_clock(void)
{
    const unsigned long nearTheWrap = (unsigned long)-1 - 9999UL; // 10 s before the counter wraps
    SensorRecoveryUseCase recovery;
    recovery.observe(UID_A, true, nearTheWrap);
    // 70 s later, 65 s of them dark.
    recovery.leaveOutOfTheSilence(0, UID_A, nearTheWrap + 5000UL, nearTheWrap + 70000UL,
                                  nearTheWrap + 70000UL);
    TEST_ASSERT_TRUE(RecoveryAction::READ == recovery.decide(0, nearTheWrap + 70000UL, UID_A));
    recovery.observe(UID_A, true, nearTheWrap + 70000UL);
    // 5 s before the dark and 56 s after it.
    TEST_ASSERT_TRUE(RecoveryAction::POWER_OFF == recovery.decide(0, nearTheWrap + 126000UL, UID_A));
}

// --- What the port's own record says about when it could answer ---------------------------------

PortPower upAt(uint32_t onAtMs, uint32_t ons, uint32_t offs, uint32_t offAtMs)
{
    PortPower power;
    power.on = true;
    power.ons = ons;
    power.onAtMs = onAtMs;
    power.offs = offs;
    power.offAtMs = offAtMs;
    return power;
}

PortPower downAt(uint32_t offAtMs, uint32_t ons, uint32_t onAtMs, uint32_t offs)
{
    PortPower power = upAt(onAtMs, ons, offs, offAtMs);
    power.on = false;
    return power;
}

// Dark owes the whole wake-up; up without a transition owes nothing; up for 3 s owes 7; and the age
// is read across the wrap of the clock.
void test_a_port_owes_the_wake_up_counted_from_its_power_coming_up(void)
{
    PortPower dark;
    TEST_ASSERT_EQUAL_UINT32(PortPower::WAKE_UP_MS, dark.wakeUpOwedMs(5000));

    PortPower thereForGood;
    thereForGood.on = true;
    TEST_ASSERT_EQUAL_UINT32(0, thereForGood.wakeUpOwedMs(5000));

    TEST_ASSERT_EQUAL_UINT32(7000, upAt(2000, 1, 0, 0).wakeUpOwedMs(5000));
    TEST_ASSERT_EQUAL_UINT32(0, upAt(2000, 1, 0, 0).wakeUpOwedMs(12000));
    // Up at 0xFFFFF000, now 0x00000F00: 7936 ms of power.
    TEST_ASSERT_EQUAL_UINT32(PortPower::WAKE_UP_MS - 7936,
                             upAt(0xFFFFF000u, 1, 0, 0).wakeUpOwedMs(0x00000F00u));
}

// A port a step takes down at 10 s and someone else gives back at 30 s: one stretch, from the moment
// it went down to the moment it had had its power back for the wake-up, reported once.
void test_a_stretch_runs_from_the_power_going_down_to_the_wake_up_after_it_comes_back(void)
{
    PortWarmUpWatch watch;
    TEST_ASSERT_FALSE(watch.look(upAt(0, 1, 0, 0), 10000).ended);
    TEST_ASSERT_FALSE(watch.dark());
    TEST_ASSERT_FALSE(watch.look(downAt(10000, 1, 0, 1), 15000).ended);
    TEST_ASSERT_TRUE(watch.dark());
    TEST_ASSERT_FALSE(watch.look(upAt(30000, 2, 1, 10000), 35000).ended);
    const PortWarmUpWatch::Stretch stretch = watch.look(upAt(30000, 2, 1, 10000), 40000);
    TEST_ASSERT_TRUE(stretch.ended);
    TEST_ASSERT_EQUAL_UINT32(10000, stretch.fromMs);
    TEST_ASSERT_EQUAL_UINT32(40000, stretch.untilMs);
    TEST_ASSERT_FALSE(watch.dark());
    TEST_ASSERT_FALSE(watch.look(upAt(30000, 2, 1, 10000), 50000).ended);
}

// Taken down again before it has come up, by another piece of work: still one stretch, from the
// first time it went down; and several transitions between two looks are seen for what they are.
void test_a_port_taken_down_again_before_it_came_up_is_one_stretch(void)
{
    PortWarmUpWatch watch;
    watch.look(upAt(0, 1, 0, 0), 10000);
    watch.look(downAt(10000, 1, 0, 1), 11000);
    watch.look(upAt(12000, 2, 1, 10000), 13000);
    // Down at 15 s and up at 20 s, both since the last look.
    TEST_ASSERT_FALSE(watch.look(upAt(20000, 3, 2, 15000), 25000).ended);
    const PortWarmUpWatch::Stretch stretch = watch.look(upAt(20000, 3, 2, 15000), 30000);
    TEST_ASSERT_TRUE(stretch.ended);
    TEST_ASSERT_EQUAL_UINT32(10000, stretch.fromMs);
    TEST_ASSERT_EQUAL_UINT32(30000, stretch.untilMs);
}

// A port dark with nothing on record - the way the board starts every port - is dark from the first
// look, and its stretch ends when whoever gives it its power has given it for the wake-up.
void test_a_port_dark_from_the_start_is_dark_from_the_first_look(void)
{
    PortWarmUpWatch watch;
    PortPower neverMoved;
    TEST_ASSERT_FALSE(watch.look(neverMoved, 1000).ended);
    TEST_ASSERT_TRUE(watch.dark());
    const PortWarmUpWatch::Stretch stretch = watch.look(upAt(2000, 1, 0, 0), 12000);
    TEST_ASSERT_TRUE(stretch.ended);
    TEST_ASSERT_EQUAL_UINT32(1000, stretch.fromMs);
    TEST_ASSERT_EQUAL_UINT32(12000, stretch.untilMs);
}

// The recovery's own power cycle is acknowledged, not left out: no stretch comes of it. And a port up
// for good is never dark.
void test_the_recoverys_own_power_cycle_and_a_port_up_for_good_make_no_stretch(void)
{
    PortWarmUpWatch watch;
    watch.look(upAt(0, 1, 0, 0), 10000);
    watch.acknowledge(downAt(10000, 1, 0, 1));
    watch.acknowledge(upAt(25000, 2, 1, 10000));
    TEST_ASSERT_FALSE(watch.dark());
    TEST_ASSERT_FALSE(watch.look(upAt(25000, 2, 1, 10000), 40000).ended);

    PortWarmUpWatch forGood;
    PortPower there;
    there.on = true;
    TEST_ASSERT_FALSE(forGood.look(there, 0).ended);
    TEST_ASSERT_FALSE(forGood.dark());
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
    RUN_TEST(test_the_dark_is_left_out_of_the_silence);
    RUN_TEST(test_silence_on_both_sides_of_the_dark_still_counts);
    RUN_TEST(test_only_the_dark_the_silence_ran_through_is_left_out);
    RUN_TEST(test_the_polling_window_counts_only_a_port_that_could_answer);
    RUN_TEST(test_a_dark_stretch_does_not_end_a_recovery_nothing_answered);
    RUN_TEST(test_after_the_dark_the_sensor_is_asked_once_before_its_silence_cycles_it);
    RUN_TEST(test_after_the_dark_the_port_is_asked_once_before_the_cycle_is_retried);
    RUN_TEST(test_the_dark_is_left_out_across_the_wrap_of_the_clock);
    RUN_TEST(test_a_port_owes_the_wake_up_counted_from_its_power_coming_up);
    RUN_TEST(test_a_stretch_runs_from_the_power_going_down_to_the_wake_up_after_it_comes_back);
    RUN_TEST(test_a_port_taken_down_again_before_it_came_up_is_one_stretch);
    RUN_TEST(test_a_port_dark_from_the_start_is_dark_from_the_first_look);
    RUN_TEST(test_the_recoverys_own_power_cycle_and_a_port_up_for_good_make_no_stretch);
    return 0;
}
