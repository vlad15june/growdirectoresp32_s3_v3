#include <unity.h>
#include "Core/Domain/UseCases/ModuleMode/OnChangeLocalIpUseCase.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/UseCases/Rules/MockSyncRulesUseCase.h"
#include "Mock/UseCases/SensorTypes/MockSyncSensorTypesUseCase.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"

// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockWifiAdapter g_mockWifiAdapter;
static MockWiFiManager g_mockWifiManager;
static MockHybridAPIService g_mockApiService;
static MockModuleConfig g_mockModuleConfig;
static MockSyncRulesUseCase g_mockSyncRulesUseCase;
static MockSyncSensorTypesUseCase g_mockSyncSensorTypesUseCase;
static ModuleStructure g_moduleStructure;
static OnChangeLocalIpUseCase* g_onChangeLocalIpUseCase;

// Test setup and teardown
void on_change_local_ip_use_case_setup(void) {
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockWifiAdapter.reset();
    g_mockWifiManager.reset();
    g_mockApiService.reset();
    g_mockModuleConfig.reset();
    g_mockSyncRulesUseCase.reset();
    
    // Enable network debugging for log messages
    g_mockDebugSystem.featureEnabled = true;
    
    // Setup a basic module structure with one node
    g_moduleStructure.nodes.clear();
    
    // Create a Node with explicit initialization
    Node testNode;
    testNode.type = "esp32";
    testNode.subtype = "relay";
    testNode.uid = "node1";
    testNode.firmware = "2.1.1";
    testNode.ip = "old_ip";
    testNode.ssid = "old_ssid";
    
    // Add the node to the module structure
    g_moduleStructure.nodes.push_back(testNode);
    
    g_onChangeLocalIpUseCase = new OnChangeLocalIpUseCase(
        g_mockWifiAdapter, 
        g_mockWifiManager, 
        g_moduleStructure, 
        g_mockApiService, 
        g_mockModuleConfig,
        g_mockSyncRulesUseCase,
        g_mockSyncSensorTypesUseCase,
        g_mockLogger, 
        g_mockDebugSystem);
}

void on_change_local_ip_use_case_teardown(void) {
    delete g_onChangeLocalIpUseCase;
    g_onChangeLocalIpUseCase = nullptr;
}

// Test functions
void test_perform_when_ip_is_zero_ip(void) {
    on_change_local_ip_use_case_setup();
    
    // Set IP to 0.0.0.0
    g_mockWifiAdapter.setLocalIP("0.0.0.0");
    
    // Execute the use case
    g_onChangeLocalIpUseCase->perform();
    
    // Verify no operations were performed on module structure or Hybrid API
    TEST_ASSERT_FALSE(g_mockApiService.sendToServerModuleStructureCalled);
    TEST_ASSERT_EQUAL_STRING("old_ip", g_moduleStructure.nodes[0].ip.c_str());
    
    on_change_local_ip_use_case_teardown();
}

void test_perform_when_wifi_not_connected(void) {
    on_change_local_ip_use_case_setup();
    
    // Set a valid IP but WiFi not connected
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(false);
    
    // Execute the use case
    g_onChangeLocalIpUseCase->perform();
    
    // Verify no operations were performed on module structure or Hybrid API
    TEST_ASSERT_FALSE(g_mockApiService.sendToServerModuleStructureCalled);
    TEST_ASSERT_EQUAL_STRING("old_ip", g_moduleStructure.nodes[0].ip.c_str());
    
    on_change_local_ip_use_case_teardown();
}

void test_perform_with_same_ip_no_change(void) {
    on_change_local_ip_use_case_setup();
    
    // First set one IP to initialize the internal _oldIpStr
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);
    g_onChangeLocalIpUseCase->perform();
    
    // Reset the mock to check if it gets called again
    g_mockApiService.reset();
    
    // Call again with the same IP
    g_onChangeLocalIpUseCase->perform();
    
    // Verify no operations were performed as the IP hasn't changed
    TEST_ASSERT_FALSE(g_mockApiService.sendToServerModuleStructureCalled);
    
    on_change_local_ip_use_case_teardown();
}

void test_perform_with_new_ip_updates_structure(void) {
    on_change_local_ip_use_case_setup();
    
    // Set a valid IP and connected WiFi
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);
    
    // Execute the use case
    g_onChangeLocalIpUseCase->perform();
    
    // Verify module structure was updated
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", g_moduleStructure.nodes[0].ip.c_str());
    TEST_ASSERT_EQUAL_STRING("TestSSID", g_moduleStructure.nodes[0].ssid.c_str());
    
    // Verify HybridAPIService was called to send updated structure
    TEST_ASSERT_TRUE(g_mockApiService.sendToServerModuleStructureCalled);
    TEST_ASSERT_EQUAL(&g_moduleStructure, g_mockApiService.lastModuleStructure);
    
    on_change_local_ip_use_case_teardown();
}

void test_perform_with_changing_ip(void) {
    on_change_local_ip_use_case_setup();
    
    // First set one IP
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);
    g_onChangeLocalIpUseCase->perform();
    
    // Reset the mock to check if it gets called again
    g_mockApiService.reset();
    
    // Now change the IP
    g_mockWifiAdapter.setLocalIP("192.168.1.101");
    g_onChangeLocalIpUseCase->perform();
    
    // Verify module structure was updated with new IP
    TEST_ASSERT_EQUAL_STRING("192.168.1.101", g_moduleStructure.nodes[0].ip.c_str());
    
    // Verify HybridAPIService was called to send updated structure
    TEST_ASSERT_TRUE(g_mockApiService.sendToServerModuleStructureCalled);
    
    on_change_local_ip_use_case_teardown();
}

void test_perform_send_structure_failure_triggers_restart(void) {
    on_change_local_ip_use_case_setup();

    // Set a valid IP and connected WiFi
    g_mockWifiAdapter.setLocalIP("192.168.1.200");
    g_mockWifiAdapter.setConnected(true);

    // Simulate HybridAPIService send failure
    g_mockApiService.sendModuleStructureReturnValue = false;

    // Ensure requestedModuleMode is not CONFIGURED_NEED_RESTART before
    g_mockModuleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);

    // Execute the use case
    g_onChangeLocalIpUseCase->perform();

    // Should have attempted to send structure
    TEST_ASSERT_TRUE(g_mockApiService.sendToServerModuleStructureCalled);

    // Should have set requestedModuleMode to CONFIGURED_NEED_RESTART
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_NEED_RESTART, g_mockModuleConfig.getRequestedModuleMode());

    on_change_local_ip_use_case_teardown();
}

// Main test runner function
extern "C" int run_on_change_local_ip_use_case_tests(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_perform_when_ip_is_zero_ip);
    RUN_TEST(test_perform_when_wifi_not_connected);
    RUN_TEST(test_perform_with_same_ip_no_change);
    RUN_TEST(test_perform_with_new_ip_updates_structure);
    RUN_TEST(test_perform_with_changing_ip);
    RUN_TEST(test_perform_send_structure_failure_triggers_restart);
    
    return UNITY_END();
}
