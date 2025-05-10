#include <unity.h>
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Application/Services/LoopServices/LoopServiceCanConfigure.h"
#include "Application/Services/LoopServices/LoopServiceReConfigure.h"
#include "Application/Services/LoopServices/LoopServiceTestMode.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Mock/Core/Domain/MockLedUseCases.h"
#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Core/Domain/MockOnChangeModuleModeUseCase.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

// Mock for the web server
class MockWebServerLooper : public IWebServerLooper {
public:
    bool loopCalled = false;
    int loopCallCount = 0;
    
    void loop() override {
        loopCalled = true;
        loopCallCount++;
    }
    
    void reset() {
        loopCalled = false;
        loopCallCount = 0;
    }
};

// Global test variables
static MockLedUseCases* g_mockLedUseCases;
static MockButtonUseCases* g_mockButtonUseCases;
static MockOnChangeModuleModeUseCase* g_mockModuleModeUseCase;
static MockLogger* g_mockLogger;
static MockDebugSystem* g_mockDebugSystem;
static MockWebServerLooper* g_mockWebServer;
static MockModuleConfig* g_mockModuleConfig;
static MockSettings* g_mockSettings;
static MockArduinoAdapter* g_mockArduinoAdapter;

// Base setup and teardown
static void base_setup(void) {
    g_mockLedUseCases = new MockLedUseCases();
    g_mockButtonUseCases = new MockButtonUseCases();
    g_mockModuleModeUseCase = new MockOnChangeModuleModeUseCase();
    g_mockLogger = new MockLogger();
    g_mockDebugSystem = new MockDebugSystem();
    g_mockWebServer = new MockWebServerLooper();
    g_mockSettings = new MockSettings();
    g_mockModuleConfig = new MockModuleConfig(g_mockLogger, g_mockDebugSystem, g_mockSettings);
    g_mockArduinoAdapter = new MockArduinoAdapter();
    
    // Enable debug features for testing
    g_mockDebugSystem->featureEnabled = true;
}

static void base_teardown(void) {
    delete g_mockLedUseCases;
    delete g_mockButtonUseCases;
    delete g_mockModuleModeUseCase;
    delete g_mockLogger;
    delete g_mockDebugSystem;
    delete g_mockWebServer;
    delete g_mockModuleConfig;
    delete g_mockSettings;
    delete g_mockArduinoAdapter;
    
    g_mockLedUseCases = nullptr;
    g_mockButtonUseCases = nullptr;
    g_mockModuleModeUseCase = nullptr;
    g_mockLogger = nullptr;
    g_mockDebugSystem = nullptr;
    g_mockWebServer = nullptr;
    g_mockModuleConfig = nullptr;
    g_mockSettings = nullptr;
    g_mockArduinoAdapter = nullptr;
}

// Helper to reset all mocks
static void reset_all_mocks(void) {
    g_mockLedUseCases->reset();
    g_mockButtonUseCases->reset();
    g_mockModuleModeUseCase->reset();
    g_mockLogger->reset();
    g_mockWebServer->reset();
    g_mockModuleConfig->reset();
    g_mockSettings->reset();
    g_mockArduinoAdapter->reset();
}

// Test the base functionality of loop services
static void test_loop_service_base_functionality(void) {
    base_setup();
    
    // Create a test service (using ConfigureDone as an example)
    LoopServiceConfigureDone service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    // Execute the loop
    service.loop();
    
    // Verify that module mode change was checked
    TEST_ASSERT_TRUE(g_mockModuleModeUseCase->loopCalled);
    
    // Verify LED updates happened
    TEST_ASSERT_TRUE(g_mockLedUseCases->loopCalled);
    
    // Button press checking should have happened
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonPressedWhenOnThenHeldForDurationCalled);
    
    base_teardown();
}

// Test button press causing mode change
static void test_button_long_press_changes_mode(void) {
    base_setup();
    
    // Create a test service
    LoopServiceConfigureDone service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    // Set up the button use case to report a long press
    g_mockButtonUseCases->buttonPressedThenHeldResult = true;
    
    // Execute the loop
    service.loop();
    
    // Verify that the service requested a mode change to TEST mode
    TEST_ASSERT_TRUE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL(ModuleModeEnum::AP_TEST_MODE, g_mockModuleConfig->getRequestedModuleMode());
    
    base_teardown();
}

// Tests for LoopServiceCanConfigure
static void test_loop_service_can_configure(void) {
    base_setup();
    
    // Create the service
    LoopServiceCanConfigure service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    // Execute the loop
    service.loop();
    
    // This service should call the web server loop
    TEST_ASSERT_TRUE(g_mockWebServer->loopCalled);
    
    base_teardown();
}

// Tests for LoopServiceReConfigure
static void test_loop_service_re_configure(void) {
    base_setup();
    
    // Create the service
    LoopServiceReConfigure service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setArduinoAdapter(g_mockArduinoAdapter);
    
    // Set up the button use case
    g_mockButtonUseCases->buttonHeldResult = true;
    
    // Execute the loop
    service.loop();
    
    // Verify button held check was called
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonHeldForDurationCalled);
    
    // This service should call the web server loop
    TEST_ASSERT_TRUE(g_mockWebServer->loopCalled);
    
    // Verify the Arduino adapter's millis was called for time check
    TEST_ASSERT_TRUE(g_mockArduinoAdapter->millisCalled);
    
    base_teardown();
}

// Test timeout behavior in LoopServiceReConfigure
static void test_loop_service_re_configure_timeout_not_reached(void) {
    base_setup();
    
    // Create the service
    LoopServiceReConfigure service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setArduinoAdapter(g_mockArduinoAdapter);
    
    // Set current time to something small (< 5 minutes)
    g_mockArduinoAdapter->currentMillis = 100000; // 100 seconds
    
    // Execute the loop
    service.loop();
    
    // Verify mode was not changed
    TEST_ASSERT_FALSE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    
    base_teardown();
}

static void test_loop_service_re_configure_timeout_reached(void) {
    base_setup();
    
    // Create the service
    LoopServiceReConfigure service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setArduinoAdapter(g_mockArduinoAdapter);
    
    // Set current time to > 5 minutes (300,000 ms)
    g_mockArduinoAdapter->currentMillis = 600000; // 10 minutes
    
    // Execute the loop
    service.loop();
    
    // Verify mode was changed to CONFIGURED_DONE
    TEST_ASSERT_TRUE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, g_mockModuleConfig->getRequestedModuleMode());
    
    base_teardown();
}

static void test_loop_service_re_configure_null_arduino_adapter(void) {
    base_setup();
    
    // Create the service
    LoopServiceReConfigure service;
    
    // Inject dependencies but NOT the Arduino adapter
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    // Execute the loop (should not crash)
    service.loop();
    
    // Verify mode was not changed
    TEST_ASSERT_FALSE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    
    base_teardown();
}

// Tests for LoopServiceTestMode
static void test_loop_service_test_mode(void) {
    base_setup();
    
    // Create the service
    LoopServiceTestMode service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    // Set up for button press test
    g_mockButtonUseCases->buttonPressedResult = true;
    
    // Execute the loop
    service.loop();
    
    // Verify button press check was called
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonPressedWhenOnCalled);
    
    base_teardown();
}

// Tests for LoopServiceConfigureDone
static void test_loop_service_configure_done(void) {
    base_setup();
    
    // Create the service
    LoopServiceConfigureDone service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    // Set up for button long press test
    g_mockButtonUseCases->buttonHeldResult = true;
    
    // Execute the loop
    service.loop();
    
    // Verify button long press check was called
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonHeldForDurationCalled);
    
    base_teardown();
}

// Test for LoopServiceConfigureDone button held functionality
static void test_loop_service_configure_done_button_held(void) {
    base_setup();
    
    // Create the service
    LoopServiceConfigureDone service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    // Set up for button long press test - button is held
    g_mockButtonUseCases->buttonHeldResult = true;
    
    // Execute the loop
    service.loop();
    
    // Verify button held check was called
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonHeldForDurationCalled);
    
    // Verify that the service requested a mode change to RE_CONFIGURE mode
    TEST_ASSERT_TRUE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_RE_CONFIGURE, g_mockModuleConfig->getRequestedModuleMode());
    
    base_teardown();
}

// Test multiple loops to check incremental behavior
static void test_multiple_loop_calls(void) {
    base_setup();
    
    // Create the service
    LoopServiceCanConfigure service;
    
    // Inject dependencies
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    // Call loop multiple times
    for (int i = 0; i < 5; i++) {
        service.loop();
    }
    
    // Verify appropriate call counts
    TEST_ASSERT_EQUAL(5, g_mockModuleModeUseCase->loopCallCount);
    TEST_ASSERT_EQUAL(5, g_mockLedUseCases->loopCount);
    TEST_ASSERT_EQUAL(5, g_mockWebServer->loopCallCount);
    
    base_teardown();
}

// Main test runner function
extern "C" int run_loop_services_tests(void) {
    printf("\n----- Running Loop Services Tests -----\n");
    
    RUN_TEST(test_loop_service_base_functionality);
    RUN_TEST(test_button_long_press_changes_mode);
    RUN_TEST(test_loop_service_can_configure);
    RUN_TEST(test_loop_service_re_configure);
    RUN_TEST(test_loop_service_re_configure_timeout_not_reached);
    RUN_TEST(test_loop_service_re_configure_timeout_reached);
    RUN_TEST(test_loop_service_re_configure_null_arduino_adapter);
    RUN_TEST(test_loop_service_test_mode);
    RUN_TEST(test_loop_service_configure_done);
    RUN_TEST(test_loop_service_configure_done_button_held);
    RUN_TEST(test_multiple_loop_calls);
    
    printf("\n----- Loop Services Tests Complete -----\n");
    
    return 0;
}
