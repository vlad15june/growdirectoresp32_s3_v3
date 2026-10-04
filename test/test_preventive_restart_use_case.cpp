#include <unity.h>
#include <time.h>
#include <cstring>

#include "Core/Domain/UseCases/Diagnostics/PreventiveRestartUseCase.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/BootTypeEnum.h"

#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Core/Domain/MockFirmwareRemoteUpdate.h"
#include "Mock/UseCases/Rules/MockEvaluateRulesUseCase.h"
#include "Mock/Infrastructure/Logging/MockBootEventLog.h"

#ifdef PREVENTIVE_RESTART_DISABLED
// Only a build with the nightly restart compiled out follows the restart request downstream here.
#include "Core/Domain/UseCases/ModuleMode/OnChangeModuleModeUseCase.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#endif

static MockSettings g_settings;
static MockModuleConfig g_moduleConfig;
static MockNTPTimeAdapter g_timeAdapter;
static MockLogger g_logger;
static MockDebugSystem g_debugSystem;
static MockDeviceModel g_deviceModel;
static MockFirmwareRemoteUpdate g_firmwareUpdate;
static MockEvaluateRulesUseCase g_evaluateRules;
static MockBootEventLog g_restartEventLog;
static PreventiveRestartUseCase* g_useCase = nullptr;

static DeviceDigitalOut g_device;

static void resetAll() {
    g_settings.reset();
    g_moduleConfig.reset();
    g_moduleConfig.setSettings(&g_settings);
    g_timeAdapter.reset();
    g_logger.reset();
    g_debugSystem.reset();
    g_deviceModel.reset();
    g_firmwareUpdate.reset();
    g_evaluateRules.reset();
    g_restartEventLog.reset();
    memset(&g_device, 0, sizeof(g_device));
}

static void setupUseCase() {
    if (g_useCase != nullptr) {
        delete g_useCase;
    }
    g_useCase = new PreventiveRestartUseCase(
        &g_moduleConfig,
        &g_deviceModel,
        &g_evaluateRules,
        &g_firmwareUpdate,
        &g_timeAdapter,
        &g_logger,
        &g_debugSystem,
        &g_restartEventLog);
}

static void setTime(int hour, int minute, int day) {
    struct tm tm = {};
    tm.tm_year = 2024 - 1900;
    tm.tm_mon = 0;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    time_t epoch = mktime(&tm);
    g_timeAdapter.setEpochTime(epoch);
}

static void setTimeFromMinutes(int minutesSinceMidnight, int day) {
    setTime(minutesSinceMidnight / 60, minutesSinceMidnight % 60, day);
}

void test_preventive_restart_triggers_when_safe() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_UINT8(15, g_settings.getLastPreventiveRestartDay());
    TEST_ASSERT_EQUAL_INT(CONFIGURED_NEED_RESTART, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_TECHNICAL, g_settings.getBootType());
    TEST_ASSERT_TRUE(g_restartEventLog.hasPending());
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(g_restartEventLog.getMessages().size()));
}

void test_preventive_restart_triggers_at_window_end() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int endMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                     PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE +
                     PreventiveRestartUseCase::PREVENTIVE_RESTART_WINDOW_MIN;
    setTimeFromMinutes(endMinutes, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_UINT8(15, g_settings.getLastPreventiveRestartDay());
    TEST_ASSERT_EQUAL_INT(CONFIGURED_NEED_RESTART, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_TECHNICAL, g_settings.getBootType());
}

void test_preventive_restart_skipped_outside_window() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int endMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                     PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE +
                     PreventiveRestartUseCase::PREVENTIVE_RESTART_WINDOW_MIN;
    setTimeFromMinutes(endMinutes + 1, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getLastPreventiveRestartDay());
    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
}

void test_preventive_restart_skipped_already_today() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    g_settings.setLastPreventiveRestartDay(15);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
}

void test_preventive_restart_skipped_device_on() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_ON;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
}

// GDV1-3794: delayOff keeps the relay closed, so it must block the preventive restart.
void test_preventive_restart_skipped_device_in_delay_off() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_DELAY_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
}

// GDV1-3794: delayOn leaves the relay open and nothing is running, so it must not block the restart.
void test_preventive_restart_triggers_with_device_in_delay_on() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_DELAY_ON;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_NEED_RESTART, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_TECHNICAL, g_settings.getBootType());
}

void test_preventive_restart_skipped_rule_starts_soon() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_evaluateRules.setWillTurnOnWithinMinutes(true);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
    TEST_ASSERT_EQUAL_INT(PreventiveRestartUseCase::PREVENTIVE_RESTART_LOOKAHEAD_MIN,
                         g_evaluateRules.getLastMinutes());
}

void test_preventive_restart_skipped_ota_in_progress() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_firmwareUpdate.setUpdateInProgress(true);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
}

void test_preventive_restart_skipped_not_configured() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(USER_CAN_CONFIGURE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(USER_CAN_CONFIGURE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
}

#ifdef PREVENTIVE_RESTART_DISABLED

// Same scenario as test_preventive_restart_triggers_when_safe: inside the window, first pass of
// the day, every device off, no rule about to start. With PREVENTIVE_RESTART_DISABLED in the
// build the module must be left exactly as it was found - no restart request, no boot type, no
// event log entry, and above all no day marker written into the settings sector.
void test_preventive_restart_not_requested_under_build_flag() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getLastPreventiveRestartDay());
    TEST_ASSERT_FALSE(g_restartEventLog.hasPending());
}

// The step after the request: OnChangeModuleModeUseCase is what turns CONFIGURED_NEED_RESTART
// into a reset of the board. Under the flag it finds nothing to act on, so the whole night
// passes without the board resetting.
void test_preventive_restart_does_not_reach_a_reset_under_build_flag() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    g_moduleConfig.setCurrentModuleMode(CONFIGURED_DONE);
    int startMinutes = PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR * 60 +
                       PreventiveRestartUseCase::PREVENTIVE_RESTART_MINUTE;
    setTimeFromMinutes(startMinutes + 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    MockArduinoAdapter arduino;
    OnChangeModuleModeUseCase modeUseCase(arduino, g_moduleConfig, g_logger, g_debugSystem,
                                          &g_timeAdapter);
    modeUseCase.loop();

    TEST_ASSERT_FALSE(arduino.hardResetCalled);
    TEST_ASSERT_FALSE(arduino.restartCalled);
}

#endif // PREVENTIVE_RESTART_DISABLED

extern "C" void run_preventive_restart_use_case_tests() {
#ifdef PREVENTIVE_RESTART_DISABLED
    RUN_TEST(test_preventive_restart_not_requested_under_build_flag);
    RUN_TEST(test_preventive_restart_does_not_reach_a_reset_under_build_flag);
#else
    // Each scenario below describes a decision a build with PREVENTIVE_RESTART_DISABLED never
    // makes: it either expects the restart to be requested, or expects one of the conditions to
    // have been asked about at all.
    RUN_TEST(test_preventive_restart_triggers_when_safe);
    RUN_TEST(test_preventive_restart_triggers_at_window_end);
    RUN_TEST(test_preventive_restart_skipped_outside_window);
    RUN_TEST(test_preventive_restart_skipped_already_today);
    RUN_TEST(test_preventive_restart_skipped_device_on);
    RUN_TEST(test_preventive_restart_skipped_device_in_delay_off);
    RUN_TEST(test_preventive_restart_triggers_with_device_in_delay_on);
    RUN_TEST(test_preventive_restart_skipped_rule_starts_soon);
    RUN_TEST(test_preventive_restart_skipped_ota_in_progress);
    RUN_TEST(test_preventive_restart_skipped_not_configured);
#endif
}
