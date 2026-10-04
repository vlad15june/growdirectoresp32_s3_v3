// How a stored sensor is described, and where that answer comes from.
//
// One rule decides it, in one place: a row the person has accepted is described as itself; any
// other stored row is described as itself too, unless the module has asked the line about it and
// nothing is coming back - then it is an empty port. The rule is not what this file is about. What
// it is about is the fact underneath it, which is about the present tense and has to stay that way:
//
//   - a sensor that was silent while the module restarted and answers an hour later is polled by
//     the sweep and reports readings, so every structure the module sends must carry it under its
//     own type again - including the record of its computed channel, which an empty-port row does
//     not have at all. A backend that is not told about a channel uid drops the readings that
//     arrive for it;
//   - and the same the other way round: a sensor that answered and has since gone quiet must
//     become an empty port on the next structure, not stay on its type until the next restart.
//
// The stand is the real one: the sensor registry over SPIFFS with its runtime error flag, the real
// sweep in SensorService, the real discovery, and the real RepublishModuleStructureUseCase - the
// path by which the catalog sync sends a structure between restarts.

#include <unity.h>

#include <string>
#include <vector>

#include "Mock/ArduinoMock.h"

#include "Application/Services/CommandService.h"
#include "Application/Services/ModuleSetupService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

/** @brief Reaches the structure fill, which is private to the setup service. */
class LiveFactTestHelper {
public:
    static void fillStructure(ModuleSetupService& service) { service.fillModuleStructure(); }
};

namespace {

const char* const MODULE_UID = "AA:BB:CC:DD:EE:FF";
const int MODULE_PIN_COUNT = 4;

// One stored row on port 1, and the three records it produces: two register channels and the VPD
// computed from them.
const char* const ROW_UID = "AA:BB:CC:DD:EE:FF.01";
const char* const AIR_TEMP_CHANNEL = "AA:BB:CC:DD:EE:FF.01.01";
const char* const HUMIDITY_CHANNEL = "AA:BB:CC:DD:EE:FF.01.02";
const char* const VPD_CHANNEL = "AA:BB:CC:DD:EE:FF.01.03";

const int ROW_ADDRESS = MODBUS_ADDRESS_MIN + 1;
const unsigned long SWEEP_INTERVAL_MS = 15000;

/** @brief An air temperature + humidity sensor of the registry, the shape that computes a VPD. */
SensorEntity makeAirSensor(SensorStatusEnum status, bool storedErrorFlag)
{
    SensorEntity entity;
    entity.uid = ROW_UID;
    entity.pin = 1;
    entity.protocol = "ModBus";
    entity.modbusDeviceAddress = ROW_ADDRESS;
    entity.status = status;
    entity.isActive = true;
    entity.isError = storedErrorFlag;
    entity.model = "air_temperature_humidity_sensor";
    entity.sensorType = "air_temp";
    entity.createdAt = "2026-09-01T10:00:00.0000000Z";

    ModbusRegisterConfig temperature;
    temperature.functionCode = 0x03;
    temperature.startRegister = 0x0000;
    temperature.numRegisters = 1;
    temperature.channel = 1;
    temperature.channelName = "temperature";
    temperature.sensorType = "air_temp";
    temperature.canConnectMinValue = -40.0f;
    temperature.canConnectMaxValue = 80.0f;
    entity.modbusRegisterConfigs.push_back(temperature);

    ModbusRegisterConfig humidity;
    humidity.functionCode = 0x03;
    humidity.startRegister = 0x0001;
    humidity.numRegisters = 1;
    humidity.channel = 2;
    humidity.channelName = "humidity";
    humidity.sensorType = "humidity";
    humidity.canConnectMinValue = 0.0f;
    humidity.canConnectMaxValue = 100.0f;
    entity.modbusRegisterConfigs.push_back(humidity);

    return entity;
}

/** @brief A catalog model the discovery holds but cannot scan for: no free port is adopted. */
SensorType makeUnscannableType()
{
    SensorType type;
    type.uid = "type-1";
    type.name = "AIR";
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = false;
    type.isSupported = true;
    type.baudRate = 9600;
    type.modbusDeviceAddress = 1;
    return type;
}

/**
 * @brief The module of this file: the registry over its real storage, the sweep, the discovery and
 *        the republish, wired the way globals.h wires them.
 */
struct LiveFactModule
{
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsSensorDao dao;
    SensorRepository sensorRepository;

    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    MockNTPTimeAdapter timeAdapter;
    MockArduinoAdapter arduino;
    MockHybridAPIService hybridApi;
    MockSensorReader sensorReader;
    MockSensorTypeRepository sensorTypeRepository;

    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;

    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModbusAddressAllocator modbusAddressAllocator;
    StartupSensorScanUseCase startupScan;
    RepublishModuleStructureUseCase republish;
    SensorService sensorService;
    CommandService commandService;
    ModuleSetupService setupService;

    unsigned long now = 0;

    LiveFactModule()
        : dao(&spiffs, &jsonConverter, &debugSystem, &logger),
          sensorRepository(&dao),
          moduleConfig(&logger, &debugSystem, &settings),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels),
          modbusAddressAllocator(&sensorRepository),
          startupScan(&logger, &debugSystem, &moduleConfig, &sensorRepository,
                      &sensorTypeRepository, &sensorReader, &moduleStructure, &sensorStructure,
                      &modbusAddressAllocator, &sensorValueModel, &virtualChannels, &arduino),
          republish(&moduleStructure, &sensorRepository, &hybridApi, &sensorStructure, &logger,
                    &debugSystem, nullptr),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader,
                        &sensorValueModel, &timeAdapter, &arduino, &hybridApi, &logger,
                        &debugSystem, nullptr, nullptr, nullptr, &virtualChannels),
          commandService(nullptr, nullptr, nullptr, &moduleStructure, &logger, &debugSystem,
                         &timeAdapter, &jsonConverter),
          setupService(&logger, &debugSystem, &moduleConfig,
                       nullptr, nullptr, nullptr, nullptr,
                       &wifiAdapter, nullptr, nullptr, nullptr, &mqttManager,
                       &moduleStructure, nullptr, nullptr,
                       &sensorRepository, &sensorTypeRepository, &sensorReader)
    {
        debugSystem.featureEnabled = false;

        settings.setUser("live@example.com");
        settings.setAccountUid("live-account");
        settings.setNoScanSensors(false);
        moduleConfig.setSettings(&settings);
        moduleConfig.setWifiAdapter(&wifiAdapter);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
        moduleConfig.setSensorPinCount(MODULE_PIN_COUNT);
        wifiAdapter.setMACAddress(MODULE_UID);
        wifiAdapter.setLocalIP("192.168.1.100");

        sensorTypeRepository.addSensorType(makeUnscannableType());

        setupService.setSensorStructureUseCase(&sensorStructure);
        setupService.setStartupSensorScan(&startupScan);
        startupScan.setRepublishModuleStructureUseCase(&republish);
        republish.setStoredSensorDescriber(&startupScan);
        commandService.setStartupSensorScan(&startupScan);
    }

    /** @brief Put a row in the registry file, the way a configuration before the restart does. */
    void store(const SensorEntity& entity)
    {
        TEST_ASSERT_TRUE(dao.saveSensor(entity));
        sensorRepository.refreshCache();
    }

    /** @brief What the line answers for the stored row, for the ping and for the sweep alike. */
    void lineAnswers(bool answers)
    {
        sensorReader.scanSensorReturnValue = answers;
        sensorReader.readMultiChannelReturnValue = answers;
        sensorReader.multiChannelValues.clear();
        if (answers)
        {
            sensorReader.multiChannelValues[AIR_TEMP_CHANNEL] = 22.5f;
            sensorReader.multiChannelValues[HUMIDITY_CHANNEL] = 55.0f;
        }
    }

    /** @brief The restart: the structure is built from the registry and the discovery is armed. */
    void boot() { LiveFactTestHelper::fillStructure(setupService); }

    /**
     * @brief Loop plans, bus executes, loop applies - until there is nothing left to do.
     *
     * A pass gives the free ports it switches on the seconds they need to come up, and has nothing
     * to hand out while that lasts, so the clock moves whenever the discovery runs dry and the
     * walk carries on until the pass is really over.
     */
    void runDiscovery()
    {
        int steps = 0;
        for (int round = 0; round < 8; round++)
        {
            StartupScanStep step;
            while (startupScan.takeNextStep(step))
            {
                startupScan.applyStepOutcome(startupScan.runStepOnBus(step));
                if (++steps > 5000)
                {
                    TEST_FAIL_MESSAGE("the discovery did not terminate");
                }
            }
            if (!startupScan.isRunning())
            {
                return;
            }
            now += StartupSensorScanUseCase::PORT_WAKE_UP_MS;
            arduino.setMillis(now);
        }
    }

    /**
     * @brief One 15 s sweep of the sensors the module holds, and the control loop iteration that
     *        follows it - the two halves the module runs, in the order it runs them.
     */
    void sweep()
    {
        now += SWEEP_INTERVAL_MS;
        arduino.setMillis(now);
        sensorService.loop();
        commandService.loop();
    }

    /** @brief The structure the catalog sync sends between restarts. */
    void republishStructure()
    {
        TEST_ASSERT_TRUE_MESSAGE(republish.execute("virtual_channel_shift"),
                                 "the republish was refused");
    }
};

/** @brief The type the structure carries for a channel, or "" when it carries no record at all. */
std::string typeOf(const ModuleStructure& structure, const char* channelUid)
{
    if (structure.nodes.empty())
    {
        return "";
    }
    for (size_t i = 0; i < structure.nodes[0].sensors.size(); i++)
    {
        if (structure.nodes[0].sensors[i].uid == channelUid)
        {
            return structure.nodes[0].sensors[i].type;
        }
    }
    return "";
}

/** @brief The three records of the row, as one line - what the backend would receive. */
std::string recordsOfTheRow(const ModuleStructure& structure)
{
    std::string air = typeOf(structure, AIR_TEMP_CHANNEL);
    std::string humidity = typeOf(structure, HUMIDITY_CHANNEL);
    std::string vpd = typeOf(structure, VPD_CHANNEL);

    return ".01 " + (air.empty() ? std::string("absent") : air) + "  .02 " +
           (humidity.empty() ? std::string("absent") : humidity) + "  .03 " +
           (vpd.empty() ? std::string("absent") : vpd);
}

/** @brief Live error flag of the row, as everything that reads the registry sees it. */
bool liveErrorFlag(ISensorRepository& repository)
{
    Utils::Optional<SensorEntity> row = repository.get(ROW_UID);
    TEST_ASSERT_TRUE_MESSAGE((bool)row, "the row left the registry");
    return row.value().isError;
}

} // namespace

// --- the sensor that was silent during the restart and answers afterwards ------------------------

/**
 * The row was stored, the module restarted, and the device was not on the line at that moment - so
 * the discovery's ping found nothing and the structure said the port was empty. Then the device
 * came back: the sweep reads it every fifteen seconds and its values are on their way to the
 * backend. From that moment every structure the module sends has to describe the row by its type
 * again, computed channel included, or the backend is being told to drop the readings it is being
 * sent.
 */
void test_live_fact_a_row_that_answers_after_the_restart_is_described_by_its_type(void)
{
    LiveFactModule module;
    module.store(makeAirSensor(SENSOR_STATUS_NEW, false));

    // Nothing on the line while the module restarts.
    module.lineAnswers(false);
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     "the silent row was not described as an empty port - "
                                     "the scenario did not happen");

    // The device is back, and the sweep reads it - twice, because one read that answers is what a
    // port the recovery has just power-cycled produces too, and that is not a device being there.
    module.lineAnswers(true);
    module.sweep();
    module.sweep();

    TEST_ASSERT_FALSE_MESSAGE(liveErrorFlag(module.sensorRepository),
                              "the sweep did not clear the error flag - the scenario did not happen");

    module.republishStructure();

    char message[256];
    snprintf(message, sizeof(message),
             "the row is being polled and its readings are being sent, and the structure describes "
             "it as: %s", recordsOfTheRow(module.moduleStructure).c_str());

    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("humidity",
                                     typeOf(module.moduleStructure, HUMIDITY_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("vpd", typeOf(module.moduleStructure, VPD_CHANNEL).c_str(),
                                     message);
}

/**
 * The same row and the same recovery, seen from the structure the boot itself builds: the boot has
 * been told nothing yet, so it describes the registry as it stands, and only the ping turns it into
 * an empty port. The record of the computed channel goes with it - a structure that says the two
 * channels are gone and the value computed from them is still there describes no sensor at all.
 */
void test_live_fact_a_silent_row_loses_its_computed_channel_too(void)
{
    LiveFactModule module;
    module.store(makeAirSensor(SENSOR_STATUS_NEW, false));

    module.lineAnswers(false);
    module.boot();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("vpd", typeOf(module.moduleStructure, VPD_CHANNEL).c_str(),
                                     "the boot did not describe the computed channel - "
                                     "the scenario did not happen");

    module.runDiscovery();

    char message[256];
    snprintf(message, sizeof(message),
             "the ping found nothing and the structure now says: %s",
             recordsOfTheRow(module.moduleStructure).c_str());

    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", typeOf(module.moduleStructure, VPD_CHANNEL).c_str(),
                                     message);
}

// --- the sensor that answered and has gone quiet -------------------------------------------------

/**
 * The other direction, and it has to be as fresh: the device answered the ping and the sweep, and
 * then it was unplugged. The sweep marks it, and once it has been quiet for a few of them the next
 * structure describes an empty port - without waiting for a restart to ask the line again.
 */
void test_live_fact_a_row_that_goes_quiet_becomes_an_empty_port(void)
{
    LiveFactModule module;
    module.store(makeAirSensor(SENSOR_STATUS_NEW, false));

    module.lineAnswers(true);
    module.boot();
    module.runDiscovery();
    module.sweep();

    module.republishStructure();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     "the answering row was not described by its type - "
                                     "the scenario did not happen");

    // Unplugged. The sweeps that follow get nothing at all, which is what tells the module the
    // device is gone rather than the line having dropped a frame.
    module.lineAnswers(false);
    module.sweep();
    module.sweep();
    module.sweep();

    TEST_ASSERT_TRUE_MESSAGE(liveErrorFlag(module.sensorRepository),
                             "the sweep did not mark the row - the scenario did not happen");

    module.republishStructure();

    char message[256];
    snprintf(message, sizeof(message),
             "the row has stopped answering the sweep and the structure still says: %s",
             recordsOfTheRow(module.moduleStructure).c_str());

    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", typeOf(module.moduleStructure, HUMIDITY_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", typeOf(module.moduleStructure, VPD_CHANNEL).c_str(),
                                     message);
}

/** A sensor the person has accepted is described as itself, answering or not - as it always was. */
void test_live_fact_an_accepted_row_that_goes_quiet_keeps_its_type(void)
{
    LiveFactModule module;
    module.store(makeAirSensor(SENSOR_STATUS_ADDED, false));

    module.lineAnswers(true);
    module.boot();
    module.runDiscovery();
    module.sweep();

    module.lineAnswers(false);
    module.sweep();
    TEST_ASSERT_TRUE_MESSAGE(liveErrorFlag(module.sensorRepository),
                             "the sweep did not mark the row - the scenario did not happen");

    module.republishStructure();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     "a sensor the person accepted was described as an empty port");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("vpd", typeOf(module.moduleStructure, VPD_CHANNEL).c_str(),
                                     "a sensor the person accepted lost its computed channel");
}

// --- the window between the restart and the first sweep ------------------------------------------

/**
 * The error flag is a field of the stored entity, so it survives a restart in the registry file,
 * and after a restart it says whatever was true before that restart. Until something has read the
 * row, it is not this module's answer to anything - a boot that asked the line nothing describes
 * the registry as it stands, exactly as it always has.
 */
void test_live_fact_a_no_scan_boot_ignores_the_error_flag_from_the_file(void)
{
    LiveFactModule module;
    module.settings.setNoScanSensors(1);
    module.store(makeAirSensor(SENSOR_STATUS_NEW, true));

    module.lineAnswers(false);
    module.boot();

    char message[256];
    snprintf(message, sizeof(message),
             "the boot that asked the line nothing described the row out of the registry file's "
             "error flag: %s", recordsOfTheRow(module.moduleStructure).c_str());

    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("humidity",
                                     typeOf(module.moduleStructure, HUMIDITY_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("vpd", typeOf(module.moduleStructure, VPD_CHANNEL).c_str(),
                                     message);
}

/**
 * And the same on a boot that does run the discovery: the first structure leaves before a single
 * question has been put to the line, so it carries the registry whole whatever the file says about
 * the row's health.
 */
void test_live_fact_the_boot_structure_ignores_the_error_flag_from_the_file(void)
{
    LiveFactModule module;
    module.store(makeAirSensor(SENSOR_STATUS_NEW, true));

    module.lineAnswers(true);
    module.boot();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     "the boot described the row out of the registry file's error flag");

    // And once the line has answered, the flag the file carried is gone for good.
    module.runDiscovery();
    module.sweep();
    module.republishStructure();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     "a row the sweep is reading was described as an empty port");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("vpd", typeOf(module.moduleStructure, VPD_CHANNEL).c_str(),
                                     "a row the sweep is reading lost its computed channel");
}

/**
 * The ping's answer is only the module's fact until the sweep has one of its own; from then on it
 * is not worth holding. The discovery outlives its own work by the whole life of the module, so
 * anything it keeps, it keeps for ever.
 */
void test_live_fact_the_ping_answer_is_dropped_once_the_sweep_has_read_the_row(void)
{
    LiveFactModule module;
    module.store(makeAirSensor(SENSOR_STATUS_NEW, false));

    module.lineAnswers(false);
    module.boot();
    module.runDiscovery();
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.silentToThePingCount(),
                              "the ping did not find the row silent - the scenario did not happen");

    module.lineAnswers(true);
    module.sweep();
    module.republishStructure();

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.silentToThePingCount(),
                              "the discovery is still holding a uid the sweep has taken over");
}

extern "C" void run_startup_live_sensor_fact_tests(void)
{
    RUN_TEST(test_live_fact_a_row_that_answers_after_the_restart_is_described_by_its_type);
    RUN_TEST(test_live_fact_a_silent_row_loses_its_computed_channel_too);
    RUN_TEST(test_live_fact_a_row_that_goes_quiet_becomes_an_empty_port);
    RUN_TEST(test_live_fact_an_accepted_row_that_goes_quiet_keeps_its_type);
    RUN_TEST(test_live_fact_a_no_scan_boot_ignores_the_error_flag_from_the_file);
    RUN_TEST(test_live_fact_the_boot_structure_ignores_the_error_flag_from_the_file);
    RUN_TEST(test_live_fact_the_ping_answer_is_dropped_once_the_sweep_has_read_the_row);
}
