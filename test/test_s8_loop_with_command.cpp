// Scenario S8 - a command arrives while the server hangs, and the control loop keeps running.
//
// The last road on which the control loop still walked into the network itself. MqttManager::loop()
// takes an inbound message off its queue and dispatches it *on the calling task*
// (MqttManager::loop() -> processMessage() -> CommandTopicHandler -> CommandService::process()), so without the off-loop path
// every command handled by this module would be handled on the control loop: a GET for the
// authoritative list, the execution, and a POST to report - each of them allowed 30 s per attempt
// and, in getModuleCommand()/getDeviceCommand(), three attempts of its own.
//
// What is under test is production code. The real LoopServiceConfigureDone drives the real
// CommandService, the real DeviceCommandExecutionUseCase, the real DeviceService, the real Outbox
// with its own network task, and the real SensorAcquisitionService driving the real SensorReader,
// ModbusSensorReader and RawBusAccessAdapter over a simulated RS-485 line. The inbound MQTT
// message goes through the real CommandTopicHandler and the real ArduinoJsonConverter. Replaced are only
// the outside world - a server that does not answer, a bus whose devices do not answer - and time.
//
// ### Reproducing the red
//
// There is deliberately no "flip this line" switch here, because the synchronous path does not
// exist in production code and writing one into the test would prove a copy of it rather
// than the real code. The red is a build where CommandService::process() fetches and
// executes on the calling task: the same scenario written against that API measures a worst step
// of the whole command duration - tens of seconds against a silent server - instead of one loop
// period.

#include <unity.h>

#include "Application/Services/CommandService.h"
#include "Application/Services/Devices/DeviceService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Core/Domain/Data/Models/DeviceModel.h"
#include "Core/Domain/UseCases/DeviceControl/DeviceCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCaseFactory.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
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
using hostsim::SimRs485Bus;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// The same 50 ms S1, S6 and S7 are held to: a loop that never pauses longer than that reacts to a
// sensor reading or a button within one human-imperceptible step.
const uint32_t MAX_ACCEPTED_STEP_MS = 50;

// One attempt against a server that has stopped answering: RestApiService sets a 30 s timeout.
const uint32_t ATTEMPT_TIMEOUT_MS = 30000;

// What one raw ModbusRead costs against a device that does not answer: the timeout
// ModuleCommandExecutionUseCase passes to IRawBusAccess.
const uint32_t RAW_READ_TIMEOUT_MS = 1000;

// One silent channel of the sweep, read off ModbusSensorReader and SensorReader rather than
// chosen: three attempts of kInterFrameGapMs (5) + kResponseWindowMs (295), with SensorReader's
// 100 ms between attempts.
const uint32_t FRAMES_PER_SILENT_CHANNEL = 3;
const uint32_t CHANNELS_PER_SENSOR = 3;
const uint32_t FRAMES_PER_SILENT_SENSOR = FRAMES_PER_SILENT_CHANNEL * CHANNELS_PER_SENSOR;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "s8-module";
const char* const DEVICE_UID = "relay-0";

// The relay pin MockModuleConfig hands out for channel 0.
const uint8_t RELAY_PIN_0 = 1;

// A register nothing in the sweep asks for, so a raw request is recognisable among the frames on
// the line.
const uint16_t RAW_MARKER_REGISTER = 0x1234;

// --- the outside world ------------------------------------------------------------------------

/**
 * @brief A server that can be made to hang, to refuse, or to answer.
 *
 * Everything below the API is production code; this is the far end of the socket.
 */
class SimServer : public MockHybridAPIService {
public:
    explicit SimServer(HostTaskScheduler& scheduler) : _scheduler(scheduler) {}

    /** How long every call takes. 0 answers instantly. */
    uint32_t callDelayMs = 0;
    /** false: every call fails after callDelayMs, as an unreachable server does. */
    bool reachable = true;

    /** Handed out by the next getDeviceCommand(), then cleared - as `status=new` behaves. */
    std::vector<DeviceCommandDto> pendingDeviceCommands;

    uint32_t getDeviceCommandCalls = 0;
    uint64_t timeInsideCallsMs = 0;
    std::vector<DeviceCommandUpdateDto> deviceCommandReports;
    std::vector<CommandResultDto> commandReports;
    std::vector<RawIoResultDto> rawIoReports;
    std::vector<std::vector<ModuleCommandUpdateDto>> moduleCommandReports;
    /** Module commands handed out by the next getModuleCommand(), then cleared. */
    std::vector<ModuleCommandDto> pendingModuleCommands;
    uint32_t getModuleCommandCalls = 0;

    std::vector<DeviceCommandDto> getDeviceCommand(bool* httpFailed = nullptr) override
    {
        getDeviceCommandCalls++;
        spendTime();

        if (httpFailed != nullptr)
        {
            *httpFailed = !reachable;
        }
        if (!reachable)
        {
            return std::vector<DeviceCommandDto>();
        }

        std::vector<DeviceCommandDto> answer;
        answer.swap(pendingDeviceCommands);
        return answer;
    }

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

    bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& update) override
    {
        spendTime();
        if (!reachable)
        {
            return false;
        }
        deviceCommandReports.push_back(update);
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

    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        spendTime();
        if (!reachable)
        {
            return false;
        }
        // Every batch, in the order the network task sent it - not just the last one. The order is
        // the point: an "InProgress" that arrives after the "Done" it precedes is a backend that
        // shows a finished command as still running.
        moduleCommandReports.push_back(updates);
        return true;
    }

    bool sendRawIoResult(const RawIoResultDto& result) override
    {
        spendTime();
        if (!reachable)
        {
            return false;
        }
        rawIoReports.push_back(result);
        return true;
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

/**
 * @brief The inbound half of MqttManager: a queue drained on the task that calls loop().
 *
 * That is the whole reason command handling can be the control loop's problem, so the test
 * keeps it: the payload is parsed by the real ArduinoJsonConverter and dispatched by the real
 * CommandTopicHandler, on the control loop's own task, exactly as MqttManager::loop() does.
 */
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
                _handler->handleMessage("GrowDirectorMqttTopic/s8-module",
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
    int loopCount = 0;
    bool init() override { return true; }
    void loop() override { loopCount++; }
    bool readAllSensors() override { return true; }
    bool readSensor(SensorEntity) override { return true; }
};

class CountingRuleService : public IRuleService {
public:
    int loopCount = 0;
    bool init() override { return true; }
    void loop() override { loopCount++; }
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

std::string deviceCommandJson(const char* state, int commandId, bool switchBack, int seconds)
{
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"DeviceCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"deviceUid\":\"%s\",\"commandId\":%d,\"requestedState\":\"%s\","
             "\"needToSwitchBack\":%s,\"secondsToSwitchBack\":%d}",
             MODULE_UID, DEVICE_UID, commandId, state, switchBack ? "true" : "false", seconds);
    return std::string(buffer);
}

std::string modbusReadJson()
{
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"ModbusRead\",\"options\":{\"channel\":0,\"address\":1,"
             "\"function\":3,\"register\":%u,\"count\":1}}",
             MODULE_UID, (unsigned)RAW_MARKER_REGISTER);
    return std::string(buffer);
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

ModuleCommandDto makeServerModuleCommand(const std::string& uid, const char* ssid)
{
    ModuleCommandDto command;
    command.uid = uid;
    command.command = "AddWifi";
    command.createdAt = "2026-08-15T10:00:00Z";
    command.options = std::string("{\"ssid\":\"") + ssid + "\",\"password\":\"secret\"}";
    return command;
}

DeviceCommandDto makeServerCommand(const char* state, int id, bool switchBack, int seconds)
{
    DeviceCommandDto command;
    command.uid = DEVICE_UID;
    command.requestedState = state;
    command.id = id;
    command.needToSwitchBack = switchBack;
    command.secondsToSwitchBack = seconds;
    // Left empty on purpose: an empty createdDate is what makes the age check skip, and the age
    // check is not what this scenario is about.
    command.createdDate = "";
    return command;
}

SensorEntity makeModbusSensor(const std::string& uid, int port, uint8_t address)
{
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.protocol = "ModBus";
    sensor.model = "S8-MULTI";
    sensor.sensorType = "temperature";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.pin = port;
    sensor.modbusDeviceAddress = address;
    sensor.baudRate = 9600;

    for (int channel = 1; channel <= static_cast<int>(CHANNELS_PER_SENSOR); channel++)
    {
        ModbusRegisterConfig config;
        config.functionCode = 0x03;
        config.startRegister = static_cast<uint16_t>(0x0100 + channel);
        config.numRegisters = 1;
        config.multiplier = 0.1f;
        config.offset = 0.0f;
        config.channel = channel;
        config.channelName = "ch" + std::to_string(channel);
        config.sensorType = "temperature";
        sensor.modbusRegisterConfigs.push_back(config);
    }

    return sensor;
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
    MockNTPTimeAdapter timeAdapter;
    MockWiFiManager wifiManager;
    ArduinoJsonConverter jsonConverter;
    ModuleStructure moduleStructure;

    ModbusSensorReader modbusSensorReader;
    RawBusAccessAdapter rawBusAccess;
    SensorReader sensorReader;
    SensorAcquisitionService acquisition;
    ModuleCommandExecutionUseCase moduleCommandUseCase;

    DeviceModel deviceModel;
    DeviceCommandExecutionUseCase deviceCommandUseCase;
    ProcessDeviceStateUseCaseFactory deviceStateFactory;
    DeviceService deviceService;

    SimServer server;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    NetworkCommandRunner networkRunner;

    CommandService commandService;
    CommandTopicHandler topicHandler;
    ScriptedMqttManager mqttManager;

    CountingSensorService sensorService;
    CountingRuleService ruleService;
    LoopServiceConfigureDone loopService;

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
          moduleCommandUseCase(&logger, &debugSystem, nullptr, nullptr, &arduino,
                               &sensorRepository, nullptr, &sensorReader, &moduleConfig,
                               &settings, nullptr, nullptr, &moduleStructure, nullptr,
                               &wifiManager, nullptr, &rawBusAccess),
          deviceModel(&moduleConfig, &moduleStructure),
          deviceCommandUseCase(&deviceModel, &arduino, &moduleStructure, &logger, &debugSystem,
                               &moduleConfig),
          deviceStateFactory(&logger, &debugSystem, &moduleConfig, &arduino, &deviceModel),
          deviceService(&moduleConfig, &moduleStructure, &deviceModel, nullptr, &arduino,
                        &deviceStateFactory, nullptr, &logger, &debugSystem),
          server(scheduler),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          networkRunner(&scheduler, &logger, &debugSystem),
          commandService(&deviceCommandUseCase, &moduleCommandUseCase, nullptr, &moduleStructure,
                         &logger, &debugSystem, &timeAdapter, &jsonConverter),
          topicHandler(&logger, &debugSystem, MODULE_UID, &commandService, &jsonConverter)
    {
        debugSystem.featureEnabled = false; // keep tens of thousands of poll lines out of the log

        // A module command carries a createdAt and CommandService refuses one it cannot date.
        timeAdapter.setEpochTime(1755000000);
        timeAdapter.setCommandTimeValid(true);

        settings.setUser("s8@example.com");
        settings.setAccountUid("s8-account");
        // MODULE_TYPE_SENSOR because a raw bus command is only accepted on a channel below
        // getSensorPinCount(), which MockModuleConfig reports as 0 for a pure device module.
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setDeviceCount(1);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setSettings(&settings);

        // init() first: DeviceModel::init() clears its device list and rebuilds it from the module
        // structure, so a device added before it would be deleted again.
        deviceService.init();

        // Owned by DeviceModel: its destructor deletes what it was given.
        device = new DeviceDigitalOut();
        device->deviceUid = DEVICE_UID;
        device->deviceType = DEVICE_TYPE_DIGITAL;
        device->state = DEVICE_STATE_OFF;
        device->isAuto = false;
        device->isManual = false;
        device->workByRule = false;
        device->workManual = false;
        device->needToSwitchBack = false;
        device->needReportingRuleUid = false;
        device->commandId = 0;
        device->ruleUid = nullptr;
        device->isRuleTurnOnDevice = false;
        deviceModel.addDevice(device);

        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        rawBusAccess.setBusLock(&busLock);
        // The line as a booted module has it: the boot begins the UARTs before the first frame
        // (ModuleSetupService::setupSensorBus()), and a line nobody began carries nothing.
        sensorReader.init();

        acquisition.setRawCommandExecutor(&moduleCommandUseCase);
        // On the device SensorService::loop() does this on its polling tick; here the sensor
        // service is a counting stand-in, so the fixture starts the task itself.
        acquisition.ensureRunning();
        commandService.setOutbox(&outbox);
        commandService.setBusCommandExecutor(&acquisition);
        networkRunner.setExecutor(&moduleCommandUseCase);
        commandService.setNetworkCommandRunner(&networkRunner);
        mqttManager.setHandler(&topicHandler);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setCommandService(&commandService);
        loopService.setDeviceService(&deviceService);
        loopService.setSensorService(&sensorService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
    }

    /** Put a silent Modbus sensor on port 0, so that the acquisition task is busy on the line. */
    void addSilentSensorOnPort0()
    {
        SimRs485Bus::Device silent;
        silent.address = 1;
        silent.silent = true;
        port0.addDevice(silent);

        std::vector<SensorEntity> sensors;
        sensors.push_back(makeModbusSensor("mod.0", 0, 1));
        acquisition.setSensors(sensors);
    }

    // One task, mirroring main.cpp: loop() calls loopService->loop() and nothing else.
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

    DeviceDigitalOut* device = nullptr;
};

} // namespace

// --------------------------------------------------------------------------------------------
// The claim under test, on the network side.
// --------------------------------------------------------------------------------------------

static void test_s8_the_loop_stays_bounded_while_a_command_arrives_and_the_server_hangs(void)
{
    ModuleUnderTest module;
    module.server.callDelayMs = ATTEMPT_TIMEOUT_MS;
    module.server.reachable = false;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // A command every five seconds for two minutes, into a server that answers nothing. Each one
    // would cost the control loop, handled on it, a GET of up to 90 s and a POST of up to 90 s.
    uint32_t delivered = 0;
    module.scheduler.createTask("mqtt-inbound", 64 * 1024, 1, [&]() {
        while (true)
        {
            module.mqttManager.deliver(deviceCommandJson("on", 100 + (int)delivered, false, 0));
            delivered++;
            module.scheduler.delayMs(5000);
        }
    });

    module.scheduler.runForMs(120000);
    cadence.closeAt(module.clock.nowMs());

    // --- the run has to be sound before its verdict means anything --------------------------
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");
    TEST_ASSERT_TRUE_MESSAGE(module.mqttManager.dispatched >= 20,
                             "hardly any command was dispatched - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.server.getDeviceCommandCalls >= 2,
                             "the command fetch never reached the network at all");

    char busyMessage[224];
    snprintf(busyMessage, sizeof(busyMessage),
             "the server stand-in was only inside a call for %lu of 120000 ms - the network was "
             "not meaningfully busy, so the scenario was not reproduced",
             (unsigned long)module.server.timeInsideCallsMs);
    TEST_ASSERT_TRUE_MESSAGE(module.server.timeInsideCallsMs >= 60000, busyMessage);

    // --- the actual claim ---------------------------------------------------------------------
    char message[288];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms), with %lu "
             "commands dispatched into a server that hangs %lu ms per call",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS,
             (unsigned long)module.mqttManager.dispatched,
             (unsigned long)ATTEMPT_TIMEOUT_MS);

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);
}

// --------------------------------------------------------------------------------------------
// The same claim without the get-out: a raw command against a device that is silent.
// --------------------------------------------------------------------------------------------

static void test_s8_the_loop_stays_bounded_while_a_raw_command_waits_for_a_silent_device(void)
{
    ModuleUnderTest module;
    module.addSilentSensorOnPort0();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    uint32_t delivered = 0;
    module.scheduler.createTask("mqtt-inbound", 64 * 1024, 1, [&]() {
        while (true)
        {
            module.mqttManager.deliver(modbusReadJson());
            delivered++;
            module.scheduler.delayMs(5000);
        }
    });

    module.scheduler.runForMs(60000);
    cadence.closeAt(module.clock.nowMs());

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_FALSE_MESSAGE(module.busLock.deadlockDetected(),
                              "a task waited for the bus lock in a way that could never be served");
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100, "the control loop barely ran");
    TEST_ASSERT_TRUE_MESSAGE(module.mqttManager.dispatched >= 10,
                             "hardly any raw command was dispatched");

    // Somebody really did spend the time on the line, and it really was silent.
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 5,
                             "the acquisition task executed almost no raw command");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.sentFrames.size() > FRAMES_PER_SILENT_SENSOR,
                             "no raw request ever went out on port 0 - nothing slow was exercised");

    char message[288];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms), with %lu "
             "raw ModbusRead commands issued against a device that never answers (%lu ms each)",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS,
             (unsigned long)module.acquisition.busCommandCount(),
             (unsigned long)RAW_READ_TIMEOUT_MS);

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);
}

// --------------------------------------------------------------------------------------------
// "What must disappear": a raw bus command no longer makes the control loop touch the line.
//
// The name is narrow on purpose: this covers the raw commands. `ScanSensors`, `PingPin` and
// `SetSensorModel` run on the acquisition task too, and their own no-lock-from-the-loop
// assertions live in test_s12_complex_commands_off_loop.cpp next to the scenarios that
// exercise them.
// --------------------------------------------------------------------------------------------

static void test_no_bus_lock_is_taken_from_the_control_loop_for_a_raw_command(void)
{
    ModuleUnderTest module;
    module.addSilentSensorOnPort0();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.scheduler.createTask("mqtt-inbound", 64 * 1024, 1, [&]() {
        while (true)
        {
            module.mqttManager.deliver(modbusReadJson());
            module.scheduler.delayMs(2000);
        }
    });

    module.scheduler.runForMs(40000);

    TEST_ASSERT_TRUE_MESSAGE(module.busLock.takesBy(SensorAcquisitionService::TASK_NAME) > 0,
                             "the acquisition task never took the line - there was nothing to observe");

    char message[224];
    snprintf(message, sizeof(message),
             "the control loop took the RS-485 lock %lu times and waited for it %lu times, "
             "while %lu raw commands were issued from it",
             (unsigned long)module.busLock.takesBy(LOOP_TASK_NAME),
             (unsigned long)module.busLock.waitsBy(LOOP_TASK_NAME),
             (unsigned long)module.acquisition.busCommandCount());

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.takesBy(LOOP_TASK_NAME), message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.waitsBy(LOOP_TASK_NAME), message);
}

// --------------------------------------------------------------------------------------------
// The command is executed and the report reaches the server.
// --------------------------------------------------------------------------------------------

static void test_a_device_command_is_executed_and_its_report_reaches_the_server(void)
{
    ModuleUnderTest module;
    module.server.pendingDeviceCommands.push_back(makeServerCommand("on", 4475, false, 0));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(deviceCommandJson("on", 4475, false, 0));
    module.scheduler.runForMs(5000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.server.getDeviceCommandCalls,
                                     "the authoritative list was not fetched exactly once");

    // The GPIO, not a flag: the relay for channel 0 is energised.
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.arduino.digitalOutputs[RELAY_PIN_0],
                                    "the relay pin was not driven high");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4475, module.device->commandId,
                                  "the executed command was not the one the server sent");

    // The report as the server saw it, not as the module recorded it.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.deviceCommandReports.size(),
                                     "the execution report never reached the server");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("done", module.server.deviceCommandReports[0].status.c_str(),
                                     "the report did not say the command was done");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_UID, module.server.deviceCommandReports[0].uid.c_str(),
                                     "the report was about a different device");
}

// --------------------------------------------------------------------------------------------
// The raw transaction is executed by the acquisition task and its result comes back.
// --------------------------------------------------------------------------------------------

static void test_a_raw_command_is_executed_by_the_acquisition_task_and_reported(void)
{
    ModuleUnderTest module;

    // A device that answers, so that the result carries data rather than a timeout.
    SimRs485Bus::Device responder;
    responder.address = 1;
    responder.registerValues.assign(RAW_MARKER_REGISTER + 1, 0);
    responder.registerValues[RAW_MARKER_REGISTER] = 0x00AB;
    module.port0.addDevice(responder);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(modbusReadJson());
    module.scheduler.runForMs(5000);

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, module.acquisition.busCommandCount(),
                                     "the acquisition task did not execute the raw command");
    TEST_ASSERT_TRUE_MESSAGE(module.busLock.takesBy(SensorAcquisitionService::TASK_NAME) > 0,
                             "the transaction did not go through the bus lock from that task");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.takesBy(LOOP_TASK_NAME),
                                     "the control loop took the line for a raw command");

    TEST_ASSERT_TRUE_MESSAGE(module.port0.sentFrames.size() >= 1,
                             "no frame reached the simulated line");

    // The two messages the backend expects, in the same shape as before, as the server saw them.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.commandReports.size(),
                                     "the raw command acknowledgement never reached the server");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("accepted", module.server.commandReports[0].status.c_str(),
                                     "the acknowledgement did not say accepted");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ModbusRead", module.server.commandReports[0].command.c_str(),
                                     "the acknowledgement named a different command");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.rawIoReports.size(),
                                     "the bytes read off the line never reached the server");
    TEST_ASSERT_FALSE_MESSAGE(module.server.rawIoReports[0].dataHex.empty(),
                              "the raw IO result carried no data");
}

// --------------------------------------------------------------------------------------------
// Second half of the same: a raw command issued mid-sweep waits its turn instead of cutting in.
// --------------------------------------------------------------------------------------------

static void test_a_raw_command_waits_for_its_turn_and_does_not_break_the_sweep(void)
{
    ModuleUnderTest module;
    module.addSilentSensorOnPort0();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // 2 s in: the sweep of the single three-channel silent sensor costs 3 x 1100 ms, so this lands
    // in the middle of it.
    module.scheduler.createTask("mqtt-inbound", 64 * 1024, 1, [&]() {
        module.scheduler.delayMs(2000);
        module.mqttManager.deliver(modbusReadJson());
    });

    module.scheduler.runForMs(12000);

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, module.acquisition.busCommandCount(),
                                     "the raw command was never executed");

    // Where did the raw request land among the frames on port 0? A silent channel costs three
    // frames and a sensor has three channels, so a sweep of this sensor is nine frames. If the
    // raw request cut into the sweep its index would not be a multiple of nine.
    size_t rawIndex = module.port0.sentFrames.size();
    for (size_t i = 0; i < module.port0.sentFrames.size(); i++)
    {
        const std::vector<uint8_t>& frame = module.port0.sentFrames[i];
        if (frame.size() >= 4 && frame[2] == (uint8_t)(RAW_MARKER_REGISTER >> 8) &&
            frame[3] == (uint8_t)(RAW_MARKER_REGISTER & 0xFF))
        {
            rawIndex = i;
            break;
        }
    }

    char message[256];
    snprintf(message, sizeof(message),
             "the raw request was frame #%lu of %lu on port 0; a sweep of the one silent "
             "three-channel sensor is %lu frames, so a request that waited for its turn lands on "
             "a multiple of %lu",
             (unsigned long)rawIndex,
             (unsigned long)module.port0.sentFrames.size(),
             (unsigned long)FRAMES_PER_SILENT_SENSOR,
             (unsigned long)FRAMES_PER_SILENT_SENSOR);

    TEST_ASSERT_TRUE_MESSAGE(rawIndex < module.port0.sentFrames.size(),
                             "the raw request never reached the line");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)(rawIndex % FRAMES_PER_SILENT_SENSOR), message);
}

// --------------------------------------------------------------------------------------------
// Two commands for one device are applied in the order they arrived.
// --------------------------------------------------------------------------------------------

static void test_two_commands_for_one_device_are_applied_in_the_order_they_arrived(void)
{
    ModuleUnderTest module;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // First: switch on. The signal arrives, the fetch answers with the command.
    module.server.pendingDeviceCommands.push_back(makeServerCommand("on", 1, false, 0));
    module.mqttManager.deliver(deviceCommandJson("on", 1, false, 0));
    module.scheduler.runForMs(3000);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.arduino.digitalOutputs[RELAY_PIN_0],
                                    "the first command did not switch the relay on");

    // Second: switch off.
    module.server.pendingDeviceCommands.push_back(makeServerCommand("off", 2, false, 0));
    module.mqttManager.deliver(deviceCommandJson("off", 2, false, 0));
    module.scheduler.runForMs(3000);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, module.arduino.digitalOutputs[RELAY_PIN_0],
                                    "the second command did not switch the relay off");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, module.device->commandId,
                                  "the device kept the first command's id - the order was lost");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.deviceCommandReports.size(),
                                     "both commands should have been reported");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("done", module.server.deviceCommandReports[0].status.c_str(),
                                     "the first report was not a success");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("done", module.server.deviceCommandReports[1].status.c_str(),
                                     "the second report was not a success");
}

static void test_a_list_of_two_commands_is_applied_in_list_order(void)
{
    ModuleUnderTest module;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // Both commands come back in one fetch, as they do when two signals arrive close together and
    // the second fetch folds into the first. The list order is the arrival order.
    module.server.pendingDeviceCommands.push_back(makeServerCommand("on", 11, false, 0));
    module.server.pendingDeviceCommands.push_back(makeServerCommand("off", 12, false, 0));
    module.mqttManager.deliver(deviceCommandJson("on", 11, false, 0));
    module.scheduler.runForMs(3000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.server.getDeviceCommandCalls,
                                     "the list was fetched more than once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, module.arduino.digitalWriteCounts[RELAY_PIN_0],
                                  "the relay was not driven twice - one of the two was skipped");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, module.arduino.digitalOutputs[RELAY_PIN_0],
                                    "the relay did not end in the second command's state");
    TEST_ASSERT_EQUAL_INT_MESSAGE(12, module.device->commandId,
                                  "the device did not end on the second command");
}

// --------------------------------------------------------------------------------------------
// The other half - MODULE_COMMAND_FETCH, end to end.
//
// AddWifi is the complex module command with the fewest collaborators: it needs a WiFi manager and
// nothing else, so what this test exercises is the fetch-execute-report path rather than the
// command's own machinery. The path is the same for every complex command.
// --------------------------------------------------------------------------------------------

static void test_a_module_command_is_fetched_executed_and_reported(void)
{
    ModuleUnderTest module;
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand("cmd-1", "greenhouse"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("AddWifi"));
    module.scheduler.runForMs(5000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.server.getModuleCommandCalls,
                                     "the authoritative module command list was not fetched exactly once");

    // The command really ran, and ran with the options the server sent - not the empty ones the
    // MQTT signal carried.
    TEST_ASSERT_TRUE_MESSAGE(module.wifiManager.addWifiNetworkCalled,
                             "the module command was never executed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("greenhouse", module.wifiManager.lastSsid.c_str(),
                                     "the command executed did not carry the server's options");

    // Two reports, in the order the backend needs them: InProgress for the accepted command, then
    // the batch with its outcome.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the result report");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("InProgress",
                                     module.server.moduleCommandReports[0][0].status.c_str(),
                                     "the first report was not InProgress");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("cmd-1", module.server.moduleCommandReports[0][0].uid.c_str(),
                                     "the InProgress report named a different command");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", module.server.moduleCommandReports[1][0].status.c_str(),
                                     "the outcome report did not say Done");
}

static void test_no_module_command_is_executed_when_the_server_is_silent(void)
{
    ModuleUnderTest module;
    module.server.reachable = false;
    module.server.callDelayMs = ATTEMPT_TIMEOUT_MS;
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand("cmd-2", "greenhouse"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("AddWifi"));

    // Long enough for the outbox to spend its whole retry budget on the fetch.
    module.scheduler.runForMs(180000);

    TEST_ASSERT_TRUE_MESSAGE(module.server.getModuleCommandCalls >= 2,
                             "the fetch was not even attempted more than once");
    // A complex command carries its parameters in the list, not in the signal, so there is no
    // payload fallback and there must not be one: executing on a guess is worse than not executing.
    TEST_ASSERT_FALSE_MESSAGE(module.wifiManager.addWifiNetworkCalled,
                              "a module command was executed although the list never arrived");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.server.moduleCommandReports.size(),
                                     "something was reported about a command that never ran");
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           "the loop blocked while the module command fetch failed");
}

// --------------------------------------------------------------------------------------------
// An execution report is never dropped in silence, and never overtakes another.
//
// The event ring holds eight. One module command list of twenty produces twenty-one reports -
// twenty InProgress, one batch of outcomes - all of them inside a single control-loop iteration,
// so the ring cannot drain in between and thirteen are refused. The claim is that the server still
// sees all twenty-one, in the order they were produced.
//
// **Do not lower commandCount.** The list size is not decoration, it is what gives the ordering
// assertion teeth, and the boundary was measured rather than guessed: with the ordering mutation
// pinned (a refused report rotated to the back of the queue instead of holding the head), the test
// survives at 9 and 10 commands, first catches it at 11, and its failure signature settles from 15
// on. Twenty leaves nine elements of margin over that boundary of eleven. Ten - the number this
// test was first written with - lets three refused reports rotate back into their own order by
// accident, and the mutation passes unnoticed.
// --------------------------------------------------------------------------------------------

static void test_reports_refused_by_a_full_ring_are_re_offered_and_keep_their_order(void)
{
    ModuleUnderTest module;

    // Slow enough that the ring cannot drain while the reports are being produced.
    module.server.callDelayMs = 1000;

    // Twenty: twenty-one reports, thirteen of them refused by the eight-deep ring. Nine above the
    // measured detection boundary of eleven - see the note above this test before changing it.
    const size_t commandCount = 20;
    for (size_t i = 0; i < commandCount; i++)
    {
        module.server.pendingModuleCommands.push_back(
            makeServerModuleCommand("cmd-" + std::to_string(i), "greenhouse"));
    }

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandJson("AddWifi"));
    module.scheduler.runForMs(120000);

    // The premise: the ring really did overflow. Without this the test proves nothing about
    // what happens when it does.
    char refusalMessage[192];
    snprintf(refusalMessage, sizeof(refusalMessage),
             "the event ring (capacity %lu) never refused anything while %lu reports were produced "
             "in one iteration - this test is about the case that did not happen",
             (unsigned long)Outbox::EVENT_RING_CAPACITY, (unsigned long)(commandCount + 1));
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.refusedEventCount() > 0, refusalMessage);

    // The claim: nothing was lost.
    char lossMessage[224];
    snprintf(lossMessage, sizeof(lossMessage),
             "the server saw %lu of %lu reports; the outbox refused %lu and CommandService dropped %lu",
             (unsigned long)module.server.moduleCommandReports.size(),
             (unsigned long)(commandCount + 1),
             (unsigned long)module.outbox.refusedEventCount(),
             (unsigned long)module.commandService.droppedReportCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)(commandCount + 1),
                                     (uint32_t)module.server.moduleCommandReports.size(),
                                     lossMessage);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.commandService.droppedReportCount(),
                                     "CommandService dropped a report of its own");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.commandService.pendingReportCount(),
                                     "a report was still waiting when the run ended");

    // And nothing overtook anything: ten InProgress in command order, then the batch of outcomes.
    for (size_t i = 0; i < commandCount; i++)
    {
        char orderMessage[160];
        snprintf(orderMessage, sizeof(orderMessage),
                 "report #%lu was not the InProgress for cmd-%lu - the queue reordered under refusal",
                 (unsigned long)i, (unsigned long)i);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.moduleCommandReports[i].size(),
                                         orderMessage);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("InProgress",
                                         module.server.moduleCommandReports[i][0].status.c_str(),
                                         orderMessage);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(("cmd-" + std::to_string(i)).c_str(),
                                         module.server.moduleCommandReports[i][0].uid.c_str(),
                                         orderMessage);
    }

    const std::vector<ModuleCommandUpdateDto>& outcomes =
        module.server.moduleCommandReports[commandCount];
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)commandCount, (uint32_t)outcomes.size(),
                                     "the outcome batch did not carry every command");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", outcomes[0].status.c_str(),
                                     "the outcome batch did not arrive last, or did not say Done");
}

// --------------------------------------------------------------------------------------------
// The fallback: the server did not answer, so the command runs from the MQTT payload.
// --------------------------------------------------------------------------------------------

static void test_the_command_runs_from_the_mqtt_payload_when_the_server_is_silent(void)
{
    ModuleUnderTest module;
    module.server.reachable = false;
    module.server.callDelayMs = ATTEMPT_TIMEOUT_MS;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(deviceCommandJson("on", 777, false, 0));

    // Long enough for the outbox to spend its whole retry budget: four attempts of 30 s plus
    // 10.5 s of backoff.
    module.scheduler.runForMs(180000);

    TEST_ASSERT_TRUE_MESSAGE(module.server.getDeviceCommandCalls >= 2,
                             "the fetch was not even attempted more than once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.arduino.digitalOutputs[RELAY_PIN_0],
                                    "the relay was not driven from the MQTT payload after the "
                                    "fetch failed - the fallback path is gone");
    TEST_ASSERT_EQUAL_INT_MESSAGE(777, module.device->commandId,
                                  "the command executed was not the one in the MQTT payload");
}

// --------------------------------------------------------------------------------------------
// The auto switch-back still runs, and still runs on the control loop.
// --------------------------------------------------------------------------------------------

static void test_the_switch_back_timer_still_runs_on_the_control_loop(void)
{
    ModuleUnderTest module;
    module.server.pendingDeviceCommands.push_back(makeServerCommand("on", 55, true, 60));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(deviceCommandJson("on", 55, true, 60));
    module.scheduler.runForMs(5000);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.arduino.digitalOutputs[RELAY_PIN_0],
                                    "the relay was not switched on");
    TEST_ASSERT_TRUE_MESSAGE(module.device->needToSwitchBack,
                             "the switch-back flag did not survive the asynchronous path");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(60000, module.device->durationOn,
                                     "the switch-back duration did not survive");

    // Still on 40 s of virtual time later.
    module.scheduler.runForMs(40000);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.arduino.digitalOutputs[RELAY_PIN_0],
                                    "the relay went off before its time");

    // Past the sixty seconds, the control loop switches it back on its own.
    module.scheduler.runForMs(25000);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, module.arduino.digitalOutputs[RELAY_PIN_0],
                                    "the relay was never switched back");
}

extern "C" void run_s8_loop_with_command_tests(void)
{
    printf("\n----- Running S8 Loop With Command Tests -----\n");

    RUN_TEST(test_s8_the_loop_stays_bounded_while_a_command_arrives_and_the_server_hangs);
    RUN_TEST(test_s8_the_loop_stays_bounded_while_a_raw_command_waits_for_a_silent_device);
    RUN_TEST(test_no_bus_lock_is_taken_from_the_control_loop_for_a_raw_command);
    RUN_TEST(test_a_device_command_is_executed_and_its_report_reaches_the_server);
    RUN_TEST(test_a_raw_command_is_executed_by_the_acquisition_task_and_reported);
    RUN_TEST(test_a_raw_command_waits_for_its_turn_and_does_not_break_the_sweep);
    RUN_TEST(test_two_commands_for_one_device_are_applied_in_the_order_they_arrived);
    RUN_TEST(test_a_list_of_two_commands_is_applied_in_list_order);
    RUN_TEST(test_a_module_command_is_fetched_executed_and_reported);
    RUN_TEST(test_no_module_command_is_executed_when_the_server_is_silent);
    RUN_TEST(test_reports_refused_by_a_full_ring_are_re_offered_and_keep_their_order);
    RUN_TEST(test_the_command_runs_from_the_mqtt_payload_when_the_server_is_silent);
    RUN_TEST(test_the_switch_back_timer_still_runs_on_the_control_loop);

    printf("\n----- S8 Loop With Command Tests Complete -----\n");
}
