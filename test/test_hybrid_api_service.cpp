#include <unity.h>
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Mqtt/MockMqttApiService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"

static MockRestAPIService g_mockRestApi;
static MockMqttApiService g_mockMqttApi;
static MockModuleConfig g_mockModuleConfig;
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static HybridAPIService* g_hybridApiService;
static ModuleStructure g_moduleStructure;
static DiagnosticDto g_diagnosticData;
static DeviceCommandUpdateDto g_deviceCommandUpdate;

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
    
    g_deviceCommandUpdate.uid = "test-device-uid";
    g_deviceCommandUpdate.status = "completed";
    g_deviceCommandUpdate.errorMessage = "";
    
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
    g_mockMqttApi.reset();
    g_mockMqttApi.sendByUidRuleStateCommandReturnValue = true;
    bool result = g_hybridApiService->sendByUidRuleStateCommand("test-uid", "test-rule-state");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_FALSE(g_mockRestApi.sendByUidRuleStateCommandCalled);

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockMqttApi.sendByUidRuleStateCommandReturnValue = false;
    result = g_hybridApiService->sendByUidRuleStateCommand("test-uid", "test-rule-state");
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendByUidRuleStateCommandCalled);
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
    result = g_hybridApiService->sendDeviceCommandExecutionResult(g_deviceCommandUpdate);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_EQUAL_STRING("test-device-uid", g_mockRestApi.lastDeviceCommandUpdate.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("completed", g_mockRestApi.lastDeviceCommandUpdate.status.c_str());

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
    ModuleCommandUpdateDto commandUpdate;
    commandUpdate.uid = "test-module-uid";
    commandUpdate.status = "completed";
    commandUpdate.errorMessage = "";
    std::vector<ModuleCommandUpdateDto> commandUpdates = {commandUpdate};
    result = g_hybridApiService->sendModuleCommandExecutionResult(commandUpdates);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendModuleCommandExecutionResultCalled);

    g_mockRestApi.reset();
    g_mockRestApi.sendModuleCommandExecutionResultReturnValue = false;
    ModuleCommandUpdateDto commandUpdate2;
    commandUpdate2.uid = "test-module-uid";
    commandUpdate2.status = "failed";
    commandUpdate2.errorMessage = "Test execution failed";
    std::vector<ModuleCommandUpdateDto> commandUpdates2 = {commandUpdate2};
    result = g_hybridApiService->sendModuleCommandExecutionResult(commandUpdates2);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendModuleCommandExecutionResultCalled);

    hybrid_api_service_teardown();
}

void test_device_command_execution_result_with_different_priorities() {
    hybrid_api_service_setup();
    
    // Test with REST_ONLY priority - success case
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    g_mockRestApi.sendDeviceCommandExecutionResultReturnValue = true;
    
    DeviceCommandUpdateDto successCommand;
    successCommand.uid = "device-123";
    successCommand.status = "completed";
    successCommand.errorMessage = "";
    
    bool result = g_hybridApiService->sendDeviceCommandExecutionResult(successCommand);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_EQUAL_STRING("device-123", g_mockRestApi.lastDeviceCommandUpdate.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("completed", g_mockRestApi.lastDeviceCommandUpdate.status.c_str());
    
    // Test with REST_ONLY priority - failure case
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendDeviceCommandExecutionResultReturnValue = false;
    
    DeviceCommandUpdateDto failedCommand;
    failedCommand.uid = "device-456";
    failedCommand.status = "failed";
    failedCommand.errorMessage = "Hardware error";
    
    result = g_hybridApiService->sendDeviceCommandExecutionResult(failedCommand);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendDeviceCommandExecutionResultCalled);
    
    // Test with MQTT_ONLY priority - success case
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_ONLY);
    g_mockMqttApi.sendDeviceCommandExecutionResultReturnValue = true;
    
    result = g_hybridApiService->sendDeviceCommandExecutionResult(successCommand);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_EQUAL_STRING("device-123", g_mockMqttApi.lastDeviceCommandUpdate.uid.c_str());
    
    // Test with REST_PREFERRED priority - REST fails, MQTT succeeds
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_PREFERRED);
    g_mockRestApi.sendDeviceCommandExecutionResultReturnValue = false;
    g_mockMqttApi.sendDeviceCommandExecutionResultReturnValue = true;
    
    result = g_hybridApiService->sendDeviceCommandExecutionResult(successCommand);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDeviceCommandExecutionResultCalled);
    
    // Test with MQTT_PREFERRED priority - MQTT fails, REST succeeds
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_PREFERRED);
    g_mockMqttApi.sendDeviceCommandExecutionResultReturnValue = false;
    g_mockRestApi.sendDeviceCommandExecutionResultReturnValue = true;
    
    result = g_hybridApiService->sendDeviceCommandExecutionResult(successCommand);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);
    
    // Test with BOTH priority - both succeed
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::BOTH);
    g_mockRestApi.sendDeviceCommandExecutionResultReturnValue = true;
    g_mockMqttApi.sendDeviceCommandExecutionResultReturnValue = true;
    
    result = g_hybridApiService->sendDeviceCommandExecutionResult(successCommand);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDeviceCommandExecutionResultCalled);
    
    // Test with BOTH priority - both fail
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendDeviceCommandExecutionResultReturnValue = false;
    g_mockMqttApi.sendDeviceCommandExecutionResultReturnValue = false;
    
    result = g_hybridApiService->sendDeviceCommandExecutionResult(failedCommand);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendDeviceCommandExecutionResultCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendDeviceCommandExecutionResultCalled);
    
    hybrid_api_service_teardown();
}

void test_send_rule_state_command_with_different_priorities() {
    hybrid_api_service_setup();
    
    const char* testRuleStateMsg = "test-rule-state-message";
    
    // Test with REST_ONLY priority - success case
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    g_mockRestApi.sendRuleStateCommandReturnValue = true;
    
    bool result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with REST_ONLY priority - failure case
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendRuleStateCommandReturnValue = false;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with MQTT_ONLY priority - success case
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_ONLY);
    g_mockMqttApi.sendRuleStateCommandReturnValue = true;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with MQTT_ONLY priority - failure case
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockMqttApi.sendRuleStateCommandReturnValue = false;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with REST_PREFERRED priority - REST succeeds
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_PREFERRED);
    g_mockRestApi.sendRuleStateCommandReturnValue = true;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_FALSE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with REST_PREFERRED priority - REST fails, MQTT succeeds
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendRuleStateCommandReturnValue = false;
    g_mockMqttApi.sendRuleStateCommandReturnValue = true;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with REST_PREFERRED priority - both fail
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendRuleStateCommandReturnValue = false;
    g_mockMqttApi.sendRuleStateCommandReturnValue = false;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with MQTT_PREFERRED priority - MQTT succeeds
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::MQTT_PREFERRED);
    g_mockMqttApi.sendRuleStateCommandReturnValue = true;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with MQTT_PREFERRED priority - MQTT fails, REST succeeds
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockMqttApi.sendRuleStateCommandReturnValue = false;
    g_mockRestApi.sendRuleStateCommandReturnValue = true;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    
    // Test with MQTT_PREFERRED priority - both fail
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockMqttApi.sendRuleStateCommandReturnValue = false;
    g_mockRestApi.sendRuleStateCommandReturnValue = false;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    
    // Test with BOTH priority - both succeed
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::BOTH);
    g_mockRestApi.sendRuleStateCommandReturnValue = true;
    g_mockMqttApi.sendRuleStateCommandReturnValue = true;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with BOTH priority - both fail
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_mockRestApi.sendRuleStateCommandReturnValue = false;
    g_mockMqttApi.sendRuleStateCommandReturnValue = false;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    // Test with priority override parameter
    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    g_hybridApiService->setApiPriority(ApiPriority::REST_ONLY);
    g_mockMqttApi.sendRuleStateCommandReturnValue = true;
    
    result = g_hybridApiService->sendRuleStateCommand(testRuleStateMsg, ApiPriority::MQTT_ONLY);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_mockRestApi.sendRuleStateCommandCalled);
    TEST_ASSERT_TRUE(g_mockMqttApi.sendRuleStateCommandCalled);
    
    hybrid_api_service_teardown();
}

// The catalog fetch is REST-only, like the rule list, and the flag that tells a failed request
// from an account with no sensor models has to survive the trip through this service. Losing it
// here would make every outage look to SyncSensorTypesUseCase like a server saying "no models".
void test_sensor_types_list_carries_the_http_failure_flag_through() {
    hybrid_api_service_setup();

    g_mockRestApi.reset();
    g_mockMqttApi.reset();
    SensorType type;
    type.uid = "type-1";
    type.name = "type_1";
    g_mockRestApi.sensorTypesListReturnValue.push_back(type);

    bool httpFailed = true; // start from the wrong value so a service that never writes it fails
    std::vector<SensorType> types = g_hybridApiService->getSensorTypesList(&httpFailed);

    TEST_ASSERT_TRUE(g_mockRestApi.getSensorTypesListCalled);
    TEST_ASSERT_FALSE_MESSAGE(g_mockMqttApi.sendToServerModuleStructureCalled,
                              "the catalog fetch is REST-only and must not touch MQTT");
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)types.size());
    TEST_ASSERT_FALSE_MESSAGE(httpFailed,
                              "a successful fetch was reported as a failed request");

    g_mockRestApi.reset();
    g_mockRestApi.getSensorTypesListHttpFailed = true;
    httpFailed = false;
    types = g_hybridApiService->getSensorTypesList(&httpFailed);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)types.size());
    TEST_ASSERT_TRUE_MESSAGE(httpFailed,
                             "a failed request came back through this service as an empty catalog "
                             "from a server that answered");
}

// No REST service at all is not "this account has no sensor models" either.
void test_sensor_types_list_reports_a_failure_when_there_is_no_rest_service() {
    HybridAPIService service(nullptr, &g_mockMqttApi, &g_mockModuleConfig, &g_mockLogger,
                             &g_mockDebugSystem, ApiPriority::REST_PREFERRED);

    bool httpFailed = false;
    std::vector<SensorType> types = service.getSensorTypesList(&httpFailed);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)types.size());
    TEST_ASSERT_TRUE_MESSAGE(httpFailed,
                             "with no REST service the catalog fetch reported success with an "
                             "empty list");
}

extern "C" void run_hybrid_api_service_tests(void) {
    RUN_TEST(test_init_returns_true_if_either_api_is_available);
    RUN_TEST(test_is_module_online_with_both_apis);
    RUN_TEST(test_send_to_server_module_structure_with_different_priorities);
    RUN_TEST(test_send_diagnostic_data_with_different_priorities);
    RUN_TEST(test_send_rule_state_command_with_different_priorities);
    RUN_TEST(test_failure_tracking);
    RUN_TEST(test_rest_only_methods);
    RUN_TEST(test_device_command_execution_result_with_different_priorities);
    RUN_TEST(test_sensor_types_list_carries_the_http_failure_flag_through);
    RUN_TEST(test_sensor_types_list_reports_a_failure_when_there_is_no_rest_service);
}
