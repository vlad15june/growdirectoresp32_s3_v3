#include <unity.h>
#include "Core/Domain/UseCases/Led/LedUseCases.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Led/MockLedAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

// Global test variables
static MockLogger* mockLogger;
static MockDebugSystem* mockDebugSystem;
static MockModuleConfig* mockModuleConfig;
static MockLedAdapter* mockLedAdapter;
static MockArduinoAdapter* mockArduino;
static LedUseCases* ledUseCases;
static FILE* debug_file = nullptr;

// Test setup and teardown
static void led_use_cases_setup(void) {
    // Open debug log file in append mode
    debug_file = fopen("led_use_cases_test_debug.log", "a");
    if (debug_file == nullptr) {
        printf("ERROR: Could not open debug log file\n");
    } else {
        fprintf(debug_file, "--- Test Started ---\n");
    }
    
    mockLogger = new MockLogger();
    mockDebugSystem = new MockDebugSystem();
    mockModuleConfig = new MockModuleConfig();
    mockLedAdapter = new MockLedAdapter();
    mockArduino = new MockArduinoAdapter();
    
    // Enable LED debug to test logging
    mockDebugSystem->featureEnabled = true;

    ledUseCases = new LedUseCases(
        mockLedAdapter,
        mockModuleConfig,
        mockArduino,
        mockLogger,
        mockDebugSystem
    );
}

static void led_use_cases_teardown(void) {
    delete ledUseCases;
    delete mockArduino;
    delete mockLedAdapter;
    delete mockModuleConfig;
    delete mockDebugSystem;
    delete mockLogger;
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "--- Test Finished ---\n\n");
        fclose(debug_file);
        debug_file = nullptr;
    }
}

// Test functions
static void test_led_on_load(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_led_on_load\n");
    led_use_cases_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling ledOnLoad()\n");
    ledUseCases->ledOnLoad();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if setAllYellowLedOn was called: %s\n", 
                                mockLedAdapter->setAllYellowLedOnCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(mockLedAdapter->setAllYellowLedOnCalled);
    
    led_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_led_on_load\n");
}

static void test_led_off_load(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_led_off_load\n");
    led_use_cases_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling ledOffLoad()\n");
    ledUseCases->ledOffLoad();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if setAllYellowLedOff was called: %s\n", 
                                mockLedAdapter->setAllYellowLedOffCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(mockLedAdapter->setAllYellowLedOffCalled);
    
    led_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_led_off_load\n");
}

static void test_led_blink_green_2_times(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_led_blink_green_2_times\n");
    led_use_cases_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling ledBlinkGreen2Times()\n");
    ledUseCases->ledBlinkGreen2Times();
    
    // Verify the green LED was turned on and off twice
    if (debug_file != nullptr) fprintf(debug_file, "Checking if setGreenLedOn was called: %s, count=%d\n", 
                                mockLedAdapter->setGreenLedOnCalled ? "Yes" : "No", mockLedAdapter->greenLedOnCount);
    TEST_ASSERT_TRUE(mockLedAdapter->setGreenLedOnCalled);
    TEST_ASSERT_EQUAL(2, mockLedAdapter->greenLedOnCount);
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if setGreenLedOff was called: %s, count=%d\n", 
                                mockLedAdapter->setGreenLedOffCalled ? "Yes" : "No", mockLedAdapter->greenLedOffCount);
    TEST_ASSERT_TRUE(mockLedAdapter->setGreenLedOffCalled);
    TEST_ASSERT_EQUAL(2, mockLedAdapter->greenLedOffCount);
    
    // Verify that delays were called 4 times (on->delay->off->delay->on->delay->off)
    if (debug_file != nullptr) fprintf(debug_file, "Checking delay count: expected=4, actual=%d\n", 
                                mockArduino->delayCount);
    TEST_ASSERT_EQUAL(4, mockArduino->delayCount);
    
    led_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_led_blink_green_2_times\n");
}

static void test_led_blink_yellow_2_times(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_led_blink_yellow_2_times\n");
    led_use_cases_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling ledBlinkYellow2Times()\n");
    ledUseCases->ledBlinkYellow2Times();
    
    // Verify the yellow LED was turned on and off twice
    if (debug_file != nullptr) fprintf(debug_file, "Checking if setAllYellowLedOn was called: %s, count=%d\n", 
                                mockLedAdapter->setAllYellowLedOnCalled ? "Yes" : "No", mockLedAdapter->allYellowLedOnCount);
    TEST_ASSERT_TRUE(mockLedAdapter->setAllYellowLedOnCalled);
    TEST_ASSERT_EQUAL(2, mockLedAdapter->allYellowLedOnCount);
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if setAllYellowLedOff was called: %s, count=%d\n", 
                                mockLedAdapter->setAllYellowLedOffCalled ? "Yes" : "No", mockLedAdapter->allYellowLedOffCount);
    TEST_ASSERT_TRUE(mockLedAdapter->setAllYellowLedOffCalled);
    TEST_ASSERT_EQUAL(2, mockLedAdapter->allYellowLedOffCount);
    
    // Verify that delays were called 4 times (on->delay->off->delay->on->delay->off)
    if (debug_file != nullptr) fprintf(debug_file, "Checking delay count: expected=4, actual=%d\n", 
                                mockArduino->delayCount);
    TEST_ASSERT_EQUAL(4, mockArduino->delayCount);
    
    led_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_led_blink_yellow_2_times\n");
}

static void test_debug_logging_disabled(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_debug_logging_disabled\n");
    led_use_cases_setup();
    
    // Disable debugging
    mockDebugSystem->featureEnabled = false;
    
    // Reset logger call count
    mockLogger->reset();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling ledOnLoad() with debugging disabled\n");
    ledUseCases->ledOnLoad();
    
    // Verify LED functionality still works
    if (debug_file != nullptr) fprintf(debug_file, "Checking if setAllYellowLedOn was called: %s\n", 
                                mockLedAdapter->setAllYellowLedOnCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(mockLedAdapter->setAllYellowLedOnCalled);
    
    led_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_debug_logging_disabled\n");
}

// Main test runner function
extern "C" int run_led_use_cases_tests(void) {
    if (debug_file != nullptr) fprintf(debug_file, "\n----- Running LedUseCases Tests -----\n");
    
    RUN_TEST(test_led_on_load);
    RUN_TEST(test_led_off_load);
    RUN_TEST(test_led_blink_green_2_times);
    RUN_TEST(test_led_blink_yellow_2_times);
    RUN_TEST(test_debug_logging_disabled);
    
    if (debug_file != nullptr) fprintf(debug_file, "\n----- LedUseCases Tests Complete -----\n");
    
    return 0;
}
