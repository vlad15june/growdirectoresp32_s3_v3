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

// Global test variables
static LoopServiceFactory* g_loopServiceFactory;
static MockArduinoAdapter* g_mockArduinoAdapter;

// Setup and teardown
static void loop_service_factory_setup(void) {
    g_mockArduinoAdapter = new MockArduinoAdapter();
    g_loopServiceFactory = new LoopServiceFactory(
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, g_mockArduinoAdapter);
}

static void loop_service_factory_teardown(void) {
    delete g_loopServiceFactory;
    delete g_mockArduinoAdapter;
    g_loopServiceFactory = nullptr;
    g_mockArduinoAdapter = nullptr;
}

// Helper function to check if a loop service is of the expected type
template <typename ExpectedType>
static bool isExpectedType(ILoopService* service) {
    return dynamic_cast<ExpectedType*>(service) != nullptr;
}

// Test functions
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
    
    loop_service_factory_teardown();
}

static void test_invalid_mode_throws_exception(void) {
    loop_service_factory_setup();
    
    // Create a value that's not in the ModuleModeEnum
    ModuleModeEnum invalidMode = static_cast<ModuleModeEnum>(999);
    
    // Check that exception is thrown
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
    
    // Create a service
    auto service = g_loopServiceFactory->createLoopService(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
    
    // Cast to base to check if adapter is set
    auto baseService = dynamic_cast<LoopServiceBase*>(service.get());
    TEST_ASSERT_NOT_NULL(baseService);
    
    // Now we need a way to verify the Arduino adapter was injected
    // For simplicity, we'll call executeLoopImplementation which uses the adapter in LoopServiceReConfigure
    // and we can check if the MockArduinoAdapter's millis method was called
    
    // First make sure the service is the right type
    TEST_ASSERT_TRUE(isExpectedType<LoopServiceReConfigure>(service.get()));
    
    // Call loop to trigger the implementation code
    service->loop();
    
    // Verify millis was called on our mock
    TEST_ASSERT_TRUE(g_mockArduinoAdapter->millisCalled);
    
    loop_service_factory_teardown();
}

// Main test runner function
extern "C" int run_loop_service_factory_tests(void) {
    printf("\n----- Running LoopServiceFactory Tests -----\n");
    
    RUN_TEST(test_create_can_configure_service);
    RUN_TEST(test_create_re_configure_service);
    RUN_TEST(test_create_test_mode_service);
    RUN_TEST(test_create_configure_done_service);
    RUN_TEST(test_invalid_mode_throws_exception);
    RUN_TEST(test_arduino_adapter_injected);
    
    printf("\n----- LoopServiceFactory Tests Complete -----\n");
    
    return 0;
}
