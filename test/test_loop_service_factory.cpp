#include <unity.h>
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Application/Interfaces/LoopServices/ILoopService.h"
#include "Application/Interfaces/LoopServices/ILoopServiceFactory.h"
#include "Application/Services/LoopServices/LoopServiceFactory.h"
#include "Application/Services/LoopServices/LoopServiceCanConfigure.h"
#include "Application/Services/LoopServices/LoopServiceReConfigure.h"
#include "Application/Services/LoopServices/LoopServiceTestMode.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"

// Global test variables
static LoopServiceFactory* g_loopServiceFactory;

// Setup and teardown
static void loop_service_factory_setup(void) {
    g_loopServiceFactory = new LoopServiceFactory();
}

static void loop_service_factory_teardown(void) {
    delete g_loopServiceFactory;
    g_loopServiceFactory = nullptr;
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

// Main test runner function
extern "C" int run_loop_service_factory_tests(void) {
    printf("\n----- Running LoopServiceFactory Tests -----\n");
    
    RUN_TEST(test_create_can_configure_service);
    RUN_TEST(test_create_re_configure_service);
    RUN_TEST(test_create_test_mode_service);
    RUN_TEST(test_create_configure_done_service);
    RUN_TEST(test_invalid_mode_throws_exception);
    
    printf("\n----- LoopServiceFactory Tests Complete -----\n");
    
    return 0;
}
