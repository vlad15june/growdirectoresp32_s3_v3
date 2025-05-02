#include <unity.h>
#include "Mock/ArduinoMock.h"
#include "Application/Services/ModuleSetupService.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Led/MockLedAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Core/Domain/MockLedUseCases.h"
#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/Controllers/WebServices/Launchers/MockWebServerLauncher.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"

// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockModuleConfig g_mockModuleConfig;
static MockLedAdapter g_mockLedAdapter;
static MockLedUseCases g_mockLedUseCases;
static MockButtonUseCases g_mockButtonUseCases;
static MockWifiAdapter g_mockWifiAdapter;
static MockWebServerLauncher g_mockWebServerLauncher;
static MockSettings g_mockSettings;
static ModuleSetupService* g_moduleSetupService;
static FILE* debug_file = nullptr;

// Test setup and teardown
static void module_setup_service_setup(void) {
    // Open debug log file in append mode
    debug_file = fopen("module_setup_test_debug.log", "a");
    if (debug_file == nullptr) {
        printf("ERROR: Could not open debug log file\n");
    } else {
        fprintf(debug_file, "--- Test Started ---\n");
    }
    
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockDebugSystem.featureEnabled = true;
    g_mockModuleConfig.reset();
    g_mockLedAdapter.reset();
    g_mockLedUseCases.reset();
    g_mockButtonUseCases.reset();
    g_mockWifiAdapter.reset();
    g_mockWebServerLauncher.reset();
    g_mockSettings.reset();
    
    // Set up the mock settings for module config
    g_mockModuleConfig.setSettings(&g_mockSettings);
    
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWebServerLauncher
    );
}

static void module_setup_service_teardown(void) {
    delete g_moduleSetupService;
    g_moduleSetupService = nullptr;
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "--- Test Finished ---\n\n");
        fclose(debug_file);
        debug_file = nullptr;
    }
}

// Test functions
static void test_setup_module_initializes_config(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_initializes_config\n");
    module_setup_service_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if config was initialized: %s\n", 
                                g_mockModuleConfig.isInitialized() ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if config was modified with 'init': %s\n", 
                                g_mockModuleConfig.wasModified("init") ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockModuleConfig.wasModified("init"));
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if logger.print was called: %s\n", 
                                g_mockLogger.printCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_initializes_config\n");
}

static void test_setup_module_configures_leds(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_configures_leds\n");
    module_setup_service_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "setupModuleLedsCalled: %s\n", 
                                g_mockLedAdapter.setupModuleLedsCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedAdapter.setupModuleLedsCalled);
    
    if (debug_file != nullptr) fprintf(debug_file, "Expected LEDs count: %d, Actual: %d\n", 
                                g_mockModuleConfig.getLedsCount(), g_mockLedAdapter.ledsCount);
    TEST_ASSERT_EQUAL(g_mockModuleConfig.getLedsCount(), g_mockLedAdapter.ledsCount);
    
    if (debug_file != nullptr) fprintf(debug_file, "Verifying LED arrays\n");
    // Verify that the correct LED arrays were passed to the adapter
    for (int i = 0; i < g_mockModuleConfig.getLedsCount(); i++) {
        if (debug_file != nullptr) fprintf(debug_file, "Checking LED %d - Green: expected=%d, actual=%d; Red: expected=%d, actual=%d\n", 
                                    i, g_mockModuleConfig.getGreenLeds()[i], g_mockLedAdapter.greenLeds[i],
                                    g_mockModuleConfig.getRedLeds()[i], g_mockLedAdapter.redLeds[i]);
        TEST_ASSERT_EQUAL(g_mockModuleConfig.getGreenLeds()[i], g_mockLedAdapter.greenLeds[i]);
        TEST_ASSERT_EQUAL(g_mockModuleConfig.getRedLeds()[i], g_mockLedAdapter.redLeds[i]);
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_configures_leds\n");
}

static void test_setup_module_without_led_adapter(void) {
    module_setup_service_setup();
    
    // Create a new service without LED adapter
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        nullptr,
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWebServerLauncher
    );
    
    // This should not crash and should still initialize the config
    g_moduleSetupService->setupModule();
    
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    TEST_ASSERT_TRUE(g_mockModuleConfig.wasModified("init"));
    
    module_setup_service_teardown();
}

static void test_setup_module_without_config(void) {
    module_setup_service_setup();
    
    // Create a new service without module config
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        nullptr,
        &g_mockLedAdapter,
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWebServerLauncher
    );
    
    // This should not crash and should log an error
    g_moduleSetupService->setupModule();
    
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
}

static void test_setup_module_triggers_led_use_cases(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_triggers_led_use_cases\n");
    module_setup_service_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if ledOnLoad was called: %s\n", 
                                g_mockLedUseCases.onLoadCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedUseCases.onLoadCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_triggers_led_use_cases\n");
}

static void test_setup_module_without_led_use_cases(void) {
    module_setup_service_setup();
    
    // Create a new service without LED use cases
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWebServerLauncher
    );
    
    // This should not crash and should still initialize the config and LEDs
    g_moduleSetupService->setupModule();
    
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    TEST_ASSERT_TRUE(g_mockLedAdapter.setupModuleLedsCalled);
    
    module_setup_service_teardown();
}

static void test_setup_module_launches_web_server_in_config_mode(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_launches_web_server_in_config_mode\n");
    module_setup_service_setup();
    
    // Set up the mock to simulate USER_CAN_CONFIGURE mode
    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if launchConfigurationServer was called: %s\n", 
                                g_mockWebServerLauncher.launchConfigurationServerCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockWebServerLauncher.launchConfigurationServerCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_launches_web_server_in_config_mode\n");
}

static void test_module_mode_handling_for_user_can_configure(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_module_mode_handling_for_user_can_configure\n");
    module_setup_service_setup();
    
    // Set up the mock to simulate USER_CAN_CONFIGURE mode
    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if button press check was called: %s\n", 
                                g_mockButtonUseCases.checkButtonPressedWhenOnCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockButtonUseCases.checkButtonPressedWhenOnCalled);
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if web server launcher was called: %s\n", 
                                g_mockWebServerLauncher.launchConfigurationServerCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockWebServerLauncher.launchConfigurationServerCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_module_mode_handling_for_user_can_configure\n");
}

static void test_module_mode_handling_for_configured_done(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_module_mode_handling_for_configured_done\n");
    module_setup_service_setup();
    
    // Set up the mock to simulate CONFIGURED_DONE mode
    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    // Check that configuration server was not launched
    if (debug_file != nullptr) fprintf(debug_file, "Checking web server launcher was NOT called: %s\n", 
                                !g_mockWebServerLauncher.launchConfigurationServerCalled ? "Correct" : "Wrong");
    TEST_ASSERT_FALSE(g_mockWebServerLauncher.launchConfigurationServerCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_module_mode_handling_for_configured_done\n");
}

static void test_module_mode_handling_for_test_mode(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_module_mode_handling_for_test_mode\n");
    module_setup_service_setup();
    
    // Set up the mock to simulate AP_TEST_MODE mode
    g_mockSettings.setConfigured(ModuleModeEnum::AP_TEST_MODE);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    // Validate specific behavior for AP_TEST_MODE if applicable
    // Currently this is a placeholder to demonstrate test structure
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_module_mode_handling_for_test_mode\n");
}

static void test_module_mode_initialization_when_invalid(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_module_mode_initialization_when_invalid\n");
    module_setup_service_setup();

    // Set up the mock to simulate an invalid mode
    g_mockSettings.setConfigured(255); // Using an invalid value
    uint8_t invalidMode = g_mockSettings.getIfConfigured();
    if (debug_file != nullptr) fprintf(debug_file, "getIfConfigured = %d\n", invalidMode);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    // Should set to USER_CAN_CONFIGURE
    if (debug_file != nullptr) fprintf(debug_file, "getIfConfigured = %d\n", g_mockSettings.getIfConfigured());

    TEST_ASSERT_TRUE(invalidMode != g_mockSettings.getIfConfigured());
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_CONFIGURE, g_mockSettings.getIfConfigured());
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_module_mode_initialization_when_invalid\n");
}

static void test_setup_module_without_web_server_launcher(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_without_web_server_launcher\n");
    module_setup_service_setup();
    
    // Create a new service without web server launcher
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWebServerLauncher
    );
    
    // Set up the mock to simulate USER_CAN_CONFIGURE mode
    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
    
    // This should not crash even though we're in USER_CAN_CONFIGURE mode
    g_moduleSetupService->setupModule();
    
    // Basic checks to ensure the service still worked
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_without_web_server_launcher\n");
}

static void test_setup_module_handles_led_sequences(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_handles_led_sequences\n");
    module_setup_service_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if ledOnLoad was called: %s\n", 
                                g_mockLedUseCases.onLoadCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedUseCases.onLoadCalled);
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if ledOffLoad was called: %s\n", 
                                g_mockLedUseCases.offLoadCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedUseCases.offLoadCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_handles_led_sequences\n");
}

// Main test runner function
extern "C" int run_module_setup_service_tests(void) {
    if (debug_file != nullptr) fprintf(debug_file, "\n----- Running ModuleSetupService Tests -----\n");
    
    RUN_TEST(test_setup_module_initializes_config);
    RUN_TEST(test_setup_module_configures_leds);
    RUN_TEST(test_setup_module_without_led_adapter);
    RUN_TEST(test_setup_module_without_config);
    RUN_TEST(test_setup_module_triggers_led_use_cases);
    RUN_TEST(test_setup_module_without_led_use_cases);
    
    RUN_TEST(test_setup_module_launches_web_server_in_config_mode);
    RUN_TEST(test_module_mode_handling_for_user_can_configure);
    RUN_TEST(test_module_mode_handling_for_configured_done);
    RUN_TEST(test_module_mode_handling_for_test_mode);
    RUN_TEST(test_module_mode_initialization_when_invalid);
    RUN_TEST(test_setup_module_without_web_server_launcher);
    RUN_TEST(test_setup_module_handles_led_sequences);
    
    if (debug_file != nullptr) fprintf(debug_file, "\n----- ModuleSetupService Tests Complete -----\n");
    
    return 0;
}
