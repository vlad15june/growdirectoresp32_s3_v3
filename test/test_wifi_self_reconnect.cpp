#include <unity.h>
#include "Application/Services/DiagnosticService.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"

static MockRestAPIService wr_mockRestApi;
static MockHybridAPIService wr_mockHybridApi;
static MockModuleConfig wr_mockModuleConfig;
static MockLogger wr_mockLogger;
static MockDebugSystem wr_mockDebugSystem;
static MockSettings wr_mockSettings;
static MockNTPTimeAdapter wr_mockTimeAdapter;
static MockWifiAdapter wr_mockWifiAdapter;
static MockArduinoAdapter wr_mockArduinoAdapter;
static MockMqttManager wr_mockMqttManager;
static JsonConverter wr_jsonConverter;
static DiagnosticService* wr_diagnosticService;

static const unsigned long TEN_MINUTES_MS = 10 * 60 * 1000;

static void wr_setup(void) {
    wr_mockRestApi.reset();
    wr_mockHybridApi.reset();
    wr_mockModuleConfig.reset();
    wr_mockLogger.reset();
    wr_mockDebugSystem.reset();
    wr_mockSettings.reset();
    wr_mockTimeAdapter.reset();
    wr_mockWifiAdapter.reset();
    wr_mockArduinoAdapter.reset();
    wr_mockMqttManager.reset();

    wr_mockDebugSystem.registerFeature(DebugFeatures::SETUP_DEBUG);
    wr_mockDebugSystem.setDebugFeatureState(DebugFeatures::SETUP_DEBUG, true);

    wr_mockModuleConfig.setSettings(&wr_mockSettings);
    wr_mockModuleConfig.setModuleUid("wifi-reconnect-uid");
    wr_mockModuleConfig.setFirmwareVersion("2.17.18");

    wr_mockSettings.setUser("test@example.com");
    wr_mockSettings.setAccountUid("acct-uid");

    wr_mockTimeAdapter.setFormattedTime("2026-07-10T10:00:00Z");

    wr_mockWifiAdapter.setConnected(true);
    wr_mockWifiAdapter.rssi = -60;

    wr_mockArduinoAdapter.setMillis(0);

    wr_mockMqttManager.isConnectedReturnValue = true;

    wr_mockHybridApi.sendDiagnosticDataReturnValue = true;

    wr_diagnosticService = new DiagnosticService(
        &wr_mockRestApi,
        &wr_mockModuleConfig,
        wr_mockLogger,
        wr_mockDebugSystem,
        &wr_jsonConverter,
        &wr_mockWifiAdapter,
        &wr_mockTimeAdapter,
        &wr_mockArduinoAdapter,
        &wr_mockMqttManager,
        &wr_mockHybridApi
    );
}

static void wr_teardown(void) {
    delete wr_diagnosticService;
    wr_diagnosticService = nullptr;
}

static void test_edge_down_triggers_single_reconnect(void) {
    wr_setup();

    wr_diagnosticService->perform();
    TEST_ASSERT_EQUAL_INT(0, wr_mockWifiAdapter.reconnectCallCount);

    wr_mockWifiAdapter.reconnectSuccess = false;
    wr_mockWifiAdapter.setConnected(false);
    wr_diagnosticService->perform();
    TEST_ASSERT_EQUAL_INT(1, wr_mockWifiAdapter.reconnectCallCount);

    wr_teardown();
}

static void test_stable_link_never_reconnects(void) {
    wr_setup();

    for (int i = 0; i < 5; i++) {
        wr_mockArduinoAdapter.setMillis((unsigned long)i * TEN_MINUTES_MS);
        wr_diagnosticService->perform();
    }
    TEST_ASSERT_EQUAL_INT(0, wr_mockWifiAdapter.reconnectCallCount);

    wr_teardown();
}

static void test_retry_is_throttled_to_ten_minutes(void) {
    wr_setup();
    wr_mockWifiAdapter.reconnectSuccess = false;

    wr_mockArduinoAdapter.setMillis(1000000);
    wr_mockWifiAdapter.setConnected(false);
    wr_diagnosticService->perform();
    TEST_ASSERT_EQUAL_INT(1, wr_mockWifiAdapter.reconnectCallCount);

    wr_mockArduinoAdapter.setMillis(1000000 + 5 * 60 * 1000);
    wr_diagnosticService->perform();
    TEST_ASSERT_EQUAL_INT(1, wr_mockWifiAdapter.reconnectCallCount);

    wr_mockArduinoAdapter.setMillis(1000000 + TEN_MINUTES_MS);
    wr_diagnosticService->perform();
    TEST_ASSERT_EQUAL_INT(2, wr_mockWifiAdapter.reconnectCallCount);

    wr_teardown();
}

static void test_recovery_rearms_edge_detection(void) {
    wr_setup();

    wr_diagnosticService->perform();

    wr_mockWifiAdapter.reconnectSuccess = true;
    wr_mockWifiAdapter.setConnected(false);
    wr_diagnosticService->perform();
    TEST_ASSERT_EQUAL_INT(1, wr_mockWifiAdapter.reconnectCallCount);
    TEST_ASSERT_TRUE(wr_mockWifiAdapter.connected);

    wr_diagnosticService->perform();
    TEST_ASSERT_EQUAL_INT(1, wr_mockWifiAdapter.reconnectCallCount);

    wr_mockWifiAdapter.setConnected(false);
    wr_diagnosticService->perform();
    TEST_ASSERT_EQUAL_INT(2, wr_mockWifiAdapter.reconnectCallCount);

    wr_teardown();
}

static void test_access_point_mode_skips_reconnect(void) {
    wr_setup();
    wr_mockWifiAdapter.setAccessPointActive(true);
    wr_mockWifiAdapter.setConnected(false);

    for (int i = 0; i < 3; i++) {
        wr_mockArduinoAdapter.setMillis((unsigned long)i * TEN_MINUTES_MS);
        wr_diagnosticService->perform();
    }
    TEST_ASSERT_EQUAL_INT(0, wr_mockWifiAdapter.reconnectCallCount);

    wr_teardown();
}

static void test_reconnect_does_not_trigger_restart(void) {
    wr_setup();
    wr_mockWifiAdapter.reconnectSuccess = false;
    wr_mockMqttManager.isConnectedReturnValue = true;

    for (int i = 0; i < 3; i++) {
        wr_mockArduinoAdapter.setMillis(1000000 + (unsigned long)i * TEN_MINUTES_MS);
        wr_mockWifiAdapter.setConnected(false);
        wr_diagnosticService->perform();
    }

    TEST_ASSERT_TRUE(wr_mockWifiAdapter.reconnectCallCount >= 1);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL_UINT8(0, wr_mockSettings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(0, wr_mockSettings.getBootType());

    wr_teardown();
}

extern "C" int run_wifi_self_reconnect_tests(void) {
    UNITY_BEGIN();

    RUN_TEST(test_edge_down_triggers_single_reconnect);
    RUN_TEST(test_stable_link_never_reconnects);
    RUN_TEST(test_retry_is_throttled_to_ten_minutes);
    RUN_TEST(test_recovery_rearms_edge_detection);
    RUN_TEST(test_access_point_mode_skips_reconnect);
    RUN_TEST(test_reconnect_does_not_trigger_restart);

    return UNITY_END();
}
