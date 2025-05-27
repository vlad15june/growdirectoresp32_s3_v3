#include <unity.h>
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Mqtt/MockMqttApiService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"

static MockRestAPIService g_mockRestApi;
static MockMqttApiService g_mockMqttApi;
static MockModuleConfig g_mockModuleConfig;
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static HybridAPIService* g_hybridApiService;
static ModuleStructure g_moduleStructure;
static DiagnosticDto g_diagnosticData;

void hybrid_api_service_setup() {
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockModuleConfig.reset();
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    
    g_moduleStructure.growdirectorUid = "test-growdirector-uid";
    g_moduleStructure.firmware = "2.1.1";
    
    g_diagnosticData.uid = "test-device-uid";
    g_diagnosticData.rssi = -75;
    g_diagnosticData.firmware = "2.1.1";
    
    g_mockDebugSystem.registerFeature(DebugFeatures::NET_DEBUG);
    g_mockDebugSystem.registerFeature(DebugFeatures::MQTT_DEBUG);
    g_mockDebugSystem.setDebugFeatureState(DebugFeatures::NET_DEBUG, true);
    g_mockDebugSystem.setDebugFeatureState(DebugFeatures::MQTT_DEBUG, true);
    
    g_hybridApiService = new HybridAPIService(
        &g_mockRestApi,
        &g_mockMqttApi,
        &g_mockModuleConfig,
        &g_mockLogger,
        &g_mockDebugSystem,
        ApiPriority::REST_PREFERRED
    );
}

void hybrid_api_service_teardown() {
    delete g_hybridApiService;
    g_hybridApiService = nullptr;
}

void test_init_returns_true_if_either_api_is_available() {
    hybrid_api_service_setup();
    
    g_mockRestApi.reset();
    bool result = g_hybridApiService->init();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.initCalled);
    
    g_mockRestApi.reset();
    g_hybridApiService = new HybridAPIService(
        &g_mockRestApi,
        nullptr,
        &g_mockModuleConfig,
        &g_mockLogger,
        &g_mockDebugSystem
    );
    result = g_hybridApiService->init();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.initCalled);
    
    delete g_hybridApiService;
    g_hybridApiService = new HybridAPIService(
        nullptr,
        &g_mockMqttApi,
        &g_mockModuleConfig,
        &g_mockLogger,
        &g_mockDebugSystem
    );
    result = g_hybridApiService->init();
    TEST_ASSERT_TRUE(result);
    
    delete g_hybridApiService;
    g_hybridApiService = new HybridAPIService(
        nullptr,
        nullptr,
        &g_mockModuleConfig,
        &g_mockLogger,
        &g_mockDebugSystem
    );
    result = g_hybridApiService->init();
    TEST_ASSERT_FALSE(result);
    
    hybrid_api_service_teardown();
}

void test_is_module_online_with_both_apis() {
    hybrid_api_service_setup();
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    bool result = g_hybridApiService->isModuleOnline();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.isModuleOnlineCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.isModuleOnlineCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockMqttApi.isModuleOnlineReturnValue = false;
    result = g_hybridApiService->isModuleOnline();
    TEST_ASSERT_TRUE(result);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.isModuleOnlineReturnValue = false;
    result = g_hybridApiService->isModuleOnline();
    TEST_ASSERT_TRUE(result);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.isModuleOnlineReturnValue = false;
    g_mockMqttApi.isModuleOnlineReturnValue = false;
    result = g_hybridApiService->isModuleOnline();
    TEST_ASSERT_FALSE(result);
    
    hybrid_api_service_teardown();
}

void test_send_to_server_module_structure_with_different_priorities() {
    hybrid_api_service_setup();
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    g_mockRestApi.sendToServerModuleStructureReturnValue = true;
    bool result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendToServerModuleStructureCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendToServerModuleStructureCalled);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    g_mockRestApi.sendToServerModuleStructureReturnValue = false;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_ONLY);
    g_mockMqttApi.sendToServerModuleStructureReturnValue = true;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendToServerModuleStructureCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendToServerModuleStructureCalled);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_ONLY);
    g_mockMqttApi.sendToServerModuleStructureReturnValue = false;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_PREFERRED);
    g_mockRestApi.sendToServerModuleStructureReturnValue = true;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendToServerModuleStructureCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendToServerModuleStructureCalled);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_PREFERRED);
    g_mockRestApi.sendToServerModuleStructureReturnValue = false;
    g_mockMqttApi.sendToServerModuleStructureReturnValue = true;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendToServerModuleStructureCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendToServerModuleStructureCalled);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_PREFERRED);
    g_mockRestApi.sendToServerModuleStructureReturnValue = false;
    g_mockMqttApi.sendToServerModuleStructureReturnValue = false;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_PREFERRED);
    g_mockMqttApi.sendToServerModuleStructureReturnValue = true;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendToServerModuleStructureCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendToServerModuleStructureCalled);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_PREFERRED);
    g_mockMqttApi.sendToServerModuleStructureReturnValue = false;
    g_mockRestApi.sendToServerModuleStructureReturnValue = true;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendToServerModuleStructureCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendToServerModuleStructureCalled);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_PREFERRED);
    g_mockMqttApi.sendToServerModuleStructureReturnValue = false;
    g_mockRestApi.sendToServerModuleStructureReturnValue = false;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::BOTH);
    g_mockRestApi.sendToServerModuleStructureReturnValue = true;
    g_mockMqttApi.sendToServerModuleStructureReturnValue = true;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_TRUE(result);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::BOTH);
    g_mockRestApi.sendToServerModuleStructureReturnValue = false;
    g_mockMqttApi.sendToServerModuleStructureReturnValue = false;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure);
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    g_mockMqttApi.sendToServerModuleStructureReturnValue = true;
    result = g_hybridApiService->sendToServerModuleStructure(g_moduleStructure, ApiPriority::MQTT_ONLY);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendToServerModuleStructureCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendToServerModuleStructureCalled);

    hybrid_api_service_teardown();
}

void test_send_diagnostic_data_with_different_priorities() {
    hybrid_api_service_setup();
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    bool result = g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_ONLY);
    result = g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_PREFERRED);
    result = g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendDiagnosticDataReturnValue = false;
    g_hybridApiService->setApiPriority(ApiPriority::REST_PREFERRED);
    result = g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_PREFERRED);
    result = g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockMqttApi.sendDiagnosticDataReturnValue = false;
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_PREFERRED);
    result = g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::BOTH);
    result = g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendDiagnosticDataReturnValue = false;
    g_mockMqttApi.sendDiagnosticDataReturnValue = false;
    g_hybridApiService->setApiPriority(ApiPriority::BOTH);
    result = g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendDiagnosticDataReturnValue = true;
    g_mockMqttApi.sendDiagnosticDataReturnValue = true;
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    result = g_hybridApiService->sendDiagnosticData(g_diagnosticData, ApiPriority::MQTT_ONLY);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendDiagnosticDataCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDiagnosticDataCalled);
    
    hybrid_api_service_teardown();
}

void test_failure_tracking() {
    hybrid_api_service_setup();
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendDiagnosticDataReturnValue = false;
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    
    for (int i = 0; i < 3; i++) {
        g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    }
    
    TEST_ASSERT_EQUAL(3, g_hybridApiService->getRestApiSendFails());
    TEST_ASSERT_EQUAL(0, g_hybridApiService->getMqttApiSendFails());
    TEST_ASSERT_GREATER_THAN(0, g_hybridApiService->getApiSendFailRatio());
    
    g_hybridApiService->resetApiStatistics();
    TEST_ASSERT_EQUAL(0, g_hybridApiService->getRestApiSendFails());
    TEST_ASSERT_EQUAL(0, g_hybridApiService->getMqttApiSendFails());
    TEST_ASSERT_EQUAL(0, g_hybridApiService->getApiSendFailRatio());
    
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockMqttApi.sendDiagnosticDataReturnValue = false;
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_ONLY);
    
    for (int i = 0; i < 2; i++) {
        g_hybridApiService->sendDiagnosticData(g_diagnosticData);
    }
    
    TEST_ASSERT_EQUAL(0, g_hybridApiService->getRestApiSendFails());
    TEST_ASSERT_EQUAL(2, g_hybridApiService->getMqttApiSendFails());
    TEST_ASSERT_GREATER_THAN(0, g_hybridApiService->getApiSendFailRatio());
    
    hybrid_api_service_teardown();
}

void test_rest_only_methods() {
    hybrid_api_service_setup();

    g_mockRestApi.reset();
    g_mockRestApi.sendRuleStateCommandReturnValue = true;
    bool result = g_hybridApiService->sendRuleStateCommand("test-rule-state");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_EQUAL_STRING("test-rule-state", g_mockRestApi.lastRuleStateCommand);

    g_mockRestApi.reset();
    g_mockRestApi.sendRuleStateCommandReturnValue = false;
    result = g_hybridApiService->sendRuleStateCommand("test-rule-state");
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    result = g_hybridApiService->sendByUidRuleStateCommand("test-uid", "test-rule-state");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendByUidRuleStateCommandCalled);

    g_mockRestApi.reset();
    g_mockRestApi.sendFirmwareVersionReturnValue = true;
    result = g_hybridApiService->sendFirmwareVersion();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendFirmwareVersionCalled);

    g_mockRestApi.reset();
    g_mockRestApi.sendFirmwareVersionReturnValue = false;
    result = g_hybridApiService->sendFirmwareVersion();
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    g_mockRestApi.sendDeviceCommandExecutionResultReturnValue = true;
    result = g_hybridApiService->sendDeviceCommandExecutionResult("test-command");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);

    g_mockRestApi.reset();
    g_mockRestApi.sendDeviceCommandExecutionResultReturnValue = false;
    result = g_hybridApiService->sendDeviceCommandExecutionResult("test-command");
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    g_mockRestApi.sendAnalogInputDeviceCommandExecutionResultReturnValue = true;
    result = g_hybridApiService->sendAnalogInputDeviceCommandExecutionResult("test-command");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendAnalogInputDeviceCommandExecutionResultCalled);

    g_mockRestApi.reset();
    g_mockRestApi.sendAnalogInputDeviceCommandExecutionResultReturnValue = false;
    result = g_hybridApiService->sendAnalogInputDeviceCommandExecutionResult("test-command");
    TEST_ASSERT_FALSE(result);

    g_mockRestApi.reset();
    g_mockRestApi.sendModuleCommandExecutionResultReturnValue = true;
    result = g_hybridApiService->sendModuleCommandExecutionResult("test-command");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendModuleCommandExecutionResultCalled);

    g_mockRestApi.reset();
    g_mockRestApi.sendModuleCommandExecutionResultReturnValue = false;
    result = g_hybridApiService->sendModuleCommandExecutionResult("test-command");
    TEST_ASSERT_FALSE(result);

    hybrid_api_service_teardown();
}

extern "C" int run_hybrid_api_service_tests(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_init_returns_true_if_either_api_is_available);
    RUN_TEST(test_is_module_online_with_both_apis);
    RUN_TEST(test_send_to_server_module_structure_with_different_priorities);
    RUN_TEST(test_send_diagnostic_data_with_different_priorities);
    RUN_TEST(test_failure_tracking);
    RUN_TEST(test_rest_only_methods);
    
    return UNITY_END();
}
