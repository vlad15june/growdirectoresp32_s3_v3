#include <unity.h>
#include "Mock/ArduinoMock.h"
#include "Application/Services/ModuleSetupService.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Led/MockLedAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"

// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockModuleConfig g_mockModuleConfig;
static MockLedAdapter g_mockLedAdapter;
static ModuleSetupService* g_moduleSetupService;

// Test setup and teardown
static void module_setup_service_setup(void) {
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockDebugSystem.featureEnabled = true;
    g_mockModuleConfig.reset();
    g_mockLedAdapter.reset();
    
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter
    );
}

static void module_setup_service_teardown(void) {
    delete g_moduleSetupService;
    g_moduleSetupService = nullptr;
}

// Test functions
static void test_setup_module_initializes_config(void) {
    printf("DEBUG: Starting test_setup_module_initializes_config\n");
    module_setup_service_setup();
    
    printf("DEBUG: Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    printf("DEBUG: Checking if config was initialized: %s\n", g_mockModuleConfig.isInitialized() ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    printf("DEBUG: Checking if config was modified with 'init': %s\n", g_mockModuleConfig.wasModified("init") ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockModuleConfig.wasModified("init"));
    printf("DEBUG: Checking if logger.print was called: %s\n", g_mockLogger.printCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    printf("DEBUG: Finished test_setup_module_initializes_config\n");
}

static void test_setup_module_configures_leds(void) {
    printf("DEBUG: Starting test_setup_module_configures_leds\n");
    module_setup_service_setup();
    
    printf("DEBUG: Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    printf("DEBUG: setupModuleLedsCalled: %s\n", g_mockLedAdapter.setupModuleLedsCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedAdapter.setupModuleLedsCalled);
    printf("DEBUG: Expected LEDs count: %d, Actual: %d\n", g_mockModuleConfig.getLedsCount(), g_mockLedAdapter.ledsCount);
    TEST_ASSERT_EQUAL(g_mockModuleConfig.getLedsCount(), g_mockLedAdapter.ledsCount);
    
    printf("DEBUG: Verifying LED arrays\n");
    // Verify that the correct LED arrays were passed to the adapter
    for (int i = 0; i < g_mockModuleConfig.getLedsCount(); i++) {
        printf("DEBUG: Checking LED %d - Green: expected=%d, actual=%d; Red: expected=%d, actual=%d\n", 
               i, g_mockModuleConfig.getGreenLeds()[i], g_mockLedAdapter.greenLeds[i],
               g_mockModuleConfig.getRedLeds()[i], g_mockLedAdapter.redLeds[i]);
        TEST_ASSERT_EQUAL(g_mockModuleConfig.getGreenLeds()[i], g_mockLedAdapter.greenLeds[i]);
        TEST_ASSERT_EQUAL(g_mockModuleConfig.getRedLeds()[i], g_mockLedAdapter.redLeds[i]);
    }
    
    module_setup_service_teardown();
    printf("DEBUG: Finished test_setup_module_configures_leds\n");
}

static void test_setup_module_without_led_adapter(void) {
    module_setup_service_setup();
    
    // Create a new service without LED adapter
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        nullptr
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
        &g_mockLedAdapter
    );
    
    // This should not crash and should log an error
    g_moduleSetupService->setupModule();
    
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
}

// Main test runner function
extern "C" int run_module_setup_service_tests(void) {
    printf("\n----- Running ModuleSetupService Tests -----\n");
    
    RUN_TEST(test_setup_module_initializes_config);
    RUN_TEST(test_setup_module_configures_leds);
    RUN_TEST(test_setup_module_without_led_adapter);
    RUN_TEST(test_setup_module_without_config);
    
    printf("\n----- ModuleSetupService Tests Complete -----\n");
    
    return 0;
}
