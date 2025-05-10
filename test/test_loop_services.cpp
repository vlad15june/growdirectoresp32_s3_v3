#include <unity.h>
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Application/Services/LoopServices/LoopServiceCanConfigure.h"
#include "Application/Services/LoopServices/LoopServiceReConfigure.h"
#include "Application/Services/LoopServices/LoopServiceTestMode.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Mock/Core/Domain/MockLedUseCases.h"
#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Core/Domain/MockOnChangeModuleModeUseCase.h"
#include "Mock/Core/Domain/MockOnChangeLocalIpUseCase.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"

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

static MockLedUseCases* g_mockLedUseCases;
static MockButtonUseCases* g_mockButtonUseCases;
static MockOnChangeModuleModeUseCase* g_mockModuleModeUseCase;
static MockOnChangeLocalIpUseCase* g_mockOnChangeLocalIpUseCase;
static MockLogger* g_mockLogger;
static MockDebugSystem* g_mockDebugSystem;
static MockWebServerLooper* g_mockWebServer;
static MockModuleConfig* g_mockModuleConfig;
static MockSettings* g_mockSettings;
static MockArduinoAdapter* g_mockArduinoAdapter;
static MockMqttManager* g_mockMqttManager;

static void base_setup(void) {
    g_mockLedUseCases = new MockLedUseCases();
    g_mockButtonUseCases = new MockButtonUseCases();
    g_mockModuleModeUseCase = new MockOnChangeModuleModeUseCase();
    g_mockOnChangeLocalIpUseCase = new MockOnChangeLocalIpUseCase();
    g_mockLogger = new MockLogger();
    g_mockDebugSystem = new MockDebugSystem();
    g_mockWebServer = new MockWebServerLooper();
    g_mockSettings = new MockSettings();
    g_mockModuleConfig = new MockModuleConfig(g_mockLogger, g_mockDebugSystem, g_mockSettings);
    g_mockArduinoAdapter = new MockArduinoAdapter();
    g_mockMqttManager = new MockMqttManager();
    
    g_mockDebugSystem->featureEnabled = true;
}

static void base_teardown(void) {
    delete g_mockLedUseCases;
    delete g_mockButtonUseCases;
    delete g_mockModuleModeUseCase;
    delete g_mockOnChangeLocalIpUseCase;
    delete g_mockLogger;
    delete g_mockDebugSystem;
    delete g_mockWebServer;
    delete g_mockModuleConfig;
    delete g_mockSettings;
    delete g_mockArduinoAdapter;
    delete g_mockMqttManager;
    
    g_mockLedUseCases = nullptr;
    g_mockButtonUseCases = nullptr;
    g_mockModuleModeUseCase = nullptr;
    g_mockOnChangeLocalIpUseCase = nullptr;
    g_mockLogger = nullptr;
    g_mockDebugSystem = nullptr;
    g_mockWebServer = nullptr;
    g_mockModuleConfig = nullptr;
    g_mockSettings = nullptr;
    g_mockArduinoAdapter = nullptr;
    g_mockMqttManager = nullptr;
}

static void reset_all_mocks(void) {
    g_mockLedUseCases->reset();
    g_mockButtonUseCases->reset();
    g_mockModuleModeUseCase->reset();
    g_mockOnChangeLocalIpUseCase->reset();
    g_mockLogger->reset();
    g_mockWebServer->reset();
    g_mockModuleConfig->reset();
    g_mockSettings->reset();
    g_mockArduinoAdapter->reset();
    g_mockMqttManager->reset();
}

static void test_loop_service_base_functionality(void) {
    base_setup();
    
    LoopServiceConfigureDone service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setOnChangeLocalIpUseCase(g_mockOnChangeLocalIpUseCase);
    service.setMqttManager(g_mockMqttManager);
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockModuleModeUseCase->loopCalled);
    TEST_ASSERT_TRUE(g_mockLedUseCases->loopCalled);
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonPressedWhenOnThenHeldForDurationCalled);
    TEST_ASSERT_TRUE(g_mockOnChangeLocalIpUseCase->performCalled);
    TEST_ASSERT_TRUE(g_mockMqttManager->loopCalled);
    
    base_teardown();
}

static void test_button_long_press_changes_mode(void) {
    base_setup();
    
    LoopServiceConfigureDone service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setOnChangeLocalIpUseCase(g_mockOnChangeLocalIpUseCase);
    service.setMqttManager(g_mockMqttManager);
    
    g_mockButtonUseCases->buttonPressedThenHeldResult = true;
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL(ModuleModeEnum::AP_TEST_MODE, g_mockModuleConfig->getRequestedModuleMode());
    
    base_teardown();
}

static void test_loop_service_can_configure(void) {
    base_setup();
    
    LoopServiceCanConfigure service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockWebServer->loopCalled);
    
    base_teardown();
}

static void test_loop_service_re_configure(void) {
    base_setup();
    
    LoopServiceReConfigure service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setArduinoAdapter(g_mockArduinoAdapter);
    
    g_mockButtonUseCases->buttonHeldResult = true;
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonHeldForDurationCalled);
    TEST_ASSERT_TRUE(g_mockWebServer->loopCalled);
    TEST_ASSERT_TRUE(g_mockArduinoAdapter->millisCalled);
    
    base_teardown();
}

static void test_loop_service_re_configure_timeout_not_reached(void) {
    base_setup();
    
    LoopServiceReConfigure service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setArduinoAdapter(g_mockArduinoAdapter);
    
    g_mockArduinoAdapter->currentMillis = 100000; // 100 seconds
    
    service.loop();
    
    TEST_ASSERT_FALSE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    
    base_teardown();
}

static void test_loop_service_re_configure_timeout_reached(void) {
    base_setup();
    
    LoopServiceReConfigure service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setArduinoAdapter(g_mockArduinoAdapter);
    
    g_mockArduinoAdapter->currentMillis = 600000; // 10 minutes
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, g_mockModuleConfig->getRequestedModuleMode());
    
    base_teardown();
}

static void test_loop_service_re_configure_null_arduino_adapter(void) {
    base_setup();
    
    LoopServiceReConfigure service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    service.loop();
    
    TEST_ASSERT_FALSE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    
    base_teardown();
}

static void test_loop_service_test_mode(void) {
    base_setup();
    
    LoopServiceTestMode service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    g_mockButtonUseCases->buttonPressedResult = true;
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonPressedWhenOnCalled);
    
    base_teardown();
}

static void test_loop_service_configure_done(void) {
    base_setup();
    
    LoopServiceConfigureDone service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setOnChangeLocalIpUseCase(g_mockOnChangeLocalIpUseCase);
    service.setMqttManager(g_mockMqttManager);
    
    g_mockButtonUseCases->buttonHeldResult = true;
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockModuleModeUseCase->loopCalled);
    TEST_ASSERT_TRUE(g_mockLedUseCases->loopCalled);
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonHeldForDurationCalled);
    TEST_ASSERT_TRUE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_RE_CONFIGURE, g_mockModuleConfig->getRequestedModuleMode());
    TEST_ASSERT_TRUE(g_mockOnChangeLocalIpUseCase->performCalled);
    TEST_ASSERT_TRUE(g_mockMqttManager->loopCalled);
    
    base_teardown();
}

static void test_loop_service_configure_done_button_held(void) {
    base_setup();
    
    LoopServiceConfigureDone service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    service.setOnChangeLocalIpUseCase(g_mockOnChangeLocalIpUseCase);
    service.setMqttManager(g_mockMqttManager);
    
    g_mockButtonUseCases->buttonHeldResult = true;
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockButtonUseCases->isButtonHeldForDurationCalled);
    TEST_ASSERT_TRUE(g_mockModuleConfig->wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_RE_CONFIGURE, g_mockModuleConfig->getRequestedModuleMode());
    
    base_teardown();
}

static void test_loop_service_configure_done_null_dependencies(void) {
    base_setup();
    
    LoopServiceConfigureDone service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setModuleConfig(g_mockModuleConfig);
    
    service.loop();
    
    TEST_ASSERT_TRUE(g_mockLedUseCases->loopCalled);
    TEST_ASSERT_TRUE(g_mockModuleModeUseCase->loopCalled);
    TEST_ASSERT_FALSE(g_mockOnChangeLocalIpUseCase->performCalled);
    TEST_ASSERT_FALSE(g_mockMqttManager->loopCalled);
    
    base_teardown();
}

static void test_multiple_loop_calls(void) {
    base_setup();
    
    LoopServiceCanConfigure service;
    
    service.setLedUseCases(g_mockLedUseCases);
    service.setButtonUseCases(g_mockButtonUseCases);
    service.setOnChangeModuleModeUseCase(g_mockModuleModeUseCase);
    service.setLogging(g_mockLogger, g_mockDebugSystem);
    service.setWebServer(g_mockWebServer);
    service.setModuleConfig(g_mockModuleConfig);
    
    for (int i = 0; i < 5; i++) {
        service.loop();
    }
    
    TEST_ASSERT_EQUAL(5, g_mockModuleModeUseCase->loopCallCount);
    TEST_ASSERT_EQUAL(5, g_mockLedUseCases->loopCount);
    TEST_ASSERT_EQUAL(5, g_mockWebServer->loopCallCount);
    
    base_teardown();
}

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
    RUN_TEST(test_loop_service_configure_done_null_dependencies);
    RUN_TEST(test_multiple_loop_calls);
    
    printf("\n----- Loop Services Tests Complete -----\n");
    
    return 0;
}
