#include <unity.h>
#include <stdio.h>
#include "Core/Domain/UseCases/ModuleMode/OnConnectivityRestoredUseCase.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Core/Domain/MockNetworkStatusProvider.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/UseCases/Rules/MockSyncRulesUseCase.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

static const unsigned long COOLDOWN_MS = 5 * 60 * 1000;

static MockLogger g_logger;
static MockDebugSystem g_debugSystem;
static MockNetworkStatusProvider g_networkStatus;
static MockModuleConfig g_moduleConfig;
static MockSyncRulesUseCase g_syncRules;
static MockArduinoAdapter g_arduino;
static OnConnectivityRestoredUseCase* g_useCase;

static void connectivity_restored_setup(void) {
    g_logger.reset();
    g_debugSystem.reset();
    g_moduleConfig.reset();
    g_syncRules.reset();
    g_arduino.reset();
    g_networkStatus.offline = false;
    g_moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_arduino.setMillis(1000000);

    g_useCase = new OnConnectivityRestoredUseCase(
        &g_networkStatus,
        &g_syncRules,
        &g_moduleConfig,
        &g_arduino,
        &g_logger,
        &g_debugSystem);
}

static void restore_once(void) {
    g_networkStatus.offline = true;
    g_useCase->perform();
    g_networkStatus.offline = false;
    g_useCase->perform();
}

static void connectivity_restored_teardown(void) {
    delete g_useCase;
    g_useCase = nullptr;
}

static void test_offline_to_online_fires_once(void) {
    connectivity_restored_setup();

    g_networkStatus.offline = false;
    g_useCase->init();

    g_networkStatus.offline = true;
    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(0, g_syncRules.forceSyncCallCount);

    g_networkStatus.offline = false;
    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(1, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

static void test_staying_online_does_not_fire(void) {
    connectivity_restored_setup();

    g_networkStatus.offline = false;
    g_useCase->init();

    g_networkStatus.offline = true;
    g_useCase->perform();
    g_networkStatus.offline = false;
    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(1, g_syncRules.forceSyncCallCount);

    g_useCase->perform();
    g_useCase->perform();
    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(1, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

static void test_staying_offline_does_not_fire(void) {
    connectivity_restored_setup();

    g_networkStatus.offline = false;
    g_useCase->init();

    g_networkStatus.offline = true;
    g_useCase->perform();
    g_useCase->perform();
    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(0, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

static void test_online_to_offline_does_not_fire(void) {
    connectivity_restored_setup();

    g_networkStatus.offline = false;
    g_useCase->init();

    g_networkStatus.offline = true;
    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(0, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

static void test_sensor_module_skips(void) {
    connectivity_restored_setup();

    g_moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
    g_networkStatus.offline = true;
    g_useCase->init();

    g_networkStatus.offline = false;
    g_useCase->perform();
    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(0, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

static void test_init_seeds_offline_then_fires(void) {
    connectivity_restored_setup();

    g_networkStatus.offline = true;
    g_useCase->init();

    g_networkStatus.offline = false;
    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(1, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

static void test_init_seeds_online_no_spurious_fire(void) {
    connectivity_restored_setup();

    g_networkStatus.offline = false;
    g_useCase->init();

    g_useCase->perform();
    TEST_ASSERT_EQUAL_INT(0, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

static void test_throttle_suppresses_flapping_within_cooldown(void) {
    connectivity_restored_setup();

    g_arduino.setMillis(1000000);
    g_useCase->init();

    g_arduino.setMillis(1000000);
    restore_once();
    TEST_ASSERT_EQUAL_INT(1, g_syncRules.forceSyncCallCount);

    g_arduino.setMillis(1000000 + 60000);
    restore_once();
    TEST_ASSERT_EQUAL_INT(1, g_syncRules.forceSyncCallCount);

    g_arduino.setMillis(1000000 + COOLDOWN_MS - 1);
    restore_once();
    TEST_ASSERT_EQUAL_INT(1, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

static void test_throttle_allows_after_cooldown(void) {
    connectivity_restored_setup();

    g_arduino.setMillis(1000000);
    g_useCase->init();

    g_arduino.setMillis(1000000);
    restore_once();
    TEST_ASSERT_EQUAL_INT(1, g_syncRules.forceSyncCallCount);

    g_arduino.setMillis(1000000 + COOLDOWN_MS);
    restore_once();
    TEST_ASSERT_EQUAL_INT(2, g_syncRules.forceSyncCallCount);

    connectivity_restored_teardown();
}

extern "C" void run_on_connectivity_restored_tests(void) {
    printf("\n----- Running OnConnectivityRestoredUseCase Tests -----\n");
    fflush(stdout);

    RUN_TEST(test_offline_to_online_fires_once);
    RUN_TEST(test_staying_online_does_not_fire);
    RUN_TEST(test_staying_offline_does_not_fire);
    RUN_TEST(test_online_to_offline_does_not_fire);
    RUN_TEST(test_sensor_module_skips);
    RUN_TEST(test_init_seeds_offline_then_fires);
    RUN_TEST(test_init_seeds_online_no_spurious_fire);
    RUN_TEST(test_throttle_suppresses_flapping_within_cooldown);
    RUN_TEST(test_throttle_allows_after_cooldown);

    printf("\n----- OnConnectivityRestoredUseCase Tests Complete -----\n");
}
