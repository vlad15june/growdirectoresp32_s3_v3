// Scenario S12 - a complex module command executes, and the control loop keeps running.
//
// The *fetching* of command lists moved off the control loop, and the *execution* stayed
// there, on the stated grounds that a GPIO write is microseconds. For the relay commands that was
// true. For the complex commands it was not:
//
//   - ScanSensors walks the whole sensor-type catalog across a pin, three attempts per address,
//     and with an empty local catalog it first walks to the server for one;
//   - SetSensorModel scans one type across the address range;
//   - PingPin scans every scannable type across the address range;
//   - UpdateFirmware downloads the image with no time limit of its own;
//   - DeleteModule asks the server whether the deletion is real.
//
// Every one of those was control-loop time. This file holds each of the five inside its slow part
// and measures the worst control-loop iteration meanwhile, then checks that the outcome still
// reaches the server in the same reports as before.
//
// What is under test is production code: the real LoopServiceConfigureDone drives the real
// CommandService, the real Outbox with its network task, the real NetworkCommandRunner with its
// task, the real SensorAcquisitionService and ModuleCommandExecutionUseCase over the real
// SensorReader/ModbusSensorReader on a simulated RS-485 line. Replaced are the outside world -
// the server, the line, the OTA transport - and time.
//
// ### Reproducing the red
//
// The red is inline execution: run this scenario (minus the NetworkCommandRunner wiring, which
// has no place in that shape) against a build that executes the command on the control loop, and
// the worst step becomes the duration of the command itself - seconds to minutes.

#include <unity.h>

#include "Application/Services/CommandService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/CheckModuleDeletionUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"

#include "Mock/Application/MockClearModuleDataService.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimFirmwareUpdater.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using hostsim::HostBusLock;
using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimFirmwareUpdater;
using hostsim::SimRs485Bus;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// The claim under test: with a complex command executing, the worst control-loop step is one
// loop period - the step depends on neither the bus nor the network at all. Tighter than the
// 50 ms bound S1-S8 use, on purpose: this is the number the short watchdog threshold needs.
const uint32_t MAX_ACCEPTED_STEP_MS = CONTROL_PERIOD_MS;

// One attempt against a server that has stopped answering; RestApiService's timeout.
const uint32_t ATTEMPT_TIMEOUT_MS = 30000;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "s12-module";

// --- the outside world ------------------------------------------------------------------------

/**
 * @brief The far end of every network call this scenario makes.
 *
 * Everything below the API is production code; this answers what the test tells it to answer,
 * after the time the test tells it to spend.
 */
class SimServer : public MockHybridAPIService {
public:
    explicit SimServer(HostTaskScheduler& scheduler) : _scheduler(scheduler) {}

    /** How long every call takes. 0 answers instantly. */
    uint32_t callDelayMs = 0;
    /** false: every call fails after callDelayMs, as an unreachable server does. */
    bool reachable = true;

    /** Handed out by the next getModuleCommand(), then cleared. */
    std::vector<ModuleCommandDto> pendingModuleCommands;
    uint32_t getModuleCommandCalls = 0;

    /** The sensor-type catalog the account has. */
    std::vector<SensorType> catalog;

    /** What /is_deleted answers. */
    ModuleDeletionQueryResult deletionAnswer;
    uint32_t deletionQueries = 0;

    uint64_t timeInsideCallsMs = 0;

    std::vector<std::vector<ModuleCommandUpdateDto>> moduleCommandReports;
    std::vector<CommandResultDto> commandReports;
    /** Every module structure the server took, as it was handed over. */
    std::vector<ModuleStructure> structures;

    std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) override
    {
        getModuleCommandCalls++;
        spendTime();
        if (httpFailed != nullptr)
        {
            *httpFailed = !reachable;
        }
        if (!reachable)
        {
            return std::vector<ModuleCommandDto>();
        }
        std::vector<ModuleCommandDto> answer;
        answer.swap(pendingModuleCommands);
        return answer;
    }

    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        getSensorTypesListCallCount++;
        spendTime();
        if (httpFailed != nullptr)
        {
            *httpFailed = !reachable;
        }
        return reachable ? catalog : std::vector<SensorType>();
    }

    ModuleDeletionQueryResult queryModuleDeletion() override
    {
        deletionQueries++;
        spendTime();
        if (!reachable)
        {
            ModuleDeletionQueryResult failed;
            failed.queryFailed = true;
            return failed;
        }
        return deletionAnswer;
    }

    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        spendTime();
        if (!reachable)
        {
            return false;
        }
        moduleCommandReports.push_back(updates);
        return true;
    }

    bool sendCommandResult(const CommandResultDto& result,
                           ApiPriority = ApiPriority::UNDEFINED) override
    {
        spendTime();
        if (!reachable)
        {
            return false;
        }
        commandReports.push_back(result);
        return true;
    }

    bool sendToServerModuleStructure(const ModuleStructure& structure,
                                     ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        spendTime();
        if (!reachable)
        {
            return false;
        }
        structures.push_back(structure);
        return MockHybridAPIService::sendToServerModuleStructure(structure, priority);
    }

private:
    void spendTime()
    {
        if (callDelayMs == 0)
        {
            return;
        }
        _scheduler.delayMs(callDelayMs);
        timeInsideCallsMs += callDelayMs;
    }

    HostTaskScheduler& _scheduler;
};

/** @brief The inbound half of MqttManager: a queue drained on the task that calls loop(). */
class ScriptedMqttManager : public IMqttManager {
public:
    bool connect() override { return true; }
    void disconnect() override {}
    bool isConnected() override { return true; }
    bool subscribe(const char*) override { return true; }
    bool unsubscribe(const char*) override { return true; }
    bool publish(const char*, const char*) override { return true; }
    void setMessageProcessor(IMessageProcessor*) override {}
    void setupMqttTopicHandlers() override {}
    int getMqttFails() const override { return 0; }
    void resetMqttFails() override {}
    uint32_t getInboundOversizeDrops() const override { return 0; }
    const char* getClientId() const override { return MODULE_UID; }
    void setClientId(const char*) override {}

    void loop() override
    {
        while (!_inbound.empty())
        {
            const std::string payload = _inbound.front();
            _inbound.pop_front();
            if (_handler != nullptr)
            {
                _handler->handleMessage("GrowDirectorMqttTopic/s12-module",
                                        reinterpret_cast<const uint8_t*>(payload.data()),
                                        static_cast<unsigned int>(payload.size()));
            }
            dispatched++;
        }
    }

    void deliver(const std::string& payload) { _inbound.push_back(payload); }
    void setHandler(ITopicHandler* handler) { _handler = handler; }

    uint32_t dispatched = 0;

private:
    std::deque<std::string> _inbound;
    ITopicHandler* _handler = nullptr;
};

class CountingSensorService : public ISensorService {
public:
    bool init() override { return true; }
    void loop() override {}
    bool readAllSensors() override { return true; }
    bool readSensor(SensorEntity) override { return true; }
};

class CountingRuleService : public IRuleService {
public:
    bool init() override { return true; }
    void loop() override {}
    bool forceSyncRules() override { return true; }
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

/** @brief A Modbus sensor type as the platform catalog describes one, scannable on the line. */
SensorType makeModbusSensorType(const char* uid, const char* name)
{
    SensorType type;
    type.uid = uid;
    type.name = name;
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = true;
    type.modbusDeviceAddress = 1;
    type.baudRate = 9600;
    type.modbusPingAddress = 0x0000;

    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = 0x0000;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channel = 1;
    config.channelName = "value";
    config.sensorType = "temperature";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    type.modbusRegisterConfig.push_back(config);

    return type;
}

/** @brief A device on the line that a scan of the type above will detect. */
SimRs485Bus::Device makeScanResponder()
{
    SimRs485Bus::Device responder;
    responder.address = 1;
    // The sim answers a read with registerValues[0..n-1]; 250 x 0.1 = 25.0, inside the
    // can-connect range, so the scan's plausibility check passes.
    responder.registerValues.assign(1, 250);
    return responder;
}

std::string moduleCommandJson(const char* command)
{
    char buffer[384];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"%s\",\"options\":{}}",
             MODULE_UID, command);
    return std::string(buffer);
}

std::string deleteModuleJson()
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"DeleteGdCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"commandUid\":\"del-1\",\"createdAt\":\"2026-08-20T10:00:00Z\"}",
             MODULE_UID);
    return std::string(buffer);
}

ModuleCommandDto makeServerModuleCommand(const std::string& uid, const char* command,
                                         const std::string& options)
{
    ModuleCommandDto dto;
    dto.uid = uid;
    dto.command = command;
    dto.createdAt = "2026-08-20T10:00:00Z";
    dto.options = options;
    return dto;
}

/**
 * @brief The module, wired the way globals.h wires it.
 */
struct ModuleUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;
    HostBusLock busLock;

    SimRs485Bus port0;
    SimRs485Bus port1;
    SimRs485Bus port2;
    SimRs485Bus port3;
    std::vector<IModbusRS485*> ports;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockSensorRepository sensorRepository;
    MockSensorTypeRepository sensorTypeRepository;
    MockNTPTimeAdapter timeAdapter;
    MockWiFiManager wifiManager;
    MockClearModuleDataService clearService;
    ArduinoJsonConverter jsonConverter;
    ModuleStructure moduleStructure;

    ModbusSensorReader modbusSensorReader;
    RawBusAccessAdapter rawBusAccess;
    SensorReader sensorReader;
    SensorAcquisitionService acquisition;

    SimServer server;
    SimFirmwareUpdater firmwareUpdater;
    CheckModuleDeletionUseCase checkDeletion;
    // The structure builder globals.h wires, with the computed channels it carries: a command that
    // saves a sensor announces it through this and not through a second copy of the record layout.
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    // The owner of the ModBus addresses of the module, over the same repository globals.h gives
    // it: a scan asks it which addresses the other ports already hold.
    ModbusAddressAllocator modbusAddressAllocator;
    // The discovery, wired only by followScansWithTheDiscovery().
    StartupSensorScanUseCase discovery;
    ModuleCommandExecutionUseCase moduleCommandUseCase;

    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    NetworkCommandRunner networkRunner;

    CommandService commandService;
    CommandTopicHandler topicHandler;
    ScriptedMqttManager mqttManager;

    CountingSensorService sensorService;
    CountingRuleService ruleService;
    LoopServiceConfigureDone loopService;

    /**
     * @param busTaskStarted Start the acquisition task here, as the sweep does on its first tick on
     *        a module with sensors. Without it nothing on this stand starts it but the commands.
     * @param moduleType The board's type. Only a sensor module has its line begun by the boot.
     */
    explicit ModuleUnderTest(bool busTaskStarted = true, int moduleType = MODULE_TYPE_SENSOR)
        : scheduler(clock),
          arduino(clock, &scheduler),
          busLock(scheduler, clock),
          port0(clock, "sim-port-0"),
          port1(clock, "sim-port-1"),
          port2(clock, "sim-port-2"),
          port3(clock, "sim-port-3"),
          ports{&port0, &port1, &port2, &port3},
          moduleConfig(&logger, &debugSystem, &settings),
          modbusSensorReader(ports, arduino, &logger, &debugSystem),
          rawBusAccess(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          server(scheduler),
          firmwareUpdater(scheduler),
          checkDeletion(&server, &logger, &debugSystem),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          modbusAddressAllocator(&sensorRepository),
          discovery(&logger, &debugSystem, &moduleConfig, &sensorRepository, &sensorTypeRepository,
                    &sensorReader, &moduleStructure, &sensorStructure, &modbusAddressAllocator, nullptr,
                    &virtualChannels, &arduino),
          moduleCommandUseCase(&logger, &debugSystem, &timeAdapter, &firmwareUpdater, &arduino,
                               &sensorRepository, &sensorTypeRepository, &sensorReader,
                               &moduleConfig, &settings, &server, &clearService,
                               &moduleStructure, nullptr, &wifiManager, nullptr,
                               &rawBusAccess, &checkDeletion, nullptr, nullptr,
                               &modbusAddressAllocator),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          networkRunner(&scheduler, &logger, &debugSystem),
          commandService(nullptr, &moduleCommandUseCase, nullptr, &moduleStructure,
                         &logger, &debugSystem, &timeAdapter, &jsonConverter),
          topicHandler(&logger, &debugSystem, MODULE_UID, &commandService, &jsonConverter)
    {
        debugSystem.featureEnabled = false;

        timeAdapter.setEpochTime(1755690000);
        timeAdapter.setCommandTimeValid(true);

        settings.setUser("s12@example.com");
        settings.setAccountUid("s12-account");
        moduleConfig.setModuleType(moduleType);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setSettings(&settings);

        // ESP32-S3 structures carry one node; without it addSensor() has nowhere to put a sensor.
        moduleStructure.nodes.push_back(Node());

        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        rawBusAccess.setBusLock(&busLock);
        // The line as a booted module has it: the boot begins the UARTs before the first frame
        // (ModuleSetupService::setupSensorBus()), and a line nobody began carries nothing. The boot
        // of a module of another type begins none.
        if (moduleType == MODULE_TYPE_SENSOR)
        {
            sensorReader.init();
        }

        moduleCommandUseCase.setSensorStructureUseCase(&sensorStructure);
        acquisition.setRawCommandExecutor(&moduleCommandUseCase);
        if (busTaskStarted)
        {
            acquisition.ensureRunning();
        }
        networkRunner.setExecutor(&moduleCommandUseCase);
        commandService.setOutbox(&outbox);
        commandService.setBusCommandExecutor(&acquisition);
        commandService.setModuleConfig(&moduleConfig);
        commandService.setNetworkCommandRunner(&networkRunner);
        mqttManager.setHandler(&topicHandler);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setCommandService(&commandService);
        loopService.setSensorService(&sensorService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
    }

    /**
     * @brief Give the command service and the acquisition task the discovery, as globals.h does.
     *
     * A ScanSensors the line has taken asks the discovery for a pass over its pin, and the
     * discovery's steps start the acquisition task on their own. Opt-in, because that pass adds its
     * own traffic to the line, and the scenarios that count what one command put there would count
     * the pass too.
     */
    void followScansWithTheDiscovery()
    {
        discovery.setRawBusAccess(&rawBusAccess);
        discovery.setPortLines(modbusSensorReader.portLines());
        acquisition.setStartupSensorScan(&discovery);
        commandService.setStartupSensorScan(&discovery);
    }

    /**
     * @brief One task, mirroring main.cpp: loop() runs the loop service, feeds the watchdog,
     *        sleeps one period. The feed is the model of the esp_task_wdt_reset() in main.cpp's
     *        loop() - the loop vouches for itself once per completed iteration, and for nothing
     *        else.
     */
    void startArduinoLoopTask(CadenceRecorder& cadence)
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this, &cadence]() {
            while (true)
            {
                cadence.tick(clock.nowMs());
                loopService.loop();
                scheduler.watchdogFeed();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }
};

/** @brief The soundness gate every measurement scenario passes before its verdict counts. */
void assertRunIsSound(ModuleUnderTest& module, CadenceRecorder& cadence)
{
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_FALSE_MESSAGE(module.busLock.deadlockDetected(),
                              "a task waited for the bus lock in a way that could never be served");
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");
    TEST_ASSERT_TRUE_MESSAGE(module.mqttManager.dispatched >= 1,
                             "the command signal was never dispatched - the scenario did not happen");
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
// 1. ScanSensors: an empty local catalog, a server that hangs 30 s per call, a silent pin.
//    The catalog fetch runs on the command network task, the scan on the acquisition task.
// --------------------------------------------------------------------------------------------

static void test_s12_the_loop_stays_bounded_while_scan_sensors_fetches_and_scans(void)
{
    ModuleUnderTest module;
    module.server.callDelayMs = ATTEMPT_TIMEOUT_MS;
    module.server.catalog.push_back(makeModbusSensorType("type-1", "S12-TEMP"));
    module.sensorTypeRepository.setShouldReturnEmpty(false); // empty because nothing was added
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("scan-1", "ScanSensors", "{\"pin\":4}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("ScanSensors"));
    module.scheduler.runForMs(300000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.server.getModuleCommandCalls >= 1,
                             "the command list was never fetched");
    TEST_ASSERT_TRUE_MESSAGE(module.server.getSensorTypesListCallCount >= 1,
                             "the catalog was never fetched - the slow network part did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.sentFrames.size() >= 1,
                             "no scan frame went out on the pin - nothing slow was exercised");

    assertLoopStayedBounded(cadence,
                            "a ScanSensors command fetched its catalog from a server hanging "
                            "30000 ms per call and scanned a silent pin");

    // Where the slow parts ran, and that the loop never touched the line for them. These are
    // consequences of running off the loop, so they come after the verdict: before it, they would be false by
    // definition and would hide the measured number the red exists to show.
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1,
                             "the scan never reached the acquisition task");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.takesBy(LOOP_TASK_NAME),
                                     "the control loop took the RS-485 lock for a scan");

    // The outcome the backend sees: InProgress, then the batch - Done, nothing found.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("InProgress", module.server.moduleCommandReports[0][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("scan-1", module.server.moduleCommandReports[0][0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("Done", module.server.moduleCommandReports[1][0].status.c_str());
}

// --------------------------------------------------------------------------------------------
// 2. SetSensorModel against a pin where nothing answers: the scan of the address range runs on
//    the acquisition task.
// --------------------------------------------------------------------------------------------

static void test_s12_the_loop_stays_bounded_while_set_sensor_model_scans_a_silent_pin(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("model-1", "SetSensorModel", "{\"pin\":4,\"uid\":\"type-1\"}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("SetSensorModel"));
    module.scheduler.runForMs(120000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.port0.sentFrames.size() >= 1,
                             "no scan frame went out on the pin");

    assertLoopStayedBounded(cadence,
                            "a SetSensorModel command scanned a silent pin across the address range");

    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1,
                             "the verification scan never reached the acquisition task");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.takesBy(LOOP_TASK_NAME),
                                     "the control loop took the RS-485 lock for the verification scan");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("InProgress", module.server.moduleCommandReports[0][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Error", module.server.moduleCommandReports[1][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Sensor not detected on specified pin",
                             module.server.moduleCommandReports[1][0].errorMessage.c_str());
}

// --------------------------------------------------------------------------------------------
// 3. PingPin against a pin where nothing answers: every scannable type, every address, on the
//    acquisition task.
// --------------------------------------------------------------------------------------------

static void test_s12_the_loop_stays_bounded_while_ping_pin_scans_a_silent_pin(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("ping-1", "PingPin", "{\"pin\":4}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("PingPin"));
    module.scheduler.runForMs(120000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.port0.sentFrames.size() >= 1,
                             "no scan frame went out on the pin");

    assertLoopStayedBounded(cadence, "a PingPin command scanned a silent pin");

    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1,
                             "the ping scan never reached the acquisition task");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.takesBy(LOOP_TASK_NAME),
                                     "the control loop took the RS-485 lock for the ping scan");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("Error", module.server.moduleCommandReports[1][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("No sensor detected on pin",
                             module.server.moduleCommandReports[1][0].errorMessage.c_str());
}

// --------------------------------------------------------------------------------------------
// 4. UpdateFirmware: a two-minute download. The loop does not wait for the connection or the
//    stream; the consequences - flags, requested restart - are still applied on the loop.
// --------------------------------------------------------------------------------------------

static void test_s12_the_loop_stays_bounded_while_a_firmware_download_runs(void)
{
    ModuleUnderTest module;
    module.firmwareUpdater.downloadMs = 120000;
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
        "fw-1", "UpdateFirmware",
        "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\",\"version\":\"9.9.9\"}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(180000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.firmwareUpdater.updateCalled,
                             "the download never started - nothing slow was exercised");

    assertLoopStayedBounded(cadence, "a 120000 ms firmware download was in progress");

    // The consequences of a finished download, exactly as before.
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.settings.getNoScanSensors(),
                                    "the no-scan flag was not set after the update");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the restart was not requested after the update");

    // The persisted status is not written by the updater. It is an EEPROM write, and the
    // updater runs on cmd-net, so the value travels back and is written in the finish step
    // on the control loop - the settings keep a single writer.
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, module.settings.getUpdateStatus(),
                                    "the DONE update status never reached the settings");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("InProgress", module.server.moduleCommandReports[0][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Done", module.server.moduleCommandReports[1][0].status.c_str());
}

// --------------------------------------------------------------------------------------------
// 5. DeleteModule: the server takes 30 s to confirm the deletion. The verification runs on the
//    command network task; the factory reset and the mode change stay on the loop.
// --------------------------------------------------------------------------------------------

static void test_s12_the_loop_stays_bounded_while_delete_module_asks_a_slow_server(void)
{
    ModuleUnderTest module;
    module.server.callDelayMs = ATTEMPT_TIMEOUT_MS;
    module.server.deletionAnswer.received200 = true;
    module.server.deletionAnswer.isDeletedFlag = 1;
    module.server.deletionAnswer.finalHttpCode = 200;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(deleteModuleJson());
    module.scheduler.runForMs(180000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.server.deletionQueries >= 1,
                             "the deletion was never verified with the server");

    assertLoopStayedBounded(cadence,
                            "a DeleteModule command verified the deletion with a server hanging "
                            "30000 ms per call");

    // The verdict was applied on the loop: wiped, and the restart requested.
    TEST_ASSERT_TRUE_MESSAGE(module.clearService.clearAllDataCalled,
                             "the factory reset never ran although the server confirmed the deletion");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the restart was not requested after the wipe");

    // The two acknowledgements, in order: accepted when the signal arrived, done when the
    // verdict was in.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.commandReports.size(),
                                     "the server did not see both acknowledgements");
    TEST_ASSERT_EQUAL_STRING("accepted", module.server.commandReports[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("DeleteModule", module.server.commandReports[0].command.c_str());
    TEST_ASSERT_EQUAL_STRING("done", module.server.commandReports[1].status.c_str());
}

// --------------------------------------------------------------------------------------------
// Delivery: a scan that finds a sensor still saves it, updates the structure, and reports it -
// the repository and structure writes happen on the loop, off the acquisition task.
// --------------------------------------------------------------------------------------------

static void test_s12_a_detected_sensor_is_saved_and_reported_with_the_structure(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    module.port0.addDevice(makeScanResponder());
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("scan-2", "ScanSensors", "{\"pin\":4}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("ScanSensors"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());

    // The scan ran on the acquisition task and found the device.
    TEST_ASSERT_TRUE_MESSAGE(module.busLock.takesBy(SensorAcquisitionService::TASK_NAME) > 0,
                             "the scan did not go through the bus lock from the acquisition task");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.takesBy(LOOP_TASK_NAME),
                                     "the control loop took the RS-485 lock for the scan");

    // The outcome: Done, with the detected sensor in the result, saved and in the structure.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    const ModuleCommandUpdateDto& outcome = module.server.moduleCommandReports[1][0];
    TEST_ASSERT_EQUAL_STRING("Done", outcome.status.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)outcome.result.sensors.size(),
                                     "the outcome did not carry the detected sensor");
    TEST_ASSERT_EQUAL_STRING("s12-module.00", outcome.result.sensors[0].uid.c_str());

    // Channel 1 of the detected sensor, stored the way the structure stores channels.
    TEST_ASSERT_NOT_NULL_MESSAGE(module.moduleStructure.findSensorByUid("s12-module.00.01"),
                                 "the detected sensor did not reach the module structure");

    // The structure follows the outcome to the server, as it always did after a sensor change.
    TEST_ASSERT_TRUE_MESSAGE(module.server.sendToServerModuleStructureCallCount >= 1,
                             "the updated structure never reached the server");
}

// --------------------------------------------------------------------------------------------
// Delivery: a scan that finds another model on a port that already has a sensor replaces that
// sensor in the structure the server is sent. The port's records in the JSON the module sends -
// under the server's pin, 4 for port 0 - are the new model's channels, and the channel of the
// model it replaced is not among them.
// --------------------------------------------------------------------------------------------

static void test_s12_a_scan_that_replaces_a_sensor_sends_a_structure_without_its_records(void)
{
    ModuleUnderTest module;

    // Port 0 holds the generic "ModBus" model, register 0 as it is on channel 0, at the address the
    // device answers on. The structure describes it the way the module describes a stored sensor.
    SensorEntity stored;
    stored.uid = std::string(MODULE_UID) + ".00";
    stored.pin = 0;
    stored.protocol = "ModBus";
    stored.model = "ModBus";
    stored.sensorType = "ModBus";
    stored.sensorTypeUid = "type-generic";
    stored.modbusDeviceAddress = 1;
    stored.baudRate = 9600;
    stored.isActive = true;
    stored.status = SENSOR_STATUS_ADDED;
    ModbusRegisterConfig channelZero;
    channelZero.functionCode = 0x03;
    channelZero.startRegister = 0x0000;
    channelZero.numRegisters = 1;
    channelZero.multiplier = 1.0f;
    channelZero.channel = 0;
    channelZero.channelName = "Unknown";
    channelZero.sensorType = "UNKNOWN";
    channelZero.canConnectMinValue = 0.0f;
    channelZero.canConnectMaxValue = 100000.0f;
    stored.modbusRegisterConfigs.push_back(channelZero);
    module.sensorRepository.sensors.push_back(stored);
    module.sensorStructure.addSensor(module.moduleStructure.nodes[0], stored);

    // The catalog holds the model the device on the port is.
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    module.port0.addDevice(makeScanResponder());
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("scan-3", "ScanSensors", "{\"pin\":4}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("ScanSensors"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());

    // The premise: Done, and the row of port 0 is the model the scan found.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("Done", module.server.moduleCommandReports[1][0].status.c_str());
    Utils::Optional<SensorEntity> row = module.sensorRepository.get("s12-module.00");
    TEST_ASSERT_TRUE(row.has_value());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("S12-TEMP", row.value().model.c_str(),
                                     "the premise: the scan replaced the generic row of port 0");

    // What reached the server last, in the JSON the module sends it as.
    TEST_ASSERT_TRUE_MESSAGE(!module.server.structures.empty(),
                             "the updated structure never reached the server");
    const std::string json = module.jsonConverter.moduleStructureToJson(module.server.structures.back());
    DynamicJsonDocument document(8192);
    TEST_ASSERT_TRUE_MESSAGE(deserializeJson(document, json) == DeserializationError::Ok,
                             "the structure the server got is not JSON");
    std::string portZero;
    for (JsonObject record : document["nodes"][0]["sensors"].as<JsonArray>())
    {
        if ((record["pin"] | -1) == 4)
        {
            portZero += std::string(portZero.empty() ? "" : " ") +
                        (record["uid"] | "") + ":" + (record["type"] | "");
        }
    }
    printf("  s12: after a scan that replaced the sensor of port 0 the server got, under pin 4: %s\n",
           portZero.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("s12-module.00.01:temperature", portZero.c_str(),
                                     "the structure the server got after the scan describes port 0 "
                                     "with a record of the sensor the scan replaced");
}

// --------------------------------------------------------------------------------------------
// Delivery: a scan that finds a sensor the module cannot store - the registry refuses the row. The
// server is told the command failed, is given no sensor in its result, and is sent no structure
// for it: the port still holds what it held.
// --------------------------------------------------------------------------------------------

static void test_s12_a_scan_whose_row_was_refused_is_reported_as_an_error_without_a_structure(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    module.port0.addDevice(makeScanResponder());
    module.sensorRepository.refuseCreateOrUpdate = true;
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("scan-4", "ScanSensors", "{\"pin\":4}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    const size_t structuresBefore = module.server.structures.size();

    module.mqttManager.deliver(moduleCommandJson("ScanSensors"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.sensorRepository.createOrUpdateSensorCallCount,
                                  "the premise: the scan found the sensor and tried to store it");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    const ModuleCommandUpdateDto& outcome = module.server.moduleCommandReports[1][0];
    printf("  s12: a scan whose row was refused was reported %s (%s) with %u sensor(s); %u "
           "structure(s) followed it\n",
           outcome.status.c_str(), outcome.errorMessage.c_str(),
           (unsigned)outcome.result.sensors.size(),
           (unsigned)(module.server.structures.size() - structuresBefore));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Error", outcome.status.c_str(),
                                     "a scan whose sensor was not stored was reported as done");
    TEST_ASSERT_EQUAL_STRING("Failed to save sensor", outcome.errorMessage.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)outcome.result.sensors.size(),
                                     "the server was given a sensor the module did not store");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)(module.server.structures.size() - structuresBefore),
                                     "a structure followed a scan that changed nothing");
}

// --------------------------------------------------------------------------------------------
// Delivery: SetSensorModel that verifies its sensor saves it under the generated uid.
// --------------------------------------------------------------------------------------------

static void test_s12_a_verified_sensor_model_is_saved_and_reported(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    module.port0.addDevice(makeScanResponder());
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("model-2", "SetSensorModel", "{\"pin\":4,\"uid\":\"type-1\"}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("SetSensorModel"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("Done", module.server.moduleCommandReports[1][0].status.c_str());

    TEST_ASSERT_NOT_NULL_MESSAGE(module.moduleStructure.findSensorByUid("s12-module.00.01"),
                                 "the verified sensor did not reach the module structure "
                                 "under the generated uid");
    TEST_ASSERT_TRUE_MESSAGE(module.server.sendToServerModuleStructureCallCount >= 1,
                             "the updated structure never reached the server");
}

// --------------------------------------------------------------------------------------------
// AddWifi runs on the control loop, and stays fast there.
//
// AddWifi looks like "a network call waiting for an answer" - it is not. It
// writes EEPROM, SPIFFS and the WiFiMulti list, and those are things the control loop also
// writes, so executing it on cmd-net created a second writer without saving a single wait. This
// pins the decision: no runner involvement, and the loop still holds its bound.
// --------------------------------------------------------------------------------------------

static void test_s12_add_wifi_executes_on_the_loop_and_stays_fast(void)
{
    ModuleUnderTest module;
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
        "wifi-1", "AddWifi", "{\"ssid\":\"greenhouse\",\"password\":\"secret\"}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("AddWifi"));
    module.scheduler.runForMs(30000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.wifiManager.addWifiNetworkCalled,
                             "the command never executed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("greenhouse", module.wifiManager.lastSsid.c_str(),
                                     "the command executed did not carry the server's options");

    assertLoopStayedBounded(cadence, "an AddWifi command executed inline on the loop");

    // Nothing about it left the loop: the command network task ran no job for it.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.networkRunner.executedCount(),
                                     "AddWifi went to the command network task - it writes "
                                     "EEPROM/SPIFFS/WiFiMulti and must stay on the loop");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("InProgress", module.server.moduleCommandReports[0][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Done", module.server.moduleCommandReports[1][0].status.c_str());
}

// --------------------------------------------------------------------------------------------
// The failure half: the status a failed download records is persisted through
// the settings by the finish step on the loop - not by the updater on cmd-net.
// --------------------------------------------------------------------------------------------

static void test_s12_a_failed_download_status_is_persisted_from_the_loop(void)
{
    ModuleUnderTest module;
    module.firmwareUpdater.downloadMs = 5000;
    module.firmwareUpdater.updateResult = false;
    module.firmwareUpdater.errorMessage = "HTTP code=-11";
    module.firmwareUpdater.failureStatus = 5; // FIRMWARE_UPDATE_STATE_HTTP_ERROR
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
        "fw-3", "UpdateFirmware",
        "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\",\"version\":\"9.9.9\"}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());
    TEST_ASSERT_TRUE_MESSAGE(module.firmwareUpdater.updateCalled, "the download never started");

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, module.settings.getUpdateStatus(),
                                    "the failure status the updater recorded never reached the settings");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("Error", module.server.moduleCommandReports[1][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Failed to start firmware update: HTTP code=-11",
                                     module.server.moduleCommandReports[1][0].errorMessage.c_str(),
                                     "the updater's reason was lost on the way to the report");

    // And the failure did not request a restart or touch the boot flags.
    TEST_ASSERT_NOT_EQUAL(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode());
}

// --------------------------------------------------------------------------------------------
// The batch queue has a ceiling. Lists that arrive while eight are already in
// flight are dropped whole before anything is reported - their commands stay `new` on the
// backend - and the drop is counted.
// --------------------------------------------------------------------------------------------

static void test_s12_a_full_batch_queue_refuses_further_lists_and_counts_them(void)
{
    ModuleUnderTest module;
    module.firmwareUpdater.downloadMs = 120000;
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
        "fw-4", "UpdateFirmware",
        "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\",\"version\":\"9.9.9\"}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // The OTA list occupies the head of the batch queue for two minutes...
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(3000);

    // ...while a chatty app sends nine more lists. Each DeleteSensors finishes on the loop
    // instantly, but its batch cannot be reported past the unfinished OTA in front of it.
    uint32_t delivered = 0;
    module.scheduler.createTask("mqtt-inbound", 64 * 1024, 1, [&]() {
        while (delivered < 9)
        {
            module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
                "del-" + std::to_string(delivered), "DeleteSensors", "{\"pin\":4}"));
            module.mqttManager.deliver(moduleCommandJson("DeleteSensors"));
            delivered++;
            module.scheduler.delayMs(2000);
        }
    });

    module.scheduler.runForMs(30000);

    // Mid-download: the queue stopped at its cap and counted what it refused.
    char capMessage[192];
    snprintf(capMessage, sizeof(capMessage),
             "with one OTA in flight and 9 more lists delivered, %lu batches were queued - "
             "the queue grew past its cap of %lu",
             (unsigned long)module.commandService.pendingBatchCount(),
             (unsigned long)CommandService::MAX_PENDING_BATCHES);
    TEST_ASSERT_TRUE_MESSAGE(
        module.commandService.pendingBatchCount() <= CommandService::MAX_PENDING_BATCHES,
        capMessage);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)CommandService::MAX_PENDING_BATCHES,
                                     (uint32_t)module.commandService.pendingBatchCount(),
                                     "the queue did not fill to its cap - the overflow case did not happen");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.commandService.droppedBatchCount(),
                                     "two of the nine lists should have been refused and counted");

    // The direct path answers the same ceiling: a DeleteGdCommandNotification arriving at a
    // full queue is refused up front - one error acknowledgement, no "accepted", and the
    // server is never asked about the deletion. (This is the cap check inside
    // processDeleteModuleCommand(), which nothing else exercises.)
    module.server.deletionAnswer.received200 = true;
    module.server.deletionAnswer.isDeletedFlag = 1;
    module.mqttManager.deliver(deleteModuleJson());
    module.scheduler.runForMs(5000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.commandReports.size(),
                                     "a refused DeleteModule must produce exactly one acknowledgement");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("error", module.server.commandReports[0].status.c_str(),
                                     "the refusal was not an error - was it accepted into a full queue?");
    TEST_ASSERT_EQUAL_STRING("DeleteModule", module.server.commandReports[0].command.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Command queue full",
                                     module.server.commandReports[0].message.c_str(),
                                     "the refusal did not name its reason");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.server.deletionQueries,
                                     "a DeleteModule refused at the ceiling still asked the server");
    TEST_ASSERT_FALSE_MESSAGE(module.clearService.clearAllDataCalled,
                              "a DeleteModule refused at the ceiling still wiped the module");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, module.commandService.droppedBatchCount(),
                                     "the refused DeleteModule was not counted with the dropped lists");

    // After the download: every accepted list is reported - one InProgress and one outcome
    // batch each for the OTA and seven DeleteSensors lists - and the refused two never appear.
    module.scheduler.runForMs(180000);
    cadence.closeAt(module.clock.nowMs());

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.commandService.pendingBatchCount(),
                                     "a batch was still stuck after everything finished");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(16, (uint32_t)module.server.moduleCommandReports.size(),
                                     "expected 8 InProgress + 8 outcome batches for the 8 accepted lists");

    for (size_t i = 0; i < module.server.moduleCommandReports.size(); i++)
    {
        for (size_t j = 0; j < module.server.moduleCommandReports[i].size(); j++)
        {
            const std::string& uid = module.server.moduleCommandReports[i][j].uid;
            TEST_ASSERT_TRUE_MESSAGE(uid != "del-7" && uid != "del-8",
                                     "a refused list was reported anyway - a drop must leave no trace");
        }
    }

    assertLoopStayedBounded(cadence, "nine command lists piled up behind a two-minute download");
}

// --------------------------------------------------------------------------------------------
// One batch in list order: a mixed list whose slow parts finish in
// reverse order is still reported as one batch, in the order the server sent the commands.
// --------------------------------------------------------------------------------------------

static void test_s12_a_mixed_list_reports_one_batch_in_list_order(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    module.firmwareUpdater.downloadMs = 5000;

    // List order: bus-slow, network, local. Completion order is the reverse: the DeleteSensors
    // finishes on the loop at once, the 5 s download second, the ~20 s silent-pin scan last.
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("ping-1", "PingPin", "{\"pin\":4}"));
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
        "fw-5", "UpdateFirmware",
        "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\",\"version\":\"9.9.9\"}"));
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("del-1", "DeleteSensors", "{\"pin\":4}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("PingPin"));
    module.scheduler.runForMs(120000);
    cadence.closeAt(module.clock.nowMs());

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());
    TEST_ASSERT_TRUE_MESSAGE(module.firmwareUpdater.updateCalled, "the download never ran");
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1, "the scan never ran");

    // Three InProgress, then exactly one batch.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, (uint32_t)module.server.moduleCommandReports.size(),
                                     "expected three InProgress reports and one outcome batch");
    for (size_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_STRING("InProgress",
                                 module.server.moduleCommandReports[i][0].status.c_str());
    }

    const std::vector<ModuleCommandUpdateDto>& batch = module.server.moduleCommandReports[3];
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, (uint32_t)batch.size(),
                                     "the outcome batch did not carry all three commands");

    // List order, not completion order. This is the assertion a reversed batch walk would
    // survive without: reverse it and these three lines go red.
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ping-1", batch[0].uid.c_str(),
                                     "outcome #0 is not the first command of the list");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("fw-5", batch[1].uid.c_str(),
                                     "outcome #1 is not the second command of the list");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("del-1", batch[2].uid.c_str(),
                                     "outcome #2 is not the third command of the list");

    TEST_ASSERT_EQUAL_STRING("Error", batch[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Done", batch[1].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Done", batch[2].status.c_str());

    assertLoopStayedBounded(cadence, "a mixed three-command list executed across three tasks");
}

// --------------------------------------------------------------------------------------------
// The age gate in prepareComplexCommand() is the only age check on
// the direct DeleteGdCommandNotification path - the list validation never sees it. An expired
// deletion must not reach the server or the wipe.
// --------------------------------------------------------------------------------------------

static void test_s12_an_expired_delete_module_command_is_not_executed(void)
{
    ModuleUnderTest module;
    module.timeAdapter.setCommandTimeValid(false);
    module.server.deletionAnswer.received200 = true;
    module.server.deletionAnswer.isDeletedFlag = 1;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(deleteModuleJson());
    module.scheduler.runForMs(30000);

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());

    // Nothing happened: no server round-trip, no wipe, no restart request.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.server.deletionQueries,
                                     "an expired DeleteModule still asked the server");
    TEST_ASSERT_FALSE_MESSAGE(module.clearService.clearAllDataCalled,
                              "an expired DeleteModule still wiped the module");

    // The backend hears the same pair it always heard for a refused command: accepted, then the
    // error naming the reason.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.commandReports.size(),
                                     "the server did not see both acknowledgements");
    TEST_ASSERT_EQUAL_STRING("accepted", module.server.commandReports[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("error", module.server.commandReports[1].status.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Command is expired",
                                     module.server.commandReports[1].message.c_str(),
                                     "the refusal did not name its reason");
}

// --------------------------------------------------------------------------------------------
// What a command did to the line, port by port.
// --------------------------------------------------------------------------------------------

static int uartBegins(ModuleUnderTest& module)
{
    return module.port0.beginCalls + module.port1.beginCalls + module.port2.beginCalls +
           module.port3.beginCalls;
}

static bool anyLineBegun(ModuleUnderTest& module)
{
    return module.port0.lineIsBegun() || module.port1.lineIsBegun() || module.port2.lineIsBegun() ||
           module.port3.lineIsBegun();
}

static int portPowerOns(ModuleUnderTest& module)
{
    return module.port0.powerOnCalls + module.port1.powerOnCalls + module.port2.powerOnCalls +
           module.port3.powerOnCalls;
}

static size_t framesSent(ModuleUnderTest& module)
{
    return module.port0.sentFrames.size() + module.port1.sentFrames.size() +
           module.port2.sentFrames.size() + module.port3.sentFrames.size();
}

/** @brief A raw ModbusRead the line can carry: one holding register of address 1 on channel 0. */
static std::string rawModbusReadJson()
{
    char buffer[384];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"ModbusRead\",\"options\":{\"channel\":0,\"address\":1,"
             "\"function\":3,\"register\":0,\"count\":1}}",
             MODULE_UID);
    return std::string(buffer);
}

// --------------------------------------------------------------------------------------------
// A sensor module nobody started the bus task for: no sensor to poll, no discovery step - a
// registry that is empty or has not read back. A command for the line is what starts it.
// --------------------------------------------------------------------------------------------

/**
 * ScanSensors reaches a module whose acquisition task nothing has started. The scan is queued to
 * that task and the task is asked to exist with it: the scan runs on the line and the server gets
 * both the InProgress and the outcome.
 */
static void test_s12_a_scan_on_a_module_with_no_bus_task_yet_is_run_and_answered(void)
{
    ModuleUnderTest module(false);
    module.server.catalog.push_back(makeModbusSensorType("type-1", "S12-TEMP"));
    module.sensorTypeRepository.setShouldReturnEmpty(false);
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("scan-nt", "ScanSensors", "{\"pin\":4}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.mqttManager.deliver(moduleCommandJson("ScanSensors"));
    module.scheduler.runForMs(120000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    char message[224];
    snprintf(message, sizeof(message),
             "a ScanSensors on a module with no bus task was served %u time(s), put %u frame(s) on the line "
             "and the server saw %u report(s)",
             (unsigned)module.acquisition.busCommandCount(), (unsigned)framesSent(module),
             (unsigned)module.server.moduleCommandReports.size());
    printf("  s12: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1, message);
    TEST_ASSERT_TRUE_MESSAGE(framesSent(module) >= 1, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(), message);
    TEST_ASSERT_EQUAL_STRING("InProgress", module.server.moduleCommandReports[0][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("scan-nt", module.server.moduleCommandReports[1][0].uid.c_str());
}

/**
 * A raw ModbusRead reaches the same module. It is queued to the task that owns the line and the task
 * is asked to exist with it: its frame goes out on the line and the server gets its result.
 */
static void test_s12_a_raw_command_on_a_module_with_no_bus_task_yet_is_run_and_answered(void)
{
    ModuleUnderTest module(false);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.mqttManager.deliver(rawModbusReadJson());
    module.scheduler.runForMs(10000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    char message[224];
    snprintf(message, sizeof(message),
             "a raw ModbusRead on a module with no bus task was served %u time(s), put %u frame(s) on the line "
             "and the server saw %u result(s)",
             (unsigned)module.acquisition.busCommandCount(), (unsigned)framesSent(module),
             (unsigned)module.server.commandReports.size());
    printf("  s12: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1, message);
    TEST_ASSERT_TRUE_MESSAGE(framesSent(module) >= 1, message);
    TEST_ASSERT_TRUE_MESSAGE(module.server.commandReports.size() >= 1, message);
}

// --------------------------------------------------------------------------------------------
// A module of another type has no sensor line. Its boot begins none, and its UART pins drive
// something else: on a hydro module, pumps 2 and 3 (17, 18) and a green LED (14); on a relay
// module, red LEDs (15, 14) - and the relay outputs are the port power pins (39-42). A command
// for the line is answered with an error at once, and neither the bus task nor the line is
// started for it.
// --------------------------------------------------------------------------------------------

/**
 * One command list with one scan command, fetched by a hydro module. It passes validation - the pin
 * is one of the hydro module's four and the catalog holds a scannable ModBus model - so what answers
 * it is the module's type. The discovery is wired as the board has it: a ScanSensors the line takes
 * is followed by a pass of the discovery, whose steps start the bus task by themselves.
 */
static void assertAScanCommandOnAHydroModuleIsRefusedOffTheLine(const char* command, const char* uid,
                                                                const char* options,
                                                                bool catalogOnlyOnTheServer)
{
    ModuleUnderTest module(false, MODULE_TYPE_HYDRO);
    module.followScansWithTheDiscovery();
    if (catalogOnlyOnTheServer)
    {
        module.server.catalog.push_back(makeModbusSensorType("type-1", "S12-TEMP"));
        module.sensorTypeRepository.setShouldReturnEmpty(false);
    }
    else
    {
        module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    }
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(uid, command, options));
    const int beginsBefore = uartBegins(module);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.mqttManager.deliver(moduleCommandJson(command));
    module.scheduler.runForMs(30000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.server.getModuleCommandCalls >= 1, "the command list was never fetched");

    const std::vector<std::vector<ModuleCommandUpdateDto>>& reports = module.server.moduleCommandReports;
    const ModuleCommandUpdateDto* last =
        (reports.empty() || reports.back().empty()) ? nullptr : &reports.back()[0];
    char message[416];
    snprintf(message, sizeof(message),
             "a %s on a hydro module began the UARTs %d -> %d time(s), raised a port's power %d time(s), "
             "put %u frame(s) on the line, started the bus task: %s, took %u discovery step(s), fetched the "
             "catalog %d time(s); the server saw %u report(s), the last %s '%s'",
             command, beginsBefore, uartBegins(module), portPowerOns(module), (unsigned)framesSent(module),
             module.acquisition.taskStarted() ? "yes" : "no",
             (unsigned)module.acquisition.startupScanStepCount(), module.server.getSensorTypesListCallCount,
             (unsigned)reports.size(), last != nullptr ? last->status.c_str() : "-",
             last != nullptr ? last->errorMessage.c_str() : "");
    printf("  s12: %s\n", message);

    TEST_ASSERT_EQUAL_INT_MESSAGE(beginsBefore, uartBegins(module), message);
    TEST_ASSERT_FALSE_MESSAGE(anyLineBegun(module), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, portPowerOns(module), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)framesSent(module), message);
    TEST_ASSERT_FALSE_MESSAGE(module.acquisition.taskStarted(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.acquisition.startupScanStepCount(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.server.getSensorTypesListCallCount, message);

    // What the backend sees for any command refused after validation: InProgress, then the batch
    // with the error.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)reports.size(), message);
    TEST_ASSERT_EQUAL_STRING("InProgress", reports[0][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING(uid, reports[1][0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Error", reports[1][0].status.c_str(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("No sensor line on this module", reports[1][0].errorMessage.c_str(), message);
}

/** The catalog is on the server only: nothing is fetched for a scan the module cannot make. */
static void test_s12_a_scan_on_a_hydro_module_is_answered_with_an_error_off_the_line(void)
{
    assertAScanCommandOnAHydroModuleIsRefusedOffTheLine("ScanSensors", "scan-hydro", "{\"pin\":2}", true);
}

static void test_s12_a_pin_ping_on_a_hydro_module_is_answered_with_an_error_off_the_line(void)
{
    assertAScanCommandOnAHydroModuleIsRefusedOffTheLine("PingPin", "ping-hydro", "{\"pin\":2}", false);
}

static void test_s12_a_sensor_model_on_a_hydro_module_is_answered_with_an_error_off_the_line(void)
{
    assertAScanCommandOnAHydroModuleIsRefusedOffTheLine("SetSensorModel", "model-hydro",
                                                        "{\"pin\":2,\"uid\":\"type-1\"}", false);
}

/**
 * A relay module has no sensor pins, so its validation refuses a pin ping before the type is asked:
 * the answer stays the one validation gives.
 */
static void test_s12_a_pin_ping_on_a_relay_module_keeps_the_answer_its_validation_gives(void)
{
    ModuleUnderTest module(false, MODULE_TYPE_DEVICE);
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S12-TEMP"));
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand("ping-relay", "PingPin", "{\"pin\":2}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.mqttManager.deliver(moduleCommandJson("PingPin"));
    module.scheduler.runForMs(30000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    const std::vector<std::vector<ModuleCommandUpdateDto>>& reports = module.server.moduleCommandReports;
    char message[224];
    snprintf(message, sizeof(message),
             "a PingPin on a relay module started the bus task: %s; the server saw %u report(s), the last %s '%s'",
             module.acquisition.taskStarted() ? "yes" : "no", (unsigned)reports.size(),
             reports.empty() ? "-" : reports.back()[0].status.c_str(),
             reports.empty() ? "" : reports.back()[0].errorMessage.c_str());
    printf("  s12: %s\n", message);
    TEST_ASSERT_FALSE_MESSAGE(module.acquisition.taskStarted(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)reports.size(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Error", reports[1][0].status.c_str(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Missing or invalid pin in options", reports[1][0].errorMessage.c_str(), message);
}

/** A raw ModbusRead with options the line could carry, sent to a module of a type with no line. */
static void assertARawReadOnAModuleWithNoLineIsRefused(int moduleType, const char* typeName)
{
    ModuleUnderTest module(false, moduleType);
    const int beginsBefore = uartBegins(module);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.mqttManager.deliver(rawModbusReadJson());
    module.scheduler.runForMs(10000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    const std::vector<CommandResultDto>& results = module.server.commandReports;
    char message[384];
    snprintf(message, sizeof(message),
             "a raw ModbusRead on a %s module started the bus task: %s, was served %u time(s), began the UARTs "
             "%d -> %d time(s), raised a port's power %d time(s), put %u frame(s) on the line; the server saw "
             "%u result(s), the first %s '%s'",
             typeName, module.acquisition.taskStarted() ? "yes" : "no",
             (unsigned)module.acquisition.busCommandCount(), beginsBefore, uartBegins(module),
             portPowerOns(module), (unsigned)framesSent(module), (unsigned)results.size(),
             results.empty() ? "-" : results[0].status.c_str(),
             results.empty() ? "" : results[0].message.c_str());
    printf("  s12: %s\n", message);

    TEST_ASSERT_FALSE_MESSAGE(module.acquisition.taskStarted(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.acquisition.busCommandCount(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(beginsBefore, uartBegins(module), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, portPowerOns(module), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)framesSent(module), message);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)results.size(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("error", results[0].status.c_str(), message);
    TEST_ASSERT_EQUAL_STRING("ModbusRead", results[0].command.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("No sensor line on this module", results[0].message.c_str(), message);
}

static void test_s12_a_raw_read_on_a_relay_module_is_answered_with_an_error_and_starts_no_bus_task(void)
{
    assertARawReadOnAModuleWithNoLineIsRefused(MODULE_TYPE_DEVICE, "relay");
}

static void test_s12_a_raw_read_on_a_hydro_module_is_answered_with_an_error_and_starts_no_bus_task(void)
{
    assertARawReadOnAModuleWithNoLineIsRefused(MODULE_TYPE_HYDRO, "hydro");
}

/**
 * RebootRemoteModule is a UDP packet; it takes the bus task's queue only to be executed off the
 * loop. A relay module has no line, and does not start that task for it.
 */
static void test_s12_a_remote_reboot_on_a_relay_module_starts_no_bus_task(void)
{
    ModuleUnderTest module(false, MODULE_TYPE_DEVICE);
    char json[384];
    snprintf(json, sizeof(json),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"RebootRemoteModule\",\"options\":{\"targetUid\":\"another-module\"}}",
             MODULE_UID);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.mqttManager.deliver(std::string(json));
    module.scheduler.runForMs(10000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    char message[224];
    snprintf(message, sizeof(message),
             "a RebootRemoteModule on a relay module started the bus task: %s, was served %u time(s); "
             "the server saw %u result(s)",
             module.acquisition.taskStarted() ? "yes" : "no", (unsigned)module.acquisition.busCommandCount(),
             (unsigned)module.server.commandReports.size());
    printf("  s12: %s\n", message);
    TEST_ASSERT_FALSE_MESSAGE(module.acquisition.taskStarted(), message);
}

// --------------------------------------------------------------------------------------------
// The watchdog. While the image comes down, every progress-time esp_task_wdt_reset()
// runs on the downloading task's stack - so it vouches for the downloader. The loop's watchdog
// entry is fed exactly once per completed loop iteration and by nobody else: a loop that hangs
// during an OTA now looks hung.
// --------------------------------------------------------------------------------------------

static void test_s12_the_download_vouches_for_the_downloader_and_the_loop_for_itself(void)
{
    ModuleUnderTest module;
    module.firmwareUpdater.downloadMs = 120000;
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
        "fw-2", "UpdateFirmware",
        "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\",\"version\":\"9.9.9\"}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(180000);
    cadence.closeAt(module.clock.nowMs());

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());
    TEST_ASSERT_TRUE_MESSAGE(module.firmwareUpdater.updateCalled, "the download never started");

    // The case happened: the download made progress and fed the watchdog while it did.
    TEST_ASSERT_TRUE_MESSAGE(module.firmwareUpdater.progressFeedTaskNames.size() >= 10,
                             "the download reported almost no progress - the scenario is empty");

    // Every progress-time feed ran on the downloading task, and none on the control loop. This
    // is the assertion that fails if the download executes on the loop: its esp_task_wdt_reset()
    // then feeds the loop's watchdog entry while the loop is parked inside the download - and a
    // hung loop stays invisible for as long as the bytes keep coming.
    for (size_t i = 0; i < module.firmwareUpdater.progressFeedTaskNames.size(); i++)
    {
        char message[192];
        snprintf(message, sizeof(message),
                 "progress feed #%lu ran on task '%s' - the download was vouching for a task "
                 "that is not the downloader",
                 (unsigned long)i, module.firmwareUpdater.progressFeedTaskNames[i].c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(NetworkCommandRunner::TASK_NAME,
                                         module.firmwareUpdater.progressFeedTaskNames[i].c_str(),
                                         message);
    }

    // The downloading task put itself under the watchdog, like every other background task.
    bool runnerSubscribed = false;
    const std::vector<std::string>& subscribed = module.scheduler.watchdogSubscribedTaskNames();
    for (size_t i = 0; i < subscribed.size(); i++)
    {
        if (subscribed[i] == NetworkCommandRunner::TASK_NAME)
        {
            runnerSubscribed = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(runnerSubscribed,
                             "the command network task never subscribed to the task watchdog");

    // And the loop's entry was fed once per completed iteration, by the loop itself - no more.
    // If anything had fed on the loop's behalf during the download, these two counts would
    // diverge by the number of progress events.
    const uint64_t loopFeeds = module.scheduler.watchdogFeedsBy(LOOP_TASK_NAME);
    char message[224];
    snprintf(message, sizeof(message),
             "the loop's watchdog entry was fed %lu times over %lu completed iterations - "
             "something other than the loop's own iteration fed it",
             (unsigned long)loopFeeds, (unsigned long)cadence.count());
    TEST_ASSERT_TRUE_MESSAGE(loopFeeds <= cadence.count(), message);
    TEST_ASSERT_TRUE_MESSAGE(loopFeeds + 1 >= cadence.count(), message);
}

extern "C" void run_s12_complex_commands_tests(void)
{
    printf("\n----- Running S12 Complex Commands Off The Loop Tests -----\n");

    RUN_TEST(test_s12_the_loop_stays_bounded_while_scan_sensors_fetches_and_scans);
    RUN_TEST(test_s12_the_loop_stays_bounded_while_set_sensor_model_scans_a_silent_pin);
    RUN_TEST(test_s12_the_loop_stays_bounded_while_ping_pin_scans_a_silent_pin);
    RUN_TEST(test_s12_the_loop_stays_bounded_while_a_firmware_download_runs);
    RUN_TEST(test_s12_the_loop_stays_bounded_while_delete_module_asks_a_slow_server);
    RUN_TEST(test_s12_a_detected_sensor_is_saved_and_reported_with_the_structure);
    RUN_TEST(test_s12_a_scan_that_replaces_a_sensor_sends_a_structure_without_its_records);
    RUN_TEST(test_s12_a_scan_whose_row_was_refused_is_reported_as_an_error_without_a_structure);
    RUN_TEST(test_s12_a_verified_sensor_model_is_saved_and_reported);
    RUN_TEST(test_s12_add_wifi_executes_on_the_loop_and_stays_fast);
    RUN_TEST(test_s12_a_failed_download_status_is_persisted_from_the_loop);
    RUN_TEST(test_s12_a_full_batch_queue_refuses_further_lists_and_counts_them);
    RUN_TEST(test_s12_a_mixed_list_reports_one_batch_in_list_order);
    RUN_TEST(test_s12_an_expired_delete_module_command_is_not_executed);
    RUN_TEST(test_s12_the_download_vouches_for_the_downloader_and_the_loop_for_itself);
    RUN_TEST(test_s12_a_scan_on_a_module_with_no_bus_task_yet_is_run_and_answered);
    RUN_TEST(test_s12_a_raw_command_on_a_module_with_no_bus_task_yet_is_run_and_answered);
    RUN_TEST(test_s12_a_scan_on_a_hydro_module_is_answered_with_an_error_off_the_line);
    RUN_TEST(test_s12_a_pin_ping_on_a_hydro_module_is_answered_with_an_error_off_the_line);
    RUN_TEST(test_s12_a_sensor_model_on_a_hydro_module_is_answered_with_an_error_off_the_line);
    RUN_TEST(test_s12_a_pin_ping_on_a_relay_module_keeps_the_answer_its_validation_gives);
    RUN_TEST(test_s12_a_raw_read_on_a_relay_module_is_answered_with_an_error_and_starts_no_bus_task);
    RUN_TEST(test_s12_a_raw_read_on_a_hydro_module_is_answered_with_an_error_and_starts_no_bus_task);
    RUN_TEST(test_s12_a_remote_reboot_on_a_relay_module_starts_no_bus_task);

    printf("\n----- S12 Complex Commands Off The Loop Tests Complete -----\n");
}
