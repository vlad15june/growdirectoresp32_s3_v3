#include <unity.h>
#include <memory>
#include <stdexcept>
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Application/Interfaces/LoopServices/ILoopService.h"
#include "Application/Interfaces/LoopServices/ILoopServiceFactory.h"
#include "Application/Services/LoopServices/LoopServiceFactory.h"
#include "Application/Services/LoopServices/LoopServiceCanConfigure.h"
#include "Application/Services/LoopServices/LoopServiceReConfigure.h"
#include "Application/Services/LoopServices/LoopServiceTestMode.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Core/Domain/MockOnChangeLocalIpUseCase.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Application/MockDiagnosticService.h"

static LoopServiceFactory* g_loopServiceFactory;
static MockArduinoAdapter* g_mockArduinoAdapter;
static MockOnChangeLocalIpUseCase* g_mockOnChangeLocalIpUseCase;
static MockMqttManager* g_mockMqttManager;
static MockDiagnosticService* g_mockDiagnosticService;

static void loop_service_factory_setup(void) {
    g_mockArduinoAdapter = new MockArduinoAdapter();
    g_mockOnChangeLocalIpUseCase = new MockOnChangeLocalIpUseCase();
    g_mockMqttManager = new MockMqttManager();
    g_mockDiagnosticService = new MockDiagnosticService();
    
    g_loopServiceFactory = new LoopServiceFactory(
        nullptr, nullptr, nullptr, nullptr, nullptr, 
        nullptr, nullptr, g_mockArduinoAdapter,
        g_mockOnChangeLocalIpUseCase, g_mockMqttManager,
        g_mockDiagnosticService);
}

static void loop_service_factory_teardown(void) {
    delete g_loopServiceFactory;
    delete g_mockArduinoAdapter;
    delete g_mockOnChangeLocalIpUseCase;
    delete g_mockMqttManager;
    delete g_mockDiagnosticService;
    
    g_loopServiceFactory = nullptr;
    g_mockArduinoAdapter = nullptr;
    g_mockOnChangeLocalIpUseCase = nullptr;
    g_mockMqttManager = nullptr;
    g_mockDiagnosticService = nullptr;
}

template <typename ExpectedType>
static bool isExpectedType(ILoopService* service) {
    return dynamic_cast<ExpectedType*>(service) != nullptr;
}

static void test_create_can_configure_service(void) {
    loop_service_factory_setup();
    
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::USER_CAN_CONFIGURE);
    TEST_ASSERT_NOT_NULL(service.get());
    TEST_ASSERT_TRUE(isExpectedType<LoopServiceCanConfigure>(service.get()));
    
    loop_service_factory_teardown();
}

static void test_create_re_configure_service(void) {
    loop_service_factory_setup();
    
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
    TEST_ASSERT_NOT_NULL(service.get());
    TEST_ASSERT_TRUE(isExpectedType<LoopServiceReConfigure>(service.get()));
    
    loop_service_factory_teardown();
}

static void test_create_test_mode_service(void) {
    loop_service_factory_setup();
    
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::AP_TEST_MODE);
    TEST_ASSERT_NOT_NULL(service.get());
    TEST_ASSERT_TRUE(isExpectedType<LoopServiceTestMode>(service.get()));
    
    loop_service_factory_teardown();
}

static void test_create_configure_done_service(void) {
    loop_service_factory_setup();
    
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::CONFIGURED_DONE);
    TEST_ASSERT_NOT_NULL(service.get());
    TEST_ASSERT_TRUE(isExpectedType<LoopServiceConfigureDone>(service.get()));
    
    service->loop();
    
    TEST_ASSERT_TRUE(g_mockOnChangeLocalIpUseCase->performCalled);
    TEST_ASSERT_TRUE(g_mockMqttManager->loopCalled);
    
    // Mock current time to trigger diagnostic service
    g_mockArduinoAdapter->currentMillis = 5 * 60 * 1000 + 1; // Just beyond the diagnostic interval
    
    // Run the loop again to trigger the diagnostic check
    service->loop();
    
    // Verify that the diagnostic service was called
    TEST_ASSERT_TRUE(g_mockDiagnosticService->performAndSendCalled);
    
    loop_service_factory_teardown();
}

static void test_invalid_mode_throws_exception(void) {
    loop_service_factory_setup();
    
    ModuleModeEnum invalidMode = static_cast<ModuleModeEnum>(999);
    
    bool exceptionThrown = false;
    try {
        auto service = g_loopServiceFactory->createLoopService(invalidMode);
    } catch (const std::invalid_argument& e) {
        exceptionThrown = true;
    }
    
    TEST_ASSERT_TRUE(exceptionThrown);
    
    loop_service_factory_teardown();
}

static void test_arduino_adapter_injected(void) {
    loop_service_factory_setup();
    
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
    
    auto baseService = dynamic_cast<LoopServiceBase*>(service.get());
    TEST_ASSERT_NOT_NULL(baseService);
    
    TEST_ASSERT_TRUE(isExpectedType<LoopServiceReConfigure>(service.get()));
    
    service->loop();
    
    TEST_ASSERT_TRUE(g_mockArduinoAdapter->millisCalled);
    
    loop_service_factory_teardown();
}

static void test_dependency_setters(void) {
    loop_service_factory_setup();
    
    MockOnChangeLocalIpUseCase newIpUseCase;
    MockMqttManager newMqttManager;
    MockDiagnosticService newDiagnosticService;
    
    g_loopServiceFactory->setOnChangeLocalIpUseCase(&newIpUseCase);
    g_loopServiceFactory->setMqttManager(&newMqttManager);
    g_loopServiceFactory->setDiagnosticService(&newDiagnosticService);
    
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::CONFIGURED_DONE);
    
    service->loop();
    
    TEST_ASSERT_FALSE(g_mockOnChangeLocalIpUseCase->performCalled);
    TEST_ASSERT_FALSE(g_mockMqttManager->loopCalled);
    TEST_ASSERT_FALSE(g_mockDiagnosticService->performAndSendCalled);
    TEST_ASSERT_TRUE(newIpUseCase.performCalled);
    TEST_ASSERT_TRUE(newMqttManager.loopCalled);
    
    // Mock current time to trigger diagnostic service
    g_mockArduinoAdapter->currentMillis = 5 * 60 * 1000 + 1; // Just beyond the diagnostic interval
    
    // Run the loop again
    service->loop();
    
    // Verify that the new diagnostic service was called
    TEST_ASSERT_TRUE(newDiagnosticService.performAndSendCalled);
    
    loop_service_factory_teardown();
}

static void test_diagnostic_timer_logic(void) {
    loop_service_factory_setup();
    
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::CONFIGURED_DONE);
    
    // Initial time - diagnostic service should not be called
    g_mockArduinoAdapter->currentMillis = 0;
    service->loop();
    TEST_ASSERT_FALSE(g_mockDiagnosticService->performAndSendCalled);
    
    // Time still under threshold - should not be called
    g_mockArduinoAdapter->currentMillis = 30 * 1000 - 1;
    service->loop();
    TEST_ASSERT_FALSE(g_mockDiagnosticService->performAndSendCalled);
    
    // Time at threshold - should be called now
    g_mockArduinoAdapter->currentMillis = 30 * 1000; 
    service->loop();
    TEST_ASSERT_TRUE(g_mockDiagnosticService->performAndSendCalled);
    
    // Reset the mock
    g_mockDiagnosticService->reset();
    
    // Another interval passes - should be called again
    g_mockArduinoAdapter->currentMillis = 2 * 30 * 1000;
    service->loop();
    TEST_ASSERT_TRUE(g_mockDiagnosticService->performAndSendCalled);
    
    loop_service_factory_teardown();
}

extern "C" int run_loop_service_factory_tests(void) {
    printf("\n----- Running LoopServiceFactory Tests -----\n");
    
    RUN_TEST(test_create_can_configure_service);
    RUN_TEST(test_create_re_configure_service);
    RUN_TEST(test_create_test_mode_service);
    RUN_TEST(test_create_configure_done_service);
    RUN_TEST(test_invalid_mode_throws_exception);
    RUN_TEST(test_arduino_adapter_injected);
    RUN_TEST(test_dependency_setters);
    RUN_TEST(test_diagnostic_timer_logic);
    
    printf("\n----- LoopServiceFactory Tests Complete -----\n");
    
    return 0;
}
