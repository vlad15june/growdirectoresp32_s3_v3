#include <unity.h>
#include "Mock/ArduinoMock.h"
#include "Application/Services/ModuleSetupService.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Application/DTOs/ModuleCommandDto.h"

// Helper class for tests
class FillModuleStructureTestHelper {
public:
    static void callFillModuleStructure(ModuleSetupService* service) {
        if (service) {
            service->fillModuleStructure();
        }
    }

    static void setWifiAdapter(ModuleSetupService* service, IWifiAdapter* wifiAdapter) {
        service->wifiAdapter = wifiAdapter;
    }
};

// Global test variables
static MockLogger g_testLogger;
static MockDebugSystem g_testDebugSystem;
static MockModuleConfig g_testModuleConfig;
static MockSettings g_testSettings;
static MockWifiAdapter g_testWifiAdapter;
static ModuleStructure g_testModuleStructure;
static ModuleSetupService* g_testModuleSetupService;

// Test setup and teardown
static void fill_module_structure_setup() {
    g_testLogger.reset();
    g_testDebugSystem.reset();
    g_testDebugSystem.featureEnabled = true;
    g_testModuleConfig.reset();
    g_testSettings.reset();
    g_testWifiAdapter.reset();
    g_testModuleStructure.nodes.clear();
    
    // Set up the mock settings for module config
    g_testModuleConfig.setSettings(&g_testSettings);
    g_testModuleConfig.setWifiAdapter(&g_testWifiAdapter);
    g_testModuleConfig.setModuleUid("AA:BB:CC:DD:EE:FF");
    
    // Set reasonable defaults for tests
    g_testWifiAdapter.setMACAddress("AA:BB:CC:DD:EE:FF");
    g_testWifiAdapter.setLocalIP("192.168.1.100");
    
    g_testSettings.setUser("test@example.com");
    g_testSettings.setSsid("Test_SSID");
    g_testSettings.setAccountUid("test-account-uid");
    g_testSettings.setBootType(BOOT_TYPE_POWER_ON);
    
    g_testModuleSetupService = new ModuleSetupService(
        &g_testLogger,
        &g_testDebugSystem,
        &g_testModuleConfig,
        nullptr, nullptr, nullptr, nullptr, // ledAdapter, buttonAdapter, ledUseCases, buttonUseCases
        &g_testWifiAdapter,
        nullptr, nullptr, nullptr, nullptr, // wifiManager, webServerLauncher, loopServiceFactory, mqttManager
        &g_testModuleStructure
    );
}

static void fill_module_structure_teardown() {
    delete g_testModuleSetupService;
    g_testModuleSetupService = nullptr;
}

// Test functions
static void test_fill_module_structure_basic_info() {
    fill_module_structure_setup();
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Verify the basic information was set correctly
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", g_testModuleStructure.growdirectorUid.c_str());
    TEST_ASSERT_EQUAL_STRING("2.1.1", g_testModuleStructure.firmware.c_str());
    TEST_ASSERT_EQUAL_STRING("test@example.com", g_testModuleStructure.email.c_str());
    TEST_ASSERT_EQUAL_STRING("test-account-uid", g_testModuleStructure.accountUid.c_str());
    
    // Verify node was created
    TEST_ASSERT_EQUAL(1, g_testModuleStructure.nodes.size());
    
    if (!g_testModuleStructure.nodes.empty()) {
        const Node& node = g_testModuleStructure.nodes[0];
        
        // Verify node basic information
        TEST_ASSERT_EQUAL_STRING("director_wifi", node.type.c_str());
        TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", node.uid.c_str());
        TEST_ASSERT_EQUAL_STRING("192.168.1.100", node.ip.c_str());
        TEST_ASSERT_EQUAL_STRING("Test_SSID", node.ssid.c_str());
        TEST_ASSERT_EQUAL_STRING("2.1.1", node.firmware.c_str());
        TEST_ASSERT_EQUAL_STRING("power-on", node.bootType.c_str());
        TEST_ASSERT_EQUAL_STRING("none", node.fileSystem.c_str());
    }
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_sensor_module() {
    fill_module_structure_setup();
    
    // Configure as a sensor module
    g_testModuleConfig.setModuleType(MODULE_TYPE_SENSOR);
    g_testModuleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Verify module structure has sensor-specific data
    TEST_ASSERT_FALSE(g_testModuleStructure.nodes.empty());
    if (!g_testModuleStructure.nodes.empty()) {
        const Node& node = g_testModuleStructure.nodes[0];
        
        // Verify sensor subtype
        TEST_ASSERT_EQUAL_STRING("sensor", node.subtype.c_str());
        
        // Verify sensors were created (should be 12 sensors: 3 for each of the 4 pins)
        TEST_ASSERT_EQUAL(g_testModuleConfig.getSensorPinCount() * 3, node.sensors.size());
        
        // Verify all sensors have correct types and UIDs
        for (size_t i = 0; i < node.sensors.size(); i++) {
            TEST_ASSERT_EQUAL_STRING("none", node.sensors[i].type.c_str());
            TEST_ASSERT_TRUE(node.sensors[i].uid.find("AA:BB:CC:DD:EE:FF.") == 0);
        }
        
        // No relays or pumps should be added
        TEST_ASSERT_EQUAL(0, node.relays.size());
        TEST_ASSERT_EQUAL(0, node.pumps.size());
    }
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_hydro_module() {
    fill_module_structure_setup();
    
    // Configure as a hydro module
    g_testModuleConfig.setModuleType(MODULE_TYPE_HYDRO);
    g_testModuleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
    g_testModuleConfig.setDeviceCount(3); // 3 pumps
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Verify module structure has hydro-specific data
    TEST_ASSERT_FALSE(g_testModuleStructure.nodes.empty());
    if (!g_testModuleStructure.nodes.empty()) {
        const Node& node = g_testModuleStructure.nodes[0];
        
        // Verify hydro subtype
        TEST_ASSERT_EQUAL_STRING("hydro", node.subtype.c_str());
        
        // Verify sensors were created based on sensor pin count
        TEST_ASSERT_EQUAL(g_testModuleConfig.getSensorPinCount(), node.sensors.size());
        
        // Verify all sensors have correct types and UIDs
        for (size_t i = 0; i < node.sensors.size(); i++) {
            TEST_ASSERT_EQUAL_STRING("none", node.sensors[i].type.c_str());
            TEST_ASSERT_TRUE(node.sensors[i].uid.find("AA:BB:CC:DD:EE:FF.") == 0);
            TEST_ASSERT_EQUAL(i + 1, node.sensors[i].pin);
        }
        
        // Verify pumps were created based on device count
        TEST_ASSERT_EQUAL(g_testModuleConfig.getDeviceCount(), node.pumps.size());
        
        // Verify all pumps have correct UIDs and pins
        for (size_t i = 0; i < node.pumps.size(); i++) {
            TEST_ASSERT_TRUE(node.pumps[i].uid.find("AA:BB:CC:DD:EE:FF.pm.") == 0);
            TEST_ASSERT_EQUAL(i + 1, node.pumps[i].pin);
        }
        
        // No relays should be added
        TEST_ASSERT_EQUAL(0, node.relays.size());
    }
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_relay_device() {
    fill_module_structure_setup();
    
    // Configure as a relay device
    g_testModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_testModuleConfig.setModuleSubType(MODULE_SUBTYPE_RELAY);
    g_testModuleConfig.setDeviceCount(4); // 4 relays
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Verify module structure has relay-specific data
    TEST_ASSERT_FALSE(g_testModuleStructure.nodes.empty());
    if (!g_testModuleStructure.nodes.empty()) {
        const Node& node = g_testModuleStructure.nodes[0];
        
        // Verify drycontact subtype
        TEST_ASSERT_EQUAL_STRING("drycontact", node.subtype.c_str());
        
        // Verify relays were created based on device count
        TEST_ASSERT_EQUAL(g_testModuleConfig.getDeviceCount(), node.relays.size());
        
        // Verify all relays have correct UIDs and pins
        for (size_t i = 0; i < node.relays.size(); i++) {
            TEST_ASSERT_TRUE(node.relays[i].uid.find("AA:BB:CC:DD:EE:FF.") == 0);
            TEST_ASSERT_EQUAL(i + 1, node.relays[i].pin);
        }
        
        // No sensors or pumps should be added
        TEST_ASSERT_EQUAL(0, node.sensors.size());
        TEST_ASSERT_EQUAL(0, node.pumps.size());
    }
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_socket_device() {
    fill_module_structure_setup();
    
    // Configure as a socket device
    g_testModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_testModuleConfig.setModuleSubType(MODULE_SUBTYPE_SOKET);
    g_testModuleConfig.setDeviceCount(2); // 2 sockets
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Verify module structure has socket-specific data
    TEST_ASSERT_FALSE(g_testModuleStructure.nodes.empty());
    if (!g_testModuleStructure.nodes.empty()) {
        const Node& node = g_testModuleStructure.nodes[0];
        
        // Verify socket subtype
        TEST_ASSERT_EQUAL_STRING("socket", node.subtype.c_str());
        
        // Verify relays were created based on device count
        TEST_ASSERT_EQUAL(g_testModuleConfig.getDeviceCount(), node.relays.size());
        
        // Verify all relays have correct UIDs and pins
        for (size_t i = 0; i < node.relays.size(); i++) {
            TEST_ASSERT_TRUE(node.relays[i].uid.find("AA:BB:CC:DD:EE:FF.") == 0);
            TEST_ASSERT_EQUAL(i + 1, node.relays[i].pin);
        }
        
        // No sensors or pumps should be added
        TEST_ASSERT_EQUAL(0, node.sensors.size());
        TEST_ASSERT_EQUAL(0, node.pumps.size());
    }
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_dimmer_module() {
    fill_module_structure_setup();
    
    // Configure as a dimmer module
    g_testModuleConfig.setModuleType(MODULE_TYPE_ANALOG_INPUT_DEVICE);
    g_testModuleConfig.setModuleSubType(MODULE_SUBTYPE_DIMMER);
    g_testModuleConfig.setDeviceCount(2); // 2 dimmers
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Verify module structure has dimmer-specific data
    TEST_ASSERT_FALSE(g_testModuleStructure.nodes.empty());
    if (!g_testModuleStructure.nodes.empty()) {
        const Node& node = g_testModuleStructure.nodes[0];
        
        // Verify dimmer subtype
        TEST_ASSERT_EQUAL_STRING("dimmer", node.subtype.c_str());
        
        // Verify relays were created based on device count
        TEST_ASSERT_EQUAL(g_testModuleConfig.getDeviceCount(), node.relays.size());
        
        // Verify all relays have correct UIDs and pins
        for (size_t i = 0; i < node.relays.size(); i++) {
            TEST_ASSERT_TRUE(node.relays[i].uid.find("AA:BB:CC:DD:EE:FF.") == 0);
            TEST_ASSERT_EQUAL(i + 1, node.relays[i].pin);
        }
        
        // No sensors or pumps should be added
        TEST_ASSERT_EQUAL(0, node.sensors.size());
        TEST_ASSERT_EQUAL(0, node.pumps.size());
    }
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_unknown_module_type() {
    fill_module_structure_setup();
    
    // Configure as an unknown module type
    g_testModuleConfig.setModuleType(255); // Invalid/unknown type
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Verify module structure has default/unknown data
    TEST_ASSERT_FALSE(g_testModuleStructure.nodes.empty());
    if (!g_testModuleStructure.nodes.empty()) {
        const Node& node = g_testModuleStructure.nodes[0];
        
        // Verify unknown subtype
        TEST_ASSERT_EQUAL_STRING("unknown", node.subtype.c_str());
        
        // No sensors, relays, or pumps should be added
        TEST_ASSERT_EQUAL(0, node.sensors.size());
        TEST_ASSERT_EQUAL(0, node.relays.size());
        TEST_ASSERT_EQUAL(0, node.pumps.size());
    }
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_with_location_data() {
    fill_module_structure_setup();
    
    // Set location data
    g_testSettings.setLatitude(40.7128);
    g_testSettings.setLongitude(-74.0060);
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Verify location data was set
    TEST_ASSERT_EQUAL_FLOAT(40.7128, g_testModuleStructure.latitude);
    TEST_ASSERT_EQUAL_FLOAT(-74.0060, g_testModuleStructure.longitude);
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_without_wifi_adapter() {
    fill_module_structure_setup();
    g_testModuleConfig.setModuleUid("default_uid"); //emulate null wifiadapter
    
    FillModuleStructureTestHelper::setWifiAdapter(g_testModuleSetupService, nullptr);
    
    // Call the method under test
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    TEST_ASSERT_EQUAL_STRING("default_uid", g_testModuleStructure.growdirectorUid.c_str());

    // Verify fallback values were used
    TEST_ASSERT_FALSE(g_testModuleStructure.nodes.empty());
    if (!g_testModuleStructure.nodes.empty()) {
        const Node& node = g_testModuleStructure.nodes[0];
        
        // Should use default values
        TEST_ASSERT_EQUAL_STRING("default_uid", node.uid.c_str());
        TEST_ASSERT_EQUAL_STRING("0.0.0.0", node.ip.c_str());
    }
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_handles_null_module_structure() {
    fill_module_structure_setup();
    
    // Create a new service with null module structure
    delete g_testModuleSetupService;
    g_testModuleSetupService = new ModuleSetupService(
        &g_testLogger,
        &g_testDebugSystem,
        &g_testModuleConfig,
        nullptr, nullptr, nullptr, nullptr, // ledAdapter, buttonAdapter, ledUseCases, buttonUseCases
        &g_testWifiAdapter,
        nullptr, nullptr, nullptr, nullptr, // wifiManager, webServerLauncher, loopServiceFactory, mqttManager
        nullptr // No module structure
    );
    
    // This should not crash
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Check that error was logged
    TEST_ASSERT_TRUE(g_testLogger.printCalled);
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_handles_null_module_config() {
    fill_module_structure_setup();
    
    // Create a new service with null module config
    delete g_testModuleSetupService;
    g_testModuleSetupService = new ModuleSetupService(
        &g_testLogger,
        &g_testDebugSystem,
        nullptr, // No module config
        nullptr, nullptr, nullptr, nullptr, // ledAdapter, buttonAdapter, ledUseCases, buttonUseCases
        &g_testWifiAdapter,
        nullptr, nullptr, nullptr, nullptr, // wifiManager, webServerLauncher, loopServiceFactory, mqttManager
        &g_testModuleStructure
    );
    
    // This should not crash
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Check that error was logged
    TEST_ASSERT_TRUE(g_testLogger.printCalled);
    
    fill_module_structure_teardown();
}

static void test_fill_module_structure_handles_null_settings() {
    fill_module_structure_setup();
    
    // Set null settings
    g_testModuleConfig.setSettings(nullptr);
    
    // This should not crash
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);
    
    // Check that error was logged
    TEST_ASSERT_TRUE(g_testLogger.printCalled);
    
    fill_module_structure_teardown();
}

// GDV1-3820. The other end of the command path: the boot type recorded by a Reboot command has to
// reach the published structure as "reboot-user" and only then fall back to power-on. The reset
// here is what used to swallow the field nobody set, so a commanded restart arrived at the backend
// as a power loss.
static void test_fill_module_structure_reports_commanded_reboot_as_user() {
    fill_module_structure_setup();

    MockArduinoAdapter arduinoAdapter;
    ModuleCommandExecutionUseCase commandUseCase(
        &g_testLogger, &g_testDebugSystem, nullptr, nullptr, &arduinoAdapter,
        nullptr, nullptr, nullptr, nullptr, &g_testSettings,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    ModuleCommandDto cmd;
    cmd.uid = "cmd-reboot-structure";
    cmd.command = "Reboot";
    cmd.options = "{\"noScanSensors\":\"true\"}";
    cmd.createdAt = "";

    auto results = commandUseCase.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_TRUE(arduinoAdapter.restartCalled);
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_USER, g_testSettings.getBootType());

    // The boot that follows the commanded restart
    FillModuleStructureTestHelper::callFillModuleStructure(g_testModuleSetupService);

    TEST_ASSERT_FALSE(g_testModuleStructure.nodes.empty());
    if (!g_testModuleStructure.nodes.empty()) {
        TEST_ASSERT_EQUAL_STRING("reboot-user", g_testModuleStructure.nodes[0].bootType.c_str());
    }

    // Reported once, then back to the default - the next boot must not claim a user reboot again
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_POWER_ON, g_testSettings.getBootType());

    fill_module_structure_teardown();
}

// Main test runner function
extern "C" void run_fill_module_structure_tests(void) {
    printf("\n----- Running FillModuleStructure Tests -----\n");
    
    RUN_TEST(test_fill_module_structure_basic_info);
    RUN_TEST(test_fill_module_structure_sensor_module);
    RUN_TEST(test_fill_module_structure_hydro_module);
    RUN_TEST(test_fill_module_structure_relay_device);
    RUN_TEST(test_fill_module_structure_socket_device);
    RUN_TEST(test_fill_module_structure_dimmer_module);
    RUN_TEST(test_fill_module_structure_unknown_module_type);
    RUN_TEST(test_fill_module_structure_with_location_data);
    RUN_TEST(test_fill_module_structure_without_wifi_adapter);
    RUN_TEST(test_fill_module_structure_handles_null_module_structure);
    RUN_TEST(test_fill_module_structure_handles_null_module_config);
    RUN_TEST(test_fill_module_structure_handles_null_settings);
    RUN_TEST(test_fill_module_structure_reports_commanded_reboot_as_user);

    printf("\n----- FillModuleStructure Tests Complete -----\n");
}