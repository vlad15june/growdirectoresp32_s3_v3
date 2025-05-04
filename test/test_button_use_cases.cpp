#include <unity.h>
#include "Core/Domain/UseCases/Buttons/ButtonUseCases.h"
#include "Mock/Infrastructure/Button/MockButtonAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"

// Global test variables
static MockButtonAdapter g_mockButtonAdapter;
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static ButtonUseCases* g_buttonUseCases;
static FILE* debug_file = nullptr;

// Test setup and teardown
static void button_use_cases_setup(void) {
    // Open debug log file in append mode
    debug_file = fopen("button_use_cases_test_debug.log", "a");
    if (debug_file == nullptr) {
        printf("ERROR: Could not open debug log file\n");
    } else {
        fprintf(debug_file, "--- Button Use Cases Test Started ---\n");
    }
    
    g_mockButtonAdapter.reset();
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockDebugSystem.featureEnabled = true; // Enable button debug feature
    
    g_buttonUseCases = new ButtonUseCases(
        &g_mockButtonAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        1 // Use button index 1 for tests
    );
}

static void button_use_cases_teardown(void) {
    delete g_buttonUseCases;
    g_buttonUseCases = nullptr;
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "--- Button Use Cases Test Finished ---\n\n");
        fclose(debug_file);
        debug_file = nullptr;
    }
}

// Test functions
static void test_check_button_pressed_when_on_returns_true() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_check_button_pressed_when_on_returns_true\n");
    button_use_cases_setup();
    
    // Configure mock to return true for button press
    g_mockButtonAdapter.pressedState = true;
    
    // Call the method under test
    bool result = g_buttonUseCases->checkButtonPressedWhenOn();
    
    // Verify results
    TEST_ASSERT_TRUE(g_mockButtonAdapter.isButtonPressedCalled);
    TEST_ASSERT_EQUAL(1, g_mockButtonAdapter.lastButtonIndex); // Check correct button index was used
    TEST_ASSERT_TRUE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_check_button_pressed_when_on_returns_true\n");
}

static void test_check_button_pressed_when_on_returns_false() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_check_button_pressed_when_on_returns_false\n");
    button_use_cases_setup();
    
    // Configure mock to return false for button press
    g_mockButtonAdapter.pressedState = false;
    
    // Call the method under test
    bool result = g_buttonUseCases->checkButtonPressedWhenOn();
    
    // Verify results
    TEST_ASSERT_TRUE(g_mockButtonAdapter.isButtonPressedCalled);
    TEST_ASSERT_EQUAL(1, g_mockButtonAdapter.lastButtonIndex);
    TEST_ASSERT_FALSE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_check_button_pressed_when_on_returns_false\n");
}

static void test_is_button_pressed_when_on_returns_state() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_button_pressed_when_on_returns_state\n");
    button_use_cases_setup();
    
    // Initial state should be false
    bool initialResult = g_buttonUseCases->isButtonPressedWhenOn();
    TEST_ASSERT_FALSE(initialResult);
    
    // Set the internal state by calling checkButtonPressedWhenOn with press state true
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // Now isButtonPressedWhenOn should return true
    bool result = g_buttonUseCases->isButtonPressedWhenOn();
    TEST_ASSERT_TRUE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_button_pressed_when_on_returns_state\n");
}

static void test_is_button_held_for_duration_returns_true() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_button_held_for_duration_returns_true\n");
    button_use_cases_setup();
    
    // Configure mock to return true for button held
    g_mockButtonAdapter.heldState = true;
    
    // Call the method under test
    bool result = g_buttonUseCases->isButtonHeldForDuration();
    
    // Verify results
    TEST_ASSERT_TRUE(g_mockButtonAdapter.wasButtonHeldCalled);
    TEST_ASSERT_EQUAL(1, g_mockButtonAdapter.lastButtonIndex);
    TEST_ASSERT_EQUAL(15000, g_mockButtonAdapter.lastDuration); // Check default duration
    TEST_ASSERT_TRUE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_button_held_for_duration_returns_true\n");
}

static void test_is_button_held_for_duration_returns_false() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_button_held_for_duration_returns_false\n");
    button_use_cases_setup();
    
    // Configure mock to return false for button held
    g_mockButtonAdapter.heldState = false;
    
    // Call the method under test
    bool result = g_buttonUseCases->isButtonHeldForDuration();
    
    // Verify results
    TEST_ASSERT_TRUE(g_mockButtonAdapter.wasButtonHeldCalled);
    TEST_ASSERT_EQUAL(1, g_mockButtonAdapter.lastButtonIndex);
    TEST_ASSERT_EQUAL(15000, g_mockButtonAdapter.lastDuration);
    TEST_ASSERT_FALSE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_button_held_for_duration_returns_false\n");
}

static void test_button_use_cases_with_logging() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_button_use_cases_with_logging\n");
    button_use_cases_setup();
    
    // Configure mocks
    g_mockButtonAdapter.pressedState = true;
    g_mockDebugSystem.featureEnabled = true;
    
    // Call methods
    g_buttonUseCases->checkButtonPressedWhenOn();
    g_buttonUseCases->isButtonHeldForDuration();
    
    // Verify logging occurred
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_button_use_cases_with_logging\n");
}

static void test_is_button_pressed_when_on_then_held_for_duration_returns_true() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_button_pressed_when_on_then_held_for_duration_returns_true\n");
    button_use_cases_setup();
    
    // Set up conditions for the test
    // First, simulate button pressed when on
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // Then, configure mock to return true for button held
    g_mockButtonAdapter.heldState = true;
    
    // Call the method under test
    bool result = g_buttonUseCases->isButtonPressedWhenOnThenHeldForDuration();
    
    // Verify results
    TEST_ASSERT_TRUE(g_mockButtonAdapter.wasButtonHeldCalled);
    TEST_ASSERT_EQUAL(1, g_mockButtonAdapter.lastButtonIndex);
    TEST_ASSERT_EQUAL(15000, g_mockButtonAdapter.lastDuration); // Check correct duration
    TEST_ASSERT_TRUE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_button_pressed_when_on_then_held_for_duration_returns_true\n");
}

static void test_is_button_pressed_when_on_then_held_for_duration_returns_false_when_not_pressed() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_button_pressed_when_on_then_held_for_duration_returns_false_when_not_pressed\n");
    button_use_cases_setup();
    
    // Don't call checkButtonPressedWhenOn(), so buttonPressedState remains false
    
    // Call the method under test
    bool result = g_buttonUseCases->isButtonPressedWhenOnThenHeldForDuration();
    
    // Verify results
    TEST_ASSERT_FALSE(g_mockButtonAdapter.wasButtonHeldCalled); // This shouldn't be called
    TEST_ASSERT_FALSE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_button_pressed_when_on_then_held_for_duration_returns_false_when_not_pressed\n");
}

static void test_is_button_pressed_when_on_then_held_for_duration_returns_false_when_not_held() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_button_pressed_when_on_then_held_for_duration_returns_false_when_not_held\n");
    button_use_cases_setup();
    
    // Simulate button pressed when on
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // But configure mock to return false for button held
    g_mockButtonAdapter.heldState = false;
    
    // Call the method under test
    bool result = g_buttonUseCases->isButtonPressedWhenOnThenHeldForDuration();
    
    // Verify results
    TEST_ASSERT_TRUE(g_mockButtonAdapter.wasButtonHeldCalled);
    TEST_ASSERT_EQUAL(1, g_mockButtonAdapter.lastButtonIndex);
    TEST_ASSERT_EQUAL(15000, g_mockButtonAdapter.lastDuration);
    TEST_ASSERT_FALSE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_button_pressed_when_on_then_held_for_duration_returns_false_when_not_held\n");
}

// Main test runner function
extern "C" int run_button_use_cases_tests(void) {
    if (debug_file != nullptr) fprintf(debug_file, "\n----- Running ButtonUseCases Tests -----\n");
    
    RUN_TEST(test_check_button_pressed_when_on_returns_true);
    RUN_TEST(test_check_button_pressed_when_on_returns_false);
    RUN_TEST(test_is_button_pressed_when_on_returns_state);
    RUN_TEST(test_is_button_held_for_duration_returns_true);
    RUN_TEST(test_is_button_held_for_duration_returns_false);
    RUN_TEST(test_button_use_cases_with_logging);
    RUN_TEST(test_is_button_pressed_when_on_then_held_for_duration_returns_true);
    RUN_TEST(test_is_button_pressed_when_on_then_held_for_duration_returns_false_when_not_pressed);
    RUN_TEST(test_is_button_pressed_when_on_then_held_for_duration_returns_false_when_not_held);
    
    if (debug_file != nullptr) fprintf(debug_file, "\n----- ButtonUseCases Tests Complete -----\n");
    
    return 0;
}
