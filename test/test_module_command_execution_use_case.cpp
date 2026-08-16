#include <unity.h>
#include <string>
#include <vector>
#include "Mock/ArduinoMock.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Core/Domain/MockLedUseCases.h"
#include "Mock/Core/Domain/MockFirmwareRemoteUpdate.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"

static MockWiFiManager* mockWiFiManager;
static MockLogger* mockLogger;
static MockDebugSystem* mockDebugSystem;
static MockLedUseCases* mockLedUseCases;
static ModuleCommandExecutionUseCase* useCase;

void setUp_module_command_execution_use_case(void) {
    mockWiFiManager = new MockWiFiManager();
    mockLogger = new MockLogger();
    mockDebugSystem = new MockDebugSystem();
    mockLedUseCases = new MockLedUseCases();

    mockDebugSystem->registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
    mockDebugSystem->setDebugFeatureState(DebugFeatures::MODULE_COMMAND_DEBUG, true);

    useCase = new ModuleCommandExecutionUseCase(
        mockLogger,      // logger
        mockDebugSystem, // debugSystem
        nullptr,         // timeAdapter
        nullptr,         // firmwareUpdater
        nullptr,         // arduinoAdapter
        nullptr,         // sensorRepository
        nullptr,         // sensorTypeRepository
        nullptr,         // sensorReader
        nullptr,         // moduleConfig
        nullptr,         // settings
        nullptr,         // hybridApiService
        nullptr,         // clearModuleDataService
        nullptr,         // moduleStructure
        nullptr,         // sensorValueModel
        mockWiFiManager, // wifiManager
        mockLedUseCases  // ledUseCases
    );
}

void tearDown_module_command_execution_use_case(void) {
    delete useCase;
    delete mockLedUseCases;
    delete mockDebugSystem;
    delete mockLogger;
    delete mockWiFiManager;

    useCase = nullptr;
    mockLedUseCases = nullptr;
    mockDebugSystem = nullptr;
    mockLogger = nullptr;
    mockWiFiManager = nullptr;
}

// addWifiNetworkSuccess = true -> status DONE, mock was called with correct credentials
void test_add_wifi_success(void) {
    setUp_module_command_execution_use_case();

    mockWiFiManager->addWifiNetworkSuccess = true;

    ModuleCommandDto cmd;
    cmd.uid = "cmd-1";
    cmd.command = "AddWifi";
    cmd.options = "{\"ssid\":\"HomeNet\",\"password\":\"secret123\"}";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_TRUE(mockWiFiManager->addWifiNetworkCalled);
    TEST_ASSERT_EQUAL_STRING("HomeNet", mockWiFiManager->lastSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("secret123", mockWiFiManager->lastPassword.c_str());

    tearDown_module_command_execution_use_case();
}

// addWifiNetworkSuccess = false -> status ERROR
void test_add_wifi_failure(void) {
    setUp_module_command_execution_use_case();

    mockWiFiManager->addWifiNetworkSuccess = false;

    ModuleCommandDto cmd;
    cmd.uid = "cmd-2";
    cmd.command = "AddWifi";
    cmd.options = "{\"ssid\":\"BadNet\",\"password\":\"wrongpass\"}";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_TRUE(mockWiFiManager->addWifiNetworkCalled);

    tearDown_module_command_execution_use_case();
}

// Empty options -> status ERROR, mock not called
void test_add_wifi_missing_options(void) {
    setUp_module_command_execution_use_case();

    ModuleCommandDto cmd;
    cmd.uid = "cmd-3";
    cmd.command = "AddWifi";
    cmd.options = "";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_FALSE(mockWiFiManager->addWifiNetworkCalled);

    tearDown_module_command_execution_use_case();
}

// Options with only password -> status ERROR
void test_add_wifi_missing_ssid(void) {
    setUp_module_command_execution_use_case();

    ModuleCommandDto cmd;
    cmd.uid = "cmd-4";
    cmd.command = "AddWifi";
    cmd.options = "{\"password\":\"secret123\"}";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_FALSE(mockWiFiManager->addWifiNetworkCalled);

    tearDown_module_command_execution_use_case();
}

// Options with only ssid -> status ERROR
void test_add_wifi_missing_password(void) {
    setUp_module_command_execution_use_case();

    ModuleCommandDto cmd;
    cmd.uid = "cmd-5";
    cmd.command = "AddWifi";
    cmd.options = "{\"ssid\":\"HomeNet\"}";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_FALSE(mockWiFiManager->addWifiNetworkCalled);

    tearDown_module_command_execution_use_case();
}

// nullptr wifiManager -> status ERROR
void test_add_wifi_no_wifi_manager(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);

    ModuleCommandExecutionUseCase ucNoWifi(
        &logger, &debugSystem,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr  // wifiManager = nullptr
    );

    ModuleCommandDto cmd;
    cmd.uid = "cmd-6";
    cmd.command = "AddWifi";
    cmd.options = "{\"ssid\":\"HomeNet\",\"password\":\"secret123\"}";
    cmd.createdAt = "";

    auto results = ucNoWifi.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
}

void test_blinking_start_calls_led_use_case(void) {
    setUp_module_command_execution_use_case();

    bool ok = useCase->execute("BlinkingStart", "", "cmd-bs-1", "");

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(mockLedUseCases->setBlinkingCalled);
    TEST_ASSERT_TRUE(mockLedUseCases->setBlinkingValue);
    TEST_ASSERT_EQUAL(1, mockLedUseCases->setBlinkingCount);

    tearDown_module_command_execution_use_case();
}

void test_blinking_stop_calls_led_use_case(void) {
    setUp_module_command_execution_use_case();

    bool ok = useCase->execute("BlinkingStop", "", "cmd-bs-2", "");

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(mockLedUseCases->setBlinkingCalled);
    TEST_ASSERT_FALSE(mockLedUseCases->setBlinkingValue);
    TEST_ASSERT_EQUAL(1, mockLedUseCases->setBlinkingCount);

    tearDown_module_command_execution_use_case();
}

void test_blinking_start_no_led_use_case(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);

    ModuleCommandExecutionUseCase ucNoLed(
        &logger, &debugSystem,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr,  // wifiManager
        nullptr   // ledUseCases = nullptr
    );

    bool ok = ucNoLed.execute("BlinkingStart", "", "cmd-bs-3", "");

    TEST_ASSERT_FALSE(ok);
}

void test_blinking_stop_no_led_use_case(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);

    ModuleCommandExecutionUseCase ucNoLed(
        &logger, &debugSystem,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr,  // wifiManager
        nullptr   // ledUseCases = nullptr
    );

    bool ok = ucNoLed.execute("BlinkingStop", "", "cmd-bs-4", "");

    TEST_ASSERT_FALSE(ok);
}

// GDV1-3821. A failed OTA used to reach the backend as the single sentence
// "Failed to start firmware update", whatever went wrong - so a read timeout (-11) on a slow
// link looked exactly like a refused connection or a missing file. The reason the updater
// recorded has to end up in the command result.
static ModuleCommandDto makeUpdateFirmwareCommand(const char* uid) {
    ModuleCommandDto cmd;
    cmd.uid = uid;
    cmd.command = "UpdateFirmware";
    cmd.options = "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\",\"version\":\"2.18.4\"}";
    cmd.createdAt = "";
    return cmd;
}

void test_update_firmware_failure_reports_reason(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockFirmwareRemoteUpdate firmwareUpdater;

    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
    debugSystem.setDebugFeatureState(DebugFeatures::MODULE_COMMAND_DEBUG, true);

    firmwareUpdater.setUpdateFromURLResult(false);
    firmwareUpdater.setLastErrorMessage("HTTP code=-11");

    ModuleCommandExecutionUseCase uc(
        &logger, &debugSystem, nullptr, &firmwareUpdater,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    auto results = uc.executeCommands({makeUpdateFirmwareCommand("cmd-fw-1")});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Failed to start firmware update: HTTP code=-11",
                             results[0].errorMessage.c_str());
    TEST_ASSERT_TRUE(firmwareUpdater.wasUpdateFromURLCalled());
}

// An updater that reports nothing must not produce a dangling "…update: " tail.
void test_update_firmware_failure_without_reason_keeps_generic_message(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockFirmwareRemoteUpdate firmwareUpdater;

    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
    debugSystem.setDebugFeatureState(DebugFeatures::MODULE_COMMAND_DEBUG, true);

    firmwareUpdater.setUpdateFromURLResult(false);

    ModuleCommandExecutionUseCase uc(
        &logger, &debugSystem, nullptr, &firmwareUpdater,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    auto results = uc.executeCommands({makeUpdateFirmwareCommand("cmd-fw-2")});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Failed to start firmware update",
                             results[0].errorMessage.c_str());
}

// The happy path keeps reporting Done with no message, and still asks for the restart.
void test_update_firmware_success_reports_done_without_message(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockFirmwareRemoteUpdate firmwareUpdater;
    MockModuleConfig moduleConfig;
    MockSettings settings;

    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
    debugSystem.setDebugFeatureState(DebugFeatures::MODULE_COMMAND_DEBUG, true);

    firmwareUpdater.setUpdateFromURLResult(true);
    firmwareUpdater.setLastErrorMessage("HTTP code=-11");  // stale reason must be ignored

    ModuleCommandExecutionUseCase uc(
        &logger, &debugSystem, nullptr, &firmwareUpdater,
        nullptr, nullptr, nullptr, nullptr, &moduleConfig, &settings,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    auto results = uc.executeCommands({makeUpdateFirmwareCommand("cmd-fw-3")});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("", results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(CONFIGURED_NEED_RESTART, moduleConfig.getRequestedModuleMode());
}

// GDV1-3820. A Reboot command used to restart the module without touching the boot type, and
// since ModuleSetupService resets that field to POWER_ON after publishing the structure, the next
// boot reported "power-on" - a commanded restart was indistinguishable from a power cut. The
// command always comes from a person, so it has to be recorded as BOOT_TYPE_USER, the way S2 does
// it and the way the UDP path records its own technical restart.
void test_reboot_command_sets_user_boot_type(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockArduinoAdapter arduinoAdapter;
    MockSettings settings;

    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
    debugSystem.setDebugFeatureState(DebugFeatures::MODULE_COMMAND_DEBUG, true);

    // The state the previous publish leaves behind
    settings.setBootType(BOOT_TYPE_POWER_ON);

    ModuleCommandExecutionUseCase uc(
        &logger, &debugSystem, nullptr, nullptr, &arduinoAdapter,
        nullptr, nullptr, nullptr, nullptr, &settings,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    ModuleCommandDto cmd;
    cmd.uid = "cmd-reboot-1";
    cmd.command = "Reboot";
    // Quoted on purpose: extractJsonValue reads quoted values only, and the boot type must not
    // displace the noScanSensors flag the handler already writes.
    cmd.options = "{\"noScanSensors\":\"true\"}";
    cmd.createdAt = "";

    auto results = uc.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_USER, settings.getBootType());
    TEST_ASSERT_EQUAL_UINT8(1, settings.getNoScanSensors());
    TEST_ASSERT_TRUE(arduinoAdapter.restartCalled);
}

// The MQTT entry point reaches the same handler and must record the same boot type.
void test_reboot_via_execute_sets_user_boot_type(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockArduinoAdapter arduinoAdapter;
    MockSettings settings;

    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
    debugSystem.setDebugFeatureState(DebugFeatures::MODULE_COMMAND_DEBUG, true);

    settings.setBootType(BOOT_TYPE_POWER_ON);

    ModuleCommandExecutionUseCase uc(
        &logger, &debugSystem, nullptr, nullptr, &arduinoAdapter,
        nullptr, nullptr, nullptr, nullptr, &settings,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    bool ok = uc.execute("Reboot", "", "cmd-reboot-2", "");

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_USER, settings.getBootType());
    TEST_ASSERT_TRUE(arduinoAdapter.restartCalled);
}

// Without settings there is nowhere to record the boot type, but the reboot still has to happen.
void test_reboot_without_settings_still_restarts(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockArduinoAdapter arduinoAdapter;

    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);

    ModuleCommandExecutionUseCase uc(
        &logger, &debugSystem, nullptr, nullptr, &arduinoAdapter,
        nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    bool ok = uc.execute("Reboot", "", "cmd-reboot-3", "");

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(arduinoAdapter.restartCalled);
}

extern "C" int run_module_command_execution_use_case_tests(void) {
    RUN_TEST(test_add_wifi_success);
    RUN_TEST(test_add_wifi_failure);
    RUN_TEST(test_add_wifi_missing_options);
    RUN_TEST(test_add_wifi_missing_ssid);
    RUN_TEST(test_add_wifi_missing_password);
    RUN_TEST(test_add_wifi_no_wifi_manager);
    RUN_TEST(test_blinking_start_calls_led_use_case);
    RUN_TEST(test_blinking_stop_calls_led_use_case);
    RUN_TEST(test_blinking_start_no_led_use_case);
    RUN_TEST(test_blinking_stop_no_led_use_case);
    RUN_TEST(test_update_firmware_failure_reports_reason);
    RUN_TEST(test_update_firmware_failure_without_reason_keeps_generic_message);
    RUN_TEST(test_update_firmware_success_reports_done_without_message);
    RUN_TEST(test_reboot_command_sets_user_boot_type);
    RUN_TEST(test_reboot_via_execute_sets_user_boot_type);
    RUN_TEST(test_reboot_without_settings_still_restarts);
    return 0;
}
