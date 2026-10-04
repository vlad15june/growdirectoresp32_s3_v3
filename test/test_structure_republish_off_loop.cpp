// Republishing the module structure, and the control loop that must not be doing it.
//
// When the sensor type catalog moves the number of a computed channel, the backend is still
// expecting readings under the old uid and drops everything that arrives under the new one until
// it is told. The module tells it by sending its whole structure again - a POST the transport
// allows 30 s, made with ApiPriority::BOTH, and made from the control
// loop it would run from inside the very iteration that applied the catalog. That would be the last outgoing HTTP
// call the loop could still make, and the largest.
//
// This file holds a module inside such a catalog change against a server that takes two seconds to
// answer, measures the worst control-loop step meanwhile, and records which task made the POST -
// the number of milliseconds alone would not say whose they were. The rest of the republish has to
// survive the move: the structure that reaches the server carries the shifted channel, and a
// structure the server refuses is still reported as a failed republish, in the same words.
//
// What is under test is production code: the real LoopServiceConfigureDone drives the real
// SyncSensorTypesUseCase over the real Outbox with its network task, the real SensorTypeRepository
// and SpiffsSensorTypeDao, the real SensorTypeCatalogWriter with its task, and the real
// RepublishModuleStructureUseCase over the real SensorStructureUseCase, VirtualSensorChannelsUseCase
// and VpdChannelUseCase. Replaced are the outside world - the server - and time.
//
// ### Reproducing the red
//
// Remove the `republishStructure.setOutbox(&outbox);` line from ModuleUnderTest's constructor. The
// use case then posts the structure itself, on whichever task asked it to - the control loop - and
// the worst step becomes the duration of the POST.

#include <unity.h>

#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// The claim under test: with a structure being republished, the worst control-loop step is one
// loop period - the step depends on the server not at all.
const uint32_t MAX_ACCEPTED_STEP_MS = CONTROL_PERIOD_MS;

// What one POST costs on the simulated server. Two seconds is modest against what this transport
// allows a single call (30 s of read timeout plus a 5 s connect), and it is two hundred loop
// periods - enough that a POST made on the loop cannot hide inside the measurement noise.
const uint32_t SERVER_CALL_MS = 2000;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const OUTBOX_TASK_NAME = "outbox-net";
const char* const MODULE_UID = "republish-module";
const char* const SENSOR_UID = "sensor-1";
const char* const TYPE_UID = "type-uid-air";

// Where the sensor's computed VPD channel sits before and after the catalog change, as the
// "<uid>:<type>" pair the structure carries. VpdChannelUseCase numbers the computed channel
// max(register channel) + 1, so a catalog that adds a third register channel moves it from 03 to
// 04 - which is exactly the shift that makes the backend drop the readings. Number 03 does not
// disappear: it becomes the new register channel, which is why the type is part of the pair.
const char* const VPD_UID_BEFORE = "sensor-1.03";
const char* const VPD_UID_AFTER = "sensor-1.04";
const char* const VPD_CHANNEL_BEFORE = "sensor-1.03:vpd";
const char* const VPD_CHANNEL_AFTER = "sensor-1.04:vpd";

// --- the outside world ----------------------------------------------------------------------

/**
 * @brief The backend: answers the structure POST after two seconds of virtual time.
 *
 * What it substitutes is the socket. Which structure is sent, with which priority and after how
 * many attempts is decided above this line, by production code that is not replaced.
 *
 * Two things are recorded. How long each call takes, so that a POST on the control loop is visible
 * as a stalled loop; and which task made it, because that is the actual claim - a short step could
 * also mean the structure was never sent at all. The catalog fetch is answered for free: then the
 * measured milliseconds belong to the republish and to nothing else.
 */
class SimServer : public MockHybridAPIService {
public:
    SimServer(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock) {}

    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        getSensorTypesListCallCount++;
        if (httpFailed != nullptr)
        {
            *httpFailed = false;
        }
        return sensorTypesListReturnValue;
    }

    bool sendToServerModuleStructure(const ModuleStructure& structure,
                                     ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        _structurePosts++;
        _structurePostsByTask[_scheduler.currentTaskName()]++;
        _lastPriority = priority;
        _scheduler.delayMs(SERVER_CALL_MS);

        if (!acceptStructures)
        {
            return false;
        }

        _acceptedAtMs = _clock.nowMs();
        _acceptedChannels.clear();
        for (size_t node = 0; node < structure.nodes.size(); node++)
        {
            for (size_t sensor = 0; sensor < structure.nodes[node].sensors.size(); sensor++)
            {
                const Sensor& record = structure.nodes[node].sensors[sensor];
                _acceptedChannels.push_back(record.uid + ":" + record.type);
            }
        }
        return true;
    }

    /** Whether the server takes the structure at all; false is how a refusing backend answers. */
    bool acceptStructures = true;

    uint64_t structurePosts() const { return _structurePosts; }

    uint64_t structurePostsBy(const char* taskName) const
    {
        std::map<std::string, uint64_t>::const_iterator it = _structurePostsByTask.find(taskName);
        return (it == _structurePostsByTask.end()) ? 0 : it->second;
    }

    ApiPriority lastPriority() const { return _lastPriority; }

    /** Virtual time at which the server last took a structure, 0 if it never did. */
    uint64_t acceptedAtMs() const { return _acceptedAtMs; }

    /** @param channel "<uid>:<type>", the pair the backend routes a reading by. */
    bool acceptedChannel(const char* channel) const
    {
        for (size_t i = 0; i < _acceptedChannels.size(); i++)
        {
            if (_acceptedChannels[i] == channel)
            {
                return true;
            }
        }
        return false;
    }

    size_t acceptedChannelCount() const { return _acceptedChannels.size(); }

private:
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
    uint64_t _structurePosts = 0;
    uint64_t _acceptedAtMs = 0;
    ApiPriority _lastPriority = ApiPriority::UNDEFINED;
    std::map<std::string, uint64_t> _structurePostsByTask;
    std::vector<std::string> _acceptedChannels;
};

/** @brief A sensor repository that keeps what is written to it, the way the real one does. */
class StoringSensorRepository : public MockSensorRepository {
public:
    bool save(const SensorEntity& sensor) override
    {
        saveCount++;
        for (size_t i = 0; i < sensors.size(); i++)
        {
            if (sensors[i].uid == sensor.uid)
            {
                sensors[i] = sensor;
                return true;
            }
        }
        sensors.push_back(sensor);
        return true;
    }

    uint32_t saveCount = 0;
};

class CadenceRecorder {
public:
    void tick(uint64_t nowMs)
    {
        if (_count > 0)
        {
            const uint64_t gap = nowMs - _lastMs;
            if (gap > _worstGapMs)
            {
                _worstGapMs = gap;
                _worstGapStartedAtMs = _lastMs;
            }
        }
        _lastMs = nowMs;
        _count++;
    }

    void closeAt(uint64_t nowMs)
    {
        if (_count > 0 && (nowMs - _lastMs) > _worstGapMs)
        {
            _worstGapMs = nowMs - _lastMs;
            _worstGapStartedAtMs = _lastMs;
        }
    }

    uint64_t count() const { return _count; }
    uint64_t worstGapMs() const { return _worstGapMs; }
    uint64_t worstGapStartedAtMs() const { return _worstGapStartedAtMs; }

private:
    uint64_t _count = 0;
    uint64_t _lastMs = 0;
    uint64_t _worstGapMs = 0;
    uint64_t _worstGapStartedAtMs = 0;
};

ModbusRegisterConfig registerConfig(uint8_t channel, const std::string& type)
{
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = channel;
    config.numRegisters = 2;
    config.multiplier = 0.1f;
    config.channel = channel;
    config.channelName = type;
    config.sensorType = type;
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 1000.0f;
    return config;
}

/**
 * @brief The sensor type the sensor is bound to, at a given revision.
 *
 * `withCo2` is the change: a third register channel pushes the computed VPD channel from 03 to 04.
 */
SensorType airSensorType(bool withCo2, const std::string& updatedAt)
{
    SensorType sensorType;
    sensorType.uid = TYPE_UID;
    sensorType.name = "air_sensor";
    sensorType.displayName = "Air sensor";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.baudRate = 9600;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = updatedAt;

    sensorType.modbusRegisterConfig.push_back(registerConfig(1, VpdChannelUseCase::AIR_TEMP_TYPE));
    sensorType.modbusRegisterConfig.push_back(registerConfig(2, VpdChannelUseCase::HUMIDITY_TYPE));
    if (withCo2)
    {
        sensorType.modbusRegisterConfig.push_back(registerConfig(3, "co2"));
    }

    for (size_t i = 0; i < sensorType.modbusRegisterConfig.size(); i++)
    {
        SubSensorType subType;
        subType.name = sensorType.modbusRegisterConfig[i].sensorType;
        subType.displayName = subType.name;
        subType.minValue = 0.0f;
        subType.maxValue = 1000.0f;
        subType.unit = "unit";
        subType.createdAt = sensorType.createdAt;
        subType.updatedAt = updatedAt;
        sensorType.sensorTypes.push_back(subType);
    }

    return sensorType;
}

/** @brief The sensor bound to that type, as the module has it before the catalog moves. */
SensorEntity airSensor()
{
    SensorEntity sensor;
    sensor.uid = SENSOR_UID;
    sensor.sensorTypeUid = TYPE_UID;
    sensor.model = "air_sensor";
    // The spelling isModbusSensor() tests for; a sensor of another protocol is not updated from
    // the catalog and would prove nothing here.
    sensor.protocol = "ModBus";
    sensor.sensorType = "air_sensor";
    sensor.pin = 1;
    sensor.modbusDeviceAddress = 1;
    sensor.isActive = true;
    sensor.modbusRegisterConfigs = airSensorType(false, "2026-01-01T00:00:00+00:00").modbusRegisterConfig;
    return sensor;
}

/**
 * @brief The module, wired the way globals.h wires it.
 */
struct ModuleUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    StoringSensorRepository sensorRepository;

    MockSpiffsAdapter flash;
    ArduinoJsonConverter jsonConverter;
    SpiffsSensorTypeDao sensorTypeDao;
    SensorTypeRepository sensorTypeRepository;
    SensorTypeCatalogWriter catalogWriter;

    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModuleStructure moduleStructure;

    SimServer server;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    RepublishModuleStructureUseCase republishStructure;
    SyncSensorTypesUseCase syncSensorTypes;

    LoopServiceConfigureDone loopService;

    ModuleUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          sensorTypeDao(&flash, &jsonConverter, &debugSystem, &logger),
          sensorTypeRepository(&sensorTypeDao, &debugSystem, &logger),
          catalogWriter(&sensorTypeDao, &scheduler, &logger, &debugSystem),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          server(scheduler, clock),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          republishStructure(&moduleStructure, &sensorRepository, &server, &sensorStructure,
                             &logger, &debugSystem, &arduino),
          syncSensorTypes(&server, &sensorTypeRepository, &arduino, &timeAdapter, &jsonConverter,
                          &logger, &debugSystem, &sensorRepository)
    {
        debugSystem.featureEnabled = false;
        timeAdapter.setEpochTime(1755690000);
        moduleConfig.setModuleUid(MODULE_UID);

        // The module already knows the sensor, its type and the structure it published for them.
        sensorRepository.sensors.push_back(airSensor());
        sensorTypeRepository.saveAll({airSensorType(false, "2026-01-01T00:00:00+00:00")});

        moduleStructure.growdirectorUid = MODULE_UID;
        Node node;
        node.type = "sensor_director";
        node.subtype = "sensor";
        moduleStructure.nodes.push_back(node);
        sensorStructure.addSensor(moduleStructure.nodes[0], sensorRepository.sensors[0]);

        // And the catalog the server now has: the same type with one register channel more, which
        // is what moves the computed channel.
        server.sensorTypesListReturnValue.push_back(
            airSensorType(true, "2026-02-11T09:01:35.44352+00:00"));

        syncSensorTypes.setOutbox(&outbox);
        syncSensorTypes.setCatalogWriter(&catalogWriter);
        syncSensorTypes.setVirtualSensorChannelsUseCase(&virtualChannels);
        syncSensorTypes.setRepublishModuleStructureUseCase(&republishStructure);

        // Remove this line to reproduce the red: the use case then posts the structure itself, on
        // whichever task asked it to.
        republishStructure.setOutbox(&outbox);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setSyncSensorTypesUseCase(&syncSensorTypes);
        loopService.setSensorTypeCatalogWriter(&catalogWriter);
        loopService.setRepublishModuleStructureUseCase(&republishStructure);
    }

    /** @brief One task, mirroring main.cpp: the loop service, then one period of sleep. */
    void startArduinoLoopTask(CadenceRecorder& cadence)
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this, &cadence]() {
            while (true)
            {
                cadence.tick(clock.nowMs());
                loopService.loop();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }

    /** The computed channels the module would publish for its sensor right now. */
    std::string virtualChannelsOfSensor()
    {
        std::vector<SensorEntity> stored = sensorRepository.getAll();
        if (stored.empty())
        {
            return "none";
        }
        std::vector<IVirtualSensorChannelsUseCase::Channel> channels =
            virtualChannels.channels(stored[0]);
        return channels.empty() ? "none" : channels[0].uid;
    }
};

/** @brief The soundness gate every measurement scenario passes before its verdict counts. */
void assertRunIsSound(ModuleUnderTest& module, CadenceRecorder& cadence)
{
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");
    TEST_ASSERT_TRUE_MESSAGE(module.server.getSensorTypesListCallCount >= 1,
                             "the catalog was never fetched - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.server.structurePosts() >= 1,
                             "the structure was never republished - nothing slow was exercised");
}

void assertLoopStayedBounded(CadenceRecorder& cadence, const char* whatRan)
{
    char message[288];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms) while %s",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS,
             whatRan);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The catalog moves the sensor's computed channel, so the structure goes out again - against a
//    server that takes two seconds to answer. The loop keeps its period, the POST happens
//    somewhere else, and what the server receives is the structure with the moved channel.
// --------------------------------------------------------------------------------------------

static void test_the_structure_is_republished_without_the_control_loop_waiting_for_the_server(void)
{
    ModuleUnderTest module;

    // The premise: before the catalog change the module publishes the computed channel at 03.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(VPD_UID_BEFORE, module.virtualChannelsOfSensor().c_str(),
                                     "the fixture does not start from the channel layout it claims");

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.syncSensorTypes.forceSync();
    module.scheduler.runForMs(60000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    assertLoopStayedBounded(cadence, "the module structure was republished to a server taking "
                                     "2000 ms to answer");

    // Where the POST ran. These are consequences of running off the loop, so they come after the verdict:
    // before it they would be false by definition and hide the measured number.
    char message[224];
    snprintf(message, sizeof(message),
             "%lu of the %lu structure POSTs ran on the control loop",
             (unsigned long)module.server.structurePostsBy(LOOP_TASK_NAME),
             (unsigned long)module.server.structurePosts());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.server.structurePostsBy(LOOP_TASK_NAME), message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(module.server.structurePosts(),
                                     module.server.structurePostsBy(OUTBOX_TASK_NAME),
                                     "the structure POST did not run on the outbox network task");

    // The channel really moved, and the structure the server took is the one that says so. This is
    // what makes a short loop step mean "somebody else did the work" rather than "nobody did".
    TEST_ASSERT_EQUAL_STRING_MESSAGE(VPD_UID_AFTER, module.virtualChannelsOfSensor().c_str(),
                                     "the catalog change did not move the computed channel");
    TEST_ASSERT_TRUE_MESSAGE(module.server.acceptedChannel(VPD_CHANNEL_AFTER),
                             "the republished structure did not carry the moved channel");
    TEST_ASSERT_FALSE_MESSAGE(module.server.acceptedChannel(VPD_CHANNEL_BEFORE),
                              "the republished structure still put the computed channel at its old "
                              "number");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(4, (unsigned)module.server.acceptedChannelCount(),
                                   "the republished structure did not carry the three register "
                                   "channels of the new catalog plus the computed one");

    // Same contract with the backend as the blocking call had: one structure, sent over both APIs.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, module.server.structurePosts(),
                                     "the structure was not republished exactly once");
    TEST_ASSERT_TRUE_MESSAGE(module.server.lastPriority() == ApiPriority::BOTH,
                             "the republished structure was not sent with ApiPriority::BOTH");

    // And the outcome came back to the use case, on the control loop.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.republishStructure.submittedCount(),
                                     "the structure was not handed to the outbox exactly once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.republishStructure.deliveredCount(),
                                     "the module never learned that the server took the structure");
    TEST_ASSERT_FALSE_MESSAGE(module.republishStructure.isSendPending(),
                              "the use case is still waiting for a structure it already got back");
}

// --------------------------------------------------------------------------------------------
// 2. The same catalog change against a server that refuses the structure. What the module reports
//    must not depend on who made the attempt: a failed republish, in the same words, and a loop
//    that does not pay for the failure either.
// --------------------------------------------------------------------------------------------

static void test_a_refused_structure_is_reported_as_a_failed_republish(void)
{
    ModuleUnderTest module;
    // This scenario reads the log, which the use case only writes with its debug feature on.
    module.debugSystem.featureEnabled = true;
    module.server.acceptStructures = false;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.syncSensorTypes.forceSync();
    // Long enough for the outbox to spend its whole retry budget: four 2 s attempts plus 500 ms,
    // 2 s and 8 s of backoff.
    module.scheduler.runForMs(120000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    // What the module reports, in the wording a field log is grepped with. Everything down to the
    // verdict holds in both shapes - that is what makes this scenario a control on the report
    // rather than a second measurement.
    TEST_ASSERT_TRUE_MESSAGE(module.logger.hasMessageContaining("STRUCTURE_REPUBLISH"),
                             "the republish left no trace in the log at all");
    TEST_ASSERT_TRUE_MESSAGE(
        module.logger.hasMessageContaining("republish_fail reason=virtual_channel_shift"),
        "a structure the server refused was not reported as a failed republish");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.republishStructure.deliveredCount(),
                                     "a refused structure was reported as delivered");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.server.acceptedChannelCount(),
                                     "the server was recorded as having taken a structure it refused");

    // A refusal must not be paid for on the control loop either.
    assertLoopStayedBounded(cadence, "the module structure was refused by the server");

    // Last, and the only assertions here that are about who made the attempts: the retries are the
    // outbox's, made on its task, and the outcome reached the use case on the loop.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.republishStructure.undeliveredCount(),
                                     "the module never learned that the structure did not arrive");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, module.server.structurePosts(),
                                     "the refused structure was not retried the outbox's four times");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.server.structurePostsBy(LOOP_TASK_NAME),
                                     "a refused structure was retried on the control loop");
}

extern "C" void run_structure_republish_off_loop_tests(void)
{
    RUN_TEST(test_the_structure_is_republished_without_the_control_loop_waiting_for_the_server);
    RUN_TEST(test_a_refused_structure_is_reported_as_a_failed_republish);
}
