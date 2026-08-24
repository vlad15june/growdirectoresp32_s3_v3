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
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
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

namespace {

class RecordingSensorReader : public MockSensorReader {
public:
    bool scanned = false;
    uint16_t modbusPingAddress = 0;
    uint16_t registerToStoreModbusDeviceAddress = 0;
    uint8_t writeRegistersCommand = 0;
    bool applyAddressAfterPowerCycle = false;

    bool scanSensor(const SensorEntity &sensor) override {
        scanned = true;
        modbusPingAddress = sensor.modbusPingAddress;
        registerToStoreModbusDeviceAddress = sensor.registerToStoreModbusDeviceAddress;
        writeRegistersCommand = sensor.writeRegistersCommand;
        applyAddressAfterPowerCycle = sensor.applyAddressAfterPowerCycle;
        return true;
    }
};

} // namespace

// PingPin builds the probe sensor from the model: the three Modbus addressing
// fields and the power-cycle flag must be copied, otherwise the manual ping
// runs with zeroed addressing (GDV1-3788 review).
void test_ping_pin_copies_modbus_addressing_fields(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSensorTypeRepository sensorTypeRepository;
    RecordingSensorReader sensorReader;
    MockModuleConfig moduleConfig;

    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
    moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
    moduleConfig.setSensorPinCount(4);

    SensorType sensorType;
    sensorType.uid = "st-1";
    sensorType.name = "rk520-02";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.modbusDeviceAddress = 1;
    sensorType.modbusPingAddress = 0x0009;
    sensorType.registerToStoreModbusDeviceAddress = 0x0200;
    sensorType.writeRegistersCommand = 6;
    sensorType.applyAddressAfterPowerCycle = true;
    sensorTypeRepository.addSensorType(sensorType);

    ModuleCommandExecutionUseCase uc(
        &logger, &debugSystem, nullptr, nullptr, nullptr,
        nullptr, &sensorTypeRepository, &sensorReader, &moduleConfig, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    ModuleCommandDto cmd;
    cmd.uid = "cmd-ping-1";
    cmd.command = "PingPin";
    cmd.options = "{\"pin\":4}";
    cmd.createdAt = "";

    auto results = uc.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_TRUE(sensorReader.scanned);
    TEST_ASSERT_EQUAL_UINT16(0x0009, sensorReader.modbusPingAddress);
    TEST_ASSERT_EQUAL_UINT16(0x0200, sensorReader.registerToStoreModbusDeviceAddress);
    TEST_ASSERT_EQUAL_UINT8(6, sensorReader.writeRegistersCommand);
    TEST_ASSERT_TRUE(sensorReader.applyAddressAfterPowerCycle);
}

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
    ModbusAddressAllocator modbusAddressAllocator;
    ModuleCommandExecutionUseCase useCase;

    /**
     * Build a sensor module with one node and a command use case ready to run commands on pin 0.
     *
     * @param wireSensorStructureUseCase Pass false to leave the structure builder unset, the way
     *        the use case comes out of its constructor before the composition root calls the setter.
     */
    explicit SensorModelStructureFixture(bool wireSensorStructureUseCase = true)
        : virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          modbusAddressAllocator(&sensorRepository),
          useCase(&logger, &debugSystem, nullptr, nullptr, nullptr,
                  &sensorRepository, &sensorTypeRepository, &sensorReader,
                  &moduleConfig, nullptr, nullptr, nullptr, &moduleStructure,
                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                  &modbusAddressAllocator) {
        debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
        debugSystem.registerFeature(DebugFeatures::SETUP_DEBUG);

        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleStructure.nodes.push_back(Node());

        if (wireSensorStructureUseCase) {
            useCase.setSensorStructureUseCase(&sensorStructure);
        }
    }

    /**
     * Register a two-channel ModBus model in the catalog the use case reads.
     *
     * The four ModBus addressing fields carry non-default values on purpose: every path that
     * builds a sensor from a model has to copy them, and a default value would make that
     * assertion pass on its own.
     *
     * @param uid Uid the SetSensorModel command refers to.
     * @param firstType Channel 1 sensor type.
     * @param secondType Channel 2 sensor type.
     */
    void addModbusType(const char* uid, const char* firstType, const char* secondType) {
        SensorType sensorType;
        sensorType.uid = uid;
        sensorType.name = "test_modbus_sensor";
        sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
        sensorType.modbusDeviceAddress = 1;
        sensorType.modbusPingAddress = 0x0009;
        sensorType.registerToStoreModbusDeviceAddress = 0x0200;
        sensorType.writeRegistersCommand = 16;
        sensorType.applyAddressAfterPowerCycle = true;

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

    /**
     * Build a SetSensorModel command for pin 0 of the module.
     *
     * @param uid Command uid.
     * @param modelUid Sensor type uid the command assigns; the payload carries pin 4, which is
     *        pin 0 of the module.
     * @param jsonPin Pin as the backend numbers it: 4 is pin 0, 1 is pin 3.
     */
    static ModuleCommandDto setSensorModelCommand(const char* uid, const char* modelUid, int jsonPin = 4) {
        ModuleCommandDto cmd;
        cmd.uid = uid;
        cmd.command = "SetSensorModel";
        cmd.options = std::string("{\"pin\":") + std::to_string(jsonPin) + ",\"uid\":\"" + modelUid + "\"}";
        cmd.createdAt = "";
        return cmd;
    }

    /**
     * Build a command that carries only a pin.
     *
     * @param uid Command uid.
     * @param command Command name, ScanSensors or PingPin.
     * @param jsonPin Pin as the backend numbers it: 4 is pin 0, 1 is pin 3.
     */
    static ModuleCommandDto pinCommand(const char* uid, const char* command, int jsonPin = 4) {
        ModuleCommandDto cmd;
        cmd.uid = uid;
        cmd.command = command;
        cmd.options = std::string("{\"pin\":") + std::to_string(jsonPin) + "}";
        cmd.createdAt = "";
        return cmd;
    }

    /**
     * Put a ModBus entity of the same module into the repository, the way a sensor configured on
     * another port of the shared UART already sits there.
     *
     * @param uid Uid of the entity.
     * @param pin Port the entity sits on.
     * @param address ModBus address the entity holds.
     */
    void addNeighbourSensor(const char* uid, int pin, int address) {
        SensorEntity neighbour;
        neighbour.uid = uid;
        neighbour.pin = pin;
        neighbour.protocol = "ModBus";
        neighbour.modbusDeviceAddress = address;
        neighbour.status = SENSOR_STATUS_ADDED;
        sensorRepository.sensors.push_back(neighbour);
    }

    /**
     * @param address ModBus address to look for among the probes the reader recorded.
     */
    bool wasProbed(int address) const {
        for (int probed : sensorReader.probedAddresses) {
            if (probed == address) {
                return true;
            }
        }
        return false;
    }
};

void test_set_sensor_model_adds_virtual_channel_to_structure(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-set-model-1", "model-air")});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());

    TEST_ASSERT_EQUAL(3, static_cast<int>(f.moduleStructure.nodes[0].sensors.size()));

    const Sensor* vpd = f.findSensor("test_module_uid.00.03");
    TEST_ASSERT_NOT_NULL(vpd);
    TEST_ASSERT_EQUAL_STRING("vpd", vpd->type.c_str());
    TEST_ASSERT_EQUAL(0, vpd->pin);

    TEST_ASSERT_NOT_NULL(f.findSensor("test_module_uid.00.01"));
    TEST_ASSERT_NOT_NULL(f.findSensor("test_module_uid.00.02"));
}

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

void test_delete_sensors_removes_virtual_channel_too(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");

    f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-set-model-3", "model-air")});
    TEST_ASSERT_EQUAL(3, static_cast<int>(f.moduleStructure.nodes[0].sensors.size()));

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

void test_set_sensor_model_without_structure_use_case_changes_nothing(void) {
    SensorModelStructureFixture f(false);
    f.addModbusType("model-air", "air_temp", "humidity");

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-set-model-4", "model-air")});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());

    TEST_ASSERT_EQUAL(0, static_cast<int>(f.moduleStructure.nodes[0].sensors.size()));
    TEST_ASSERT_NULL(f.findSensor("test_module_uid.00.01"));
    TEST_ASSERT_NULL(f.findSensor("test_module_uid.00.03"));

    TEST_ASSERT_TRUE(f.logger.hasMessageContaining("sensor structure use case is not wired"));
}

/**
 * GDV1-3831: pins 2 and 3 share one UART, so an address held by the neighbour on pin 3 must never
 * be probed - and never accepted - while a model is set on pin 2, even when that address is the one
 * pin 2 would prefer.
 */
void test_set_sensor_model_never_probes_an_address_held_by_a_neighbour(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.03", 3, MODBUS_ADDRESS_MIN + 2);
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 1};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3831-1", "model-air", 2)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_FALSE(f.wasProbed(MODBUS_ADDRESS_MIN + 2));
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 1, f.sensorRepository.lastSavedSensor.modbusDeviceAddress);
    TEST_ASSERT_TRUE(f.logger.hasJsonAction("address_skipped_occupied"));
    TEST_ASSERT_TRUE(f.logger.jsonDetailsFor("address_skipped_occupied")
                         .find("test_module_uid.03") != std::string::npos);
}

/**
 * The address of the entity's own port is tried right after the default one, before the sweep.
 */
void test_set_sensor_model_prefers_the_own_address_of_the_pin(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 3};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3831-2", "model-air", 1)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(2, static_cast<int>(f.sensorReader.probedAddresses.size()));
    TEST_ASSERT_EQUAL(1, f.sensorReader.probedAddresses[0]);
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 3, f.sensorReader.probedAddresses[1]);
    TEST_ASSERT_TRUE(f.logger.jsonDetailsFor("address_accepted").find("per_port") != std::string::npos);
}

/**
 * The device answers only where the neighbour already sits: it is moved to a free address,
 * re-verified and only then saved.
 */
void test_set_sensor_model_moves_a_device_off_an_occupied_address(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.02", 2, MODBUS_ADDRESS_MIN + 2);
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 2};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3831-3", "model-air", 1)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(1, f.sensorReader.writeSensorAddressCallCount);
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 2, f.sensorReader.lastWriteFromAddress);
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 3, f.sensorReader.lastWriteToAddress);
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 3, f.sensorRepository.lastSavedSensor.modbusDeviceAddress);
    TEST_ASSERT_TRUE(f.logger.hasJsonAction("collision_detected"));
    TEST_ASSERT_TRUE(f.logger.hasJsonAction("reassign_attempt"));
    TEST_ASSERT_TRUE(f.logger.hasJsonAction("reassign_done"));
}

/**
 * A device the firmware cannot move is reported as an error, not as Done.
 */
void test_set_sensor_model_reports_error_when_the_address_write_is_rejected(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.02", 2, MODBUS_ADDRESS_MIN + 2);
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 2};
    f.sensorReader.writeSensorAddressReturnValue = false;

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3831-4", "model-air", 1)});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Failed to move sensor to a free ModBus address", results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(0, f.sensorRepository.saveCallCount);
    TEST_ASSERT_TRUE(f.logger.jsonDetailsFor("reassign_fail").find("write_rejected") != std::string::npos);
}

/**
 * The write was acknowledged but the device does not answer at the new address: nothing is saved.
 */
void test_set_sensor_model_reports_error_when_the_device_is_silent_after_the_move(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.02", 2, MODBUS_ADDRESS_MIN + 2);
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 2};
    f.sensorReader.answersAtNewAddressAfterWrite = false;

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3831-5", "model-air", 1)});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Sensor does not answer at the new ModBus address", results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(0, f.sensorRepository.saveCallCount);
    TEST_ASSERT_TRUE(f.logger.jsonDetailsFor("reassign_fail").find("verify_failed") != std::string::npos);
}

/**
 * The whole range is held by other entities of the module, one of them a row left over from a pin
 * the module no longer has: there is nowhere to move the device to.
 */
void test_set_sensor_model_reports_error_when_no_free_address_is_left(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.00", 0, MODBUS_ADDRESS_MIN);
    f.addNeighbourSensor("test_module_uid.01", 1, MODBUS_ADDRESS_MIN + 1);
    f.addNeighbourSensor("test_module_uid.02", 2, MODBUS_ADDRESS_MIN + 2);
    f.addNeighbourSensor("test_module_uid.10", 3, MODBUS_ADDRESS_MIN + 3);
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 2};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3831-6", "model-air", 1)});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("No free ModBus address on module", results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(0, f.sensorReader.writeSensorAddressCallCount);
    TEST_ASSERT_EQUAL(0, f.sensorRepository.saveCallCount);
    TEST_ASSERT_TRUE(f.logger.jsonDetailsFor("reassign_fail").find("no_free_address") != std::string::npos);
}

/**
 * Reconfiguring the sensor that already sits on this pin must not let it block itself.
 */
void test_set_sensor_model_does_not_treat_its_own_address_as_occupied(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.03", 3, MODBUS_ADDRESS_MIN + 3);
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 3};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3831-7", "model-air", 1)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(0, f.sensorReader.writeSensorAddressCallCount);
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 3, f.sensorRepository.lastSavedSensor.modbusDeviceAddress);
}

/**
 * A device answering at the free default address of its type is saved as before, with no write
 * into the device at all.
 */
void test_set_sensor_model_keeps_a_free_default_address_untouched(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.sensorReader.answeringAddresses = {1};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3831-8", "model-air", 1)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(1, static_cast<int>(f.sensorReader.probedAddresses.size()));
    TEST_ASSERT_EQUAL(1, f.sensorReader.probedAddresses[0]);
    TEST_ASSERT_EQUAL(0, f.sensorReader.writeSensorAddressCallCount);
    TEST_ASSERT_EQUAL(1, f.sensorRepository.lastSavedSensor.modbusDeviceAddress);
}

/**
 * ScanSensors carries the same defect and gets the same guard.
 */
void test_scan_sensors_moves_a_device_off_an_occupied_address(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.02", 2, MODBUS_ADDRESS_MIN + 2);
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 2};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::pinCommand(
        "cmd-3831-9", "ScanSensors", 1)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 3, f.sensorReader.lastWriteToAddress);
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 3, f.sensorRepository.lastCreatedOrUpdatedSensor.modbusDeviceAddress);
    TEST_ASSERT_EQUAL(1, static_cast<int>(results[0].result.sensors.size()));
}

/**
 * ScanSensors reports the failed move as an error instead of saving a colliding address.
 */
void test_scan_sensors_reports_error_when_the_device_cannot_be_moved(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.02", 2, MODBUS_ADDRESS_MIN + 2);
    f.sensorReader.answeringAddresses = {MODBUS_ADDRESS_MIN + 2};
    f.sensorReader.writeSensorAddressReturnValue = false;

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::pinCommand(
        "cmd-3831-10", "ScanSensors", 1)});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL(0, f.sensorRepository.createOrUpdateSensorCallCount);
}

/**
 * Nothing answers anywhere: ScanSensors still reports Done with an empty sensor list, as before.
 */
void test_scan_sensors_still_reports_done_with_an_empty_list(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.sensorReader.scanSensorReturnValue = false;

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::pinCommand(
        "cmd-3831-11", "ScanSensors", 1)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(0, static_cast<int>(results[0].result.sensors.size()));
    TEST_ASSERT_EQUAL(0, f.sensorRepository.createOrUpdateSensorCallCount);
}

/**
 * PingPin only reports what answers and saves nothing, so it keeps sweeping every address of the
 * range, occupied ones included.
 */
void test_ping_pin_still_sweeps_every_address_and_saves_nothing(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.addNeighbourSensor("test_module_uid.02", 2, MODBUS_ADDRESS_MIN + 2);
    f.sensorReader.scanSensorReturnValue = false;

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::pinCommand(
        "cmd-3831-12", "PingPin", 1)});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_TRUE(f.wasProbed(MODBUS_ADDRESS_MIN + 2));
    TEST_ASSERT_EQUAL(0, f.sensorRepository.saveCallCount);
    TEST_ASSERT_EQUAL(0, f.sensorRepository.createOrUpdateSensorCallCount);
    TEST_ASSERT_EQUAL(0, f.sensorReader.writeSensorAddressCallCount);
}

/**
 * SetSensorModel builds the stored entity from the model, so the four ModBus addressing fields
 * have to reach storage; zeros there send the address write into the broadcast fallbacks and the
 * pre-scan ping to register 0x0000 (GDV1-3848).
 */
void test_set_sensor_model_copies_modbus_addressing_fields(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.sensorReader.answeringAddresses = {1};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::setSensorModelCommand(
        "cmd-3848-1", "model-air", 1)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(1, f.sensorRepository.saveCallCount);
    TEST_ASSERT_EQUAL_UINT16(0x0009, f.sensorRepository.lastSavedSensor.modbusPingAddress);
    TEST_ASSERT_EQUAL_UINT16(0x0200, f.sensorRepository.lastSavedSensor.registerToStoreModbusDeviceAddress);
    TEST_ASSERT_EQUAL_UINT8(16, f.sensorRepository.lastSavedSensor.writeRegistersCommand);
    TEST_ASSERT_TRUE(f.sensorRepository.lastSavedSensor.applyAddressAfterPowerCycle);
}

/**
 * ScanSensors stores the entity it discovered, and it must carry the same four fields.
 */
void test_scan_sensors_copies_modbus_addressing_fields(void) {
    SensorModelStructureFixture f;
    f.addModbusType("model-air", "air_temp", "humidity");
    f.sensorReader.answeringAddresses = {1};

    auto results = f.useCase.executeCommands({SensorModelStructureFixture::pinCommand(
        "cmd-3848-2", "ScanSensors", 1)});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_EQUAL(1, f.sensorRepository.createOrUpdateSensorCallCount);
    TEST_ASSERT_EQUAL_UINT16(0x0009, f.sensorRepository.lastCreatedOrUpdatedSensor.modbusPingAddress);
    TEST_ASSERT_EQUAL_UINT16(0x0200, f.sensorRepository.lastCreatedOrUpdatedSensor.registerToStoreModbusDeviceAddress);
    TEST_ASSERT_EQUAL_UINT8(16, f.sensorRepository.lastCreatedOrUpdatedSensor.writeRegistersCommand);
    TEST_ASSERT_TRUE(f.sensorRepository.lastCreatedOrUpdatedSensor.applyAddressAfterPowerCycle);
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
    RUN_TEST(test_ping_pin_copies_modbus_addressing_fields);
    RUN_TEST(test_set_sensor_model_adds_virtual_channel_to_structure);
    RUN_TEST(test_set_sensor_model_without_air_pair_adds_no_virtual_channel);
    RUN_TEST(test_scan_sensors_adds_virtual_channel_to_structure);
    RUN_TEST(test_delete_sensors_removes_virtual_channel_too);
    RUN_TEST(test_set_sensor_model_without_structure_use_case_changes_nothing);
    RUN_TEST(test_set_sensor_model_never_probes_an_address_held_by_a_neighbour);
    RUN_TEST(test_set_sensor_model_prefers_the_own_address_of_the_pin);
    RUN_TEST(test_set_sensor_model_moves_a_device_off_an_occupied_address);
    RUN_TEST(test_set_sensor_model_reports_error_when_the_address_write_is_rejected);
    RUN_TEST(test_set_sensor_model_reports_error_when_the_device_is_silent_after_the_move);
    RUN_TEST(test_set_sensor_model_reports_error_when_no_free_address_is_left);
    RUN_TEST(test_set_sensor_model_does_not_treat_its_own_address_as_occupied);
    RUN_TEST(test_set_sensor_model_keeps_a_free_default_address_untouched);
    RUN_TEST(test_scan_sensors_moves_a_device_off_an_occupied_address);
    RUN_TEST(test_scan_sensors_reports_error_when_the_device_cannot_be_moved);
    RUN_TEST(test_scan_sensors_still_reports_done_with_an_empty_list);
    RUN_TEST(test_ping_pin_still_sweeps_every_address_and_saves_nothing);
    RUN_TEST(test_set_sensor_model_copies_modbus_addressing_fields);
    RUN_TEST(test_scan_sensors_copies_modbus_addressing_fields);
    return 0;
}
