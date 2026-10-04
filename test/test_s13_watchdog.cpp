// Scenario S13 - the watchdog becomes an instrument.
//
// Every liveness signal is honest now: the control loop feeds its own watchdog entry once per
// completed iteration and background tasks feed their own. What this file pins down is the
// *threshold*: with the worst honest loop step in milliseconds and every legal long pause
// bounded (constants.h, WDT_WORST_LEGAL_PAUSE_S), a frozen control loop must be caught within
// WDT_TIMEOUT_S - not within the five minutes a greenhouse cannot afford - while every legal
// long operation keeps the watchdog quiet. And the external hardware watchdog's kicks must be
// the control loop's statement, not an independent task's: when the loop stalls past the
// threshold, the kicks stop and the chip decides.
//
// What is under test is production code: the real services and tasks of the S8/S12 bench, the
// real per-task feed attribution (watchdogSubscribeCurrentTask()/watchdogFeed() and
// feedTaskWatchdog() land on the calling task), the real WDT_TIMEOUT_S the device arms, and
// the real LoopLivenessGate the external kicker asks. Replaced are the server, the line, the
// OTA transport, the watchdog's timer - and time.
//
// ### Reproducing the red
//
// The red is a 300 s WDT_TIMEOUT_S: the frozen-loop scenario then measures a 300 s blindness
// against the seconds this file demands. The second red is a catalog fetch that makes its three
// attempts back to back without feeding: the legal-pause scenario then measures a ~90 s feed gap
// against the WDT_WORST_LEGAL_PAUSE_S budget.

#include <unity.h>

#include "constants.h"

#include "Application/Services/CommandService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/CheckModuleDeletionUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Infrastructure/Adapters/ExternalWDT/LoopLivenessGate.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"

#include "Mock/Application/MockClearModuleDataService.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
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
#include "Sim/SimTaskWatchdog.h"
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
using hostsim::SimTaskWatchdog;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// The claim: a frozen loop is blind for at most this long. WDT_TIMEOUT_S must fit under it -
// minutes-not-five - and the frozen-loop scenario measures the actual blindness against it.
const uint32_t MAX_ACCEPTED_BLINDNESS_MS = 200000;

// The per-task legal-pause budget the threshold is computed from: on a build whose legal
// pauses exceed it, the quiet-watchdog scenario prints the excess.
const uint32_t LEGAL_PAUSE_BUDGET_MS = (uint32_t)WDT_WORST_LEGAL_PAUSE_S * 1000UL;

// The sensor-acq line of that same table: one bounded step of a line operation. A task that
// runs a longer sequence on the line has to feed between its steps, or this entry - and the
// threshold derived from it - stops describing the build.
const uint32_t SENSOR_ACQ_PAUSE_BUDGET_MS = (uint32_t)WDT_SENSOR_ACQ_LEGAL_PAUSE_S * 1000UL;

// One _apiMutex hold of the deletion query, derived from the transport's own named budget:
// the attempts run inside the lock, each paying the default connect on top of its read
// timeout. Derived, not copied, so a changed budget moves this model - and the gap
// assertions - instead of silently outdating them.
const uint32_t DELETION_QUERY_HOLD_MS =
    RestAPIService::DELETION_QUERY_MAX_ATTEMPTS
    * (RestAPIService::DEFAULT_CONNECT_TIMEOUT_MS
       + RestAPIService::DELETION_QUERY_READ_TIMEOUT_MS
       + RestAPIService::DELETION_QUERY_RETRY_PAUSE_MS);

// One REST call against a server that answers slowly; RestApiService's timeout.
const uint32_t SLOW_CALL_MS = 30000;

// The external watchdog chip's kick cadence (ExternalWDT.h WDT_KICK_INTERVAL_MS).
const uint32_t EXT_KICK_INTERVAL_MS = 100;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "s13-module";

// --- the outside world ------------------------------------------------------------------------

class SimServer : public MockHybridAPIService {
public:
    explicit SimServer(HostTaskScheduler& scheduler) : _scheduler(scheduler) {}

    uint32_t callDelayMs = 0;
    bool reachable = true;
    /** Separately switchable: a server that answers everything except the catalog. */
    bool catalogReachable = true;

    /**
     * The transport's _apiMutex, modelled: every API call of the real RestApiService
     * serialises on one lock, so a slow call makes every other caller *wait* - the half of a
     * task's feed gap that a lock-free mock cannot produce. Tickets keep it FIFO, which is
     * what FreeRTOS mutexes give equal-priority waiters on the device.
     */
    class ApiLock {
    public:
        ApiLock(SimServer& server) : _server(server)
        {
            const uint32_t ticket = _server._nextApiTicket++;
            while (_server._apiServing != ticket)
            {
                _server._scheduler.delayMs(10);
            }
        }
        ~ApiLock() { _server._apiServing++; }

    private:
        SimServer& _server;
    };

    std::vector<ModuleCommandDto> pendingModuleCommands;
    uint32_t getModuleCommandCalls = 0;
    std::vector<SensorType> catalog;

    std::vector<std::vector<ModuleCommandUpdateDto>> moduleCommandReports;

    ModuleDeletionQueryResult deletionAnswer;
    uint32_t deletionQueries = 0;

    ModuleDeletionQueryResult queryModuleDeletion() override
    {
        ApiLock lock(*this);
        deletionQueries++;
        // The real method loops its three attempts inside the lock; the hold is the sum.
        _scheduler.delayMs(DELETION_QUERY_HOLD_MS);
        if (!reachable)
        {
            ModuleDeletionQueryResult failed;
            failed.queryFailed = true;
            return failed;
        }
        return deletionAnswer;
    }

    std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) override
    {
        ApiLock lock(*this);
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
        ApiLock lock(*this);
        getSensorTypesListCallCount++;
        spendTime();
        const bool ok = reachable && catalogReachable;
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        return ok ? catalog : std::vector<SensorType>();
    }

    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        ApiLock lock(*this);
        spendTime();
        if (!reachable)
        {
            return false;
        }
        moduleCommandReports.push_back(updates);
        return true;
    }

    bool sendCommandResult(const CommandResultDto&, ApiPriority = ApiPriority::UNDEFINED) override
    {
        ApiLock lock(*this);
        spendTime();
        return reachable;
    }

    bool sendToServerModuleStructure(const ModuleStructure& structure,
                                     ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiLock lock(*this);
        spendTime();
        if (!reachable)
        {
            return false;
        }
        return MockHybridAPIService::sendToServerModuleStructure(structure, priority);
    }

private:
    void spendTime()
    {
        if (callDelayMs > 0)
        {
            _scheduler.delayMs(callDelayMs);
        }
    }

    HostTaskScheduler& _scheduler;
    uint32_t _nextApiTicket = 0;
    uint32_t _apiServing = 0;
};

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
                _handler->handleMessage("GrowDirectorMqttTopic/s13-module",
                                        reinterpret_cast<const uint8_t*>(payload.data()),
                                        static_cast<unsigned int>(payload.size()));
            }
        }
    }

    void deliver(const std::string& payload) { _inbound.push_back(payload); }
    void setHandler(ITopicHandler* handler) { _handler = handler; }

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
             "\"commandUid\":\"del-1\",\"createdAt\":\"2026-08-21T10:00:00Z\"}",
             MODULE_UID);
    return std::string(buffer);
}

ModuleCommandDto makeServerModuleCommand(const std::string& uid, const char* command,
                                         const std::string& options)
{
    ModuleCommandDto dto;
    dto.uid = uid;
    dto.command = command;
    dto.createdAt = "2026-08-21T10:00:00Z";
    dto.options = options;
    return dto;
}

/**
 * @brief The module, wired the way globals.h wires it, plus the two watchdog models: the task
 *        watchdog armed with the device's WDT_TIMEOUT_S, and the external kicker asking the
 *        real LoopLivenessGate before every kick, as ExternalWDT's task does.
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
    // The owner of the ModBus addresses of the module, over the same repository globals.h gives
    // it: a scan asks it which addresses the other ports already hold.
    ModbusAddressAllocator modbusAddressAllocator;
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

    SimTaskWatchdog taskWatchdog;

    // The external kicker, modelled the way ExternalWDT::wdtTask() works on the device: its own
    // cadence, gated by the real LoopLivenessGate. The gate's stall limit is what the device
    // configures - the task watchdog threshold.
    LoopLivenessGate loopGate;
    uint64_t extKickCount = 0;
    uint64_t extStarvedCount = 0;
    uint64_t extLastKickAtMs = 0;

    // The loop freezes at this virtual moment (0 = never): one long stall with no iterations,
    // no feeds and no beats - what a deadlocked call on the loop looks like from outside.
    uint64_t freezeLoopAtMs = 0;
    uint32_t freezeForMs = 600000;

    ModuleUnderTest()
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
          modbusAddressAllocator(&sensorRepository),
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
          topicHandler(&logger, &debugSystem, MODULE_UID, &commandService, &jsonConverter),
          taskWatchdog(scheduler, clock, (uint32_t)WDT_TIMEOUT_S * 1000UL),
          loopGate((uint32_t)WDT_TIMEOUT_S * 1000UL)
    {
        debugSystem.featureEnabled = false;

        timeAdapter.setEpochTime(1755770000);
        timeAdapter.setCommandTimeValid(true);

        settings.setUser("s13@example.com");
        settings.setAccountUid("s13-account");
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setSettings(&settings);

        moduleStructure.nodes.push_back(Node());

        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        rawBusAccess.setBusLock(&busLock);
        // The line as a booted module has it: the boot begins the UARTs before the first frame
        // (ModuleSetupService::setupSensorBus()), and a line nobody began carries nothing.
        sensorReader.init();

        acquisition.setRawCommandExecutor(&moduleCommandUseCase);
        acquisition.ensureRunning();
        networkRunner.setExecutor(&moduleCommandUseCase);
        commandService.setOutbox(&outbox);
        commandService.setBusCommandExecutor(&acquisition);
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

        taskWatchdog.start();
    }

    /**
     * @brief One task, mirroring main.cpp: subscribe once (esp_task_wdt_add), then per
     *        iteration feed the task watchdog, beat the external gate, run the loop service,
     *        sleep one period. A configured freeze is one long stall with none of that.
     */
    void startArduinoLoopTask()
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this]() {
            scheduler.watchdogSubscribeCurrentTask();
            bool frozen = false;
            while (true)
            {
                if (freezeLoopAtMs != 0 && !frozen && clock.nowMs() >= freezeLoopAtMs)
                {
                    frozen = true;
                    scheduler.delayMs(freezeForMs);
                    continue;
                }
                scheduler.watchdogFeed();
                loopGate.noteAlive((uint32_t)clock.nowMs());
                loopService.loop();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }

    /** @brief The external kicker: ExternalWDT::wdtTask()'s decision, on the harness clock. */
    void startExternalKickerTask()
    {
        scheduler.createTask("ext-wdt-kicker", 64 * 1024, 2, [this]() {
            while (true)
            {
                if (loopGate.allowKick((uint32_t)clock.nowMs()))
                {
                    extKickCount++;
                    extLastKickAtMs = clock.nowMs();
                }
                else
                {
                    extStarvedCount++;
                }
                scheduler.delayMs(EXT_KICK_INTERVAL_MS);
            }
        });
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// The claim under test: a frozen control loop is caught within the threshold - and the
// threshold is minutes-not-five. The freeze is a single stall with no feeds, which is what any
// deadlocked call on the loop looks like to the watchdog.
// --------------------------------------------------------------------------------------------

static void test_s13_a_frozen_loop_is_caught_within_the_threshold(void)
{
    ModuleUnderTest module;
    module.freezeLoopAtMs = 30000;
    module.startArduinoLoopTask();

    module.scheduler.runForMs(650000);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.taskWatchdog.fired(),
                             "the loop froze and the watchdog never noticed at all");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(LOOP_TASK_NAME, module.taskWatchdog.firedTaskName().c_str(),
                                     "the watchdog fired for a task that was not the frozen loop");

    // The headline number: how long a dead loop is invisible. This is what 300 s of threshold
    // means five minutes of relays in their last state - and what the computed threshold caps.
    char message[256];
    snprintf(message, sizeof(message),
             "the frozen loop went unnoticed for %lu ms (threshold %d s, acceptance %lu ms) - "
             "a greenhouse waits this long in the last relay state",
             (unsigned long)module.taskWatchdog.firedGapMs(),
             (int)WDT_TIMEOUT_S,
             (unsigned long)MAX_ACCEPTED_BLINDNESS_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.taskWatchdog.firedGapMs() <= MAX_ACCEPTED_BLINDNESS_MS,
                             message);

    // And it fired *because of* the threshold, not earlier and not much later: the gap at the
    // moment of firing sits between the threshold and the threshold plus one model poll.
    TEST_ASSERT_TRUE_MESSAGE(module.taskWatchdog.firedGapMs() > (uint64_t)WDT_TIMEOUT_S * 1000UL,
                             "the watchdog fired before its own threshold elapsed");
    TEST_ASSERT_TRUE_MESSAGE(module.taskWatchdog.firedGapMs() <=
                                 (uint64_t)WDT_TIMEOUT_S * 1000UL + 2 * SimTaskWatchdog::POLL_MS +
                                     CONTROL_PERIOD_MS,
                             "the watchdog fired long after its threshold - the model drifted");
}

// --------------------------------------------------------------------------------------------
// Legal long operations do not kill the system. The slowest lawful world at once: every REST
// call takes 30 s, the sensor-type catalog is unreachable (three attempts per fetch), an OTA
// download takes two minutes with sparse progress, and a scan sweeps a silent pin through a
// 10 s port warmup. Every subscribed task must stay inside the legal-pause budget the
// threshold was computed from - and the watchdog must stay quiet.
// --------------------------------------------------------------------------------------------

static void test_s13_legal_long_operations_keep_the_watchdog_quiet(void)
{
    ModuleUnderTest module;
    module.server.callDelayMs = SLOW_CALL_MS;
    module.server.catalogReachable = false; // the catalog fetch spends all three attempts
    module.firmwareUpdater.downloadMs = 120000;
    module.firmwareUpdater.progressEveryMs = 25000; // sparse progress on a slow link

    module.startArduinoLoopTask();
    module.startExternalKickerTask();

    // ScanSensors with an empty local catalog: fetch on cmd-net (3 x 30 s), then nothing.
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("scan-1", "ScanSensors", "{\"pin\":4}"));
    module.mqttManager.deliver(moduleCommandJson("ScanSensors"));
    module.scheduler.runForMs(250000);

    // A two-minute download with 25 s between progress feeds - and, mid-download, a
    // DeleteModule: its verification is the longest single holder of the API lock (three
    // attempts inside one hold), so every task reporting meanwhile queues behind it. This is
    // the concurrent half of the feed gaps - the wait for the lock, not just one's own call.
    module.server.deletionAnswer.received200 = true;
    module.server.deletionAnswer.isDeletedFlag = 1;
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
        "fw-1", "UpdateFirmware",
        "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\",\"version\":\"9.9.9\"}"));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(30000);
    module.mqttManager.deliver(deleteModuleJson());
    module.scheduler.runForMs(250000);

    // A silent-pin scan on the acquisition task: port isolation warmup plus the address range.
    module.sensorTypeRepository.addSensorType(makeModbusSensorType("type-1", "S13-TEMP"));
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("ping-1", "PingPin", "{\"pin\":4}"));
    module.mqttManager.deliver(moduleCommandJson("PingPin"));
    module.scheduler.runForMs(150000);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");

    // The scenario happened: the slow parts all ran.
    TEST_ASSERT_TRUE_MESSAGE(module.server.getSensorTypesListCallCount >= 3,
                             "the catalog fetch did not spend its three attempts");
    TEST_ASSERT_TRUE_MESSAGE(module.firmwareUpdater.updateCalled, "the download never ran");
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1, "the scan never ran");
    TEST_ASSERT_TRUE_MESSAGE(module.server.deletionQueries >= 1,
                             "the deletion was never verified - the longest lock holder "
                             "never contended");

    // The verdict: nothing legal tripped the watchdog...
    char firedMessage[256];
    snprintf(firedMessage, sizeof(firedMessage),
             "the watchdog fired for '%s' with a %lu ms gap during legal operations only",
             module.taskWatchdog.firedTaskName().c_str(),
             (unsigned long)module.taskWatchdog.firedGapMs());
    TEST_ASSERT_FALSE_MESSAGE(module.taskWatchdog.fired(), firedMessage);

    // ...and every task stayed inside the budget the threshold is computed from. This is the
    // 2x margin made measurable: the worst legal gap observed on the slowest lawful bench must
    // fit the WDT_WORST_LEGAL_PAUSE_S entry of constants.h.
    const char* tasks[] = {LOOP_TASK_NAME, "sensor-acq", "outbox-net",
                           NetworkCommandRunner::TASK_NAME};
    for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]); i++)
    {
        char message[224];
        snprintf(message, sizeof(message),
                 "task '%s' showed a %lu ms legal feed gap - over the %lu ms budget the "
                 "watchdog threshold is computed from",
                 tasks[i], (unsigned long)module.taskWatchdog.worstGapMs(tasks[i]),
                 (unsigned long)LEGAL_PAUSE_BUDGET_MS);
        TEST_ASSERT_TRUE_MESSAGE(module.taskWatchdog.worstGapMs(tasks[i]) <= LEGAL_PAUSE_BUDGET_MS,
                                 message);
    }

    // The external kicker never starved either: the loop kept beating through all of it.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.extStarvedCount,
                                     "the external watchdog was starved during legal operations");
    TEST_ASSERT_TRUE_MESSAGE(module.extKickCount > 0, "the external kicker never ran");
}

// --------------------------------------------------------------------------------------------
// The address write is the longest single operation the acquisition task can be handed, and it
// is the one the scan sweep does not cover: a scan feeds the watchdog before its port warmup,
// an address write - the fallback chain of ModbusSensorReader - would feed nowhere at all without feeds of its own.
// Setting a sensor model on a module whose neighbour already holds the answering address ends
// in exactly that chain, on sensor-acq, and a model that applies its address only after a power
// cycle adds two ten-second warmups to it.
//
// The claim is the sensor-acq line of the budget table in constants.h: whatever sequence this
// task runs on the line, the gap the watchdog sees is one bounded step of it - a port warmup
// plus a ping, not the whole chain.
// --------------------------------------------------------------------------------------------

static SensorType makePowerCycleModbusSensorType(const char* uid, const char* name)
{
    SensorType type = makeModbusSensorType(uid, name);
    type.registerToStoreModbusDeviceAddress = 0x0200;
    type.writeRegistersCommand = 6;
    type.applyAddressAfterPowerCycle = true;
    return type;
}

/**
 * @brief Drive one SetSensorModel that ends in the address-write fallback chain on sensor-acq.
 * @param module The stand.
 * @param type The sensor model the command assigns.
 * @return The worst feed gap the acquisition task showed while it ran.
 */
static uint64_t runAddressWriteOnAcquisition(ModuleUnderTest& module, const SensorType& type)
{
    module.startArduinoLoopTask();
    module.startExternalKickerTask();

    module.sensorTypeRepository.addSensorType(type);

    // The neighbour of pin 0 on the shared line already holds 0x72.
    SensorEntity neighbour;
    neighbour.uid = std::string(MODULE_UID) + ".01";
    neighbour.pin = 1;
    neighbour.protocol = "ModBus";
    neighbour.modbusDeviceAddress = MODBUS_ADDRESS_MIN + 2;
    neighbour.status = SENSOR_STATUS_ADDED;
    module.sensorRepository.sensors.push_back(neighbour);

    // The device on pin 0 answers only where the neighbour sits, and never moves: the write is
    // never confirmed, so every fallback stage of the chain runs.
    SimRs485Bus::Device device;
    device.address = MODBUS_ADDRESS_MIN + 2;
    device.registerValues.assign(4, 220);
    module.port0.addDevice(device);

    std::string options = std::string("{\"pin\":4,\"uid\":\"") + type.uid + "\"}";
    module.server.pendingModuleCommands.push_back(
        makeServerModuleCommand("set-model-1", "SetSensorModel", options));
    module.mqttManager.deliver(moduleCommandJson("SetSensorModel"));
    module.scheduler.runForMs(400000);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1,
                             "the command never reached the acquisition task");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.writes.size() > 0,
                             "no address write went out - this scenario did not reach the chain "
                             "it is about");

    return module.taskWatchdog.worstGapMs("sensor-acq");
}

static void assertAcquisitionStayedInsideItsBudget(ModuleUnderTest& module, uint64_t worstGapMs)
{
    char firedMessage[256];
    snprintf(firedMessage, sizeof(firedMessage),
             "the watchdog fired for '%s' with a %lu ms gap during a legal address write",
             module.taskWatchdog.firedTaskName().c_str(),
             (unsigned long)module.taskWatchdog.firedGapMs());
    TEST_ASSERT_FALSE_MESSAGE(module.taskWatchdog.fired(), firedMessage);

    char message[256];
    snprintf(message, sizeof(message),
             "task 'sensor-acq' showed a %lu ms feed gap writing a sensor address - over the "
             "%lu ms the sensor-acq line of the budget table allows, which is what "
             "WDT_WORST_LEGAL_PAUSE_S is built out of",
             (unsigned long)worstGapMs, (unsigned long)SENSOR_ACQ_PAUSE_BUDGET_MS);
    TEST_ASSERT_TRUE_MESSAGE(worstGapMs <= SENSOR_ACQ_PAUSE_BUDGET_MS, message);
}

static void test_s13_an_address_write_feeds_its_task_between_the_steps(void)
{
    ModuleUnderTest module;
    uint64_t worstGapMs = runAddressWriteOnAcquisition(
        module, makeModbusSensorType("type-plain", "S13-PLAIN"));

    printf("  s13: address write without the power-cycle flag, worst sensor-acq gap %lu ms\n",
           (unsigned long)worstGapMs);
    assertAcquisitionStayedInsideItsBudget(module, worstGapMs);
}

static void test_s13_an_address_write_that_power_cycles_feeds_its_task_too(void)
{
    ModuleUnderTest module;
    uint64_t worstGapMs = runAddressWriteOnAcquisition(
        module, makePowerCycleModbusSensorType("type-pc", "S13-RK520"));

    // The scenario is the one that costs the most: the flag turned the port off and on again,
    // twice, each time paying the ten-second warmup before the address could be re-checked.
    TEST_ASSERT_TRUE_MESSAGE(module.port0.powerOffTransitions >= 2,
                             "the model asked for a power cycle and the port was never cycled - "
                             "this is not the expensive path");

    printf("  s13: address write with applyAddressAfterPowerCycle, worst sensor-acq gap %lu ms\n",
           (unsigned long)worstGapMs);
    assertAcquisitionStayedInsideItsBudget(module, worstGapMs);
}

// --------------------------------------------------------------------------------------------
// The external hardware watchdog is the control loop's statement. While the loop beats, the
// kicker kicks on its own cadence; once the loop has been silent past the stall limit - the
// task watchdog threshold - the kicks stop for good and the chip decides. Same shape as "the
// download vouches for the downloader": every kick is made on behalf of a task that actually
// proved itself alive.
// --------------------------------------------------------------------------------------------

static void test_s13_the_kicks_stop_when_the_loop_stalls_and_not_before(void)
{
    ModuleUnderTest module;
    module.freezeLoopAtMs = 30000;
    module.freezeForMs = 400000;
    module.startArduinoLoopTask();
    module.startExternalKickerTask();

    module.scheduler.runForMs(300000);

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());

    const uint64_t stallLimitMs = (uint64_t)WDT_TIMEOUT_S * 1000UL;
    const uint64_t expectedStopMs = module.freezeLoopAtMs + stallLimitMs;

    // The case happened: kicks flowed while the loop was alive and for the stall limit after.
    TEST_ASSERT_TRUE_MESSAGE(module.extKickCount >= expectedStopMs / EXT_KICK_INTERVAL_MS / 2,
                             "the kicker barely ran while the loop was alive");

    // The kicks stopped when the stall limit ran out - not earlier, not later.
    char message[256];
    snprintf(message, sizeof(message),
             "the last kick was at %lu ms; the loop froze at %lu ms with a %lu ms stall limit, "
             "so the kicks had to stop at ~%lu ms",
             (unsigned long)module.extLastKickAtMs,
             (unsigned long)module.freezeLoopAtMs,
             (unsigned long)stallLimitMs,
             (unsigned long)expectedStopMs);
    TEST_ASSERT_TRUE_MESSAGE(module.extLastKickAtMs <= expectedStopMs + EXT_KICK_INTERVAL_MS,
                             message);
    TEST_ASSERT_TRUE_MESSAGE(module.extLastKickAtMs + EXT_KICK_INTERVAL_MS >= expectedStopMs,
                             message);

    // And they stayed stopped: every cadence slot after the stop was a withheld kick, which on
    // the device means the chip's own period now runs out unrefreshed.
    TEST_ASSERT_TRUE_MESSAGE(module.extStarvedCount > 0,
                             "no kick was ever withheld although the loop was long dead");
    const uint64_t runEndMs = module.clock.nowMs();
    const uint64_t expectedStarved = (runEndMs - expectedStopMs) / EXT_KICK_INTERVAL_MS;
    TEST_ASSERT_TRUE_MESSAGE(module.extStarvedCount + 2 >= expectedStarved,
                             "kicks resumed after the loop had been declared dead");
}

// --------------------------------------------------------------------------------------------
// The refusal latches. A loop silent past the threshold on a module still running means two
// mechanisms failed at once - the stall itself, and the task watchdog that should have reset
// the module before the gate's limit ran out. A later "revival" of such a loop earns no trust:
// the kicks stay stopped and the chip resets the module into a clean start with the cause on
// record.
// --------------------------------------------------------------------------------------------

static void test_s13_a_revived_loop_does_not_win_the_kicks_back(void)
{
    ModuleUnderTest module;
    module.freezeLoopAtMs = 30000;
    module.freezeForMs = 200000; // the loop revives at 230 s - after the 210 s stall limit
    module.startArduinoLoopTask();
    module.startExternalKickerTask();

    module.scheduler.runForMs(400000);

    TEST_ASSERT_FALSE(module.scheduler.stepBudgetExhausted());

    const uint64_t stallLimitMs = (uint64_t)WDT_TIMEOUT_S * 1000UL;
    const uint64_t expectedStopMs = module.freezeLoopAtMs + stallLimitMs;
    const uint64_t reviveAtMs = module.freezeLoopAtMs + module.freezeForMs;

    // The case happened: the gate refused before the loop came back, and the loop did come
    // back and beat again.
    TEST_ASSERT_TRUE_MESSAGE(reviveAtMs > expectedStopMs,
                             "scenario error: the loop revived before the stall limit ran out");
    TEST_ASSERT_TRUE_MESSAGE(module.taskWatchdog.fired(),
                             "the stall was never even seen by the task watchdog model");
    TEST_ASSERT_TRUE_MESSAGE(
        module.scheduler.lastWatchdogFeedAtMs(LOOP_TASK_NAME) > reviveAtMs,
        "scenario error: the revived loop never fed again");

    // The claim: no kick after the stop, revival or not.
    char message[256];
    snprintf(message, sizeof(message),
             "the last kick was at %lu ms; the kicks stopped at ~%lu ms and the loop revived "
             "at %lu ms - a kick after the stop means the gate was talked out of firing",
             (unsigned long)module.extLastKickAtMs,
             (unsigned long)expectedStopMs,
             (unsigned long)reviveAtMs);
    TEST_ASSERT_TRUE_MESSAGE(module.extLastKickAtMs <= expectedStopMs + EXT_KICK_INTERVAL_MS,
                             message);
    TEST_ASSERT_TRUE_MESSAGE(module.loopGate.latched(),
                             "the gate did not latch although it refused kicks");
}

// --------------------------------------------------------------------------------------------
// The gate's arithmetic, at the millisecond. The kicker samples the clock and then asks; the
// control loop runs on another core and can record its beat in between, so the gate is regularly
// asked about a "now" that is older than the last beat it holds. That is a live loop reporting
// itself, not a stall - and reading it as a stall latches the gate for good and sends a healthy
// module through the chip's reset. The same subtraction also has to keep working where millis()
// wraps, since the two values it takes then sit on either side of the boundary.
// --------------------------------------------------------------------------------------------

static void test_s13_a_beat_newer_than_the_kickers_clock_is_not_a_stall(void)
{
    LoopLivenessGate gate((uint32_t)WDT_TIMEOUT_S * 1000UL);

    // The kicker read the clock at this millisecond; the loop completed an iteration one
    // millisecond later, before the kicker got to ask.
    const uint32_t kickerReadMs = 1000000;
    gate.noteAlive(kickerReadMs + 1);

    TEST_ASSERT_TRUE_MESSAGE(gate.allowKick(kickerReadMs),
                             "the gate withheld a kick from a loop that had just beaten: a beat "
                             "one millisecond ahead of the kicker's clock reading was measured "
                             "as a silence of the whole counter range");
    TEST_ASSERT_FALSE_MESSAGE(gate.latched(),
                              "the gate latched on a live loop - the latch is permanent, so this "
                              "is a hardware reset of a healthy module");
}

static void test_s13_a_loop_silent_past_the_limit_still_latches_the_gate(void)
{
    const uint32_t stallLimitMs = (uint32_t)WDT_TIMEOUT_S * 1000UL;
    LoopLivenessGate gate(stallLimitMs);

    const uint32_t beatMs = 1000000;
    gate.noteAlive(beatMs);

    TEST_ASSERT_TRUE_MESSAGE(gate.allowKick(beatMs + stallLimitMs),
                             "the gate refused at exactly the stall limit - the limit is the "
                             "longest silence still vouched for");
    TEST_ASSERT_FALSE_MESSAGE(gate.allowKick(beatMs + stallLimitMs + 1),
                              "a loop silent past the stall limit still got its kick");
    TEST_ASSERT_TRUE_MESSAGE(gate.latched(), "the refusal did not latch");
}

static void test_s13_the_gate_measures_across_the_millis_wraparound(void)
{
    const uint32_t stallLimitMs = (uint32_t)WDT_TIMEOUT_S * 1000UL;
    const uint32_t beforeWrapMs = 0xFFFFFFFFUL - 49; // 50 ms of counter left

    // A beat 50 ms before the counter wraps and a kick 50 ms after it: 100 ms of silence, not
    // the 49.7 days the raw difference of the two numbers suggests.
    LoopLivenessGate live(stallLimitMs);
    live.noteAlive(beforeWrapMs);
    TEST_ASSERT_TRUE_MESSAGE(live.allowKick(beforeWrapMs + 100),
                             "the gate read the millisecond counter wrapping as a stall");
    TEST_ASSERT_FALSE(live.latched());

    // The same race, on the boundary: the kicker's clock reading is the last millisecond before
    // the wrap, the loop's beat the first one after it.
    LoopLivenessGate raced(stallLimitMs);
    raced.noteAlive(2);
    TEST_ASSERT_TRUE_MESSAGE(raced.allowKick(0xFFFFFFFFUL - 2),
                             "a beat that landed just past the wraparound was measured as a stall");
    TEST_ASSERT_FALSE(raced.latched());

    // And a real stall that starts before the boundary and ends after it is still caught.
    LoopLivenessGate stalled(stallLimitMs);
    stalled.noteAlive(beforeWrapMs);
    TEST_ASSERT_FALSE_MESSAGE(stalled.allowKick(beforeWrapMs + stallLimitMs + 1),
                              "a stall spanning the wraparound was not caught");
    TEST_ASSERT_TRUE_MESSAGE(stalled.latched(), "the refusal did not latch across the wraparound");
}

// --------------------------------------------------------------------------------------------
// The structure republish, in the blocking form it keeps for a caller wired without an outbox:
// it feeds the calling task's watchdog entry between its attempts. One attempt is a bounded legal
// pause; three back to back would exceed the budget the threshold is computed from, so the gap
// the watchdog sees must be one attempt, never the whole retry loop.
//
// This is no longer how the device runs it - globals.h gives the use case the outbox, and the
// POST is made on the network task with the outbox's own retry budget. The branch is kept, so
// the claim about it is kept too: it is what a caller assembled without an outbox gets, and a
// retry loop that stopped feeding would be a reboot on a slow server.
// --------------------------------------------------------------------------------------------

namespace {

class StubSensorStructure : public ISensorStructureUseCase {
public:
    void addSensor(Node&, const SensorEntity&) override {}
    void addMultiChannelModbusSensor(Node&, const SensorEntity&) override {}
    void addMultiChannelI2CSensor(Node&, const SensorEntity&) override {}
    void addSingleChannelSensor(Node&, const SensorEntity&) override {}
    void addVirtualChannelSensors(Node&, const SensorEntity&) override {}
};

} // namespace

static void test_s13_the_republish_feeds_its_caller_between_attempts(void)
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockHybridAPIService api;
    MockSensorRepository sensorRepository;
    MockArduinoAdapter arduino;
    StubSensorStructure sensorStructure;
    ModuleStructure structure;
    structure.nodes.push_back(Node());

    api.sendModuleStructureReturnValue = false; // the server refuses every attempt

    RepublishModuleStructureUseCase useCase(&structure, &sensorRepository, &api,
                                            &sensorStructure, &logger, &debugSystem, &arduino);

    TEST_ASSERT_FALSE(useCase.execute("watchdog-test"));

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, api.sendToServerModuleStructureCallCount,
                                  "the republish did not spend its three attempts");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, arduino.feedTaskWatchdogCount,
                                  "the caller was not vouched for between the attempts - "
                                  "three back-to-back calls are one unfed gap");
}

extern "C" void run_s13_watchdog_tests(void)
{
    printf("\n----- Running S13 Watchdog Tests -----\n");

    RUN_TEST(test_s13_a_frozen_loop_is_caught_within_the_threshold);
    RUN_TEST(test_s13_legal_long_operations_keep_the_watchdog_quiet);
    RUN_TEST(test_s13_an_address_write_feeds_its_task_between_the_steps);
    RUN_TEST(test_s13_an_address_write_that_power_cycles_feeds_its_task_too);
    RUN_TEST(test_s13_the_kicks_stop_when_the_loop_stalls_and_not_before);
    RUN_TEST(test_s13_a_revived_loop_does_not_win_the_kicks_back);
    RUN_TEST(test_s13_a_beat_newer_than_the_kickers_clock_is_not_a_stall);
    RUN_TEST(test_s13_a_loop_silent_past_the_limit_still_latches_the_gate);
    RUN_TEST(test_s13_the_gate_measures_across_the_millis_wraparound);
    RUN_TEST(test_s13_the_republish_feeds_its_caller_between_attempts);

    printf("\n----- S13 Watchdog Tests Complete -----\n");
}
