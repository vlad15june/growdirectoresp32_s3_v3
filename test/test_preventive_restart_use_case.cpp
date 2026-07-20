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

void test_preventive_restart_triggers_when_safe() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    setTime(2, 14, 15);

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

void test_preventive_restart_skipped_outside_window() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    setTime(3, 14, 15);

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
    setTime(2, 30, 15);

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
    setTime(2, 30, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_ON;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
}

void test_preventive_restart_skipped_rule_starts_soon() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    setTime(2, 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_evaluateRules.setWillTurnOnWithinMinutes(true);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(CONFIGURED_DONE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
    TEST_ASSERT_EQUAL_INT(5, g_evaluateRules.getLastMinutes());
}

void test_preventive_restart_skipped_ota_in_progress() {
    resetAll();
    setupUseCase();

    g_moduleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    setTime(2, 14, 15);

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
    setTime(2, 14, 15);

    g_device.deviceUid = "relay_1";
    g_device.state = DEVICE_STATE_OFF;
    g_deviceModel.addDevice(&g_device);

    g_useCase->perform();

    TEST_ASSERT_EQUAL_INT(USER_CAN_CONFIGURE, g_moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.getBootType());
}

extern "C" int run_preventive_restart_use_case_tests() {
    UNITY_BEGIN();

    RUN_TEST(test_preventive_restart_triggers_when_safe);
    RUN_TEST(test_preventive_restart_skipped_outside_window);
    RUN_TEST(test_preventive_restart_skipped_already_today);
    RUN_TEST(test_preventive_restart_skipped_device_on);
    RUN_TEST(test_preventive_restart_skipped_rule_starts_soon);
    RUN_TEST(test_preventive_restart_skipped_ota_in_progress);
    RUN_TEST(test_preventive_restart_skipped_not_configured);

    return UNITY_END();
}
