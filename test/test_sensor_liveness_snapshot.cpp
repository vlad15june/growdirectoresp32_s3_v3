// The structure is a snapshot; whether a sensor is answering is not. This file is about the seam
// between the two.
//
// The module keeps one error flag per stored row, and it goes up on the first read that comes back
// with nothing - a collision with a discovery step or a raw command, a device that answered late, a
// frame lost to noise. The structure, on the other hand, is built rarely: at boot, when the
// discovery finishes, when the catalog moves a computed channel. Two things follow, and they are
// the two halves of this file:
//
//   - a structure built in the second a transient failure was up describes the row as an empty
//     port ("type":"none"), and the backend then drops every reading that arrives for its channels.
//     On a quiet module nothing builds another structure, so the sensor is gone from the app until
//     the next reboot. The answer is that the describer waits for three missed sweeps, not one;
//   - and the mirror image: a row that was quiet when the structure was built and answers ten
//     minutes later, or one that was there and has really gone, stays as it was described for just
//     as long. The answer is that the control loop compares the settled fact against what the last
//     structure said and sends the structure again when they part.
//
// The waiting is on both edges, and the second one is not symmetry for its own sake: the port
// recovery cuts the power of a port that has been failing for a minute and calls it recovered on
// the first read that answers, so a bad contact produces a cycle of a minute of silence and one
// answer. A module that believed that answer would describe itself twice a cycle for as long as
// the contact is bad. And because no pair of thresholds can answer every period - for any pair
// there is one that resonates with it - there is a floor under the interval between two
// liveness structures, and the differences it holds back are counted, not dropped.
//
// The stand is the module: the real registry over SPIFFS with its runtime error flag, the real
// sweep in SensorService, the real discovery, the real CommandService loop that the firmware runs
// every iteration, and the real RepublishModuleStructureUseCase. Replaced are the line, the server
// and the clock. What the server *took* is kept by value, structure by structure, so the
// assertions are about what reached the far end and not about what the module happens to hold.
//
// ### Reproducing the red
//
// One edit each, in StartupSensorScanUseCase.h unless said otherwise:
//
//   - in lineIsSilentFor(), drop the `&& missedSweepsSince(lastReadMs) >= ...` conjunct so that one
//     failed read is enough again: the transient empties the port, and a flap of any period sends
//     a structure every time the line drops a frame;
//   - in the same function, return `false` as soon as the row answers, instead of waiting for
//     ANSWERING_SWEEPS_TO_RETURN: the recovery flap costs a structure twice a cycle again;
//   - remove the LIVENESS_REPUBLISH_MIN_INTERVAL_MS check in reviewSensorLiveness(): the flap
//     that crosses both thresholds is unbounded again;
//   - make onDescribedStructureSettled() return without restoring the notes: a change of liveness
//     that happened while the uplink was down is lost for good;
//   - empty the body of reviewSensorLiveness(), or delete its call in CommandService::loop(): the
//     module keeps the description it made once for ever.

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
class LivenessTestHelper {
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

// A second row on a second port, for the scenarios where one sensor has to do something while
// another is doing something else.
const char* const SECOND_ROW_UID = "AA:BB:CC:DD:EE:FF.02";
const char* const SECOND_AIR_CHANNEL = "AA:BB:CC:DD:EE:FF.02.01";
const char* const SECOND_HUMIDITY_CHANNEL = "AA:BB:CC:DD:EE:FF.02.02";
const int SECOND_ROW_ADDRESS = MODBUS_ADDRESS_MIN + 2;

/**
 * @brief The far end of the module's calls, keeping by value every structure it *took*.
 *
 * Attempts are counted separately: a send the uplink refused is not something the server holds,
 * and the difference between the two is the whole of the offline scenario below.
 */
class RecordingServer : public MockHybridAPIService {
public:
    std::vector<ModuleStructure> structures;
    size_t structureAttempts = 0;

    bool sendToServerModuleStructure(const ModuleStructure& moduleStructure,
                                     ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        structureAttempts++;
        const bool accepted =
            MockHybridAPIService::sendToServerModuleStructure(moduleStructure, priority);
        if (accepted)
        {
            structures.push_back(moduleStructure);
        }
        return accepted;
    }
};

/** @brief An air temperature + humidity sensor of the registry, the shape that computes a VPD. */
SensorEntity makeAirSensor(SensorStatusEnum status)
{
    SensorEntity entity;
    entity.uid = ROW_UID;
    entity.pin = 1;
    entity.protocol = "ModBus";
    entity.modbusDeviceAddress = ROW_ADDRESS;
    entity.status = status;
    entity.isActive = true;
    entity.isError = false;
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

/** @brief The same sensor on port 2, so the stand can hold two rows that behave differently. */
SensorEntity makeSecondAirSensor(SensorStatusEnum status)
{
    SensorEntity entity = makeAirSensor(status);
    entity.uid = SECOND_ROW_UID;
    entity.pin = 2;
    entity.modbusDeviceAddress = SECOND_ROW_ADDRESS;
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
 * @brief The module of this file, wired the way globals.h wires it: the registry over its storage,
 *        the sweep, the discovery, the republish and the control loop that drives them.
 */
struct LivenessModule
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
    RecordingServer server;
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

    LivenessModule()
        : dao(&spiffs, &jsonConverter, &debugSystem, &logger),
          sensorRepository(&dao),
          moduleConfig(&logger, &debugSystem, &settings),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels),
          modbusAddressAllocator(&sensorRepository),
          startupScan(&logger, &debugSystem, &moduleConfig, &sensorRepository,
                      &sensorTypeRepository, &sensorReader, &moduleStructure, &sensorStructure,
                      &modbusAddressAllocator, &sensorValueModel, &virtualChannels, &arduino),
          republish(&moduleStructure, &sensorRepository, &server, &sensorStructure, &logger,
                    &debugSystem, &arduino),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader,
                        &sensorValueModel, &timeAdapter, &arduino, &server, &logger,
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

        settings.setUser("liveness@example.com");
        settings.setAccountUid("liveness-account");
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
    void boot() { LivenessTestHelper::fillStructure(setupService); }

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
     * @brief Fifteen seconds of the module: one sweep of the sensors, then the control loop.
     *
     * Both halves are the production ones - SensorService::loop() reads the registry's sensors on
     * its own tick, CommandService::loop() is what main.cpp reaches every iteration.
     */
    void sweep()
    {
        now += SWEEP_INTERVAL_MS;
        arduino.setMillis(now);
        sensorService.loop();
        commandService.loop();
    }

    /** @brief The control loop alone, without the clock moving: what an idle iteration does. */
    void controlLoop() { commandService.loop(); }

    /** @brief Fifteen seconds in which the line answers or does not, as told. */
    void sweepWith(bool lineAnswering)
    {
        lineAnswers(lineAnswering);
        sweep();
    }

    /** @brief What the line answers for each of the two rows, separately. */
    void bothRowsAnswer(bool first, bool second)
    {
        sensorReader.scanSensorReturnValue = first || second;

        sensorReader.multiChannelAnswerByUid[ROW_UID] = first;
        sensorReader.multiChannelAnswerByUid[SECOND_ROW_UID] = second;

        std::map<std::string, float>& firstValues = sensorReader.multiChannelValuesByUid[ROW_UID];
        firstValues.clear();
        firstValues[AIR_TEMP_CHANNEL] = 22.5f;
        firstValues[HUMIDITY_CHANNEL] = 55.0f;

        std::map<std::string, float>& secondValues =
            sensorReader.multiChannelValuesByUid[SECOND_ROW_UID];
        secondValues.clear();
        secondValues[SECOND_AIR_CHANNEL] = 21.0f;
        secondValues[SECOND_HUMIDITY_CHANNEL] = 60.0f;
    }

    /** @brief The structure the catalog sync sends between restarts. */
    void republishStructure()
    {
        TEST_ASSERT_TRUE_MESSAGE(republish.execute("virtual_channel_shift"),
                                 "the republish was refused");
    }

    size_t structuresSent() const { return server.structures.size(); }
};

/** @brief The type a structure carries for a channel, or "" when it carries no record at all. */
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

/** @brief A module past its restart, with the row answering and the sweep reading it. */
void bringUpWithTheRowAnswering(LivenessModule& module, SensorStatusEnum status)
{
    module.store(makeAirSensor(status));
    module.lineAnswers(true);
    module.boot();
    module.runDiscovery();
    module.sweep();
    module.republishStructure();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     "the answering row was not described by its type - "
                                     "the scenario did not happen");
}

} // namespace

// --- the transient that must not empty a port -----------------------------------------------------

/**
 * The line drops one read of the row - and the catalog sync happens to send a structure in that
 * same second. One missed read is not the sensor being gone, and the structure must not say it is:
 * the record the backend receives decides whether the readings that follow are kept or dropped.
 */
void test_liveness_a_structure_built_on_a_single_missed_read_keeps_the_row(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_NEW);

    // One sweep gets nothing, and the structure is built right then.
    module.lineAnswers(false);
    module.sweep();

    TEST_ASSERT_TRUE_MESSAGE(liveErrorFlag(module.sensorRepository),
                             "the sweep did not raise the error flag - the scenario did not happen");

    module.republishStructure();

    char message[256];
    snprintf(message, sizeof(message),
             "one missed read, and the structure the server was sent says: %s",
             recordsOfTheRow(module.server.structures.back()).c_str());

    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("humidity",
                                     typeOf(module.server.structures.back(), HUMIDITY_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("vpd",
                                     typeOf(module.server.structures.back(), VPD_CHANNEL).c_str(),
                                     message);
}

/**
 * Two missed reads are still not enough, and the third is: the row is quiet for three quarters of a
 * minute before the module tells the server its port is empty. The structure of the second miss is
 * the one that matters - a debounce that only delayed the decision by one sweep would pass the test
 * above and fail this one.
 */
void test_liveness_the_port_is_emptied_on_the_third_missed_read(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_NEW);

    module.lineAnswers(false);

    module.sweep();
    module.sweep();
    module.republishStructure();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     "two missed reads emptied the port");

    module.sweep();
    module.republishStructure();

    char message[256];
    snprintf(message, sizeof(message),
             "three missed reads, and the structure the server was sent says: %s",
             recordsOfTheRow(module.server.structures.back()).c_str());

    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), VPD_CHANNEL).c_str(),
                                     message);
}

// --- the module describing itself again ----------------------------------------------------------

/**
 * The row was quiet while the module restarted, so the structure said its port was empty. An hour
 * later the device is plugged back in and the sweep starts reading it. Nothing on a settled module
 * builds another structure - no catalog change, no restart - so unless the loop notices by itself,
 * the sensor is missing from the app until somebody power-cycles the module.
 */
void test_liveness_a_row_that_comes_back_is_described_again_by_the_loop(void)
{
    LivenessModule module;
    module.store(makeAirSensor(SENSOR_STATUS_NEW));

    // Nothing on the line while the module restarts: the ping finds nothing and the discovery
    // sends the structure that says so.
    module.lineAnswers(false);
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     "the silent row was not described as an empty port - "
                                     "the scenario did not happen");

    const size_t structuresBefore = module.structuresSent();

    // The device is back. One read that answers is not yet a device that is there - a port the
    // recovery has just power-cycled produces exactly one of those - so nothing is sent for it.
    module.lineAnswers(true);
    module.sweep();

    TEST_ASSERT_FALSE_MESSAGE(liveErrorFlag(module.sensorRepository),
                              "the sweep did not clear the error flag - the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore, module.structuresSent(),
                              "a single read that answered was taken for a sensor that is back");

    // The second one in a row is.
    module.sweep();

    char message[256];
    snprintf(message, sizeof(message),
             "the row is being read every sweep and the last structure the server was sent says: %s",
             recordsOfTheRow(module.server.structures.back()).c_str());

    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore + 1, module.structuresSent(),
                              "the module did not describe itself again after the sensor came back");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.startupScan.livenessRepublishCount(),
                              "the structure that went out did not come from the liveness review");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("humidity",
                                     typeOf(module.server.structures.back(), HUMIDITY_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("vpd",
                                     typeOf(module.server.structures.back(), VPD_CHANNEL).c_str(),
                                     message);

    // And once said, it is not said again: the structure that went out is what the module holds.
    for (int i = 0; i < 20; i++)
    {
        module.sweep();
    }
    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore + 1, module.structuresSent(),
                              "the module keeps sending structures after the change was reported");
}

/**
 * The other direction, and it is the one that leaves a ghost in the app: the sensor answered when
 * the structure was built and has since been unplugged. Its records stay in the structure the
 * server holds - the app shows a live sensor whose last value never changes - until the module
 * describes itself again.
 */
void test_liveness_a_row_that_goes_quiet_is_described_again_by_the_loop(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_NEW);

    const size_t structuresBefore = module.structuresSent();

    module.lineAnswers(false);
    module.sweep();
    module.sweep();

    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore, module.structuresSent(),
                              "two missed reads were enough to describe the module again");

    module.sweep();

    char message[256];
    snprintf(message, sizeof(message),
             "the sensor has been quiet for three sweeps and the last structure the server was "
             "sent says: %s", recordsOfTheRow(module.server.structures.back()).c_str());

    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore + 1, module.structuresSent(),
                              "the module did not describe itself again after the sensor went away");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.startupScan.livenessRepublishCount(),
                              "the structure that went out did not come from the liveness review");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), HUMIDITY_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), VPD_CHANNEL).c_str(),
                                     message);

    // The description now matches the fact, so the loop has nothing left to say about it.
    for (int i = 0; i < 20; i++)
    {
        module.sweep();
    }
    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore + 1, module.structuresSent(),
                              "the module keeps sending structures after the change was reported");
}

// --- what the loop must not do -------------------------------------------------------------------

/**
 * A device on a marginal line answers, misses, answers, misses. Neither state settles, so neither
 * belongs in a structure - and a module that sent one on every transition would spend its uplink on
 * a sensor that is working.
 */
void test_liveness_a_flap_inside_the_debounce_sends_no_structure(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_NEW);

    const size_t structuresBefore = module.structuresSent();

    // Fifty sweeps of alternating answers - twelve and a half minutes of a bad connector.
    for (int i = 0; i < 50; i++)
    {
        module.lineAnswers((i % 2) == 0);
        module.sweep();
    }

    char message[192];
    snprintf(message, sizeof(message),
             "a flapping sensor cost the server %u structures",
             (unsigned)(module.structuresSent() - structuresBefore));

    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore, module.structuresSent(), message);

    // Two misses in a row, over and over, and still nothing settles.
    for (int i = 0; i < 60; i++)
    {
        module.lineAnswers((i % 3) == 0);
        module.sweep();
    }

    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore, module.structuresSent(),
                              "two missed reads out of every three were taken for a sensor that is gone");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.startupScan.livenessRepublishCount(),
                              "the liveness review acted on a sensor that never settled");
}

/**
 * The flap the module makes for itself, and the one that matters: the port recovery cuts the power
 * of a port that has been failing for a minute and brings it back, and one read that answers is all
 * it takes to call the port recovered. A device with a bad contact therefore lives in a cycle of
 * about a minute of silence and a single answer. Believing that single answer would cost a
 * structure twice a cycle - the debounce on the silent edge alone does not stop it, because both
 * states do settle; what stops it is that a lone answer settles nothing.
 */
void test_liveness_a_recovery_flap_settles_once_and_says_nothing_more(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_NEW);

    const size_t structuresBefore = module.structuresSent();

    // Sixty cycles of {three silent sweeps, one that answers} - sixty simulated minutes of a port
    // the recovery keeps power-cycling.
    for (int cycle = 0; cycle < 60; cycle++)
    {
        module.sweepWith(false);
        module.sweepWith(false);
        module.sweepWith(false);
        module.sweepWith(true);
    }

    char message[192];
    snprintf(message, sizeof(message),
             "a port the recovery keeps power-cycling cost the server %u structures in an hour",
             (unsigned)(module.structuresSent() - structuresBefore));

    // One: the sensor really did stop answering, and the app has to be told once. Not one per cycle.
    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore + 1, module.structuresSent(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     "the one structure that went out did not describe the empty port");
}

/**
 * And the flap that is built to defeat the hysteresis: quiet for exactly as long as it takes to be
 * given up on, answering for exactly as long as it takes to be taken back, over and over. No pair
 * of thresholds can answer this one - for any pair there is a period that resonates with it - so
 * what answers it is the floor under the interval between two structures. The point of the test is
 * the number: whatever the line does, this is what an hour of it costs.
 */
void test_liveness_a_flap_that_crosses_both_thresholds_is_bounded(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_NEW);

    const size_t structuresBefore = module.structuresSent();

    // Cycles of {three silent, two answering} = 75 s each; 48 of them make a simulated hour.
    for (int cycle = 0; cycle < 48; cycle++)
    {
        module.sweepWith(false);
        module.sweepWith(false);
        module.sweepWith(false);
        module.sweepWith(true);
        module.sweepWith(true);
    }

    const unsigned sent = (unsigned)(module.structuresSent() - structuresBefore);
    // One per five minutes, plus the first, which is never held back.
    const unsigned ceiling =
        60 / (StartupSensorScanUseCase::LIVENESS_REPUBLISH_MIN_INTERVAL_MS / 60000) + 1;

    char message[224];
    snprintf(message, sizeof(message),
             "a flap tuned to both thresholds cost the server %u structures in a simulated hour, "
             "and the floor allows %u", sent, ceiling);

    TEST_ASSERT_TRUE_MESSAGE(sent <= ceiling, message);
    TEST_ASSERT_TRUE_MESSAGE(sent >= 1, "the module said nothing at all about a sensor that changed");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.livenessRepublishHeldCount() > 0,
                             "nothing was held back - the bound came from somewhere else");
}

/**
 * A liveness change that happens while the uplink is down. The description is built and handed to
 * a server that never takes it, so what the module holds and what the server holds have parted -
 * and nothing else on a settled module will ever send a structure again. The note may only be
 * believed once the structure has arrived; until then the module still owes it.
 */
void test_liveness_a_change_the_uplink_refused_is_offered_again(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_NEW);

    const size_t structuresTaken = module.structuresSent();

    // The uplink is down: every send is refused.
    module.server.sendModuleStructureReturnValue = false;

    module.lineAnswers(false);
    module.sweep();
    module.sweep();
    module.sweep();

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.livenessRepublishCount() >= 1,
                             "the module did not even try while the uplink was down - "
                             "the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(structuresTaken, module.structuresSent(),
                              "the server took a structure while the uplink was down");

    // Ten more minutes of outage, then the network comes back. The sensor is still gone.
    for (int i = 0; i < 40; i++)
    {
        module.sweep();
    }
    TEST_ASSERT_EQUAL_MESSAGE(structuresTaken, module.structuresSent(),
                              "the server took a structure while the uplink was down");

    module.server.sendModuleStructureReturnValue = true;
    for (int i = 0; i < 40; i++)
    {
        module.sweep();
    }

    char message[256];
    snprintf(message, sizeof(message),
             "the uplink is back and the last structure the server holds says: %s",
             recordsOfTheRow(module.server.structures.back()).c_str());

    TEST_ASSERT_EQUAL_MESSAGE(structuresTaken + 1, module.structuresSent(),
                              "the change that was lost to the outage was never offered again");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.livenessUndeliveredCount() >= 1,
                             "the describer was never told that a structure had not arrived");
}

/**
 * A module configured not to scan on boot. What it may not do is talk to the line before the loop
 * exists, and it does not: the boot describes the registry as it stands and asks nothing. What it
 * still owes is a snapshot that is true - the sweep runs on such a module like any other, and a
 * sensor that has really gone must not stay in the app as a live reading. Without that, such
 * a module would send nothing at all, whatever happened to its sensors.
 */
void test_liveness_a_no_scan_boot_still_reports_a_settled_change(void)
{
    LivenessModule module;
    module.settings.setNoScanSensors(1);
    module.store(makeAirSensor(SENSOR_STATUS_NEW));

    module.lineAnswers(true);
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, module.structuresSent(),
                              "a boot that skips the discovery sent a structure - "
                              "the scenario did not happen");

    // And the structure such a boot builds is the registry as it stands, whatever the line is
    // doing: nothing has been asked of it, so there is no fact to describe a row by.
    char bootMessage[256];
    snprintf(bootMessage, sizeof(bootMessage),
             "the boot of a no-scan module described its registry as: %s",
             recordsOfTheRow(module.moduleStructure).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.moduleStructure, AIR_TEMP_CHANNEL).c_str(),
                                     bootMessage);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("vpd", typeOf(module.moduleStructure, VPD_CHANNEL).c_str(),
                                     bootMessage);

    // Two sweeps that answer: nothing has changed, so nothing is sent.
    module.sweep();
    module.sweep();
    TEST_ASSERT_EQUAL_MESSAGE(0, module.structuresSent(),
                              "a module where nothing changed sent a structure anyway");

    // The sensor goes away for good.
    module.lineAnswers(false);
    module.sweep();
    module.sweep();
    module.sweep();

    char message[256];
    snprintf(message, sizeof(message),
             "the sensor of a no-scan module has been gone for three sweeps and the server was "
             "sent %u structure(s), the last saying: %s",
             (unsigned)module.structuresSent(),
             module.structuresSent() ? recordsOfTheRow(module.server.structures.back()).c_str()
                                     : "(nothing)");

    TEST_ASSERT_EQUAL_MESSAGE(1, module.structuresSent(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), VPD_CHANNEL).c_str(),
                                     message);
}

/**
 * Two sensors, and one of them waiting for the interval while the other is coming back.
 *
 * A row is taken back only after it has been *seen* answering twice, and the seeing is the review
 * pass - so a pass that stopped at the first row whose description no longer fits would leave every
 * row behind it standing still for as long as that one had to wait. The row that came back second
 * would go on being described as an empty port while it reports readings every fifteen seconds.
 *
 * Which of the two rows the registry hands out first is not something a test can choose - the
 * repository holds them in a hash map - so the scenario is run both ways round.
 */
void runBothRowsComeBackScenario(bool secondRowComesBackFirst)
{
    LivenessModule module;
    module.store(makeAirSensor(SENSOR_STATUS_NEW));
    module.store(makeSecondAirSensor(SENSOR_STATUS_NEW));

    module.bothRowsAnswer(true, true);
    module.boot();
    module.runDiscovery();
    module.sweep();
    module.republishStructure();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.server.structures.back(), SECOND_AIR_CHANNEL).c_str(),
                                     "the second row was not described by its type - "
                                     "the scenario did not happen");

    // Both go away, and the module says so once. That structure starts the interval.
    module.bothRowsAnswer(false, false);
    module.sweep();
    module.sweep();
    module.sweep();

    const size_t structuresAfterBothWentAway = module.structuresSent();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     "the first row was not given up on - the scenario did not happen");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOf(module.server.structures.back(), SECOND_AIR_CHANNEL).c_str(),
                                     "the second row was not given up on - the scenario did not happen");

    // One of them comes back at once - and has to wait out the interval, because the module has
    // just sent a structure. The other comes back two sweeps later, while that wait is running.
    module.bothRowsAnswer(!secondRowComesBackFirst, secondRowComesBackFirst);
    module.sweep();
    module.sweep();
    module.bothRowsAnswer(true, true);

    // The rest of the interval, with both of them reporting readings the whole way.
    for (int i = 0; i < 24; i++)
    {
        module.sweep();
    }

    char message[256];
    snprintf(message, sizeof(message),
             "%s came back first; both have been answering for minutes and the last structure the "
             "server holds says: .01 %s  .02 %s",
             secondRowComesBackFirst ? "the second row" : "the first row",
             typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
             typeOf(module.server.structures.back(), SECOND_AIR_CHANNEL).c_str());

    TEST_ASSERT_TRUE_MESSAGE(module.structuresSent() > structuresAfterBothWentAway,
                             "the module never described itself again after both sensors came back");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.server.structures.back(), SECOND_AIR_CHANNEL).c_str(),
                                     message);
}

void test_liveness_a_row_coming_back_is_not_held_up_by_the_first_row(void)
{
    runBothRowsComeBackScenario(false);
}

void test_liveness_a_row_coming_back_is_not_held_up_by_the_second_row(void)
{
    runBothRowsComeBackScenario(true);
}

/** A module whose sensors are all answering describes itself once and then says nothing. */
void test_liveness_a_settled_module_sends_nothing(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_NEW);

    const size_t structuresBefore = module.structuresSent();

    for (int i = 0; i < 40; i++)
    {
        module.sweep();
        // And an idle iteration between the sweeps is exactly what the loop spends most of its
        // time on: it must not turn into a structure either.
        module.controlLoop();
        module.controlLoop();
    }

    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore, module.structuresSent(),
                              "a module where nothing changed sent structures anyway");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.startupScan.livenessRepublishCount(),
                              "the liveness review acted on a module where nothing changed");
}

/**
 * A sensor the person has accepted is described as itself whether it answers or not, and the review
 * asks the describer's own question - so an accepted sensor that goes quiet produces no structure
 * at all. Were the two to disagree, the loop would find a difference the structure cannot express
 * and send one every fifteen seconds for the life of the module.
 */
void test_liveness_an_accepted_row_that_goes_quiet_sends_no_structure(void)
{
    LivenessModule module;
    bringUpWithTheRowAnswering(module, SENSOR_STATUS_ADDED);

    const size_t structuresBefore = module.structuresSent();

    module.lineAnswers(false);
    for (int i = 0; i < 20; i++)
    {
        module.sweep();
    }

    TEST_ASSERT_TRUE_MESSAGE(liveErrorFlag(module.sensorRepository),
                             "the sweep did not mark the row - the scenario did not happen");

    char message[192];
    snprintf(message, sizeof(message),
             "an accepted sensor that went quiet cost the server %u structures",
             (unsigned)(module.structuresSent() - structuresBefore));

    TEST_ASSERT_EQUAL_MESSAGE(structuresBefore, module.structuresSent(), message);
    TEST_ASSERT_EQUAL_MESSAGE(0, module.startupScan.livenessRepublishCount(),
                              "the liveness review acted on a sensor the person had accepted");

    module.republishStructure();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp",
                                     typeOf(module.server.structures.back(), AIR_TEMP_CHANNEL).c_str(),
                                     "a sensor the person accepted was described as an empty port");
}

extern "C" void run_sensor_liveness_snapshot_tests(void)
{
    RUN_TEST(test_liveness_a_structure_built_on_a_single_missed_read_keeps_the_row);
    RUN_TEST(test_liveness_the_port_is_emptied_on_the_third_missed_read);
    RUN_TEST(test_liveness_a_row_that_comes_back_is_described_again_by_the_loop);
    RUN_TEST(test_liveness_a_row_that_goes_quiet_is_described_again_by_the_loop);
    RUN_TEST(test_liveness_a_flap_inside_the_debounce_sends_no_structure);
    RUN_TEST(test_liveness_a_recovery_flap_settles_once_and_says_nothing_more);
    RUN_TEST(test_liveness_a_flap_that_crosses_both_thresholds_is_bounded);
    RUN_TEST(test_liveness_a_change_the_uplink_refused_is_offered_again);
    RUN_TEST(test_liveness_a_no_scan_boot_still_reports_a_settled_change);
    RUN_TEST(test_liveness_a_row_coming_back_is_not_held_up_by_the_first_row);
    RUN_TEST(test_liveness_a_row_coming_back_is_not_held_up_by_the_second_row);
    RUN_TEST(test_liveness_a_settled_module_sends_nothing);
    RUN_TEST(test_liveness_an_accepted_row_that_goes_quiet_sends_no_structure);
}
