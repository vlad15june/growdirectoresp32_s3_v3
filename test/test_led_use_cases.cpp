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
        fprintf(debug_file, "Checking if setAllYellowLedOn was called: %s, index=%d\n",
                g_mockLedAdapter.setYellowLedOnCalled ? "Yes" : "No", g_mockLedAdapter.lastYellowLedIndex);
    TEST_ASSERT_TRUE(g_mockLedAdapter.setYellowLedOnCalled);
    TEST_ASSERT_EQUAL(0, g_mockLedAdapter.lastYellowLedIndex);

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

    // Verify LED adapter was still called
    if (debug_file != nullptr)
        fprintf(debug_file, "Checking if setAllYellowLedOn was called: %s, index=%d\n",
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

    if (debug_file != nullptr)
        fprintf(debug_file, "\n----- LedUseCases Tests Complete -----\n");

    return 0;
}
