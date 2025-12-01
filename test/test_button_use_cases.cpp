#include <unity.h>
#include "Core/Domain/UseCases/Buttons/ButtonUseCases.h"
#include "Mock/Infrastructure/Button/MockButtonAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"

// Global test variables
static MockButtonAdapter g_mockButtonAdapter;
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockSettings g_mockSettings;
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
    g_mockSettings.reset();
    g_mockDebugSystem.featureEnabled = true; // Enable button debug feature
    
    g_buttonUseCases = new ButtonUseCases(
        &g_mockButtonAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        1, // Use button index 1 for tests
        &g_mockSettings
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
    TEST_ASSERT_EQUAL(10000, g_mockButtonAdapter.lastDuration); // Changed to 10000
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
    TEST_ASSERT_EQUAL(10000, g_mockButtonAdapter.lastDuration); // Changed to 10000
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

static void test_is_test_mode_requested_returns_false_without_prior_press() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_test_mode_requested_returns_false_without_prior_press\n");
    button_use_cases_setup();
    
    // Set up conditions for the test - this simulates the test mode sequence:
    // 1. Button was pressed before reboot (need to simulate this with a new ButtonUseCases with settings)
    // For now, this test will fail because it needs settings mock
    // We'll create a separate test for the full scenario
    
    // First, simulate button pressed when on
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // Then, configure mock to return true for button held
    g_mockButtonAdapter.heldState = true;
    
    // Call the method under test - this will return false because buttonWasPressedBeforeReboot is false
    bool result = g_buttonUseCases->isTestModeRequested();
    
    // Verify results - should be false because we don't have the "pressed before reboot" state
    TEST_ASSERT_FALSE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_test_mode_requested_returns_false_without_prior_press\n");
}

static void test_is_test_mode_requested_returns_false_when_not_pressed() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_test_mode_requested_returns_false_when_not_pressed\n");
    button_use_cases_setup();
    
    // Don't call checkButtonPressedWhenOn(), so buttonPressedState remains false
    
    // Call the method under test
    bool result = g_buttonUseCases->isTestModeRequested();
    
    // Verify results
    TEST_ASSERT_FALSE(g_mockButtonAdapter.wasButtonHeldCalled); // This shouldn't be called
    TEST_ASSERT_FALSE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_test_mode_requested_returns_false_when_not_pressed\n");
}

static void test_is_test_mode_requested_returns_false_when_not_held() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_is_test_mode_requested_returns_false_when_not_held\n");
    button_use_cases_setup();
    
    // Simulate button pressed when on
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // But configure mock to return false for button held
    g_mockButtonAdapter.heldState = false;
    
    // Call the method under test
    bool result = g_buttonUseCases->isTestModeRequested();
    
    // Verify results - should be false (no "pressed before reboot" state)
    TEST_ASSERT_FALSE(g_mockButtonAdapter.wasButtonHeldCalled); // Won't even check held state
    TEST_ASSERT_FALSE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_is_test_mode_requested_returns_false_when_not_held\n");
}

static void test_test_mode_full_sequence() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_test_mode_full_sequence\n");
    button_use_cases_setup();
    
    // Simulate the full test mode sequence:
    // 1. Button pressed during operation
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // Verify that button state was saved to settings
    TEST_ASSERT_EQUAL(1, g_mockSettings.getButtonPressedBeforeReboot());
    
    // 2. Simulate reboot - create new ButtonUseCases instance that reads saved state
    delete g_buttonUseCases;
    g_mockButtonAdapter.reset();
    
    // Button is still pressed after reboot
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases = new ButtonUseCases(
        &g_mockButtonAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        1,
        &g_mockSettings  // This should read buttonPressedBeforeReboot = 1
    );
    
    // Check button state at startup
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // 3. Button is held for duration
    g_mockButtonAdapter.heldState = true;
    
    // 4. Check test mode activation
    bool result = g_buttonUseCases->isTestModeRequested();
    
    // Verify test mode is activated
    TEST_ASSERT_TRUE(result);
    
    // Verify that saved state is cleared after successful activation
    TEST_ASSERT_EQUAL(0, g_mockSettings.getButtonPressedBeforeReboot());
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_test_mode_full_sequence\n");
}

static void test_test_mode_button_not_pressed_before_reboot() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_test_mode_button_not_pressed_before_reboot\n");
    button_use_cases_setup();
    
    // Button not pressed before reboot (default state in settings is 0)
    // Button is pressed now
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases->checkButtonPressedWhenOn();
    g_mockButtonAdapter.heldState = true;
    
    // This should fail because button wasn't pressed before reboot
    bool result = g_buttonUseCases->isTestModeRequested();
    
    TEST_ASSERT_FALSE(result);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_test_mode_button_not_pressed_before_reboot\n");
}

static void test_test_mode_button_not_pressed_at_startup() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_test_mode_button_not_pressed_at_startup\n");
    button_use_cases_setup();
    
    // Simulate button was pressed before reboot
    g_mockSettings.setButtonPressedBeforeReboot(1);
    
    // Recreate ButtonUseCases to simulate reboot
    delete g_buttonUseCases;
    g_buttonUseCases = new ButtonUseCases(
        &g_mockButtonAdapter,
        &g_mockLogger,
        &g_mockDebugSystem,
        1,
        &g_mockSettings
    );
    
    // Button NOT pressed at startup
    g_mockButtonAdapter.pressedState = false;
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // This should fail and clear the saved state
    bool result = g_buttonUseCases->isTestModeRequested();
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(0, g_mockSettings.getButtonPressedBeforeReboot());
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_test_mode_button_not_pressed_at_startup\n");
}

static void test_factory_reset_requested() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_factory_reset_requested\n");
    button_use_cases_setup();
    
    // Simulate button pressed at power on
    g_mockButtonAdapter.pressedState = true;
    g_buttonUseCases->checkButtonPressedWhenOn();
    
    // Button held for factory reset duration
    g_mockButtonAdapter.heldState = true;
    
    bool result = g_buttonUseCases->isFactoryResetRequested();
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockButtonAdapter.wasButtonHeldCalled);
    TEST_ASSERT_EQUAL(10000, g_mockButtonAdapter.lastDuration);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_factory_reset_requested\n");
}

static void test_add_mode_requested() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_add_mode_requested\n");
    button_use_cases_setup();
    
    // Add mode doesn't require button to be pressed at power on
    // Just need to hold button for duration
    g_mockButtonAdapter.heldState = true;
    
    bool result = g_buttonUseCases->isAddModeRequested();
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockButtonAdapter.wasButtonHeldCalled);
    TEST_ASSERT_EQUAL(10000, g_mockButtonAdapter.lastDuration);
    
    button_use_cases_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_add_mode_requested\n");
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
    RUN_TEST(test_is_test_mode_requested_returns_false_without_prior_press);
    RUN_TEST(test_is_test_mode_requested_returns_false_when_not_pressed);
    RUN_TEST(test_is_test_mode_requested_returns_false_when_not_held);
    RUN_TEST(test_test_mode_full_sequence);
    RUN_TEST(test_test_mode_button_not_pressed_before_reboot);
    RUN_TEST(test_test_mode_button_not_pressed_at_startup);
    RUN_TEST(test_factory_reset_requested);
    RUN_TEST(test_add_mode_requested);
    
    if (debug_file != nullptr) fprintf(debug_file, "\n----- ButtonUseCases Tests Complete -----\n");
    
    return 0;
}
