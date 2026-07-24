#include <unity.h>
#include "Core/Domain/UseCases/Led/LedUseCases.h"
#include "Core/Domain/UseCases/Led/LedUseCases.cpp"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Led/MockLedAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Core/Domain/MockNetworkStatusProvider.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"

class TestDeviceModel : public IDeviceModel {
public:
    std::vector<Device*> devices;

    bool init() override { return true; }
    Device* getDeviceByUid(const char*) override { return nullptr; }
    std::vector<Device*> getAllDevices() override { return devices; }
    int getDeviceCount() const override { return (int)devices.size(); }
    bool addDevice(Device* device) override { devices.push_back(device); return true; }
    bool removeDevice(const char*) override { return false; }
    void clearDevices() override {
        for (Device* d : devices) delete d;
        devices.clear();
    }

    Device* addWithState(DeviceStateEnum state) {
        Device* d = new Device();
        d->state = state;
        devices.push_back(d);
        return d;
    }

    ~TestDeviceModel() override { clearDevices(); }
};

// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockModuleConfig g_mockModuleConfig;
static MockSettings g_mockSettings;
static MockLedAdapter g_mockLedAdapter;
static MockArduinoAdapter g_mockArduinoAdapter;
static LedUseCases *g_ledUseCases;
static FILE *debug_file = nullptr;

// Test setup and teardown
static void led_use_cases_setup(void)
{
    // Open debug log file in append mode
    debug_file = fopen("led_use_cases_test_debug.log", "a");
    if (debug_file == nullptr)
    {
        printf("ERROR: Could not open debug log file\n");
    }
    else
    {
        fprintf(debug_file, "--- Test Started ---\n");
    }

    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockDebugSystem.featureEnabled = true;
    g_mockModuleConfig.reset();
    g_mockSettings.setConfigured(0);
    g_mockModuleConfig.setSettings(&g_mockSettings);
    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.reset();

    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem);
}

static void led_use_cases_teardown(void)
{
    delete g_ledUseCases;
    g_ledUseCases = nullptr;

    if (debug_file != nullptr)
    {
        fprintf(debug_file, "--- Test Finished ---\n\n");
        fclose(debug_file);
        debug_file = nullptr;
    }
}

// Test functions
static void test_led_on_load(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_led_on_load\n");
    led_use_cases_setup();

    if (debug_file != nullptr)
        fprintf(debug_file, "Calling ledOnLoad()\n");
    g_ledUseCases->ledOnLoad();

    if (debug_file != nullptr)
        fprintf(debug_file, "Checking if setYellowLedOn was called: %s, index=%d\n",
                g_mockLedAdapter.setYellowLedOnCalled ? "Yes" : "No", g_mockLedAdapter.lastYellowLedIndex);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setYellowLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.lastYellowLedIndex);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllYellowLedOnCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_led_on_load\n");
}

static void test_led_off_load(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_led_off_load\n");
    led_use_cases_setup();

    if (debug_file != nullptr)
        fprintf(debug_file, "Calling ledOffLoad()\n");
    g_ledUseCases->ledOffLoad();

    if (debug_file != nullptr)
        fprintf(debug_file, "Checking if setAllYellowLedOff was called: %s\n",
                g_mockLedAdapter.setAllYellowLedOffCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllYellowLedOffCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_led_off_load\n");
}

static void test_null_dependencies_handle_gracefully(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_null_dependencies_handle_gracefully\n");
    led_use_cases_setup();

    // Create a new use case with null logger and debug system
    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        nullptr,
        nullptr);

    // This should not crash
    if (debug_file != nullptr)
        fprintf(debug_file, "Calling ledOnLoad() with null logger/debugSystem\n");
    g_ledUseCases->ledOnLoad();

    if (debug_file != nullptr)
        fprintf(debug_file, "Checking if setYellowLedOn was called: %s, index=%d\n",
                g_mockLedAdapter.setYellowLedOnCalled ? "Yes" : "No", g_mockLedAdapter.lastYellowLedIndex);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setYellowLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.lastYellowLedIndex);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_null_dependencies_handle_gracefully\n");
}

static void test_blinking_mode_toggles_red_led(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_blinking_mode_toggles_red_led\n");
    led_use_cases_setup();

    g_ledUseCases->setBlinking(true);
    TEST_ASSERT_TRUE(g_ledUseCases->isBlinking());

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(600);
    g_ledUseCases->loop();
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOnCalled);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllGreenLedOffCalled);

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(1300);
    g_ledUseCases->loop();
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOffCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_blinking_mode_toggles_red_led\n");
}

static void test_blinking_mode_overrides_config_state(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_blinking_mode_overrides_config_state\n");
    led_use_cases_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_ledUseCases->setBlinking(true);
    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(600);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllGreenLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.greenLedOnCount);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_blinking_mode_overrides_config_state\n");
}

static void test_configured_done_offline_lights_solid_red(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_offline_lights_solid_red\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = true;

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        nullptr,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllGreenLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.greenLedOnCount);

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(3000);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllGreenLedOnCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_offline_lights_solid_red\n");
}

static void test_configured_done_online_blinks_green(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_online_blinks_green\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = false;

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        nullptr,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(5001);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllGreenLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOnCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_online_blinks_green\n");
}

static void test_configured_done_offline_device_on_lights_green(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_offline_device_on_lights_green\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = true;

    TestDeviceModel deviceModel;
    deviceModel.addWithState(DEVICE_STATE_ON);

    g_mockModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_mockModuleConfig.setLedsCount(1);

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &deviceModel,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setGreenLedOnCalled);
    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.lastGreenLedIndex);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setRedLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOnCalled);

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(1001);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setRedLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.lastRedLedIndex);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setGreenLedOffCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.greenLedOnCount);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_offline_device_on_lights_green\n");
}

static void test_configured_done_offline_device_off_stays_red(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_offline_device_off_stays_red\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = true;

    TestDeviceModel deviceModel;
    deviceModel.addWithState(DEVICE_STATE_OFF);

    g_mockModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_mockModuleConfig.setLedsCount(1);

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &deviceModel,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setRedLedOnCalled);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setGreenLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setGreenLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.greenLedOnCount);

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(1001);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setRedLedOnCalled);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setGreenLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setGreenLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.greenLedOnCount);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_offline_device_off_stays_red\n");
}

static void test_configured_done_offline_mixed_devices_use_per_index(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_offline_mixed_devices_use_per_index\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = true;

    TestDeviceModel deviceModel;
    deviceModel.addWithState(DEVICE_STATE_ON);
    deviceModel.addWithState(DEVICE_STATE_OFF);

    g_mockModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_mockModuleConfig.setLedsCount(2);

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &deviceModel,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOffCount);
    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.redLedOnCount);
    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.redLedOffCount);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllGreenLedOnCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_offline_mixed_devices_use_per_index\n");
}

static void test_configured_done_online_device_on_stays_green(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_online_device_on_stays_green\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = false;

    TestDeviceModel deviceModel;
    deviceModel.addWithState(DEVICE_STATE_ON);
    deviceModel.addWithState(DEVICE_STATE_OFF);

    g_mockModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_mockModuleConfig.setLedsCount(2);

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &deviceModel,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(5001);
    g_ledUseCases->loop();

    TEST_ASSERT_EQUAL(2, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOffCalled);

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(6002);
    g_ledUseCases->loop();

    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOffCount);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_online_device_on_stays_green\n");
}

static void test_blinking_mode_off_restores_normal(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_blinking_mode_off_restores_normal\n");
    led_use_cases_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_ledUseCases->setBlinking(true);
    g_ledUseCases->setBlinking(false);
    TEST_ASSERT_FALSE(g_ledUseCases->isBlinking());

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(2000);
    g_ledUseCases->loop();

    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOnCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_blinking_mode_off_restores_normal\n");
}

static void test_user_can_configure_blinks_yellow_first_led(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_user_can_configure_blinks_yellow_first_led\n");
    led_use_cases_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(600);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setYellowLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.lastYellowLedIndex);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOffCalled);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllGreenLedOffCalled);

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(1200);
    g_ledUseCases->loop();

    TEST_ASSERT_FALSE(g_mockLedAdapter.setYellowLedOnCalled);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOffCalled);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllGreenLedOffCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_user_can_configure_blinks_yellow_first_led\n");
}

static void test_user_can_re_configure_blinks_green_first_led(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_user_can_re_configure_blinks_green_first_led\n");
    led_use_cases_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_RE_CONFIGURE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(600);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setGreenLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.lastGreenLedIndex);
    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setYellowLedOnCalled);

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(1200);
    g_ledUseCases->loop();

    TEST_ASSERT_FALSE(g_mockLedAdapter.setGreenLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllGreenLedOffCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_user_can_re_configure_blinks_green_first_led\n");
}

static void test_ap_test_mode_is_noop(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_ap_test_mode_is_noop\n");
    led_use_cases_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::AP_TEST_MODE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();
    g_mockArduinoAdapter.setMillis(6000);
    g_ledUseCases->loop();

    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllGreenLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllGreenLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setGreenLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setRedLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setYellowLedOnCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_ap_test_mode_is_noop\n");
}

static void test_unknown_config_status_turns_all_off(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_unknown_config_status_turns_all_off\n");
    led_use_cases_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_NEED_RESTART);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOffCalled);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllGreenLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOnCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllGreenLedOnCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_unknown_config_status_turns_all_off\n");
}

static void test_configured_done_offline_dimmer_subtype_uses_per_device(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_offline_dimmer_subtype_uses_per_device\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = true;

    TestDeviceModel deviceModel;
    deviceModel.addWithState(DEVICE_STATE_ON);

    g_mockModuleConfig.setModuleType(MODULE_TYPE_SENSOR);
    g_mockModuleConfig.setModuleSubType(MODULE_SUBTYPE_DIMMER);
    g_mockModuleConfig.setLedsCount(1);

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &deviceModel,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setGreenLedOnCalled);
    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setRedLedOffCalled);
    TEST_ASSERT_FALSE(g_mockLedAdapter.setAllRedLedOnCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_offline_dimmer_subtype_uses_per_device\n");
}

static void test_configured_done_online_hydro_device_on_solid_green(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_online_hydro_device_on_solid_green\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = false;

    TestDeviceModel deviceModel;
    deviceModel.addWithState(DEVICE_STATE_ON);

    g_mockModuleConfig.setModuleType(MODULE_TYPE_HYDRO);
    g_mockModuleConfig.setLedsCount(1);

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &deviceModel,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    g_mockLedAdapter.reset();
    g_mockArduinoAdapter.setMillis(5001);
    g_ledUseCases->loop();

    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.greenLedOffCount);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setAllRedLedOffCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_online_hydro_device_on_solid_green\n");
}

static void test_configured_done_offline_step_on_treated_as_on(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "Starting test_configured_done_offline_step_on_treated_as_on\n");
    led_use_cases_setup();

    MockNetworkStatusProvider netStatus;
    netStatus.offline = true;

    TestDeviceModel deviceModel;
    deviceModel.addWithState(DEVICE_STATE_STEP_ON);

    g_mockModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_mockModuleConfig.setLedsCount(1);

    delete g_ledUseCases;
    g_ledUseCases = new LedUseCases(
        &g_mockLedAdapter,
        &g_mockModuleConfig,
        &g_mockArduinoAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &deviceModel,
        &netStatus);

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_mockArduinoAdapter.setMillis(0);
    g_ledUseCases->loop();

    TEST_ASSERT_TRUE(g_mockLedAdapter.setGreenLedOnCalled);
    TEST_ASSERT_EQUAL(1, g_mockLedAdapter.greenLedOnCount);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setRedLedOffCalled);

    led_use_cases_teardown();
    if (debug_file != nullptr)
        fprintf(debug_file, "Finished test_configured_done_offline_step_on_treated_as_on\n");
}

// Main test runner function
extern "C" int run_led_use_cases_tests(void)
{
    if (debug_file != nullptr)
        fprintf(debug_file, "\n----- Running LedUseCases Tests -----\n");

    RUN_TEST(test_led_on_load);
    RUN_TEST(test_led_off_load);
    RUN_TEST(test_null_dependencies_handle_gracefully);
    RUN_TEST(test_blinking_mode_toggles_red_led);
    RUN_TEST(test_blinking_mode_overrides_config_state);
    RUN_TEST(test_blinking_mode_off_restores_normal);
    RUN_TEST(test_configured_done_offline_lights_solid_red);
    RUN_TEST(test_configured_done_offline_device_on_lights_green);
    RUN_TEST(test_configured_done_offline_device_off_stays_red);
    RUN_TEST(test_configured_done_offline_mixed_devices_use_per_index);
    RUN_TEST(test_configured_done_online_device_on_stays_green);
    RUN_TEST(test_configured_done_online_blinks_green);
    RUN_TEST(test_user_can_configure_blinks_yellow_first_led);
    RUN_TEST(test_user_can_re_configure_blinks_green_first_led);
    RUN_TEST(test_ap_test_mode_is_noop);
    RUN_TEST(test_unknown_config_status_turns_all_off);
    RUN_TEST(test_configured_done_offline_dimmer_subtype_uses_per_device);
    RUN_TEST(test_configured_done_online_hydro_device_on_solid_green);
    RUN_TEST(test_configured_done_offline_step_on_treated_as_on);

    if (debug_file != nullptr)
        fprintf(debug_file, "\n----- LedUseCases Tests Complete -----\n");

    return 0;
}
