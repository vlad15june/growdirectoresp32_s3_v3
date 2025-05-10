#include <unity.h>
#include "Core/Domain/UseCases/ModuleMode/OnChangeModuleModeUseCase.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"

// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockArduinoAdapter g_mockArduinoAdapter;
static MockModuleConfig g_mockModuleConfig;
static MockSettings g_mockSettings;
static OnChangeModuleModeUseCase* g_onChangeModuleModeUseCase;

// Test setup and teardown
void on_change_module_mode_use_case_setup(void) {
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockArduinoAdapter.reset();
    g_mockModuleConfig.reset();
    g_mockSettings.reset();
    
    // Enable debug logging for module mode
    g_mockDebugSystem.featureEnabled = true;
    
    // Set up module config with settings
    g_mockModuleConfig.setSettings(&g_mockSettings);
    
    // Create the use case
    g_onChangeModuleModeUseCase = new OnChangeModuleModeUseCase(
        g_mockArduinoAdapter,
        g_mockModuleConfig,
        g_mockLogger,
        g_mockDebugSystem);
}

void on_change_module_mode_use_case_teardown(void) {
    delete g_onChangeModuleModeUseCase;
    g_onChangeModuleModeUseCase = nullptr;
}

// Test functions
void test_loop_when_no_mode_change(void) {
    on_change_module_mode_use_case_setup();
    
    // Set the requested mode to the default mode (CONFIGURED_DONE)
    g_mockSettings.setConfigured(CONFIGURED_DONE);
    g_mockModuleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    
    // Run the loop
    g_onChangeModuleModeUseCase->loop();
    
    // Verify no restart was called
    TEST_ASSERT_FALSE(g_mockArduinoAdapter.restartCalled);
    
    // Verify the boot type wasn't changed
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("bootType"));
    
    on_change_module_mode_use_case_teardown();
}

void test_loop_when_mode_changes_to_need_restart(void) {
    on_change_module_mode_use_case_setup();
    
    // Set the requested mode to CONFIGURED_NEED_RESTART
    g_mockModuleConfig.setRequestedModuleMode(CONFIGURED_NEED_RESTART);
    
    // Run the loop
    g_onChangeModuleModeUseCase->loop();
    
    // Verify boot type was set to USER
    TEST_ASSERT_EQUAL(BOOT_TYPE_USER, g_mockSettings.getBootType());
    
    // Verify delay was called before restart (for settings to save)
    TEST_ASSERT_EQUAL(2, g_mockArduinoAdapter.delayCount);
    TEST_ASSERT_EQUAL(1000, g_mockArduinoAdapter.lastDelayMs);
    
    // Verify restart was called
    TEST_ASSERT_TRUE(g_mockArduinoAdapter.restartCalled);
    
    // Verify that mode was NOT saved to settings
    TEST_ASSERT_NOT_EQUAL(CONFIGURED_NEED_RESTART, g_mockSettings.getIfConfigured());
    
    on_change_module_mode_use_case_teardown();
}

void test_loop_when_mode_changes_to_user_can_configure(void) {
    on_change_module_mode_use_case_setup();
    
    // Set the requested mode to USER_CAN_CONFIGURE
    g_mockModuleConfig.setRequestedModuleMode(USER_CAN_CONFIGURE);
    
    // Run the loop
    g_onChangeModuleModeUseCase->loop();
    
    // Verify mode was saved in settings
    TEST_ASSERT_EQUAL(USER_CAN_CONFIGURE, g_mockSettings.getIfConfigured());
    
    // Verify boot type was set to USER
    TEST_ASSERT_EQUAL(BOOT_TYPE_USER, g_mockSettings.getBootType());
    
    // Verify delay was called before restart (for settings to save)
    TEST_ASSERT_EQUAL(2, g_mockArduinoAdapter.delayCount);
    TEST_ASSERT_EQUAL(1000, g_mockArduinoAdapter.lastDelayMs);
    
    // Verify restart was called
    TEST_ASSERT_TRUE(g_mockArduinoAdapter.restartCalled);
    
    on_change_module_mode_use_case_teardown();
}

void test_loop_when_mode_changes_to_user_can_reconfigure(void) {
    on_change_module_mode_use_case_setup();
    
    // Set the requested mode to USER_CAN_RE_CONFIGURE
    g_mockModuleConfig.setRequestedModuleMode(USER_CAN_RE_CONFIGURE);
    
    // Run the loop
    g_onChangeModuleModeUseCase->loop();
    
    // Verify mode was saved in settings
    TEST_ASSERT_EQUAL(USER_CAN_RE_CONFIGURE, g_mockSettings.getIfConfigured());
    
    // Verify boot type was set to USER
    TEST_ASSERT_EQUAL(BOOT_TYPE_USER, g_mockSettings.getBootType());
    
    // Verify delay was called before restart (for settings to save)
    TEST_ASSERT_EQUAL(2, g_mockArduinoAdapter.delayCount);
    TEST_ASSERT_EQUAL(1000, g_mockArduinoAdapter.lastDelayMs);
    
    // Verify restart was called
    TEST_ASSERT_TRUE(g_mockArduinoAdapter.restartCalled);
    
    on_change_module_mode_use_case_teardown();
}

void test_loop_when_mode_changes_to_ap_test_mode(void) {
    on_change_module_mode_use_case_setup();
    
    // Set the requested mode to AP_TEST_MODE
    g_mockModuleConfig.setRequestedModuleMode(AP_TEST_MODE);
    
    // Run the loop
    g_onChangeModuleModeUseCase->loop();
    
    // Verify mode was saved in settings
    TEST_ASSERT_EQUAL(AP_TEST_MODE, g_mockSettings.getIfConfigured());
    
    // Verify boot type was set to USER
    TEST_ASSERT_EQUAL(BOOT_TYPE_USER, g_mockSettings.getBootType());
    
    // Verify delay was called before restart (for settings to save)
    TEST_ASSERT_EQUAL(2, g_mockArduinoAdapter.delayCount);
    TEST_ASSERT_EQUAL(1000, g_mockArduinoAdapter.lastDelayMs);
    
    // Verify restart was called
    TEST_ASSERT_TRUE(g_mockArduinoAdapter.restartCalled);
    
    on_change_module_mode_use_case_teardown();
}

void test_loop_when_mode_changes_to_configured_done(void) {
    on_change_module_mode_use_case_setup();
    
    // First set current mode to something else
    g_onChangeModuleModeUseCase = new OnChangeModuleModeUseCase(
        g_mockArduinoAdapter,
        g_mockModuleConfig,
        g_mockLogger,
        g_mockDebugSystem);
    
    g_mockModuleConfig.setCurrentModuleMode(CONFIGURED_DONE);
    
    // Set the requested mode to USER_CAN_CONFIGURE (different from current)
    g_mockModuleConfig.setRequestedModuleMode(USER_CAN_CONFIGURE);
    
    // Run the loop once to change internal current mode
    g_onChangeModuleModeUseCase->loop();
    
    // Reset mocks
    g_mockArduinoAdapter.reset();
    g_mockSettings.reset();
    
    // Now set the requested mode to CONFIGURED_DONE
    g_mockModuleConfig.setRequestedModuleMode(CONFIGURED_DONE);
    
    // Run the loop again
    g_onChangeModuleModeUseCase->loop();
    
    // Verify mode was saved in settings
    TEST_ASSERT_EQUAL(CONFIGURED_DONE, g_mockSettings.getIfConfigured());
    
    // Verify boot type was set to USER
    TEST_ASSERT_EQUAL(BOOT_TYPE_USER, g_mockSettings.getBootType());
    
    // Verify delay was called before restart (for settings to save)
    TEST_ASSERT_EQUAL(2, g_mockArduinoAdapter.delayCount);
    TEST_ASSERT_EQUAL(1000, g_mockArduinoAdapter.lastDelayMs);
    
    // Verify restart was called
    TEST_ASSERT_TRUE(g_mockArduinoAdapter.restartCalled);
    
    on_change_module_mode_use_case_teardown();
}

// Main test runner function
extern "C" int run_on_change_module_mode_use_case_tests(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_loop_when_no_mode_change);
    RUN_TEST(test_loop_when_mode_changes_to_need_restart);
    RUN_TEST(test_loop_when_mode_changes_to_user_can_configure);
    RUN_TEST(test_loop_when_mode_changes_to_user_can_reconfigure);
    RUN_TEST(test_loop_when_mode_changes_to_ap_test_mode);
    RUN_TEST(test_loop_when_mode_changes_to_configured_done);
    
    return UNITY_END();
}
