#include <unity.h>
#include "Mock/ArduinoMock.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"


// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockSettings g_mockSettings;
static MockNTPTimeAdapter g_mockTimeAdapter;
static ModuleConfig* g_moduleConfig;

// Test setup and teardown - make these static to avoid conflicts
static void module_config_setup(void) {
    g_mockLogger.printCalled = false;
    g_mockDebugSystem.featureEnabled = true;
    g_mockSettings.reset();
    g_mockTimeAdapter.reset();
    g_moduleConfig = new ModuleConfig(&g_mockLogger, &g_mockDebugSystem, &g_mockSettings, &g_mockTimeAdapter);
}

static void module_config_teardown(void) {
    delete g_moduleConfig;
    g_moduleConfig = nullptr;
}

// Test functions - make these static too
static void test_default_module_types(void) {
    module_config_setup();
    TEST_ASSERT_EQUAL(MODULE_TYPE_DEVICE, g_moduleConfig->getModuleType());
    TEST_ASSERT_EQUAL(MODULE_SUBTYPE_RELAY, g_moduleConfig->getModuleSubType());
    module_config_teardown();
}

static void test_init_calls_dependencies(void) {
    module_config_setup();
    g_moduleConfig->Init();
    
    // Verify settings was initialized
    // TEST_ASSERT_TRUE(g_mockSettings.wasBeginCalled());
    
    // Verify time adapter was initialized
    TEST_ASSERT_TRUE(g_mockTimeAdapter.wasSetupCalled());
    TEST_ASSERT_TRUE(g_mockTimeAdapter.wasTimeRestored());
    module_config_teardown();
}

static void test_get_settings(void) {
    module_config_setup();
    ISettings* settings = g_moduleConfig->getSettings();
    TEST_ASSERT_EQUAL(&g_mockSettings, settings);
    module_config_teardown();
}

static void test_setters(void) {
    module_config_setup();
    // Create new instances
    MockLogger newLogger;
    MockSettings newSettings;
    MockNTPTimeAdapter newTimeAdapter;
    
    // Set new instances
    g_moduleConfig->setLogger(&newLogger);
    g_moduleConfig->setSettings(&newSettings);
    g_moduleConfig->setTimeAdapter(&newTimeAdapter);
    
    // Test initialization with new instances
    g_moduleConfig->Init();
    
    // Verify new settings was initialized
    // TEST_ASSERT_TRUE(newSettings.wasBeginCalled());
    
    // Verify new time adapter was initialized
    TEST_ASSERT_TRUE(newTimeAdapter.wasSetupCalled());
    TEST_ASSERT_TRUE(newTimeAdapter.wasTimeRestored());
    
    // Verify getter returns correct instance
    TEST_ASSERT_EQUAL(&newSettings, g_moduleConfig->getSettings());
    TEST_ASSERT_EQUAL(&newTimeAdapter, g_moduleConfig->getTimeAdapter());
    module_config_teardown();
}

// Main test runner function
extern "C" int run_module_config_tests(void) {
    printf("\n----- Running ModuleConfig Tests -----\n");
    
    RUN_TEST(test_default_module_types);
    RUN_TEST(test_init_calls_dependencies);
    RUN_TEST(test_get_settings);
    RUN_TEST(test_setters);
    
    printf("\n----- ModuleConfig Tests Complete -----\n");
    
    return 0;
}
