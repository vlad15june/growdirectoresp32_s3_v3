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
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Sim/SimHttpClient.h"
#include "Mock/Infrastructure/Udp/MockUdpManager.h"
#include "Infrastructure/Controllers/UDP/UdpInboundDispatcher.h"

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

    static void callFillModuleStructure(ModuleSetupService* service) {
        if (service) {
            service->fillModuleStructure();
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
    
    // The LED is told the module is coming up as soon as its pins exist. That is what the boot
    // shows: the boot pattern, not the steady light of ledOnLoad().
    if (debug_file != nullptr) fprintf(debug_file, "Checking if the boot was announced: %s\n", 
                                g_mockLedUseCases.stages.empty() ? "No" : "Yes");
    TEST_ASSERT_TRUE(!g_mockLedUseCases.stages.empty());
    TEST_ASSERT_EQUAL(LED_BOOT_STAGE_BOOTING, g_mockLedUseCases.stages[0]);
    
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

/**
 * The link log is armed by the boot, and armed before the boot hands the radio its networks - and so
 * before anything can ask the radio to associate with them.
 *
 * What it is armed with is the radio's own events, and the first of them is the connect the boot's
 * own association produces: a module whose events are armed afterwards has no line for the link it
 * is running on, and a reconnect hours later would be the first thing its log ever said about the
 * network.
 */
static void test_setup_arms_the_link_log_before_it_asks_for_the_association(void) {
    module_setup_service_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    WifiLinkLogUseCase linkLog(&g_mockLogger, &g_mockDebugSystem, nullptr);
    g_moduleSetupService->setWifiLinkLog(&linkLog);

    int armedWhenAsked = 0;
    g_mockWiFiManager.onRegisterNetworks = [&armedWhenAsked]() {
        armedWhenAsked = g_mockWifiAdapter.reportLinkEventsToCallCount;
    };

    g_moduleSetupService->setupModule();

    TEST_ASSERT_TRUE_MESSAGE(g_mockWiFiManager.registerNetworksCalled,
                             "the boot never handed the radio its networks - there is no ordering "
                             "to assert");
    TEST_ASSERT_EQUAL_MESSAGE(1, g_mockWifiAdapter.reportLinkEventsToCallCount,
                              "the boot did not arm the radio's link events exactly once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&linkLog, g_mockWifiAdapter.linkLog,
                                  "the radio was armed with something other than the link log the "
                                  "boot was given");
    TEST_ASSERT_EQUAL_MESSAGE(1, armedWhenAsked,
                              "the networks were handed over before the link events were armed - "
                              "the connect they produce would not reach the log");

    module_setup_service_teardown();
}

/**
 * And a module wired without one boots the same: the link log is a late dependency, injected by
 * globals.h, and every stand in these tests is built without it.
 */
static void test_setup_without_a_link_log_leaves_the_radio_alone(void) {
    module_setup_service_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_moduleSetupService->setupModule();

    TEST_ASSERT_TRUE_MESSAGE(g_mockWiFiManager.registerNetworksCalled,
                             "the boot never reached the radio");
    TEST_ASSERT_EQUAL_MESSAGE(0, g_mockWifiAdapter.reportLinkEventsToCallCount,
                              "a boot with no link log armed the radio with a null one");

    module_setup_service_teardown();
}

/**
 * The end of the boot, said once. The milestone is the one the front panel changes hands on, and
 * until now it could only be inferred from the uptime the next line to be written happened to
 * carry.
 */
static void test_setup_says_the_boot_is_over_once(void) {
    module_setup_service_setup();

    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    // MockLogger::reset() keeps what it captured, and every test in this file boots a module, so
    // the boot under test is the lines from here on.
    const size_t linesBefore = g_mockLogger.messages.size();

    g_moduleSetupService->setupModule();

    int said = 0;
    for (size_t i = linesBefore; i < g_mockLogger.messages.size(); i++) {
        if (g_mockLogger.messages[i].find("BOOT: complete in ") == 0) {
            said++;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, said, "a boot did not say exactly once that it was over");

    module_setup_service_teardown();
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
    
    // In at the start, out at the end: the boot announces itself when the LED pins exist and
    // hands the LED over, cleared, when setup is done.
    if (debug_file != nullptr) fprintf(debug_file, "Checking the boot sequence on the LED: %u stage(s)\n", 
                                (unsigned)g_mockLedUseCases.stages.size());
    TEST_ASSERT_EQUAL(2, (int)g_mockLedUseCases.stages.size());
    TEST_ASSERT_EQUAL(LED_BOOT_STAGE_BOOTING, g_mockLedUseCases.stages[0]);
    TEST_ASSERT_EQUAL(LED_BOOT_STAGE_RUNNING, g_mockLedUseCases.stages[1]);
    
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

// The broker keeps the session - the subscriptions and the pushes the module missed - under the
// client ID, so the ID is what the guarantee hangs on: the same string on every boot of this
// module, and a different one on every other module. It is the deployment prefix plus the module
// UID, and the UID is the station MAC, which comes from the chip rather than from storage.
//
// Setup runs once per boot today, so what this pins is the composition itself: doing it twice must
// produce the same ID, not prefix+uid+uid. A module that changes its client ID is a client the
// broker has no session for, and everything queued under the old one is dropped without a word.
static void test_the_mqtt_client_id_is_the_prefix_and_the_module_uid_however_often_setup_runs() {
    module_setup_service_setup();

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

    // What globals.h builds the manager with, and what ModuleConfig answers on the board.
    g_mockMqttManager.reset();
    g_mockMqttManager.clientId = "GrowDirectorWifiClient";
    g_mockMqttManager.connectReturnValue = true;
    g_mockModuleConfig.setModuleUid("AA:BB:CC:DD:EE:01");

    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    TEST_ASSERT_EQUAL_STRING("GrowDirectorWifiClientAA:BB:CC:DD:EE:01",
                             g_mockMqttManager.clientId.c_str());

    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);
    TEST_ASSERT_EQUAL_STRING("GrowDirectorWifiClientAA:BB:CC:DD:EE:01",
                             g_mockMqttManager.clientId.c_str());

    module_setup_service_teardown();
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

// Setup hands the radio its networks and associates with none of them: the boot's association is
// the link supervision's, once the loop runs. How long setup takes does not depend on the network.
static void test_configure_done_mode_configure_uses_wifi_manager() {
    module_setup_service_setup();
    g_mockWiFiManager.reset();
    g_mockWifiAdapter.reset();

    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);

    TEST_ASSERT_TRUE_MESSAGE(g_mockWiFiManager.registerNetworksCalled, "setup did not hand the radio its networks");
    TEST_ASSERT_FALSE_MESSAGE(g_mockWifiAdapter.reconnectCalled, "setup associated the station itself");

    module_setup_service_teardown();
}

// No network to hand over is no reason to stop: the boot goes on, and says how many it handed.
static void test_configure_done_mode_configure_handles_wifi_manager_failure() {
    module_setup_service_setup();
    g_mockWiFiManager.reset();
    g_mockWiFiManager.registerNetworksResult = 0;

    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);

    TEST_ASSERT_TRUE(g_mockWiFiManager.registerNetworksCalled);
    TEST_ASSERT_TRUE_MESSAGE(g_mockDeviceService.wasInitCalled(), "the boot stopped after no network was handed over");

    module_setup_service_teardown();
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

// A boot with no link registers the topic handlers all the same: connect() only configures the
// client then, and the subscriptions it buffers are made on the first CONNACK.
static void test_configure_done_mode_configure_sets_up_mqtt_topic_handlers_without_a_link() {
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

    // No link: the manager configures the client and says no
    g_mockMqttManager.reset();
    g_mockMqttManager.connectReturnValue = false;

    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);

    TEST_ASSERT_EQUAL(1, g_mockMqttManager.connectCallCount);
    TEST_ASSERT_EQUAL_MESSAGE(1, g_mockMqttManager.setupMqttTopicHandlersCallCount,
                              "a boot with no link registered no MQTT topic handlers");

    module_setup_service_teardown();
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

/** @brief The service of a configured module, with this API service behind it and nothing of the network else. */
static void build_service_with_api(IHybridAPIService* api) {
    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger, &g_mockDebugSystem, &g_mockModuleConfig, &g_mockLedAdapter, nullptr,
        &g_mockLedUseCases, &g_mockButtonUseCases, &g_mockWifiAdapter, &g_mockWiFiManager,
        &g_mockWebServerLauncher, &g_mockLoopServiceFactory, &g_mockMqttManager, nullptr,
        &g_mockDeviceService, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        api, &g_mockArduinoAdapter);
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
}

// The REST client learns the server's address and the module's token from setup itself, and not as
// a side effect of a request that setup happens to make.
static void test_configure_done_gives_the_rest_client_the_stored_server_address(void) {
    module_setup_service_setup();
    g_mockArduinoAdapter = MockArduinoAdapter();
    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    MockHybridAPIService api;
    build_service_with_api(&api);
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);

    TEST_ASSERT_TRUE_MESSAGE(api.initCalled, "setup did not hand the REST client the stored server address");

    module_setup_service_teardown();
}

// Guard, with the real transport: the first request after the boot - here the module command fetch,
// which does not read the settings itself - goes to the stored server with the stored token.
static void test_the_first_request_after_the_boot_carries_the_stored_server_and_token(void) {
    module_setup_service_setup();
    g_mockArduinoAdapter = MockArduinoAdapter();
    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    g_mockSettings.setServerUrl("http://gd.test");
    g_mockSettings.setToken("TOK123");
    g_mockModuleConfig.setModuleUid("UID-1");

    ArduinoJsonConverter json;
    hostsim::SimHttpClient http;
    http.alwaysRespond(200, "[]");
    RestAPIService rest(&http, &g_mockModuleConfig, &g_mockSettings, &g_mockLogger, &g_mockDebugSystem, &json);
    HybridAPIService api(&rest, nullptr, &g_mockModuleConfig, &g_mockLogger, &g_mockDebugSystem,
                         ApiPriority::REST_PREFERRED);
    build_service_with_api(&api);
    ModuleSetupServiceTestHelper::callConfigureDoneModeConfigure(g_moduleSetupService);

    api.getModuleCommand();

    TEST_ASSERT_TRUE_MESSAGE(http.requestCount() >= 1, "the module command fetch made no request");
    const std::string url = http.lastRequest().url;
    TEST_ASSERT_TRUE_MESSAGE(url.compare(0, strlen("http://gd.test/"), "http://gd.test/") == 0,
                             ("the first request after the boot went to \"" + url + "\"").c_str());
    TEST_ASSERT_TRUE_MESSAGE(url.find("token=TOK123") != std::string::npos,
                             ("the first request after the boot carried no token: \"" + url + "\"").c_str());

    module_setup_service_teardown();
}

/** @brief A UDP manager whose controller exists only once it has been initialized, as UdpManager's does. */
class ListeningUdpManager : public MockUdpManager {
public:
    int initializeCalls = 0;
    uint16_t port = 0;
    std::string group;

    bool initialize(uint16_t localPort, const char* multicastIP) override {
        initializeCalls++;
        port = localPort;
        group = multicastIP != nullptr ? multicastIP : "";
        return true;
    }

    IUdpController* getController() override { return initializeCalls > 0 ? &controller : nullptr; }
};

// Setup opens no UDP listener - it has no link to listen on. The first link does, through
// startListening(): the port and the group, then the dispatcher on the controller that made, so that a
// packet reaches the handlers registered in setup.
static void test_setup_opens_no_udp_listener_and_the_first_link_does(void) {
    module_setup_service_setup();
    g_mockArduinoAdapter = MockArduinoAdapter();
    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    ListeningUdpManager udp;
    UdpInboundDispatcher dispatcher(&udp, &g_mockLogger, &g_mockDebugSystem);
    int delivered = 0;
    // What the remote sensor receiver does in setup: a handler on the dispatcher.
    dispatcher.addHandler([&delivered](const UdpPacketData&) { delivered++; });

    delete g_moduleSetupService;
    g_moduleSetupService = new ModuleSetupService(
        &g_mockLogger, &g_mockDebugSystem, &g_mockModuleConfig, &g_mockLedAdapter, nullptr,
        &g_mockLedUseCases, &g_mockButtonUseCases, &g_mockWifiAdapter, &g_mockWiFiManager,
        &g_mockWebServerLauncher, &g_mockLoopServiceFactory, &g_mockMqttManager, nullptr,
        &g_mockDeviceService, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &udp,
        nullptr, &g_mockArduinoAdapter, nullptr, nullptr, nullptr, nullptr, &dispatcher, nullptr);
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);

    g_moduleSetupService->setupModule();
    TEST_ASSERT_EQUAL_MESSAGE(0, udp.initializeCalls, "setup opened the UDP listeners with no link to listen on");

    g_moduleSetupService->startListening();
    TEST_ASSERT_EQUAL_MESSAGE(1, udp.initializeCalls, "the first link did not open the UDP listeners");
    TEST_ASSERT_EQUAL(1234, udp.port);
    TEST_ASSERT_EQUAL_STRING("239.1.2.3", udp.group.c_str());

    udp.controller.emitPacket("{}");
    TEST_ASSERT_EQUAL_MESSAGE(1, delivered, "a packet on the listener did not reach the handler registered in setup");

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

// --- The boot does not talk to the line ---
// Filling the sensor structure describes the registry and arms the discovery; the pings, the pin
// scans, the address writes and the per-model register writes are steps the
// acquisition task takes afterwards. What is asserted here is the boot's side of that split.

static MockSensorRepository g_mockSensorRepository;
static MockSensorReader g_mockSensorReader;
static ModuleStructure g_prepareModuleStructure;

class SetupBootScanProbe : public IStartupSensorScan {
public:
    int armCalls = 0;
    int rescanCalls = 0;
    int rescanPin = -1;
    int prepareArmCalls = 0;
    int describeCalls = 0;

    /** Records what the boot asked for; the records themselves are the structure use case's job. */
    void describeStoredSensors(Node& node) override {
        describeCalls++;
        (void)node;
    }
    void arm() override { armCalls++; }
    void rescan(int pin) override { rescanCalls++; rescanPin = pin; }
    void armPreparationOnly() override { prepareArmCalls++; }
    bool isRunning() const override { return armCalls > 0 || prepareArmCalls > 0; }
    bool isScanEntryRunning() const override { return isRunning() || rescanCalls > 0; }
    bool takeNextStep(StartupScanStep&) override { return false; }
    void returnStep() override {}
    StartupScanOutcome runStepOnBus(const StartupScanStep&) override { return StartupScanOutcome(); }
    void applyStepOutcome(const StartupScanOutcome&) override {}
    bool holdsAWindowOpen() const override { return false; }
    bool keepsThePortDark(int) const override { return false; }
    void reviewSensorLiveness() override {}
    void onDescribedStructureSettled(bool) override {}
};

static SetupBootScanProbe g_startupScanProbe;

static SensorEntity makePrepareTestWeatherSensor(void) {
    SensorEntity sensor;
    sensor.uid = "AA:BB:CC:DD:EE:FF.02";
    sensor.model = "XF700A";
    sensor.protocol = "ModBus";
    sensor.pin = 2;
    sensor.modbusDeviceAddress = 0x72;
    sensor.status = SENSOR_STATUS_ADDED;
    return sensor;
}

static void build_service_with_sensor_pipeline(void) {
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
        &g_prepareModuleStructure, // moduleStructure
        &g_mockDeviceService,
        nullptr, // ruleService
        &g_mockSensorRepository,
        nullptr, // sensorTypeRepository
        &g_mockSensorReader);
    g_moduleSetupService->setSensorStructureUseCase(&g_sensorStructureUseCase);
    g_moduleSetupService->setStartupSensorScan(&g_startupScanProbe);
}

static void prepare_test_setup(void) {
    module_setup_service_setup();
    g_mockSensorRepository = MockSensorRepository();
    g_mockSensorReader.reset();
    g_startupScanProbe = SetupBootScanProbe();
    g_prepareModuleStructure.nodes.clear();
    g_mockModuleConfig.setModuleType(MODULE_TYPE_SENSOR);
    g_mockModuleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
    g_mockSensorRepository.sensors.push_back(makePrepareTestWeatherSensor());
    build_service_with_sensor_pipeline();
}

// A normal boot: nothing on the line, and the discovery is armed to do it later
static void test_boot_touches_no_sensor_and_arms_the_discovery(void) {
    prepare_test_setup();

    g_mockSettings.setNoScanSensors(0);

    ModuleSetupServiceTestHelper::callFillModuleStructure(g_moduleSetupService);

    TEST_ASSERT_EQUAL(0, g_mockSensorReader.scanSensorCallCount);
    TEST_ASSERT_EQUAL(0, g_mockSensorReader.prepareSensorsForReadingCallCount);
    TEST_ASSERT_EQUAL(0, g_mockSensorReader.writeSensorAddressCallCount);
    TEST_ASSERT_EQUAL(1, g_startupScanProbe.armCalls);
    TEST_ASSERT_EQUAL(0, g_startupScanProbe.prepareArmCalls);
    TEST_ASSERT_EQUAL(1, g_startupScanProbe.describeCalls);

    module_setup_service_teardown();
}

/** @brief How many times the boot switched this port on through the sensor reader. */
static int powerOnsOfPort(int pin) {
    int ons = 0;
    for (size_t i = 0; i < g_mockSensorReader.portPowerCalls.size(); i++) {
        if (g_mockSensorReader.portPowerCalls[i].first == pin && g_mockSensorReader.portPowerCalls[i].second) {
            ons++;
        }
    }
    return ons;
}

// A configured sensor module gives the port of each stored ModBus sensor its power during the setup,
// once - so the port has come up by the sweep's first visit - and leaves the free ports alone.
static void test_the_boot_powers_the_stored_sensor_ports_of_a_configured_sensor_module(void) {
    prepare_test_setup();
    // The board's sensor module has its four sensor pins (ModuleConfig::getSensorPinCount()).
    g_mockModuleConfig.setSensorPinCount(SENSOR_PIN_COUNT);
    g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);

    g_moduleSetupService->setupModule();

    TEST_ASSERT_TRUE_MESSAGE(g_mockSensorReader.initCalled, "the boot did not begin the sensor bus");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, powerOnsOfPort(2),
                                  "the boot did not switch the stored sensor's port on, once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, powerOnsOfPort(0) + powerOnsOfPort(1) + powerOnsOfPort(3),
                                  "the boot switched a free port on");

    module_setup_service_teardown();
}

// Nowhere else: not in the board test, not in the configuration modes - none of their loops reads a
// sensor - and not on a module of another type, whose line is never begun.
static void test_the_boot_powers_no_port_outside_a_configured_sensor_module(void) {
    const ModuleModeEnum modes[3] = {ModuleModeEnum::AP_TEST_MODE, ModuleModeEnum::USER_CAN_CONFIGURE,
                                     ModuleModeEnum::USER_CAN_RE_CONFIGURE};
    for (int m = 0; m < 3; m++) {
        prepare_test_setup();
        g_mockModuleConfig.setSensorPinCount(SENSOR_PIN_COUNT);
        g_mockSettings.setConfigured(modes[m]);
        g_moduleSetupService->setupModule();
        char message[96];
        snprintf(message, sizeof(message), "the boot in mode %d switched a port on", (int)modes[m]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, powerOnsOfPort(2), message);
        module_setup_service_teardown();
    }

    // A hydro module has sensor pins too (ModuleConfig::getSensorPinCount()), and its UART pins
    // drive pumps (ModuleSetupService::setupSensorBus()).
    const int types[2] = {MODULE_TYPE_DEVICE, MODULE_TYPE_HYDRO};
    for (int t = 0; t < 2; t++) {
        prepare_test_setup();
        g_mockModuleConfig.setModuleType(types[t]);
        g_mockModuleConfig.setSensorPinCount(SENSOR_PIN_COUNT);
        g_mockSettings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
        g_moduleSetupService->setupModule();
        char message[96];
        snprintf(message, sizeof(message), "the boot of a module of type %d switched a port on", types[t]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, powerOnsOfPort(2), message);
        module_setup_service_teardown();
    }
}

// The fast path: the flag is consumed by the boot that used it, and no discovery is armed
static void test_boot_with_no_scan_flag_does_not_arm_the_discovery(void) {
    prepare_test_setup();

    g_mockSettings.setNoScanSensors(1);

    ModuleSetupServiceTestHelper::callFillModuleStructure(g_moduleSetupService);

    TEST_ASSERT_EQUAL(0, g_mockSensorReader.scanSensorCallCount);
    TEST_ASSERT_EQUAL(0, g_mockSensorReader.prepareSensorsForReadingCallCount);
    TEST_ASSERT_EQUAL(0, g_startupScanProbe.armCalls);
    TEST_ASSERT_EQUAL(1, g_startupScanProbe.prepareArmCalls);
    TEST_ASSERT_EQUAL(1, g_startupScanProbe.describeCalls);
    TEST_ASSERT_EQUAL_UINT8(0, g_mockSettings.getNoScanSensors());

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
    RUN_TEST(test_setup_arms_the_link_log_before_it_asks_for_the_association);
    RUN_TEST(test_setup_without_a_link_log_leaves_the_radio_alone);
    RUN_TEST(test_setup_says_the_boot_is_over_once);
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
    RUN_TEST(test_the_mqtt_client_id_is_the_prefix_and_the_module_uid_however_often_setup_runs);
    RUN_TEST(test_configure_done_mode_configure_handles_mqtt_connection_subscribe_was_not_called);
    RUN_TEST(test_configure_done_mode_configure_sets_up_mqtt_topic_handlers_on_successful_connection);
    RUN_TEST(test_configure_done_mode_configure_sets_up_mqtt_topic_handlers_without_a_link);
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

    RUN_TEST(test_configure_done_gives_the_rest_client_the_stored_server_address);
    RUN_TEST(test_the_first_request_after_the_boot_carries_the_stored_server_and_token);
    RUN_TEST(test_setup_opens_no_udp_listener_and_the_first_link_does);

    // Computed VPD channel in the module structure (GDV1-3782)
    RUN_TEST(test_structure_adds_vpd_channel_for_air_sensor);
    RUN_TEST(test_structure_has_no_vpd_channel_without_pair);
    RUN_TEST(test_structure_two_air_sensors_get_own_vpd);
    RUN_TEST(test_structure_vpd_channel_is_not_duplicated);
    RUN_TEST(test_structure_skips_vpd_channel_on_overflow);

    // Sensor preparation on every setup pass (GDV1-3847)
    RUN_TEST(test_boot_touches_no_sensor_and_arms_the_discovery);
    RUN_TEST(test_the_boot_powers_the_stored_sensor_ports_of_a_configured_sensor_module);
    RUN_TEST(test_the_boot_powers_no_port_outside_a_configured_sensor_module);
    RUN_TEST(test_boot_with_no_scan_flag_does_not_arm_the_discovery);

    // Boot-time duplicate ModBus address resolution (GDV1-3831)

    if (debug_file != nullptr) fprintf(debug_file, "\n----- ModuleSetupService Tests Complete -----\n");
    
    return 0;
}
