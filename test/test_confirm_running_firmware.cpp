// A freshly booted image confirms itself by working, not by starting.
//
// With verifyRollbackLater() overridden the Arduino core no longer confirms a new image before
// setup(); the bootloader's pending-verify state survives into the control loop, and cancelling
// the rollback becomes the business of ConfirmRunningFirmwareUseCase, driven by the real
// LoopServiceConfigureDone - one call per completed iteration. These tests drive that loop
// service directly and read the confirmation call off a scripted IOtaPartitionControl: per the
// task, the host records that esp_ota_mark_app_valid_cancel_rollback was asked for, it does not
// imitate the bootloader's reaction.
//
// ### Reproducing the red
//
// The red is a loop that does not invoke the use case: with the use case wired but never called,
// "reached the working state" produces zero confirmation calls and the first test below fails.

#include <unity.h>

#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Core/Domain/UseCases/FirmwareUpdate/ConfirmRunningFirmwareUseCase.h"

#include "Mock/Core/Domain/MockOtaPartitionControl.h"
#include "Mock/Core/Domain/MockOtaUpdateLog.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"

#include <cstdio>

namespace {

/**
 * @brief The CONFIGURED_DONE loop reduced to what confirmation needs: the loop service itself,
 *        the structure the setup flow fills, and the scripted bootloader bookkeeping.
 */
struct ConfirmHarness {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    ModuleStructure moduleStructure;
    MockOtaPartitionControl otaControl;
    MockOtaUpdateLog otaLog;
    MockArduinoAdapter arduino;
    ConfirmRunningFirmwareUseCase useCase;
    LoopServiceConfigureDone loopService;

    ConfirmHarness()
        : moduleConfig(&logger, &debugSystem, &settings),
          useCase(&otaControl, &moduleStructure, &arduino, &logger, &debugSystem, &otaLog)
    {
        debugSystem.featureEnabled = false;
        moduleConfig.setSettings(&settings);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setConfirmRunningFirmwareUseCase(&useCase);
    }

    /** @brief The setup flow finished: the structure carries the module uid. */
    void structureIsFilled() { moduleStructure.growdirectorUid = "confirm-module"; }

    /** @brief The module has been up for the whole probation window. */
    void uptimeReached() { arduino.setMillis(ConfirmRunningFirmwareUseCase::MIN_UPTIME_MS); }

    void runLoopIterations(uint32_t count)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            loopService.loop();
        }
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// The headline: an image on probation is confirmed exactly once - after the loop has proven
// itself, and no matter how long it keeps running afterwards.
// --------------------------------------------------------------------------------------------

static void test_confirm_probation_image_confirmed_exactly_once_when_working_state_reached(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::PENDING_VERIFY;
    harness.structureIsFilled();
    harness.uptimeReached();

    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, harness.otaControl.markValidCalls,
                                  "the working state was reached but the image was not "
                                  "confirmed exactly once");

    // Ten more lifetimes of iterations change nothing: confirmation is a per-boot event.
    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS * 10);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, harness.otaControl.markValidCalls,
                                  "the image was confirmed more than once");
}

// --------------------------------------------------------------------------------------------
// The other half of the contract: an image that does not reach the working state is never
// confirmed - the next reset stays a rollback.
// --------------------------------------------------------------------------------------------

static void test_confirm_image_short_of_the_iteration_bar_is_not_confirmed(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::PENDING_VERIFY;
    harness.structureIsFilled();
    harness.uptimeReached();

    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS - 1);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, harness.otaControl.markValidCalls,
                                  "an image one iteration short of the bar was confirmed");
}

static void test_confirm_image_that_never_read_its_structure_is_not_confirmed(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::PENDING_VERIFY;
    harness.uptimeReached();
    // The structure stays empty: setup never managed to fill it.

    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS * 10);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, harness.otaControl.markValidCalls,
                                  "an image that never read its configuration was confirmed");
}

// --------------------------------------------------------------------------------------------
// A normal boot - the running image already confirmed - asks the bootloader for nothing.
// --------------------------------------------------------------------------------------------

static void test_confirm_already_valid_image_is_left_alone(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::VALID;
    harness.structureIsFilled();
    harness.uptimeReached();

    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS * 10);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, harness.otaControl.markValidCalls,
                                  "a normal boot re-confirmed an already valid image");
}

// --------------------------------------------------------------------------------------------
// The main gate: uptime. Iterations prove the loop turns; only time proves the module survived
// a full round of every liveness check the device runs - the watchdog threshold is by
// construction the longest a stall can go unnoticed. An image with the iterations done but the
// window not yet elapsed stays on probation, however many iterations it piles up.
// --------------------------------------------------------------------------------------------

static void test_confirm_image_short_of_the_uptime_window_is_not_confirmed(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::PENDING_VERIFY;
    harness.structureIsFilled();
    harness.arduino.setMillis(ConfirmRunningFirmwareUseCase::MIN_UPTIME_MS - 1);

    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS * 10);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, harness.otaControl.markValidCalls,
                                  "an image one millisecond short of the probation window was "
                                  "confirmed - iterations alone must not confirm");

    // The window elapses; the very next iteration confirms.
    harness.uptimeReached();
    harness.runLoopIterations(1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, harness.otaControl.markValidCalls,
                                  "the elapsed probation window did not confirm the image");
}

// --------------------------------------------------------------------------------------------
// The numbers are the numbers. The iteration bar must not quietly become 1 - that would
// confirm the moment the loop starts, the exact behaviour probation exists to prevent - and
// the probation window must stay derived from the watchdog threshold, so the two move
// together.
// --------------------------------------------------------------------------------------------

static void test_confirm_the_probation_numbers_are_pinned(void)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        100, ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS,
        "the iteration bar changed - if this is intentional, change this test in the same "
        "commit and say why in its message");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        (uint32_t)WDT_TIMEOUT_S * 1000UL, ConfirmRunningFirmwareUseCase::MIN_UPTIME_MS,
        "the probation window is no longer the watchdog threshold - the derivation broke");
    TEST_ASSERT_TRUE_MESSAGE(
        ConfirmRunningFirmwareUseCase::MIN_UPTIME_MS >= 60000,
        "a probation window under a minute confirms images that never faced a single "
        "diagnostic period");
}

// --------------------------------------------------------------------------------------------
// A refused confirmation write is retried, bounded. One otadata write failing once must not be
// terminal - the next reset would roll back a working image - and a flash refusing for good
// must produce a loud verdict, not a forever-retrying loop.
// --------------------------------------------------------------------------------------------

static void test_confirm_a_refused_write_is_retried_and_confirmed_once(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::PENDING_VERIFY;
    harness.structureIsFilled();
    harness.uptimeReached();
    harness.otaControl.markValidResult = false;

    // The working state is reached, the first write is refused.
    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, harness.otaControl.markValidCalls,
                                  "the first confirmation attempt never happened");

    // The flash recovers; the next iteration retries and the image is confirmed exactly once.
    harness.otaControl.markValidResult = true;
    harness.runLoopIterations(1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, harness.otaControl.markValidCalls,
                                  "the refused write was never retried - one transient flash "
                                  "refusal became terminal");

    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, harness.otaControl.markValidCalls,
                                  "a confirmed image kept asking the bootloader");
}

static void test_confirm_a_flash_refusing_for_good_gets_a_bounded_number_of_attempts(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::PENDING_VERIFY;
    harness.structureIsFilled();
    harness.uptimeReached();
    harness.otaControl.markValidResult = false;

    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS * 10);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        (int)ConfirmRunningFirmwareUseCase::MAX_CONFIRM_ATTEMPTS,
        harness.otaControl.markValidCalls,
        "a flash refusing for good was not asked exactly the bounded number of times");
    TEST_ASSERT_TRUE_MESSAGE(harness.otaLog.hasLineContaining("giving up"),
                             "the exhausted retries left no loud verdict in the OTA log");
}

// --------------------------------------------------------------------------------------------
// Diagnosis: the first loop iteration after boot tells the OTA log what the bootloader did.
// After a rollback the rejected image sits in the other slot in the aborted state - that boot
// says so explicitly; a probation boot announces itself; a plain boot claims neither.
// --------------------------------------------------------------------------------------------

static void test_confirm_a_boot_after_rollback_is_named_in_the_ota_log(void)
{
    ConfirmHarness harness;
    // The post-rollback picture: the module runs the old, confirmed image; the slot with the
    // update the bootloader rejected is aborted.
    harness.otaControl.runningState = OtaImageState::VALID;
    harness.otaControl.otherState = OtaImageState::ABORTED;
    harness.structureIsFilled();

    harness.runLoopIterations(1);

    TEST_ASSERT_TRUE_MESSAGE(harness.otaLog.hasLineContaining("ROLLBACK"),
                             "a boot after a rollback did not say so in the OTA log");
}

static void test_confirm_a_probation_boot_announces_itself_in_the_ota_log(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::PENDING_VERIFY;
    harness.structureIsFilled();

    harness.runLoopIterations(1);

    TEST_ASSERT_TRUE_MESSAGE(harness.otaLog.hasLineContaining("probation"),
                             "a probation boot did not announce itself in the OTA log");
}

static void test_confirm_a_plain_boot_claims_no_rollback(void)
{
    ConfirmHarness harness;
    harness.otaControl.runningState = OtaImageState::VALID;
    harness.otaControl.otherState = OtaImageState::VALID;
    harness.structureIsFilled();

    harness.runLoopIterations(ConfirmRunningFirmwareUseCase::REQUIRED_HEALTHY_ITERATIONS * 2);

    TEST_ASSERT_FALSE_MESSAGE(harness.otaLog.hasLineContaining("ROLLBACK"),
                              "a plain boot claimed a rollback happened");
    TEST_ASSERT_FALSE_MESSAGE(harness.otaLog.hasLineContaining("probation"),
                              "a plain boot claimed to be on probation");
}

extern "C" void run_confirm_running_firmware_tests(void)
{
    printf("\n----- Running Confirm Running Firmware Tests -----\n");

    RUN_TEST(test_confirm_probation_image_confirmed_exactly_once_when_working_state_reached);
    RUN_TEST(test_confirm_image_short_of_the_iteration_bar_is_not_confirmed);
    RUN_TEST(test_confirm_image_that_never_read_its_structure_is_not_confirmed);
    RUN_TEST(test_confirm_image_short_of_the_uptime_window_is_not_confirmed);
    RUN_TEST(test_confirm_the_probation_numbers_are_pinned);
    RUN_TEST(test_confirm_a_refused_write_is_retried_and_confirmed_once);
    RUN_TEST(test_confirm_a_flash_refusing_for_good_gets_a_bounded_number_of_attempts);
    RUN_TEST(test_confirm_already_valid_image_is_left_alone);
    RUN_TEST(test_confirm_a_boot_after_rollback_is_named_in_the_ota_log);
    RUN_TEST(test_confirm_a_probation_boot_announces_itself_in_the_ota_log);
    RUN_TEST(test_confirm_a_plain_boot_claims_no_rollback);

    printf("\n----- Confirm Running Firmware Tests Complete -----\n");
}
