#include <unity.h>

// Arduino.h stand-in first: ArduinoJsonConverter.h pulls in headers that expect it (env:native).
#include "Mock/ArduinoMock.h"

#include "Application/Services/DiagnosticService.h"
#include "Application/Services/Net/LinkSupervisor.h"
#include "Mock/Application/MockOutbox.h"
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
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"

// Global test variables
static MockRestAPIService g_mockRestApi;
static MockHybridAPIService g_mockHybridApi;
static MockModuleConfig g_mockModuleConfig;
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockSettings g_mockSettings;
static MockNTPTimeAdapter g_mockTimeAdapter;
static MockWifiAdapter g_mockWifiAdapter;
static MockArduinoAdapter g_mockArduinoAdapter;
static MockMqttManager g_mockMqttManager;
static ArduinoJsonConverter g_jsonConverter;
static DiagnosticService* g_diagnosticService;
static LinkSupervisor* g_linkSupervisor;

// Test setup and teardown functions
void diagnostic_service_setup(void) {
    // Reset all mocks
    g_mockRestApi.reset();
    g_mockHybridApi.reset();
    g_mockModuleConfig.reset();
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockSettings.reset();
    g_mockTimeAdapter.reset();
    g_mockWifiAdapter.reset();
    g_mockArduinoAdapter.reset();
    g_mockMqttManager.reset();
    
    // Enable diagnostic debugging
    g_mockDebugSystem.registerFeature(DebugFeatures::DIAGNOSTIC_DEBUG);
    g_mockDebugSystem.setDebugFeatureState(DebugFeatures::DIAGNOSTIC_DEBUG, true);
    
    // Configure module config with settings
    g_mockModuleConfig.setSettings(&g_mockSettings);
    g_mockModuleConfig.setModuleUid("test-module-uid-1234");
    g_mockModuleConfig.setFirmwareVersion("2.1.1");
    
    // Configure settings
    g_mockSettings.setUser("test@example.com");
    g_mockSettings.setAccountUid("test-account-uid-5678");
    
    // Configure time adapter
    g_mockTimeAdapter.setFormattedTime("2023-04-15T12:34:56Z");
    
    // Configure WiFi adapter
    g_mockWifiAdapter.setConnected(true);
    g_mockWifiAdapter.rssi = -65;
    
    // Configure Arduino adapter
    g_mockArduinoAdapter.currentMillis = 3600000; // 1 hour
    g_mockArduinoAdapter.setFreeHeap(100000);
    g_mockArduinoAdapter.setMinFreeHeap(80000);
    g_mockArduinoAdapter.setHeapSize(200000);
    g_mockArduinoAdapter.setMaxAllocHeap(150000);
    g_mockArduinoAdapter.setPsramSize(8388608);
    g_mockArduinoAdapter.setFreePsram(8000000);
    
    // Configure MQTT manager
    g_mockMqttManager.isConnectedReturnValue = true;
    g_mockMqttManager.mqttFailsReturnValue = 5;
    g_mockMqttManager.inboundOversizeDropsReturnValue = 3;
    
    // Configure Hybrid API service
    g_mockHybridApi.sendDiagnosticDataReturnValue = true;
    
    // Create diagnostic service with both REST and Hybrid APIs
    g_diagnosticService = new DiagnosticService(
        &g_mockRestApi,
        &g_mockModuleConfig,
        g_mockLogger,
        g_mockDebugSystem,
        &g_jsonConverter,
        &g_mockWifiAdapter,
        &g_mockTimeAdapter,
        &g_mockArduinoAdapter,
        &g_mockMqttManager,
        &g_mockHybridApi
    );

    // What the report reads the network failures off: the decisions about the link.
    g_linkSupervisor = new LinkSupervisor(&g_mockRestApi, &g_mockModuleConfig, g_mockLogger, g_mockDebugSystem,
                                          &g_mockWifiAdapter, &g_mockTimeAdapter, &g_mockArduinoAdapter,
                                          &g_mockMqttManager);
    g_diagnosticService->setLinkSupervisor(g_linkSupervisor);
}

void diagnostic_service_teardown(void) {
    delete g_diagnosticService;
    g_diagnosticService = nullptr;
    delete g_linkSupervisor;
    g_linkSupervisor = nullptr;
}

// Test cases
void test_perform_collects_correct_diagnostic_data(void) {
    diagnostic_service_setup();
    
    // Perform diagnostics
    DiagnosticDto diagnosticData = g_diagnosticService->perform();
    
    // Verify data was collected correctly
    TEST_ASSERT_EQUAL_INT(-65, diagnosticData.rssi);
    TEST_ASSERT_EQUAL_STRING("test-module-uid-1234", diagnosticData.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("test@example.com", diagnosticData.email.c_str());
    TEST_ASSERT_EQUAL_STRING("test-account-uid-5678", diagnosticData.accountUid.c_str());
    TEST_ASSERT_EQUAL_STRING("2023-04-15T12:34:56Z", diagnosticData.timestamp.c_str());
    TEST_ASSERT_EQUAL_UINT32(3600000, diagnosticData.uptimeMs);
    TEST_ASSERT_EQUAL_INT(0, diagnosticData.networkFails);
    TEST_ASSERT_EQUAL_INT(0, diagnosticData.apiSendFails);
    TEST_ASSERT_EQUAL_STRING("connected", diagnosticData.apiStatus.c_str());
    TEST_ASSERT_EQUAL_STRING("connected", diagnosticData.mqttStatus.c_str());
    TEST_ASSERT_EQUAL_INT(5, diagnosticData.mqttFails);
    // Inbound messages dropped for exceeding the MQTT size limit (GDV1-3813)
    TEST_ASSERT_EQUAL_UINT32(3, diagnosticData.mqttOversizeDrops);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, diagnosticData.apiSendFailRatio);
    TEST_ASSERT_EQUAL_STRING("2.1.1", diagnosticData.firmware.c_str());
    TEST_ASSERT_EQUAL_UINT32(100000, diagnosticData.freeHeap);
    TEST_ASSERT_EQUAL_UINT32(80000, diagnosticData.minFreeHeap);
    TEST_ASSERT_EQUAL_UINT32(200000, diagnosticData.heapSize);
    TEST_ASSERT_EQUAL_UINT32(150000, diagnosticData.maxAllocHeap);
    TEST_ASSERT_EQUAL_UINT32(8388608, diagnosticData.psramSize);
    TEST_ASSERT_EQUAL_UINT32(8000000, diagnosticData.psramFree);
    TEST_ASSERT_EQUAL_UINT32(0, diagnosticData.failsInHour);

    diagnostic_service_teardown();
}

void test_perform_serializes_psram_to_json(void) {
    diagnostic_service_setup();

    DiagnosticDto diagnosticData = g_diagnosticService->perform();
    std::string json = g_jsonConverter.diagnosticToJson(diagnosticData);

    TEST_ASSERT_TRUE(json.find("\"psramSize\":8388608") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"psramFree\":8000000") != std::string::npos);

    diagnostic_service_teardown();
}

// The largest block a single allocation can still get. Free heap can be plentiful while this
// number is already too small for the next TLS handshake, which is what a fragmented heap looks
// like from the outside - so it has to leave the module with the diagnostics.
void test_perform_serializes_max_alloc_heap_to_json(void) {
    diagnostic_service_setup();

    DiagnosticDto diagnosticData = g_diagnosticService->perform();
    std::string json = g_jsonConverter.diagnosticToJson(diagnosticData);

    TEST_ASSERT_TRUE(json.find("\"maxAllocHeap\":150000") != std::string::npos);

    diagnostic_service_teardown();
}

// GDV1-3813: the count of inbound messages dropped for exceeding the MQTT size limit
// has to leave the module with the diagnostics, otherwise the drop is only visible
// with a live debug session attached.
void test_perform_serializes_mqtt_oversize_drops_to_json(void) {
    diagnostic_service_setup();

    DiagnosticDto diagnosticData = g_diagnosticService->perform();
    std::string json = g_jsonConverter.diagnosticToJson(diagnosticData);

    TEST_ASSERT_TRUE(json.find("\"mqttOversizeDrops\":3") != std::string::npos);

    diagnostic_service_teardown();
}

void test_perform_handles_offline_state(void) {
    diagnostic_service_setup();
    
    // Set offline state
    g_mockWifiAdapter.setConnected(false);
    g_mockMqttManager.isConnectedReturnValue = false;
    g_mockHybridApi.isModuleOnlineReturnValue = false;
    g_mockRestApi.countOffline = 10;
    
    // The link's supervision decides; the report reads what it decided
    g_linkSupervisor->superviseOnce();
    DiagnosticDto diagnosticData = g_diagnosticService->perform();
    
    // Verify offline state is detected
    TEST_ASSERT_EQUAL_STRING("disconnected", diagnosticData.apiStatus.c_str());
    TEST_ASSERT_EQUAL_STRING("disconnected", diagnosticData.mqttStatus.c_str());
    TEST_ASSERT_GREATER_THAN(0, diagnosticData.networkFails); // Should increment on state change
    
    diagnostic_service_teardown();
}

void test_perform_and_send_uses_hybrid_api(void) {
    diagnostic_service_setup();
    
    // Reset any previously collected diagnostics
    g_mockRestApi.reset();
    g_mockHybridApi.reset();
    
    // Perform and send diagnostics
    bool result = g_diagnosticService->performAndSend();
    
    // Verify Hybrid API was called correctly
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockHybridApi.sendDiagnosticDataCalled);
    TEST_ASSERT_FALSE(g_mockRestApi.sendDiagnosticDataCalled); // REST API should not be called
    
    // Check that diagnostic data was sent with correct values
    DiagnosticDto sentData = g_mockHybridApi.lastDiagnosticData;
    TEST_ASSERT_EQUAL_INT(-65, sentData.rssi);
    TEST_ASSERT_EQUAL_STRING("test-module-uid-1234", sentData.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("test@example.com", sentData.email.c_str());
    TEST_ASSERT_EQUAL_STRING("2.1.1", sentData.firmware.c_str());
    
    diagnostic_service_teardown();
}

void test_perform_and_send_falls_back_to_rest_api(void) {
    diagnostic_service_setup();
    
    // Create a diagnostic service with only REST API
    delete g_diagnosticService;
    g_diagnosticService = new DiagnosticService(
        &g_mockRestApi,
        &g_mockModuleConfig,
        g_mockLogger,
        g_mockDebugSystem,
        &g_jsonConverter,
        &g_mockWifiAdapter,
        &g_mockTimeAdapter,
        &g_mockArduinoAdapter,
        &g_mockMqttManager,
        nullptr // No Hybrid API
    );
    
    // Reset mocks
    g_mockRestApi.reset();
    g_mockHybridApi.reset();
    
    // Perform and send diagnostics
    bool result = g_diagnosticService->performAndSend();
    
    // Verify REST API was called and Hybrid API was not
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_FALSE(g_mockHybridApi.sendDiagnosticDataCalled);
    
    diagnostic_service_teardown();
}

void test_perform_and_send_handles_failure(void) {
    diagnostic_service_setup();
    
    // Force send to fail
    g_mockHybridApi.sendDiagnosticDataReturnValue = false;
    
    // Perform and send diagnostics
    bool result = g_diagnosticService->performAndSend();
    
    // Verify result
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockHybridApi.sendDiagnosticDataCalled);
    
    diagnostic_service_teardown();
}

void test_perform_records_failures(void) {
    diagnostic_service_setup();
    
    // Record some failures
    for (int i = 0; i < 3; i++) {
        g_linkSupervisor->recordFailure();
    }
    
    // Perform diagnostics
    DiagnosticDto diagnosticData = g_diagnosticService->perform();
    
    // Verify failures were recorded
    TEST_ASSERT_EQUAL_UINT32(3, diagnosticData.failsInHour);
    
    diagnostic_service_teardown();
}

// The report decides nothing: a link that was there and went away, a module offline for over an hour -
// the report of both restarts no radio, asks for no restart and has no clock synchronized. Those are
// the link's supervision's decisions, on its own tick.
void test_perform_is_a_report_and_decides_nothing(void) {
    diagnostic_service_setup();
    MockOutbox outbox;
    g_diagnosticService->setOutbox(&outbox);
    g_mockModuleConfig.setModuleType(MODULE_TYPE_SENSOR);
    g_mockModuleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
    g_mockTimeAdapter.reset();

    g_mockArduinoAdapter.currentMillis = 30000;
    g_diagnosticService->perform();

    // The link goes, and stays gone, with the broker and the server, for over an hour.
    g_mockWifiAdapter.setConnected(false);
    g_mockMqttManager.isConnectedReturnValue = false;
    g_mockRestApi.countOffline = 10;
    for (unsigned long now = 60000; now <= 60000 + 2 * 60 * 60 * 1000UL; now += 30000) {
        g_mockArduinoAdapter.currentMillis = now;
        g_diagnosticService->perform();
    }
    // And comes back.
    g_mockWifiAdapter.setConnected(true);
    g_mockMqttManager.isConnectedReturnValue = true;
    g_mockRestApi.countOffline = 0;
    g_mockArduinoAdapter.currentMillis += 30000;
    g_diagnosticService->perform();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, outbox.wifiReconnectRequests,
                                     "the diagnostic report asked for the radio to be restarted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockWifiAdapter.reconnectCallCount,
                                  "the diagnostic report restarted the radio");
    TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::CONFIGURED_DONE, g_mockModuleConfig.getRequestedModuleMode(),
                              "the diagnostic report asked for a restart");
    TEST_ASSERT_FALSE_MESSAGE(g_mockTimeAdapter.wasModified("forceSyncTime"),
                              "the diagnostic report had the clock synchronized");

    diagnostic_service_teardown();
}

// Run all tests
extern "C" void run_diagnostic_service_tests(void) {
    RUN_TEST(test_perform_collects_correct_diagnostic_data);
    RUN_TEST(test_perform_serializes_psram_to_json);
    RUN_TEST(test_perform_serializes_max_alloc_heap_to_json);
    RUN_TEST(test_perform_serializes_mqtt_oversize_drops_to_json);
    RUN_TEST(test_perform_handles_offline_state);
    RUN_TEST(test_perform_and_send_uses_hybrid_api);
    RUN_TEST(test_perform_and_send_falls_back_to_rest_api);
    RUN_TEST(test_perform_and_send_handles_failure);
    RUN_TEST(test_perform_records_failures);
    RUN_TEST(test_perform_is_a_report_and_decides_nothing);
}
