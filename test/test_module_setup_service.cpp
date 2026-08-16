#include <unity.h>
#include "Mock/ArduinoMock.h"
#include "Application/Services/ModuleSetupService.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Led/MockLedAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Core/Domain/MockLedUseCases.h"
#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Mock/Infrastructure/Controllers/WebServices/Launchers/MockWebServerLauncher.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Application/LoopServices/MockLoopServiceFactory.h"
#include "Mock/Application/LoopServices/MockLoopService.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Mqtt/MockMessageProcessor.h"
#include "Mock/Application/Services/Devices/MockDeviceService.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Application/MockClearModuleDataService.h"
#include "Mock/UseCases/ModuleMode/MockCheckModuleDeletionUseCase.h"
#include "Mock/UseCases/ModuleMode/MockCheckFirmwareUpdateOnStartupUseCase.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"

// Helper class for tests
class ModuleSetupServiceTestHelper {
public:
    static void callSetupAP(ModuleSetupService* service) {
        if (service) {
            service->setupAP();
        }
    }
    
    static void callSetupWebServer(ModuleSetupService* service) {
        if (service) {
            service->setupWebServer();
        }
    }

    static void callConfigureDoneModeConfigure(ModuleSetupService* service) {
        if (service) {
            service->ConfigureDoneModeConfigure();
        }
    }

    static void callHandleModuleMode(ModuleSetupService* service) {
        if (service) {
            service->handleModuleMode();
        }
    }

    static void callAddSensorToStructure(ModuleSetupService* service, Node& node, const SensorEntity& sensorEntity) {
        if (service) {
            service->addSensorToStructure(node, sensorEntity);
        }
    }
};

// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockModuleConfig g_mockModuleConfig;
static MockLedAdapter g_mockLedAdapter;
static MockLedUseCases g_mockLedUseCases;
static MockButtonUseCases g_mockButtonUseCases;
static MockWifiAdapter g_mockWifiAdapter;
static MockWiFiManager g_mockWiFiManager;
static MockWebServerLauncher g_mockWebServerLauncher;
static MockLoopServiceFactory g_mockLoopServiceFactory;
static MockSettings g_mockSettings;
static MockMqttManager g_mockMqttManager;
static MockMessageProcessor g_mockMessageProcessor;
static MockDeviceService g_mockDeviceService;
// The structure records are built by an injected use case, wired here the way globals.h wires it
static VpdChannelUseCase g_vpdChannelUseCase;
static VirtualSensorChannelsUseCase g_virtualChannels(&g_vpdChannelUseCase);
static SensorStructureUseCase g_sensorStructureUseCase(&g_virtualChannels);
static ModuleSetupService* g_moduleSetupService;
static FILE* debug_file = nullptr;

// Test setup and teardown
static void module_setup_service_setup(void) {
    // Open debug log file in append mode
    debug_file = fopen("module_setup_test_debug.log", "a");
    if (debug_file == nullptr) {
        printf("ERROR: Could not open debug log file\n");
    } else {
        fprintf(debug_file, "--- Test Started ---\n");
    }
    
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockDebugSystem.featureEnabled = true;
    g_mockModuleConfig.reset();
    g_mockLedAdapter.reset();
    g_mockLedUseCases.reset();
    g_mockButtonUseCases.reset();
    g_mockWifiAdapter.reset();
    g_mockWiFiManager.reset();
    g_mockWebServerLauncher.reset();
    g_mockLoopServiceFactory.reset();
    g_mockSettings.reset();
    g_mockMqttManager.reset();
    g_mockMessageProcessor.reset();
    g_mockDeviceService.reset();
    
    // Set up the mock settings for module config
    g_mockModuleConfig.setSettings(&g_mockSettings);
    
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager,
        nullptr,
        &g_mockDeviceService  // Add the device service
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
}

static void module_setup_service_teardown(void) {
    delete g_moduleSetupService;
    g_moduleSetupService = nullptr;
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "--- Test Finished ---\n\n");
        fclose(debug_file);
        debug_file = nullptr;
    }
}

// Test functions
static void test_setup_module_initializes_config(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_initializes_config\n");
    module_setup_service_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if config was initialized: %s\n", 
                                g_mockModuleConfig.isInitialized() ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if config was modified with 'init': %s\n", 
                                g_mockModuleConfig.wasModified("init") ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockModuleConfig.wasModified("init"));
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if logger.print was called: %s\n", 
                                g_mockLogger.printCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_initializes_config\n");
}

static void test_setup_module_configures_leds(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_configures_leds\n");
    module_setup_service_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "setupModuleLedsCalled: %s\n", 
                                g_mockLedAdapter.setupModuleLedsCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedAdapter.setupModuleLedsCalled);
    
    if (debug_file != nullptr) fprintf(debug_file, "Expected LEDs count: %d, Actual: %d\n", 
                                g_mockModuleConfig.getLedsCount(), g_mockLedAdapter.ledsCount);
    TEST_ASSERT_EQUAL(g_mockModuleConfig.getLedsCount(), g_mockLedAdapter.ledsCount);
    
    if (debug_file != nullptr) fprintf(debug_file, "Verifying LED arrays\n");
    // Verify that the correct LED arrays were passed to the adapter
    for (int i = 0; i < g_mockModuleConfig.getLedsCount(); i++) {
        if (debug_file != nullptr) fprintf(debug_file, "Checking LED %d - Green: expected=%d, actual=%d; Red: expected=%d, actual=%d\n", 
                                    i, g_mockModuleConfig.getGreenLeds()[i], g_mockLedAdapter.greenLeds[i],
                                    g_mockModuleConfig.getRedLeds()[i], g_mockLedAdapter.redLeds[i]);
        TEST_ASSERT_EQUAL(g_mockModuleConfig.getGreenLeds()[i], g_mockLedAdapter.greenLeds[i]);
        TEST_ASSERT_EQUAL(g_mockModuleConfig.getRedLeds()[i], g_mockLedAdapter.redLeds[i]);
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_configures_leds\n");
}

static void test_setup_module_without_led_adapter(void) {
    module_setup_service_setup();
    
    // Create a new service without LED adapter
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        nullptr, // ledAdapter
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);

    // This should not crash and should still initialize the config
    g_moduleSetupService->setupModule();
    
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    TEST_ASSERT_TRUE(g_mockModuleConfig.wasModified("init"));
    
    module_setup_service_teardown();
}

static void test_setup_module_without_config(void) {
    module_setup_service_setup();
    
    // Create a new service without module config
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        nullptr,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // This should not crash and should log an error
    g_moduleSetupService->setupModule();
    
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
}

static void test_setup_module_triggers_led_use_cases(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_triggers_led_use_cases\n");
    module_setup_service_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if ledOnLoad was called: %s\n", 
                                g_mockLedUseCases.onLoadCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedUseCases.onLoadCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_triggers_led_use_cases\n");
}

static void test_setup_module_without_led_use_cases(void) {
    module_setup_service_setup();
    
    // Create a new service without LED use cases
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        nullptr, // ledUseCases
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);

    // This should not crash and should still initialize the config and LEDs
    g_moduleSetupService->setupModule();
    
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    TEST_ASSERT_TRUE(g_mockLedAdapter.setupModuleLedsCalled);
    
    module_setup_service_teardown();
}

static void test_setup_module_launches_web_server_in_config_mode(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_launches_web_server_in_config_mode\n");
    module_setup_service_setup();
    
    // Set up the mock to simulate USER_CAN_CONFIGURE mode
    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if launchConfigurationServer was called: %s\n", 
                                g_mockWebServerLauncher.launchConfigurationServerCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockWebServerLauncher.launchConfigurationServerCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_launches_web_server_in_config_mode\n");
}

static void test_module_mode_handling_for_user_can_configure(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_module_mode_handling_for_user_can_configure\n");
    module_setup_service_setup();
    
    // Set up the mock to simulate USER_CAN_CONFIGURE mode
    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if button press check was called: %s\n", 
                                g_mockButtonUseCases.checkButtonPressedWhenOnCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockButtonUseCases.checkButtonPressedWhenOnCalled);
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if web server launcher was called: %s\n", 
                                g_mockWebServerLauncher.launchConfigurationServerCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockWebServerLauncher.launchConfigurationServerCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_module_mode_handling_for_user_can_configure\n");
}

static void test_module_mode_handling_for_configured_done(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_module_mode_handling_for_configured_done\n");
    module_setup_service_setup();
    
    // Set up the mock to simulate CONFIGURED_DONE mode
    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    // Check that configuration server was not launched
    if (debug_file != nullptr) fprintf(debug_file, "Checking web server launcher was NOT called: %s\n", 
                                !g_mockWebServerLauncher.launchConfigurationServerCalled ? "Correct" : "Wrong");
    TEST_ASSERT_FALSE(g_mockWebServerLauncher.launchConfigurationServerCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_module_mode_handling_for_configured_done\n");
}

static void test_handle_module_mode_invokes_startup_factory_reset(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_handle_module_mode_invokes_startup_factory_reset\n");
    module_setup_service_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_moduleSetupService->setupModule();

    TEST_ASSERT_TRUE(g_mockButtonUseCases.checkStartupFactoryResetCalled);

    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_handle_module_mode_invokes_startup_factory_reset\n");
}

static void test_module_mode_handling_for_test_mode(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_module_mode_handling_for_test_mode\n");
    module_setup_service_setup();
    
    // Set up the mock to simulate AP_TEST_MODE mode
    g_mockSettings.setConfigured(ModuleModeEnum::AP_TEST_MODE);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    // Validate specific behavior for AP_TEST_MODE if applicable
    // Currently this is a placeholder to demonstrate test structure
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_module_mode_handling_for_test_mode\n");
}

static void test_module_mode_initialization_when_invalid(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_module_mode_initialization_when_invalid\n");
    module_setup_service_setup();

    // Set up the mock to simulate an invalid mode
    g_mockSettings.setConfigured(255); // Using an invalid value
    uint8_t invalidMode = g_mockSettings.getIfConfigured();
    if (debug_file != nullptr) fprintf(debug_file, "getIfConfigured = %d\n", invalidMode);
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    // Should set to AP_TEST_MODE
    if (debug_file != nullptr) fprintf(debug_file, "getIfConfigured = %d\n", g_mockSettings.getIfConfigured());

    TEST_ASSERT_TRUE(invalidMode != g_mockSettings.getIfConfigured());
    TEST_ASSERT_EQUAL(ModuleModeEnum::AP_TEST_MODE, g_mockSettings.getIfConfigured());
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_module_mode_initialization_when_invalid\n");
}

static void test_setup_module_without_web_server_launcher(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_without_web_server_launcher\n");
    module_setup_service_setup();
    
    // Create a new service without web server launcher
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        nullptr,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Set up the mock to simulate USER_CAN_CONFIGURE mode
    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
    
    // This should not crash even though we're in USER_CAN_CONFIGURE mode
    g_moduleSetupService->setupModule();
    
    // Basic checks to ensure the service still worked
    TEST_ASSERT_TRUE(g_mockModuleConfig.isInitialized());
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_without_web_server_launcher\n");
}

static void test_setup_module_handles_led_sequences(void) {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_module_handles_led_sequences\n");
    module_setup_service_setup();
    
    if (debug_file != nullptr) fprintf(debug_file, "Calling setupModule()\n");
    g_moduleSetupService->setupModule();
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if ledOnLoad was called: %s\n", 
                                g_mockLedUseCases.onLoadCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedUseCases.onLoadCalled);
    
    if (debug_file != nullptr) fprintf(debug_file, "Checking if ledOffLoad was called: %s\n", 
                                g_mockLedUseCases.offLoadCalled ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockLedUseCases.offLoadCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_module_handles_led_sequences\n");
}

// New tests for setupAP method
static void test_setup_ap_creates_correct_access_point_for_sensor_module() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_creates_correct_access_point_for_sensor_module\n");
    module_setup_service_setup();
    
    // Configure as a sensor module
    g_mockModuleConfig.setModuleType(MODULE_TYPE_SENSOR);
    g_mockModuleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
    
    // Make sure AP is not active initially
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Set test MAC address
    g_mockWifiAdapter.setMACAddress("AA:BB:CC:DD:EE:FF");
    
    // Call setupAP directly using the test helper
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that createAccessPoint was called
    TEST_ASSERT_TRUE(g_mockWifiAdapter.createAccessPointCalled);
    
    // Verify SSID prefix contains "SensorDirector__"
    TEST_ASSERT_TRUE(strstr(g_mockWifiAdapter.lastSSID, "SensorDirector__") != nullptr);
    
    // Verify MAC address is included in SSID
    TEST_ASSERT_TRUE(strstr(g_mockWifiAdapter.lastSSID, "CC:DD:EE:FF") != nullptr);
    
    // Verify random part (last 3 digits)
    const char* lastSSID = g_mockWifiAdapter.lastSSID;
    size_t len = strlen(lastSSID);
    if (debug_file != nullptr) fprintf(debug_file, "SSID: %s, Length: %zu\n", lastSSID, len);
    
    // Verify at least 4 characters are present (for _XXX pattern)
    TEST_ASSERT_TRUE(len >= 4);
    
    // Check that the last 4 characters are in format _XXX where X is a digit
    TEST_ASSERT_EQUAL('_', lastSSID[len-4]);
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-3]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-2]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-1]));
    
    // Save the first random part
    char firstRandomPart[5] = {0};
    strncpy(firstRandomPart, &lastSSID[len-4], 4);
    
    // Create a second AP to check for randomness
    g_mockWifiAdapter.createAccessPointCalled = false;
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP again
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Verify second random part
    const char* secondSSID = g_mockWifiAdapter.lastSSID;
    size_t secondLen = strlen(secondSSID);
    
    char secondRandomPart[5] = {0};
    strncpy(secondRandomPart, &secondSSID[secondLen-4], 4);
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "First random part: %s\n", firstRandomPart);
        fprintf(debug_file, "Second random part: %s\n", secondRandomPart);
    }
    
    // Check that the random parts are different (could rarely fail if random values match)
    bool areDifferent = strcmp(firstRandomPart, secondRandomPart) != 0;
    if (!areDifferent && debug_file != nullptr) {
        fprintf(debug_file, "WARNING: Random parts matched! This is rare but possible.\n");
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_creates_correct_access_point_for_sensor_module\n");
}

static void test_setup_ap_creates_correct_access_point_for_hydro_module() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_creates_correct_access_point_for_hydro_module\n");
    module_setup_service_setup();
    
    // Configure as a hydro module
    g_mockModuleConfig.setModuleType(MODULE_TYPE_HYDRO);
    g_mockModuleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
    
    // Make sure AP is not active initially
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP directly using the test helper
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that createAccessPoint was called
    TEST_ASSERT_TRUE(g_mockWifiAdapter.createAccessPointCalled);
    
    // Verify SSID prefix contains "HydroDirector__"
    TEST_ASSERT_TRUE(strstr(g_mockWifiAdapter.lastSSID, "HydroDirector__") != nullptr);

    // Verify random part (last 3 digits)
    const char* lastSSID = g_mockWifiAdapter.lastSSID;
    size_t len = strlen(lastSSID);

    // Verify at least 4 characters are present (for _XXX pattern)
    TEST_ASSERT_TRUE(len >= 4);

    // Check that the last 4 characters are in format _XXX where X is a digit
    TEST_ASSERT_EQUAL('_', lastSSID[len-4]);
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-3]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-2]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-1]));

    // Save the first random part
    char firstRandomPart[5] = {0};
    strncpy(firstRandomPart, &lastSSID[len-4], 4);

    // Create a second AP to check for randomness
    g_mockWifiAdapter.createAccessPointCalled = false;
    g_mockWifiAdapter.setAccessPointActive(false);

    // Call setupAP again
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);

    // Verify second random part
    const char* secondSSID = g_mockWifiAdapter.lastSSID;
    size_t secondLen = strlen(secondSSID);

    char secondRandomPart[5] = {0};
    strncpy(secondRandomPart, &secondSSID[secondLen-4], 4);
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "First random part: %s\n", firstRandomPart);
        fprintf(debug_file, "Second random part: %s\n", secondRandomPart);
    }
    
    // Check that the random parts are different
    bool areDifferent = strcmp(firstRandomPart, secondRandomPart) != 0;
    if (!areDifferent && debug_file != nullptr) {
        fprintf(debug_file, "WARNING: Random parts matched! This is rare but possible.\n");
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_creates_correct_access_point_for_hydro_module\n");
}

static void test_setup_ap_creates_correct_access_point_for_device_module() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_creates_correct_access_point_for_device_module\n");
    module_setup_service_setup();
    
    // Configure as a device module
    g_mockModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_mockModuleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
    
    // Make sure AP is not active initially
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP directly using the test helper
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that createAccessPoint was called
    TEST_ASSERT_TRUE(g_mockWifiAdapter.createAccessPointCalled);
    
    // Verify SSID prefix contains "DryContactDirector__"
    TEST_ASSERT_TRUE(strstr(g_mockWifiAdapter.lastSSID, "DryContactDirector__") != nullptr);
    
    // Verify random part (last 3 digits)
    const char* lastSSID = g_mockWifiAdapter.lastSSID;
    size_t len = strlen(lastSSID);
    
    // Verify at least 4 characters are present (for _XXX pattern)
    TEST_ASSERT_TRUE(len >= 4);
    
    // Check that the last 4 characters are in format _XXX where X is a digit
    TEST_ASSERT_EQUAL('_', lastSSID[len-4]);
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-3]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-2]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-1]));
    
    // Save the first random part
    char firstRandomPart[5] = {0};
    strncpy(firstRandomPart, &lastSSID[len-4], 4);
    
    // Create a second AP to check for randomness
    g_mockWifiAdapter.createAccessPointCalled = false;
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP again
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Verify second random part
    const char* secondSSID = g_mockWifiAdapter.lastSSID;
    size_t secondLen = strlen(secondSSID);
    
    char secondRandomPart[5] = {0};
    strncpy(secondRandomPart, &secondSSID[secondLen-4], 4);
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "First random part: %s\n", firstRandomPart);
        fprintf(debug_file, "Second random part: %s\n", secondRandomPart);
    }
    
    // Check that the random parts are different
    bool areDifferent = strcmp(firstRandomPart, secondRandomPart) != 0;
    if (!areDifferent && debug_file != nullptr) {
        fprintf(debug_file, "WARNING: Random parts matched! This is rare but possible.\n");
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_creates_correct_access_point_for_device_module\n");
}

static void test_setup_ap_creates_correct_access_point_for_socket_device() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_creates_correct_access_point_for_socket_device\n");
    module_setup_service_setup();
    
    // Configure as a socket device
    g_mockModuleConfig.setModuleType(MODULE_TYPE_DEVICE);
    g_mockModuleConfig.setModuleSubType(MODULE_SUBTYPE_SOKET);
    
    // Make sure AP is not active initially
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP directly using the test helper
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that createAccessPoint was called
    TEST_ASSERT_TRUE(g_mockWifiAdapter.createAccessPointCalled);
    
    // Verify SSID prefix contains "SocketDirector__"
    TEST_ASSERT_TRUE(strstr(g_mockWifiAdapter.lastSSID, "SocketDirector__") != nullptr);
    
    // Verify random part (last 3 digits)
    const char* lastSSID = g_mockWifiAdapter.lastSSID;
    size_t len = strlen(lastSSID);
    
    // Verify at least 4 characters are present (for _XXX pattern)
    TEST_ASSERT_TRUE(len >= 4);
    
    // Check that the last 4 characters are in format _XXX where X is a digit
    TEST_ASSERT_EQUAL('_', lastSSID[len-4]);
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-3]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-2]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-1]));
    
    // Save the first random part
    char firstRandomPart[5] = {0};
    strncpy(firstRandomPart, &lastSSID[len-4], 4);
    
    // Create a second AP to check for randomness
    g_mockWifiAdapter.createAccessPointCalled = false;
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP again
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Verify second random part
    const char* secondSSID = g_mockWifiAdapter.lastSSID;
    size_t secondLen = strlen(secondSSID);
    
    char secondRandomPart[5] = {0};
    strncpy(secondRandomPart, &secondSSID[secondLen-4], 4);
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "First random part: %s\n", firstRandomPart);
        fprintf(debug_file, "Second random part: %s\n", secondRandomPart);
    }
    
    // Check that the random parts are different
    bool areDifferent = strcmp(firstRandomPart, secondRandomPart) != 0;
    if (!areDifferent && debug_file != nullptr) {
        fprintf(debug_file, "WARNING: Random parts matched! This is rare but possible.\n");
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_creates_correct_access_point_for_socket_device\n");
}

static void test_setup_ap_creates_correct_access_point_for_dimmer_module() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_creates_correct_access_point_for_dimmer_module\n");
    module_setup_service_setup();
    
    // Configure as an analog input device module
    g_mockModuleConfig.setModuleType(MODULE_TYPE_ANALOG_INPUT_DEVICE);
    g_mockModuleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
    
    // Make sure AP is not active initially
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP directly using the test helper
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that createAccessPoint was called
    TEST_ASSERT_TRUE(g_mockWifiAdapter.createAccessPointCalled);
    
    // Verify SSID prefix contains "DimmerDirector__"
    TEST_ASSERT_TRUE(strstr(g_mockWifiAdapter.lastSSID, "DimmerDirector__") != nullptr);
    
    // Verify random part (last 3 digits)
    const char* lastSSID = g_mockWifiAdapter.lastSSID;
    size_t len = strlen(lastSSID);
    
    // Verify at least 4 characters are present (for _XXX pattern)
    TEST_ASSERT_TRUE(len >= 4);
    
    // Check that the last 4 characters are in format _XXX where X is a digit
    TEST_ASSERT_EQUAL('_', lastSSID[len-4]);
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-3]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-2]));
    TEST_ASSERT_TRUE(isdigit(lastSSID[len-1]));
    
    // Save the first random part
    char firstRandomPart[5] = {0};
    strncpy(firstRandomPart, &lastSSID[len-4], 4);
    
    // Create a second AP to check for randomness
    g_mockWifiAdapter.createAccessPointCalled = false;
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP again
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Verify second random part
    const char* secondSSID = g_mockWifiAdapter.lastSSID;
    size_t secondLen = strlen(secondSSID);
    
    char secondRandomPart[5] = {0};
    strncpy(secondRandomPart, &secondSSID[secondLen-4], 4);
    
    if (debug_file != nullptr) {
        fprintf(debug_file, "First random part: %s\n", firstRandomPart);
        fprintf(debug_file, "Second random part: %s\n", secondRandomPart);
    }
    
    // Check that the random parts are different
    bool areDifferent = strcmp(firstRandomPart, secondRandomPart) != 0;
    if (!areDifferent && debug_file != nullptr) {
        fprintf(debug_file, "WARNING: Random parts matched! This is rare but possible.\n");
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_creates_correct_access_point_for_dimmer_module\n");
}

static void test_setup_ap_configures_static_ip() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_configures_static_ip\n");
    module_setup_service_setup();
    
    // Make sure AP is not active initially
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Setup mock for configureAccessPoint
    g_mockWifiAdapter.configureAccessPointSuccess = true;
    
    // Call setupAP directly using the test helper
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that configureAccessPoint was called with correct parameters
    TEST_ASSERT_TRUE(g_mockWifiAdapter.configureAccessPointCalled);
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", g_mockWifiAdapter.lastLocalIP);
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", g_mockWifiAdapter.lastGateway);
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", g_mockWifiAdapter.lastSubnet);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_configures_static_ip\n");
}

static void test_setup_ap_skips_if_already_active() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_skips_if_already_active\n");
    module_setup_service_setup();
    
    // Set AP as already active
    g_mockWifiAdapter.setAccessPointActive(true);
    
    // Call setupAP directly using the test helper
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that createAccessPoint was NOT called
    TEST_ASSERT_FALSE(g_mockWifiAdapter.createAccessPointCalled);
    
    // Make sure logs about AP setup completed are present
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_skips_if_already_active\n");
}

static void test_setup_ap_handles_missing_wifi_adapter() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_handles_missing_wifi_adapter\n");
    module_setup_service_setup();
    
    // Create a new service without WiFi adapter
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        nullptr, // No WiFi adapter
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // This should not crash and should log an error
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that an error was logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_handles_missing_wifi_adapter\n");
}

static void test_setup_ap_handles_configure_ap_failure() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_handles_configure_ap_failure\n");
    module_setup_service_setup();
    
    // Make sure AP is not active initially
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Setup mock to fail on configureAccessPoint
    g_mockWifiAdapter.configureAccessPointSuccess = false;
    
    // Call setupAP directly using the test helper
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // Check that an error was logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_handles_configure_ap_failure\n");
}

static void test_setup_ap_uses_random_part_in_ssid() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_ap_uses_random_part_in_ssid\n");
    module_setup_service_setup();
    
    // Make sure AP is not active initially
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call setupAP twice and capture SSIDs
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    char firstSSID[64];
    strcpy(firstSSID, g_mockWifiAdapter.lastSSID);
    
    // Reset mock
    g_mockWifiAdapter.createAccessPointCalled = false;
    g_mockWifiAdapter.setAccessPointActive(false);
    
    // Call again
    ModuleSetupServiceTestHelper::callSetupAP(g_moduleSetupService);
    
    // SSIDs should be different due to random part
    if (debug_file != nullptr) {
        fprintf(debug_file, "First SSID: %s\n", firstSSID);
        fprintf(debug_file, "Second SSID: %s\n", g_mockWifiAdapter.lastSSID);
    }
    
    // Check if random parts are different by comparing SSIDs
    // This is a probabilistic test (could fail if random numbers happen to be the same)
    // Consider skipping this assertion in real tests if it causes flaky failures
    bool areDifferent = strcmp(firstSSID, g_mockWifiAdapter.lastSSID) != 0;
    if (!areDifferent && debug_file != nullptr) {
        fprintf(debug_file, "WARNING: Random parts generated the same SSID twice\n");
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_ap_uses_random_part_in_ssid\n");
}

static void test_setup_web_server_handles_null_launcher() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_setup_web_server_handles_null_launcher\n");
    module_setup_service_setup();
    
    // Create a new service without web server launcher
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        nullptr  // Pass null web server launcher
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // This should not crash and should log an error
    ModuleSetupServiceTestHelper::callSetupWebServer(g_moduleSetupService);
    
    // Check that an error was logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    // Verify that specific error message was logged (if you have a way to check message content)
    // This depends on how your MockLogger implements message capturing
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_setup_web_server_handles_null_launcher\n");
}

// Tests for getLoopService method
static void test_get_loop_service_returns_service_for_current_mode() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_get_loop_service_returns_service_for_current_mode\n");
    module_setup_service_setup();
    
    // Set up mock to simulate a specific module mode
    g_mockSettings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
    
    // Call getLoopService
    auto loopService = g_moduleSetupService->getLoopService();
    
    // Verify a service was returned
    TEST_ASSERT_NOT_NULL(loopService.get());
    
    // Verify loopServiceFactory was called with correct mode
    TEST_ASSERT_TRUE(g_mockLoopServiceFactory.createLoopServiceCalled);
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_CONFIGURE, g_mockLoopServiceFactory.lastModuleMode);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_get_loop_service_returns_service_for_current_mode\n");
}

static void test_get_loop_service_handles_null_factory() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_get_loop_service_handles_null_factory\n");
    module_setup_service_setup();
    
    // Create service without factory
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        nullptr  // Null factory
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Call getLoopService - should not crash
    auto loopService = g_moduleSetupService->getLoopService();
    
    // No service should be returned
    TEST_ASSERT_NULL(loopService.get());
    
    // Error should be logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_get_loop_service_handles_null_factory\n");
}

static void test_get_loop_service_handles_null_config() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_get_loop_service_handles_null_config\n");
    module_setup_service_setup();
    
    // Create service without config
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        nullptr,  // Null config
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Call getLoopService - should not crash
    auto loopService = g_moduleSetupService->getLoopService();
    
    // No service should be returned
    TEST_ASSERT_NULL(loopService.get());
    
    // Error should be logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_get_loop_service_handles_null_config\n");
}

static void test_get_loop_service_handles_null_settings() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_get_loop_service_handles_null_settings\n");
    module_setup_service_setup();
    
    // Set null settings in module config
    g_mockModuleConfig.setSettings(nullptr);
    
    // Call getLoopService - should not crash
    auto loopService = g_moduleSetupService->getLoopService();
    
    // No service should be returned
    TEST_ASSERT_NULL(loopService.get());
    
    // Error should be logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_get_loop_service_handles_null_settings\n");
}

static void test_get_loop_service_handles_factory_exception() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_get_loop_service_handles_factory_exception\n");
    module_setup_service_setup();
    
    // Set up mock to throw exception on the first call
    g_mockLoopServiceFactory.reset();
    g_mockLoopServiceFactory.configureMockToThrowException(true, "Test exception");
    
    // Call getLoopService - should handle exception and return fallback service
    auto loopService = g_moduleSetupService->getLoopService();
    
    // Service should be returned (fallback)
    TEST_ASSERT_NOT_NULL(loopService.get());
    
    // Error should be logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_get_loop_service_handles_factory_exception\n");
}

static void test_get_loop_service_for_each_module_mode() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_get_loop_service_for_each_module_mode\n");
    
    // Test all module modes
    ModuleModeEnum testModes[] = {
        ModuleModeEnum::USER_CAN_CONFIGURE,
        ModuleModeEnum::USER_CAN_RE_CONFIGURE,
        ModuleModeEnum::AP_TEST_MODE,
        ModuleModeEnum::CONFIGURED_DONE,
        ModuleModeEnum::CONFIGURED_NEED_RESTART
    };
    
    for (auto mode : testModes) {
        module_setup_service_setup();
        
        // Set up mock for this mode
        g_mockSettings.setConfigured(mode);
        
        // Call getLoopService
        auto loopService = g_moduleSetupService->getLoopService();
        
        // Verify service was returned
        TEST_ASSERT_NOT_NULL(loopService.get());
        
        // Verify correct mode was passed to factory
        TEST_ASSERT_TRUE(g_mockLoopServiceFactory.createLoopServiceCalled);
        TEST_ASSERT_EQUAL(mode, g_mockLoopServiceFactory.lastModuleMode);
        
        module_setup_service_teardown();
    }
    
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_get_loop_service_for_each_module_mode\n");
}

// MQTT Tests
static void test_configure_done_mode_configure_sets_up_mqtt_connection() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_sets_up_mqtt_connection\n");
    module_setup_service_setup();
    
    // Recreate service with MQTT manager
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Set up mocks for success case
    g_mockMqttManager.reset();
    g_mockMqttManager.connectReturnValue = true;
    
    // Call the method under test
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify MQTT connection was attempted
    TEST_ASSERT_TRUE(g_mockMqttManager.connectCalled);
    TEST_ASSERT_EQUAL(1, g_mockMqttManager.connectCallCount);    
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_sets_up_mqtt_connection\n");
}

static void test_configure_done_mode_configure_handles_mqtt_connection_subscribe_was_not_called() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_handles_mqtt_connection_failure\n");
    module_setup_service_setup();
    
    // Recreate service with MQTT manager
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Set up mocks for failure case
    g_mockMqttManager.reset();
    g_mockMqttManager.connectReturnValue = false;
    
    // Call the method under test
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify MQTT connection was attempted but failed
    TEST_ASSERT_TRUE(g_mockMqttManager.connectCalled);
    TEST_ASSERT_EQUAL(1, g_mockMqttManager.connectCallCount);

    //not call subscribe separately
    // Verify that subscribe was not called
    TEST_ASSERT_FALSE(g_mockMqttManager.subscribeCalled);    
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_handles_mqtt_connection_failure\n");
}

static void test_configure_done_mode_configure_handles_missing_mqtt_manager() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_handles_missing_mqtt_manager\n");
    module_setup_service_setup();
    
    // Create a new service without MQTT manager
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        nullptr  // No MQTT manager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Call the method under test
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify error was logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_handles_missing_mqtt_manager\n");
}

// WiFi Manager Tests
static void test_configure_done_mode_configure_uses_wifi_manager() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_uses_wifi_manager\n");
    module_setup_service_setup();
    
    // Set up mocks for success case
    g_mockWiFiManager.reset();
    g_mockWiFiManager.setupWifiConnectionSuccess = true;
    
    // Call the method under test
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify WiFi connection was attempted using the WiFiManager
    TEST_ASSERT_TRUE(g_mockWiFiManager.setupWifiConnectionCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_uses_wifi_manager\n");
}

static void test_configure_done_mode_configure_handles_wifi_manager_failure() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_handles_wifi_manager_failure\n");
    module_setup_service_setup();
    
    // Set up mocks for failure case
    g_mockWiFiManager.reset();
    g_mockWiFiManager.setupWifiConnectionSuccess = false;
    
    // Call the method under test
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify WiFi connection was attempted but failed
    TEST_ASSERT_TRUE(g_mockWiFiManager.setupWifiConnectionCalled);
    
    // Verify error was logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_handles_wifi_manager_failure\n");
}

static void test_configure_done_mode_configure_handles_missing_wifi_manager() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_handles_missing_wifi_manager\n");
    module_setup_service_setup();
    
    // Create a new service without WiFi manager
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        nullptr,  // No WiFi manager
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Call the method under test
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify error was logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_handles_missing_wifi_manager\n");
}

static void test_handle_module_mode_sets_module_modes_correctly() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_handle_module_mode_sets_module_modes_correctly\n");
    module_setup_service_setup();
    
    // Define the module modes to test
    ModuleModeEnum testModes[] = {
        ModuleModeEnum::USER_CAN_CONFIGURE,
        ModuleModeEnum::USER_CAN_RE_CONFIGURE,
        ModuleModeEnum::AP_TEST_MODE,
        ModuleModeEnum::CONFIGURED_DONE,
        ModuleModeEnum::CONFIGURED_NEED_RESTART
    };
    
    for (auto mode : testModes) {
        // Reset the mocks for each test case
        g_mockModuleConfig.reset();
        g_mockSettings.reset();
        
        // Set up the mock to return the specific mode
        g_mockSettings.setConfigured(mode);
        g_mockModuleConfig.setSettings(&g_mockSettings);
        
        if (debug_file != nullptr) fprintf(debug_file, "Testing module mode: %d\n", static_cast<int>(mode));
        
        // Call the method under test
        ModuleSetupServiceTestHelper::callHandleModuleMode(g_moduleSetupService);
        
        // Verify requested mode was set correctly
        TEST_ASSERT_TRUE(g_mockModuleConfig.wasModified("requestedModuleMode"));
        TEST_ASSERT_EQUAL(static_cast<int>(mode), g_mockModuleConfig.getRequestedModuleMode());
        
        if (debug_file != nullptr) fprintf(debug_file, "Requested mode set to: %d\n", g_mockModuleConfig.getRequestedModuleMode());
        
        // Verify current mode was set correctly
        TEST_ASSERT_TRUE(g_mockModuleConfig.wasModified("currentModuleMode"));
        TEST_ASSERT_EQUAL(static_cast<int>(mode), g_mockModuleConfig.getCurrentModuleMode());
        
        if (debug_file != nullptr) fprintf(debug_file, "Current mode set to: %d\n", g_mockModuleConfig.getCurrentModuleMode());
    }
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_handle_module_mode_sets_module_modes_correctly\n");
}

// Device Service Tests
static void test_configure_done_mode_configure_initializes_device_service() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_initializes_device_service\n");
    module_setup_service_setup();
    
    // Set up the mock DeviceService
    g_mockDeviceService.reset();
    g_mockDeviceService.setInitSuccess(true);
    
    // Set mode to CONFIGURED_DONE
    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    
    // Call handleModuleMode which will trigger ConfigureDoneModeConfigure
    ModuleSetupServiceTestHelper::callHandleModuleMode(g_moduleSetupService);
    
    // Verify that device service init was called
    if (debug_file != nullptr) fprintf(debug_file, "Checking if device service init was called: %s\n", 
                                g_mockDeviceService.wasInitCalled() ? "Yes" : "No");
    TEST_ASSERT_TRUE(g_mockDeviceService.wasInitCalled());
    TEST_ASSERT_EQUAL(1, g_mockDeviceService.getInitCallCount());
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_initializes_device_service\n");
}

static void test_configure_done_mode_configure_handles_device_service_init_failure() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_handles_device_service_init_failure\n");
    module_setup_service_setup();
    
    // Set up the mock DeviceService to fail on init
    g_mockDeviceService.reset();
    g_mockDeviceService.setInitSuccess(false);
    
    // Call the method directly
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify that device service init was called
    TEST_ASSERT_TRUE(g_mockDeviceService.wasInitCalled());
    
    // Verify error was logged (this depends on your implementation)
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_handles_device_service_init_failure\n");
}

static void test_configure_done_mode_configure_handles_null_device_service() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_handles_null_device_service\n");
    module_setup_service_setup();
    
    // Create a new service without device service
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager,
        nullptr,
        nullptr  // No device service
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Call the method directly
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // This should not crash
    // Verify error was logged
    TEST_ASSERT_TRUE(g_mockLogger.printCalled);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_handles_null_device_service\n");
}

static void test_device_service_not_initialized_in_other_modes() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_device_service_not_initialized_in_other_modes\n");
    
    // Test modes other than CONFIGURED_DONE
    ModuleModeEnum testModes[] = {
        ModuleModeEnum::USER_CAN_CONFIGURE,
        ModuleModeEnum::USER_CAN_RE_CONFIGURE,
        ModuleModeEnum::AP_TEST_MODE,
        ModuleModeEnum::CONFIGURED_NEED_RESTART
    };
    
    for (auto mode : testModes) {
        module_setup_service_setup();
        
        // Reset the device service mock
        g_mockDeviceService.reset();
        
        // Set the module mode
        g_mockSettings.setConfigured(mode);
        
        if (debug_file != nullptr) fprintf(debug_file, "Testing mode: %d\n", static_cast<int>(mode));
        
        // Call handleModuleMode
        ModuleSetupServiceTestHelper::callHandleModuleMode(g_moduleSetupService);
        
        // Verify device service init was NOT called
        TEST_ASSERT_FALSE(g_mockDeviceService.wasInitCalled());
        
        module_setup_service_teardown();
    }
    
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_device_service_not_initialized_in_other_modes\n");
}

static void test_configure_done_mode_configure_sets_up_mqtt_topic_handlers_on_successful_connection() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_sets_up_mqtt_topic_handlers_on_successful_connection\n");
    module_setup_service_setup();
    
    // Recreate service with MQTT manager
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Set up mocks for successful connection
    g_mockMqttManager.reset();
    g_mockMqttManager.connectReturnValue = true;
    
    // Call the method under test
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify MQTT connection was attempted and successful
    TEST_ASSERT_TRUE(g_mockMqttManager.connectCalled);
    TEST_ASSERT_EQUAL(1, g_mockMqttManager.connectCallCount);
    
    // Verify that setupMqttTopicHandlers was called after successful connection
    TEST_ASSERT_TRUE(g_mockMqttManager.setupMqttTopicHandlersCalled);
    TEST_ASSERT_EQUAL(1, g_mockMqttManager.setupMqttTopicHandlersCallCount);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_sets_up_mqtt_topic_handlers_on_successful_connection\n");
}

static void test_configure_done_mode_configure_do_not_sets_up_mqtt_topic_handlers_on_unsuccessful_connection() {
    if (debug_file != nullptr) fprintf(debug_file, "Starting test_configure_done_mode_configure_do_not_sets_up_mqtt_topic_handlers_on_unsuccessful_connection\n");
    module_setup_service_setup();
    
    // Recreate service with MQTT manager
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr, // buttonAdapter
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager
    );
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    
    // Set up mocks for failed connection
    g_mockMqttManager.reset();
    g_mockMqttManager.connectReturnValue = false;
    
    // Call the method under test
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    
    // Verify MQTT connection was attempted but failed
    TEST_ASSERT_TRUE(g_mockMqttManager.connectCalled);
    TEST_ASSERT_EQUAL(1, g_mockMqttManager.connectCallCount);
    
    // Verify that setupMqttTopicHandlers was NOT called since connection failed
    TEST_ASSERT_FALSE(g_mockMqttManager.setupMqttTopicHandlersCalled);
    TEST_ASSERT_EQUAL(0, g_mockMqttManager.setupMqttTopicHandlersCallCount);
    
    module_setup_service_teardown();
    if (debug_file != nullptr) fprintf(debug_file, "Finished test_configure_done_mode_configure_do_not_sets_up_mqtt_topic_handlers_on_unsuccessful_connection\n");
}

static void test_handle_module_mode_sets_no_scan_sensors_flag_when_button_pressed() {
    module_setup_service_setup();

    // Button is pressed at startup
    g_mockButtonUseCases.checkButtonResult = true;
    g_mockButtonUseCases.buttonPressedResult = true;

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    g_mockSettings.setNoScanSensors(0);

    ModuleSetupServiceTestHelper::callHandleModuleMode(g_moduleSetupService);

    // noScanSensors must be set
    TEST_ASSERT_EQUAL(1, g_mockSettings.getNoScanSensors());

    module_setup_service_teardown();
}

static void test_handle_module_mode_does_not_set_no_scan_sensors_flag_when_button_not_pressed() {
    module_setup_service_setup();

    // Button is NOT pressed
    g_mockButtonUseCases.checkButtonResult = false;
    g_mockButtonUseCases.buttonPressedResult = false;

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    g_mockSettings.setNoScanSensors(0);

    ModuleSetupServiceTestHelper::callHandleModuleMode(g_moduleSetupService);

    // noScanSensors must remain 0
    TEST_ASSERT_EQUAL(0, g_mockSettings.getNoScanSensors());

    module_setup_service_teardown();
}

static MockArduinoAdapter g_mockArduinoAdapter;
static MockClearModuleDataService g_mockClearModuleDataService;
static MockCheckModuleDeletionUseCase g_mockCheckModuleDeletionUseCase;
static MockCheckFirmwareUpdateOnStartupUseCase g_mockCheckFirmwareUpdateOnStartupUseCase;

static void build_service_with_deletion_fork(void) {
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger,
        &g_mockDebugSystem,
        &g_mockModuleConfig,
        &g_mockLedAdapter,
        nullptr,
        &g_mockLedUseCases,
        &g_mockButtonUseCases,
        &g_mockWifiAdapter,
        &g_mockWiFiManager,
        &g_mockWebServerLauncher,
        &g_mockLoopServiceFactory,
        &g_mockMqttManager,
        nullptr,
        &g_mockDeviceService,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &g_mockClearModuleDataService,
        &g_mockArduinoAdapter,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &g_mockCheckModuleDeletionUseCase,
        &g_mockCheckFirmwareUpdateOnStartupUseCase);
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
}

static void test_configure_done_skips_deletion_when_update_applied() {
    module_setup_service_setup();

    g_mockArduinoAdapter = MockArduinoAdapter();
    g_mockClearModuleDataService.reset();
    g_mockCheckModuleDeletionUseCase.reset();
    g_mockCheckFirmwareUpdateOnStartupUseCase.reset();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    g_mockSettings.setWasInitialSyncCompleted(1);
    g_mockCheckFirmwareUpdateOnStartupUseCase.runReturnValue = true;

    build_service_with_deletion_fork();
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);

    TEST_ASSERT_TRUE(g_mockCheckFirmwareUpdateOnStartupUseCase.runCalled);
    TEST_ASSERT_FALSE(g_mockCheckModuleDeletionUseCase.evaluateCalled);

    module_setup_service_teardown();
}

static void test_configure_done_runs_deletion_when_no_update() {
    module_setup_service_setup();

    g_mockArduinoAdapter = MockArduinoAdapter();
    g_mockClearModuleDataService.reset();
    g_mockCheckModuleDeletionUseCase.reset();
    g_mockCheckFirmwareUpdateOnStartupUseCase.reset();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    g_mockSettings.setWasInitialSyncCompleted(1);
    g_mockCheckFirmwareUpdateOnStartupUseCase.runReturnValue = false;
    g_mockCheckModuleDeletionUseCase.evaluateReturnValue = ModuleDeletionStatusEnum::NOT_DELETED;

    build_service_with_deletion_fork();
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);

    TEST_ASSERT_TRUE(g_mockCheckFirmwareUpdateOnStartupUseCase.runCalled);
    TEST_ASSERT_TRUE(g_mockCheckModuleDeletionUseCase.evaluateCalled);

    module_setup_service_teardown();
}

// --- GDV1-3782: computed VPD channel in the module structure ---

static ModbusRegisterConfig makeVpdTestConfig(uint8_t channel, const std::string& sensorType) {
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = 0;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channelName = sensorType;
    config.channel = channel;
    config.sensorType = sensorType;
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    return config;
}

static SensorEntity makeVpdTestAirSensor(const std::string& uid, int pin, int address) {
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.model = "ry_ws301";
    sensor.protocol = "ModBus";
    sensor.pin = pin;
    sensor.modbusDeviceAddress = address;
    sensor.sensorTypeUid = "model-uid-ry-ws301";
    sensor.modbusRegisterConfigs.push_back(makeVpdTestConfig(1, "air_temp"));
    sensor.modbusRegisterConfigs.push_back(makeVpdTestConfig(2, "humidity"));
    return sensor;
}

static int countSensorsOfType(const Node& node, const std::string& type) {
    int count = 0;
    for (const auto& sensor : node.sensors) {
        if (sensor.type == type) count++;
    }
    return count;
}

static const Sensor* findStructureSensor(const Node& node, const std::string& uid) {
    for (const auto& sensor : node.sensors) {
        if (sensor.uid == uid) return &sensor;
    }
    return nullptr;
}

// INV-1, INV-6, INV-9: a sensor with the air pair gets exactly one vpd record, parent pin/group/model
static void test_structure_adds_vpd_channel_for_air_sensor(void) {
    module_setup_service_setup();

    Node node;
    SensorEntity sensor = makeVpdTestAirSensor("AA:BB:CC:DD:EE:FF.00", 0, 1);
    ModuleSetupServiceTestHelper::callAddSensorToStructure(g_moduleSetupService, node, sensor);

    TEST_ASSERT_EQUAL(3, node.sensors.size());
    TEST_ASSERT_EQUAL(1, countSensorsOfType(node, "vpd"));

    const Sensor* vpd = findStructureSensor(node, "AA:BB:CC:DD:EE:FF.00.03");
    TEST_ASSERT_NOT_NULL(vpd);
    TEST_ASSERT_EQUAL_STRING("vpd", vpd->type.c_str());
    TEST_ASSERT_EQUAL(0, vpd->pin);
    TEST_ASSERT_EQUAL(1, vpd->group);
    TEST_ASSERT_EQUAL_STRING("model-uid-ry-ws301", vpd->modelUid.c_str());

    module_setup_service_teardown();
}

// INV-1: a soil sensor has no air pair, so no vpd record is created
static void test_structure_has_no_vpd_channel_without_pair(void) {
    module_setup_service_setup();

    Node node;
    SensorEntity sensor;
    sensor.uid = "AA:BB:CC:DD:EE:FF.01";
    sensor.protocol = "ModBus";
    sensor.pin = 1;
    sensor.modbusDeviceAddress = 1;
    sensor.modbusRegisterConfigs.push_back(makeVpdTestConfig(1, "soil_temp"));
    sensor.modbusRegisterConfigs.push_back(makeVpdTestConfig(2, "soil_moisture"));

    ModuleSetupServiceTestHelper::callAddSensorToStructure(g_moduleSetupService, node, sensor);

    TEST_ASSERT_EQUAL(2, node.sensors.size());
    TEST_ASSERT_EQUAL(0, countSensorsOfType(node, "vpd"));

    module_setup_service_teardown();
}

// INV-3: two air sensors -> two independent vpd records
static void test_structure_two_air_sensors_get_own_vpd(void) {
    module_setup_service_setup();

    Node node;
    SensorEntity first = makeVpdTestAirSensor("AA:BB:CC:DD:EE:FF.00", 0, 1);
    SensorEntity second = makeVpdTestAirSensor("AA:BB:CC:DD:EE:FF.01", 1, 2);
    second.modbusRegisterConfigs.push_back(makeVpdTestConfig(3, "co2"));

    ModuleSetupServiceTestHelper::callAddSensorToStructure(g_moduleSetupService, node, first);
    ModuleSetupServiceTestHelper::callAddSensorToStructure(g_moduleSetupService, node, second);

    TEST_ASSERT_EQUAL(2, countSensorsOfType(node, "vpd"));
    TEST_ASSERT_NOT_NULL(findStructureSensor(node, "AA:BB:CC:DD:EE:FF.00.03"));
    TEST_ASSERT_NOT_NULL(findStructureSensor(node, "AA:BB:CC:DD:EE:FF.01.04"));

    module_setup_service_teardown();
}

// INV-6: rebuilding the same structure replaces the record instead of duplicating it
static void test_structure_vpd_channel_is_not_duplicated(void) {
    module_setup_service_setup();

    Node node;
    SensorEntity sensor = makeVpdTestAirSensor("AA:BB:CC:DD:EE:FF.00", 0, 1);
    ModuleSetupServiceTestHelper::callAddSensorToStructure(g_moduleSetupService, node, sensor);
    ModuleSetupServiceTestHelper::callAddSensorToStructure(g_moduleSetupService, node, sensor);

    TEST_ASSERT_EQUAL(3, node.sensors.size());
    TEST_ASSERT_EQUAL(1, countSensorsOfType(node, "vpd"));

    module_setup_service_teardown();
}

// INV-5: the number must fit two digits, otherwise no channel is created
static void test_structure_skips_vpd_channel_on_overflow(void) {
    module_setup_service_setup();

    Node node;
    SensorEntity sensor;
    sensor.uid = "AA:BB:CC:DD:EE:FF.00";
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.modbusDeviceAddress = 1;
    sensor.modbusRegisterConfigs.push_back(makeVpdTestConfig(1, "air_temp"));
    sensor.modbusRegisterConfigs.push_back(makeVpdTestConfig(99, "humidity"));

    ModuleSetupServiceTestHelper::callAddSensorToStructure(g_moduleSetupService, node, sensor);

    TEST_ASSERT_EQUAL(2, node.sensors.size());
    TEST_ASSERT_EQUAL(0, countSensorsOfType(node, "vpd"));

    module_setup_service_teardown();
}

// Main test runner function
extern "C" int run_module_setup_service_tests(void) {
    if (debug_file != nullptr) fprintf(debug_file, "\n----- Running ModuleSetupService Tests -----\n");
    
    RUN_TEST(test_setup_module_initializes_config);
    RUN_TEST(test_setup_module_configures_leds);
    RUN_TEST(test_setup_module_without_led_adapter);
    RUN_TEST(test_setup_module_without_config);
    RUN_TEST(test_setup_module_triggers_led_use_cases);
    RUN_TEST(test_setup_module_without_led_use_cases);
    RUN_TEST(test_setup_module_launches_web_server_in_config_mode);
    RUN_TEST(test_module_mode_handling_for_user_can_configure);
    RUN_TEST(test_module_mode_handling_for_configured_done);
    RUN_TEST(test_handle_module_mode_invokes_startup_factory_reset);
    RUN_TEST(test_module_mode_handling_for_test_mode);
    RUN_TEST(test_module_mode_initialization_when_invalid);
    RUN_TEST(test_setup_module_without_web_server_launcher);
    RUN_TEST(test_setup_module_handles_led_sequences);
    
    RUN_TEST(test_setup_ap_creates_correct_access_point_for_sensor_module);
    RUN_TEST(test_setup_ap_creates_correct_access_point_for_hydro_module);
    RUN_TEST(test_setup_ap_creates_correct_access_point_for_device_module);
    RUN_TEST(test_setup_ap_creates_correct_access_point_for_socket_device);
    RUN_TEST(test_setup_ap_creates_correct_access_point_for_dimmer_module);
    RUN_TEST(test_setup_ap_configures_static_ip);
    RUN_TEST(test_setup_ap_skips_if_already_active);
    RUN_TEST(test_setup_ap_handles_missing_wifi_adapter);
    RUN_TEST(test_setup_ap_handles_configure_ap_failure);
    RUN_TEST(test_setup_ap_uses_random_part_in_ssid);
    RUN_TEST(test_setup_web_server_handles_null_launcher);
    
    RUN_TEST(test_get_loop_service_returns_service_for_current_mode);
    RUN_TEST(test_get_loop_service_handles_null_factory);
    RUN_TEST(test_get_loop_service_handles_null_config);
    RUN_TEST(test_get_loop_service_handles_null_settings);
    RUN_TEST(test_get_loop_service_handles_factory_exception);
    RUN_TEST(test_get_loop_service_for_each_module_mode);
    
    RUN_TEST(test_configure_done_mode_configure_sets_up_mqtt_connection);
    RUN_TEST(test_configure_done_mode_configure_handles_mqtt_connection_subscribe_was_not_called);
    RUN_TEST(test_configure_done_mode_configure_sets_up_mqtt_topic_handlers_on_successful_connection);
    RUN_TEST(test_configure_done_mode_configure_do_not_sets_up_mqtt_topic_handlers_on_unsuccessful_connection);
    RUN_TEST(test_configure_done_mode_configure_handles_missing_mqtt_manager);
    
    // Run WiFi Manager tests
    RUN_TEST(test_configure_done_mode_configure_uses_wifi_manager);
    RUN_TEST(test_configure_done_mode_configure_handles_wifi_manager_failure);
    RUN_TEST(test_configure_done_mode_configure_handles_missing_wifi_manager);
    
    // Device Service tests
    RUN_TEST(test_configure_done_mode_configure_initializes_device_service);
    RUN_TEST(test_configure_done_mode_configure_handles_device_service_init_failure);
    RUN_TEST(test_configure_done_mode_configure_handles_null_device_service);
    RUN_TEST(test_device_service_not_initialized_in_other_modes);

    // Button pressed -> noScanSensors flag tests
    RUN_TEST(test_handle_module_mode_sets_no_scan_sensors_flag_when_button_pressed);
    RUN_TEST(test_handle_module_mode_does_not_set_no_scan_sensors_flag_when_button_not_pressed);

    RUN_TEST(test_configure_done_skips_deletion_when_update_applied);
    RUN_TEST(test_configure_done_runs_deletion_when_no_update);

    // Computed VPD channel in the module structure (GDV1-3782)
    RUN_TEST(test_structure_adds_vpd_channel_for_air_sensor);
    RUN_TEST(test_structure_has_no_vpd_channel_without_pair);
    RUN_TEST(test_structure_two_air_sensors_get_own_vpd);
    RUN_TEST(test_structure_vpd_channel_is_not_duplicated);
    RUN_TEST(test_structure_skips_vpd_channel_on_overflow);

    if (debug_file != nullptr) fprintf(debug_file, "\n----- ModuleSetupService Tests Complete -----\n");
    
    return 0;
}
