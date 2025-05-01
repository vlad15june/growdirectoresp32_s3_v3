#include <unity.h>
#include "Mock/ArduinoMock.h"
#include "Application/Services/ModuleSetupService.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Led/MockLedAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Core/Domain/MockLedUseCases.h"

// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockModuleConfig g_mockModuleConfig;
static MockLedAdapter g_mockLedAdapter;
static MockLedUseCases g_mockLedUseCases;
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
    
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        &g_mockLedUseCases
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
        &g_mockLedUseCases
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
        &g_mockLedUseCases
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
        nullptr
    );
    
    // This should not crash and should still initialize the config and LEDs
    g_moduleSetupService->setupModule();
    
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    TEST_ASSERT_TRUE(g_mockLedAdapter.setupModuleLedsCalled);
    
    module_setup_service_teardown();
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
    
    if (debug_file != nullptr) fprintf(debug_file, "\n----- ModuleSetupService Tests Complete -----\n");
    
    return 0;
}
