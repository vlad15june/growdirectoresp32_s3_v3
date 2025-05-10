#include <unity.h>
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

static LoopServiceFactory* g_loopServiceFactory;
static MockArduinoAdapter* g_mockArduinoAdapter;
static MockOnChangeLocalIpUseCase* g_mockOnChangeLocalIpUseCase;
static MockMqttManager* g_mockMqttManager;

static void loop_service_factory_setup(void) {
    g_mockArduinoAdapter = new MockArduinoAdapter();
    g_mockOnChangeLocalIpUseCase = new MockOnChangeLocalIpUseCase();
    g_mockMqttManager = new MockMqttManager();
    
    g_loopServiceFactory = new LoopServiceFactory(
        nullptr, nullptr, nullptr, nullptr, nullptr, 
        nullptr, nullptr, g_mockArduinoAdapter,
        g_mockOnChangeLocalIpUseCase, g_mockMqttManager);
}

static void loop_service_factory_teardown(void) {
    delete g_loopServiceFactory;
    delete g_mockArduinoAdapter;
    delete g_mockOnChangeLocalIpUseCase;
    delete g_mockMqttManager;
    
    g_loopServiceFactory = nullptr;
    g_mockArduinoAdapter = nullptr;
    g_mockOnChangeLocalIpUseCase = nullptr;
    g_mockMqttManager = nullptr;
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
    
    g_loopServiceFactory->setOnChangeLocalIpUseCase(&newIpUseCase);
    g_loopServiceFactory->setMqttManager(&newMqttManager);
    
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::CONFIGURED_DONE);
    
    service->loop();
    
    TEST_ASSERT_FALSE(g_mockOnChangeLocalIpUseCase->performCalled);
    TEST_ASSERT_FALSE(g_mockMqttManager->loopCalled);
    TEST_ASSERT_TRUE(newIpUseCase.performCalled);
    TEST_ASSERT_TRUE(newMqttManager.loopCalled);
    
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
    
    printf("\n----- LoopServiceFactory Tests Complete -----\n");
    
    return 0;
}
