#include <unity.h>

// External test functions
extern "C" void run_mock_settings_tests(void);
extern "C" int run_module_config_tests(void);
extern "C" int run_module_setup_service_tests(void);
extern "C" int run_led_use_cases_tests(void);
extern "C" int run_button_use_cases_tests(void);
extern "C" int run_loop_service_factory_tests(void);
extern "C" int run_loop_services_tests(void);
extern "C" void run_module_structure_tests(void);

static void setUp_module_config(void) {
    // Setup for each test
}

static void tearDown_module_config(void) {
    // Clean up after each test
}

// Example of a test that doesn't require hardware
static void test_simple_math(void) {
    setUp_module_config();
    TEST_ASSERT_EQUAL(2 + 2, 4);
    TEST_ASSERT_NOT_EQUAL(2 + 2, 5);
    tearDown_module_config();
}

int main(void) {
    UNITY_BEGIN();
    
    // Run tests defined in this file
    RUN_TEST(test_simple_math);
    
    // Run tests from the MockSettings test file
    run_mock_settings_tests();
    
    // Run tests from the ModuleConfig test file
    run_module_config_tests();
    
    // Run tests from the ModuleSetupService test file
    run_module_setup_service_tests();
    
    // Run tests from the LedUseCases test file
    run_led_use_cases_tests();
    
    // Run tests from the ButtonUseCases test file
    run_button_use_cases_tests();
    
    // Run tests from the LoopServiceFactory test file
    run_loop_service_factory_tests();
    
    // Run tests from the LoopServices test file
    run_loop_services_tests();
    
    // Run tests for ModuleStructure
    run_module_structure_tests();
    
    return UNITY_END();
}