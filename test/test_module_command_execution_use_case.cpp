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
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
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

// GDV1-3782. Picking the sensor model in the app publishes the structure right after the command,
// and that structure used to carry only the register channels: the module built the records with
// ModuleStructure::addSensor, which knows nothing about computed channels. VPD was calculated and
// sent in hardware/update from the first read, but the backend had never been told the channel
// exists, so the app showed no VPD until the module restarted and rebuilt the whole structure
// (module 9C:13:9E:9A:51:9C, firmware 2.18.6, 2026-08-15). The command path has to build the
// records the same way the setup flow does.
struct SensorModelStructureFixture {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockModuleConfig moduleConfig;
    MockSensorRepository sensorRepository;
    MockSensorTypeRepository sensorTypeRepository;
    MockSensorReader sensorReader;
    ModuleStructure moduleStructure;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModuleCommandExecutionUseCase useCase;

    SensorModelStructureFixture()
        : virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          useCase(&logger, &debugSystem, nullptr, nullptr, nullptr,
                  &sensorRepository, &sensorTypeRepository, &sensorReader,
                  &moduleConfig, nullptr, nullptr, nullptr, &moduleStructure) {
        debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
        debugSystem.registerFeature(DebugFeatures::SETUP_DEBUG);

        // Pins exist only on a sensor module
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);

        // The module always has exactly one node
        moduleStructure.nodes.push_back(Node());

        useCase.setSensorStructureUseCase(&sensorStructure);
    }

    // Sensor type of two ModBus register channels, e.g. the air temperature + humidity of RY-WS301
    void addModbusType(const char* uid, const char* firstType, const char* secondType) {
        SensorType sensorType;
        sensorType.uid = uid;
        sensorType.name = "test_modbus_sensor";
        sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
        sensorType.modbusDeviceAddress = 1;

        ModbusRegisterConfig first;
        first.channel = 1;
        first.sensorType = firstType;
        ModbusRegisterConfig second;
        second.channel = 2;
        second.sensorType = secondType;

        sensorType.modbusRegisterConfig.push_back(first);
        sensorType.modbusRegisterConfig.push_back(second);

        sensorTypeRepository.addSensorType(sensorType);
    }

    const Sensor* findSensor(const std::string& uid) const {
        for (const auto& sensor : moduleStructure.nodes[0].sensors) {
            if (sensor.uid == uid) {
                return &sensor;
            }
        }
        return nullptr;
    }

    // Pin 4 in the command payload is pin 0 of the module
    static ModuleCommandDto setSensorModelCommand(const char* uid, const char* modelUid) {
        ModuleCommandDto cmd;
        cmd.uid = uid;
        cmd.command = "SetSensorModel";
        cmd.options = std::string("{\"pin\":4,\"uid\":\"") + modelUid + "\"}";
        cmd.createdAt = "";
        return cmd;
    }
};

// The pair air_temp + humidity -> the structure carries the computed VPD channel immediately
void test_set_sensor_model_adds_virtual_channel_to_structure(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-set-model-1", "model-air")});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());

    // Two register channels plus the computed one, and nothing else
    TEST_ASSERT_EQUAL(3, static_cast<int>(f.moduleStructure.nodes[0].sensors.size()));

    const Sensor* vpd = f.findSensor("test_module_uid.00.03");
    TEST_ASSERT_NOT_NULL(vpd);
    TEST_ASSERT_EQUAL_STRING("vpd", vpd->type.c_str());
    TEST_ASSERT_EQUAL(0, vpd->pin);

    TEST_ASSERT_NOT_NULL(f.findSensor("test_module_uid.00.01"));
    TEST_ASSERT_NOT_NULL(f.findSensor("test_module_uid.00.02"));
}

// No air pair -> no computed channel is invented
void test_set_sensor_model_without_air_pair_adds_no_virtual_channel(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-soil", "soil_temp", "ec");

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-set-model-2", "model-soil")});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());

    TEST_ASSERT_EQUAL(2, static_cast<int>(f.moduleStructure.nodes[0].sensors.size()));
    TEST_ASSERT_NULL(f.findSensor("test_module_uid.00.03"));
    TEST_ASSERT_NOT_NULL(f.findSensor("test_module_uid.00.01"));
    TEST_ASSERT_NOT_NULL(f.findSensor("test_module_uid.00.02"));
}

// ScanSensors adds the sensor it detected through the same builder, so it has the same channels
void test_scan_sensors_adds_virtual_channel_to_structure(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");

    ModuleCommandDto cmd;
    cmd.uid = "cmd-scan-1";
    cmd.command = "ScanSensors";
    cmd.options = "{\"pin\":4}";
    cmd.createdAt = "";

    auto results = f.useCase.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());

    const Sensor* vpd = f.findSensor("test_module_uid.00.03");
    TEST_ASSERT_NOT_NULL(vpd);
    TEST_ASSERT_EQUAL_STRING("vpd", vpd->type.c_str());
}

// DeleteSensors clears the pin, and a computed channel carries the pin of its sensor - so it goes
// with the register channels and needs no separate handling.
void test_delete_sensors_removes_virtual_channel_too(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");

    f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-set-model-3", "model-air")});
    TEST_ASSERT_EQUAL(3, static_cast<int>(f.moduleStructure.nodes[0].sensors.size()));

    // The repository has to report the sensor for DeleteSensors to find anything on the pin
    SensorEntity stored;
    stored.uid = "test_module_uid.00";
    stored.pin = 0;
    f.sensorRepository.sensors.push_back(stored);

    ModuleCommandDto cmd;
    cmd.uid = "cmd-delete-1";
    cmd.command = "DeleteSensors";
    cmd.options = "{\"pin\":4}";
    cmd.createdAt = "";

    auto results = f.useCase.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(0, static_cast<int>(f.moduleStructure.nodes[0].sensors.size()));
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
    RUN_TEST(test_set_sensor_model_adds_virtual_channel_to_structure);
    RUN_TEST(test_set_sensor_model_without_air_pair_adds_no_virtual_channel);
    RUN_TEST(test_scan_sensors_adds_virtual_channel_to_structure);
    RUN_TEST(test_delete_sensors_removes_virtual_channel_too);
    return 0;
}
