// Scenario S18 - a restart discovers its sensors while the module is already working.
//
// The shape under test: the boot describes the registry and starts the tasks; the discovery of
// what is actually on the RS-485 line - ping every stored entity, try the catalog on the free
// ports, resolve the address collisions, apply the per-model register writes - happens afterwards,
// on the task that owns the line, between the sweeps of the sensors the module already knows.
//
// What this file guards: none of that happens inside setup(), before the
// control loop exists. It would be five to six minutes on a stand with four ports, during which the module
// has no telemetry, no rules, no relays and no button, and its LED sits on solid - a state an
// installer cannot tell from a hang. A module that reboots after every added sensor, as this
// product's app instructs, would pay it every time.
//
// What is under test is production code: the real ModuleSetupService builds the structure, the
// real StartupSensorScanUseCase plans and applies the discovery, the real SensorAcquisitionService
// executes its steps on the real SensorReader over the real ModbusSensorReader on a simulated
// RS-485 line, the real LoopServiceConfigureDone drives the real CommandService and SensorService,
// the real SensorRepository holds the registry over an in-memory DAO, and the real Outbox carries
// the telemetry. Replaced are the outside world - the line, the server, MQTT - and time.
//
// ### Reproducing the red
//
// The red of the first test is the boot that scans in setup(): make the structure fill scan -
// verify every stored entity with sensorReader->scanSensor() and walk the catalog over the free
// pins inside ModuleSetupService::processSensorsFromRepository() - and the first assertion fails
// with hundreds of frames on the line before the control loop has run once, and the telemetry
// assertion fails because the first reading cannot be taken until the marathon is over.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/CommandService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/ModuleSetupService.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
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
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorDao.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/SimTaskWatchdog.h"
#include "Sim/VirtualClock.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using hostsim::HostBusLock;
using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimRs485Bus;
using hostsim::SimTaskWatchdog;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// The same bound S12 holds the loop to: with slow work in progress the worst control-loop step is
// one loop period, because the step depends on neither the line nor the network.
const uint32_t MAX_ACCEPTED_STEP_MS = CONTROL_PERIOD_MS;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "s18-module";
const int MODULE_PIN_COUNT = 4;

/** The port the module already has a sensor on; the other three are free and silent. */
const int KNOWN_PORT = 0;
const uint8_t KNOWN_ADDRESS = MODBUS_ADDRESS_MIN;

// Long enough for the whole discovery - three free ports against a two-model catalog, every probe
// unanswered - plus a dozen sweeps of the known sensor after it.
const uint64_t OBSERVATION_WINDOW_MS = 600000;

// --- the outside world --------------------------------------------------------------------------

/** @brief The far end of every call the module makes, and a record of what it was told. */
class SimServer : public MockHybridAPIService {
public:
    struct Update {
        uint64_t atMs = 0;
        size_t sensorCount = 0;
        size_t onlineCount = 0;
        std::vector<SensorDto> sensors;
    };

    std::vector<Update> updates;
    std::vector<ModuleCommandDto> pendingModuleCommands;
    std::vector<std::vector<ModuleCommandUpdateDto>> moduleCommandReports;
    std::vector<RawIoResultDto> rawIoReports;
    uint32_t structureSends = 0;

    /** @brief One structure the server took: when it had it, and the records of the module's node. */
    struct StructureSeen {
        uint64_t atMs = 0;
        std::vector<Sensor> sensors;
    };
    std::vector<StructureSeen> structures;

    /**
     * @brief How long a structure takes to go out, spent on the task that sends it.
     *
     * Zero by default: the server answers at once. A stand that asks what the module does while a
     * structure is still on its way sets it, and the scheduler to spend it on.
     */
    uint32_t structurePostMs = 0;
    HostTaskScheduler* scheduler = nullptr;

    VirtualClock* clock = nullptr;

    std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) override
    {
        if (httpFailed != nullptr)
        {
            *httpFailed = false;
        }
        std::vector<ModuleCommandDto> answer;
        answer.swap(pendingModuleCommands);
        return answer;
    }

    bool sendModuleUpdateData(const UpdateDto& update,
                              ApiPriority = ApiPriority::UNDEFINED) override
    {
        Update record;
        record.atMs = clock != nullptr ? clock->nowMs() : 0;
        record.sensorCount = update.sensors.size();
        record.sensors = update.sensors;
        for (size_t i = 0; i < update.sensors.size(); i++)
        {
            if (update.sensors[i].state == "online")
            {
                record.onlineCount++;
            }
        }
        updates.push_back(record);
        return true;
    }

    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& reports) override
    {
        moduleCommandReports.push_back(reports);
        return true;
    }

    bool sendRawIoResult(const RawIoResultDto& result) override
    {
        rawIoReports.push_back(result);
        return true;
    }

    bool sendToServerModuleStructure(const ModuleStructure& structure,
                                     ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        if (structurePostMs > 0 && scheduler != nullptr)
        {
            scheduler->delayMs(structurePostMs);
        }
        StructureSeen seen;
        seen.atMs = clock != nullptr ? clock->nowMs() : 0;
        if (!structure.nodes.empty())
        {
            seen.sensors = structure.nodes[0].sensors;
        }
        structures.push_back(seen);
        structureSends++;
        return MockHybridAPIService::sendToServerModuleStructure(structure, priority);
    }

    /** @brief When the module first reported a sensor of its own as online, or 0 if it never did. */
    uint64_t firstOnlineUpdateMs() const
    {
        for (size_t i = 0; i < updates.size(); i++)
        {
            if (updates[i].onlineCount > 0)
            {
                return updates[i].atMs;
            }
        }
        return 0;
    }
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
                _handler->handleMessage("GrowDirectorMqttTopic/s18-module",
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

class CountingRuleService : public IRuleService {
public:
    bool init() override { return true; }
    void loop() override {}
    bool forceSyncRules() override { return true; }
};

class CountingDeviceService : public IDeviceService {
public:
    bool init() override { return true; }
    void loop() override {}
};

/** @brief One period of line time, and who spent it. */
struct BusWindow {
    uint64_t startedAtMs = 0;
    uint64_t endedAtMs = 0;
    bool discovery = false;
    /** A raw command (ModbusRead and the like) rather than the bus half of a module command. */
    bool raw = false;
    /** Discovery only: which rung of the ladder held the line, and what it heard. */
    StartupScanStep::Kind kind = StartupScanStep::NONE;
    bool answered = false;
    /** Discovery only: the port the step was about, and the catalog model it tried, if one. */
    int pin = -1;
    int model = -1;
    /** Discovery only: how long that port had had its power when the step began; 0 while dark. */
    uint64_t pinPoweredForMs = 0;
    /** Discovery only: the address the step's entity names - for a cheap probe, the one it asked. */
    int address = 0;
    /** Discovery only: a cheap probe answered with the wire live and silent once it was isolated. */
    bool heardBeforeTheWireWasIsolated = false;

    uint64_t lengthMs() const { return endedAtMs - startedAtMs; }
};

/**
 * @brief The longest single hold of the line by one rung of the ladder that found nothing.
 *
 * @param untilMs Only windows that ended by this moment; 0 for the whole run. What a rung costs
 *        depends on what is on the line while it runs, and a run in which somebody plugs a sensor
 *        in half way through has two answers to that.
 */
uint64_t worstSilentWindowMs(const std::vector<BusWindow>& windows, StartupScanStep::Kind kind,
                             uint64_t untilMs = 0)
{
    uint64_t worst = 0;
    for (size_t i = 0; i < windows.size(); i++)
    {
        if (untilMs > 0 && windows[i].endedAtMs > untilMs)
        {
            continue;
        }
        if (windows[i].discovery && windows[i].kind == kind && !windows[i].answered &&
            windows[i].lengthMs() > worst)
        {
            worst = windows[i].lengthMs();
        }
    }
    return worst;
}

/** @brief The line time one rung of the ladder spent in total. */
uint64_t totalWindowMs(const std::vector<BusWindow>& windows, StartupScanStep::Kind kind)
{
    uint64_t total = 0;
    for (size_t i = 0; i < windows.size(); i++)
    {
        if (windows[i].discovery && windows[i].kind == kind)
        {
            total += windows[i].lengthMs();
        }
    }
    return total;
}

/**
 * @brief Records when each piece of discovery work had the line, without changing what it does.
 */
class RecordingStartupScan : public IStartupSensorScan {
public:
    RecordingStartupScan(IStartupSensorScan& inner, VirtualClock& clock,
                         std::vector<BusWindow>& windows)
        : _inner(inner), _clock(clock), _windows(windows)
    {
    }

    void describeStoredSensors(Node& node) override { _inner.describeStoredSensors(node); }
    void arm() override { _inner.arm(); }
    void rescan(int pin) override { _inner.rescan(pin); }
    void armPreparationOnly() override { _inner.armPreparationOnly(); }
    bool isRunning() const override { return _inner.isRunning(); }
    bool isScanEntryRunning() const override { return _inner.isScanEntryRunning(); }
    bool takeNextStep(StartupScanStep& step) override
    {
        const bool taken = _inner.takeNextStep(step);
        if (taken && _startedAtMs == 0)
        {
            _startedAtMs = _clock.nowMs();
        }
        return taken;
    }
    void returnStep() override { _inner.returnStep(); }

    StartupScanOutcome runStepOnBus(const StartupScanStep& step) override
    {
        if (_beforeEachStep)
        {
            _beforeEachStep(step);
        }
        BusWindow window;
        window.discovery = true;
        window.kind = step.kind;
        window.pin = step.pin;
        window.model = step.model;
        window.address = step.entity.modbusDeviceAddress;
        window.startedAtMs = _clock.nowMs();
        if (_powerStretchOf && step.pin >= 0)
        {
            window.pinPoweredForMs = _powerStretchOf(step.pin);
        }
        StartupScanOutcome outcome = _inner.runStepOnBus(step);
        window.endedAtMs = _clock.nowMs();
        window.answered = outcome.answered;
        window.heardBeforeTheWireWasIsolated = outcome.heardBeforeTheWireWasIsolated;
        _windows.push_back(window);
        return outcome;
    }

    void applyStepOutcome(const StartupScanOutcome& outcome) override
    {
        _inner.applyStepOutcome(outcome);
        if (_finishedAtMs == 0 && !_inner.isRunning())
        {
            _finishedAtMs = _clock.nowMs();
            if (_countFrames)
            {
                _framesAtFinish = _countFrames();
            }
        }
    }

    bool holdsAWindowOpen() const override { return _inner.holdsAWindowOpen(); }
    bool keepsThePortDark(int pin) const override { return _inner.keepsThePortDark(pin); }

    void reviewSensorLiveness() override { _inner.reviewSensorLiveness(); }

    void onDescribedStructureSettled(bool delivered) override
    {
        _inner.onDescribedStructureSettled(delivered);
    }

    /** @brief When the pass a restart arms had answered for every port it was given. */
    uint64_t finishedAtMs() const { return _finishedAtMs; }

    /**
     * @brief When the ladder took its first step - which is not when the boot armed it.
     *
     * The boot arms the discovery and the ladder waits out the boot's own minute before it uses
     * the line (StartupSensorScanUseCase::BOOT_SETTLE_MS), so what a pass costs is measured from
     * here rather than from zero.
     */
    uint64_t startedAtMs() const { return _startedAtMs; }

    /**
     * @brief What to read off the line when that moment comes.
     *
     * The passes that follow the restart's own go on putting frames on the free ports for as long
     * as the module runs, so a count taken at the end of the observation window would be a
     * measure of the window rather than of the pass.
     */
    void countFramesAtFinish(const std::function<size_t()>& count) { _countFrames = count; }
    size_t framesAtFinish() const { return _framesAtFinish; }

    /** @brief How to read how long a port has had its power, for the record of every step. */
    void measurePowerWith(const std::function<uint64_t(int)>& stretchOf) { _powerStretchOf = stretchOf; }

    /**
     * @brief What to do as each step of the discovery is about to go on the line, before any frame of
     *        it: for a device on the bus whose state turns on what the pass has reached rather than on
     *        the clock.
     */
    void beforeEachStep(const std::function<void(const StartupScanStep&)>& hook) { _beforeEachStep = hook; }

private:
    IStartupSensorScan& _inner;
    VirtualClock& _clock;
    std::vector<BusWindow>& _windows;
    uint64_t _finishedAtMs = 0;
    uint64_t _startedAtMs = 0;
    std::function<size_t()> _countFrames;
    size_t _framesAtFinish = 0;
    std::function<uint64_t(int)> _powerStretchOf;
    std::function<void(const StartupScanStep&)> _beforeEachStep;
};

/** @brief The same for the bus half of a module command from the server. */
class RecordingCommandUseCase : public ModuleCommandExecutionUseCase {
public:
    RecordingCommandUseCase(VirtualClock& clock, std::vector<BusWindow>& windows,
                            ILogger* logger, IDebugSystem* debugSystem,
                            INTPTimeAdapter* timeAdapter, IArduinoAdapter* arduino,
                            ISensorRepository* sensorRepository,
                            ISensorTypeRepository* sensorTypeRepository,
                            ISensorReader* sensorReader, IModuleConfig* moduleConfig,
                            ISettings* settings, IHybridAPIService* api,
                            ModuleStructure* moduleStructure, IRawBusAccess* rawBusAccess,
                            IModbusAddressAllocator* allocator)
        : ModuleCommandExecutionUseCase(logger, debugSystem, timeAdapter, nullptr, arduino,
                                        sensorRepository, sensorTypeRepository, sensorReader,
                                        moduleConfig, settings, api, nullptr, moduleStructure,
                                        nullptr, nullptr, nullptr, rawBusAccess, nullptr, nullptr,
                                        nullptr, allocator),
          _clock(clock), _windows(windows)
    {
    }

    ComplexCommandOutcome executeBusWork(const ComplexCommandWork& work) override
    {
        BusWindow window;
        window.discovery = false;
        window.startedAtMs = _clock.nowMs();
        ComplexCommandOutcome outcome = ModuleCommandExecutionUseCase::executeBusWork(work);
        window.endedAtMs = _clock.nowMs();
        _windows.push_back(window);
        return outcome;
    }

    RawBusCommandResult executeRaw(const std::string& command, const std::string& options) override
    {
        BusWindow window;
        window.discovery = false;
        window.raw = true;
        window.startedAtMs = _clock.nowMs();
        RawBusCommandResult result = ModuleCommandExecutionUseCase::executeRaw(command, options);
        window.endedAtMs = _clock.nowMs();
        _windows.push_back(window);
        return result;
    }

private:
    VirtualClock& _clock;
    std::vector<BusWindow>& _windows;
};

/** @brief What the module's telemetry looked like over a stretch of the run. */
struct TelemetrySeen {
    /** Updates the module sent in the window. */
    size_t sends = 0;
    /** How many of them carried a sensor of the module as online. */
    size_t online = 0;
    /** The longest the module went without one of those, counting from the window's own edges. */
    uint64_t worstOnlineGapMs = 0;
};

/**
 * @brief Read the telemetry of the module's own sensors off the server, between two moments.
 *
 * Not "how often did the module talk to the server" but "how often did what it said carry a
 * reading": a sensor whose port has lost its power is reported offline, and an update that says
 * offline is an update the app shows nothing new in.
 */
TelemetrySeen telemetryBetween(const SimServer& server, uint64_t fromMs, uint64_t untilMs)
{
    TelemetrySeen seen;
    uint64_t lastOnlineMs = fromMs;
    for (size_t i = 0; i < server.updates.size(); i++)
    {
        const SimServer::Update& update = server.updates[i];
        if (update.atMs < fromMs || update.atMs > untilMs)
        {
            continue;
        }
        seen.sends++;
        if (update.onlineCount == 0)
        {
            continue;
        }
        seen.online++;
        if ((update.atMs - lastOnlineMs) > seen.worstOnlineGapMs)
        {
            seen.worstOnlineGapMs = update.atMs - lastOnlineMs;
        }
        lastOnlineMs = update.atMs;
    }
    if (untilMs > lastOnlineMs && (untilMs - lastOnlineMs) > seen.worstOnlineGapMs)
    {
        seen.worstOnlineGapMs = untilMs - lastOnlineMs;
    }
    return seen;
}

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

/** @brief A scannable ModBus model of the catalog. */
SensorType makeCatalogType(const char* uid, const char* name, int defaultAddress)
{
    SensorType type;
    type.uid = uid;
    type.name = name;
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = true;
    type.isSupported = true;
    type.baudRate = 9600;
    type.modbusDeviceAddress = defaultAddress;
    type.modbusPingAddress = 0x0000;

    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = 0x0000;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.channel = 1;
    config.channelName = "value";
    config.sensorType = "temperature";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    type.modbusRegisterConfig.push_back(config);

    return type;
}

/** @brief The sensor the module already knows about, stored before the restart. */
SensorEntity makeKnownSensor()
{
    SensorEntity sensor;
    sensor.uid = std::string(MODULE_UID) + ".00";
    sensor.protocol = "ModBus";
    sensor.model = "S18-TEMP";
    sensor.sensorType = "temperature";
    sensor.sensorTypeUid = "type-1";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.status = SENSOR_STATUS_ADDED;
    sensor.pin = KNOWN_PORT;
    sensor.modbusDeviceAddress = KNOWN_ADDRESS;
    sensor.baudRate = 9600;

    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = 0x0000;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.channel = 1;
    config.channelName = "value";
    config.sensorType = "temperature";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    sensor.modbusRegisterConfigs.push_back(config);

    return sensor;
}

std::string scanSensorsSignalJson()
{
    char buffer[384];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"ScanSensors\",\"options\":{}}",
             MODULE_UID);
    return std::string(buffer);
}

ModuleCommandDto makeServerCommand(const char* uid, const char* command, const char* options)
{
    ModuleCommandDto dto;
    dto.uid = uid;
    dto.command = command;
    dto.createdAt = "2026-09-05T10:00:00Z";
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
    MockWifiAdapter wifiAdapter;
    MockNTPTimeAdapter timeAdapter;
    MockSpiffsSensorDao sensorDao;
    SensorRepository sensorRepository;
    MockSensorTypeRepository sensorTypeRepository;
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;
    ArduinoJsonConverter jsonConverter;

    ModbusSensorReader modbusSensorReader;
    RawBusAccessAdapter rawBusAccess;
    SensorReader sensorReader;

    SimServer server;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModbusAddressAllocator modbusAddressAllocator;

    std::vector<BusWindow> busWindows;
    RecordingCommandUseCase moduleCommandUseCase;
    StartupSensorScanUseCase startupScan;
    RecordingStartupScan recordingStartupScan;

    SensorAcquisitionService acquisition;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    NetworkCommandRunner networkRunner;
    RepublishModuleStructureUseCase republish;

    CommandService commandService;
    CommandTopicHandler topicHandler;
    ScriptedMqttManager mqttManager;

    SensorService sensorService;
    CountingDeviceService deviceService;
    CountingRuleService ruleService;
    LoopServiceConfigureDone loopService;

    MockButtonUseCases buttonUseCases;
    ModuleSetupService setupService;

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
          sensorRepository(&sensorDao),
          modbusSensorReader(ports, arduino, &logger, &debugSystem),
          rawBusAccess(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          modbusAddressAllocator(&sensorRepository),
          moduleCommandUseCase(clock, busWindows, &logger, &debugSystem, &timeAdapter, &arduino,
                               &sensorRepository, &sensorTypeRepository, &sensorReader,
                               &moduleConfig, &settings, &server, &moduleStructure, &rawBusAccess,
                               &modbusAddressAllocator),
          startupScan(&logger, &debugSystem, &moduleConfig, &sensorRepository,
                      &sensorTypeRepository, &sensorReader, &moduleStructure, &sensorStructure,
                      &modbusAddressAllocator, &sensorValueModel, &virtualChannels, &arduino),
          recordingStartupScan(startupScan, clock, busWindows),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          networkRunner(&scheduler, &logger, &debugSystem),
          republish(&moduleStructure, &sensorRepository, &server, &sensorStructure, &logger,
                    &debugSystem, &arduino),
          commandService(nullptr, &moduleCommandUseCase, nullptr, &moduleStructure,
                         &logger, &debugSystem, &timeAdapter, &jsonConverter),
          topicHandler(&logger, &debugSystem, MODULE_UID, &commandService, &jsonConverter),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader,
                        &sensorValueModel, &timeAdapter, &arduino, &server, &logger,
                        &debugSystem),
          setupService(&logger, &debugSystem, &moduleConfig,
                       nullptr, nullptr, nullptr, &buttonUseCases,
                       &wifiAdapter, nullptr, nullptr, nullptr, &mqttManager,
                       &moduleStructure, nullptr, nullptr,
                       &sensorRepository, &sensorTypeRepository, &sensorReader)
    {
        debugSystem.featureEnabled = false;
        server.clock = &clock;
        server.scheduler = &scheduler;

        wireThePortsTheWayTheBoardWiresThem();

        timeAdapter.setEpochTime(1757066400);
        timeAdapter.setCommandTimeValid(true);

        settings.setUser("s18@example.com");
        settings.setAccountUid("s18-account");
        settings.setNoScanSensors(false);
        settings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
        moduleConfig.setSettings(&settings);
        moduleConfig.setWifiAdapter(&wifiAdapter);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
        moduleConfig.setSensorPinCount(MODULE_PIN_COUNT);
        wifiAdapter.setMACAddress(MODULE_UID);
        wifiAdapter.setLocalIP("192.168.1.100");

        // The known sensor answers; the three free ports have nothing on them at all.
        SimRs485Bus::Device known;
        known.address = KNOWN_ADDRESS;
        known.registerValues.assign(1, 250);
        port0.addDevice(known);

        sensorRepository.save(makeKnownSensor());

        sensorTypeRepository.addSensorType(makeCatalogType("type-1", "S18-TEMP", 1));
        sensorTypeRepository.addSensorType(makeCatalogType("type-2", "S18-HUMID", 2));

        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        rawBusAccess.setBusLock(&busLock);

        setupService.setSensorStructureUseCase(&sensorStructure);
        setupService.setStartupSensorScan(&recordingStartupScan);
        moduleCommandUseCase.setSensorStructureUseCase(&sensorStructure);

        acquisition.setRawCommandExecutor(&moduleCommandUseCase);
        acquisition.setStartupSensorScan(&recordingStartupScan);
        startupScan.setRawBusAccess(&rawBusAccess);
        // As globals.h does it: what shares a wire is read off the ports wired above.
        startupScan.setPortLines(modbusSensorReader.portLines());
        recordingStartupScan.countFramesAtFinish([this]() { return freePortFrames(); });
        recordingStartupScan.measurePowerWith([this](int pin) {
            return (pin >= 0 && static_cast<size_t>(pin) < ports.size())
                       ? static_cast<SimRs485Bus*>(ports[static_cast<size_t>(pin)])->currentPowerStretchMs()
                       : (uint64_t)0;
        });
        networkRunner.setExecutor(&moduleCommandUseCase);

        commandService.setOutbox(&outbox);
        commandService.setBusCommandExecutor(&acquisition);
        commandService.setStartupSensorScan(&recordingStartupScan);
        commandService.setNetworkCommandRunner(&networkRunner);
        mqttManager.setHandler(&topicHandler);

        sensorService.setOutbox(&outbox);
        sensorService.setAcquisition(&acquisition);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setCommandService(&commandService);
        loopService.setSensorService(&sensorService);
        loopService.setDeviceService(&deviceService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
    }

    /**
     * @brief The four ports as the board has them: two UARTs, four power pins, all of them down.
     *
     * globals.h gives ports 0 and 1 one HardwareSerial and ports 2 and 3 another, and each port
     * its own SENSOR_POWER pin. So a question put to a free port is heard by whatever is plugged
     * into its pair, and the only thing that can stop it being heard is that pair's power. A stand
     * of four independent buses cannot show any of that, and it is what a module in the field is
     * made of: a sensor on port 0 and a free port 1 are the same wire.
     */
    void wireThePortsTheWayTheBoardWiresThem()
    {
        port0.shareTheLineWith(port1);
        port2.shareTheLineWith(port3);

        for (size_t i = 0; i < ports.size(); i++)
        {
            SimRs485Bus* port = static_cast<SimRs485Bus*>(ports[i]);
            port->setPortPowerLikeTheBoard(true);
            port->setMasterArduino(&arduino);
            port->startUnpowered();
        }
    }

    /**
     * @brief What the boot does: the whole of setup, then the sensor service's own start.
     *
     * ModuleSetupService::setupModule() as main.cpp calls it - it begins the RS-485 line, and in
     * the configured mode it describes the registry and arms the discovery. What it reaches that
     * this stand has no part for (the radio, the LEDs, the firmware check) it skips, as it does
     * on a module where those are missing.
     */
    void boot()
    {
        setupService.setupModule();
        sensorService.init();
    }

    /**
     * @brief Send the structure the way globals.h has it sent: the discovery asks the republish,
     *        the republish builds it from the registry and hands it to the outbox, and the loop
     *        hands the outcome back.
     *
     * Opt-in, because the republish rebuilds the node from the registry every time it sends: the
     * stands that read the records the discovery writes into the node directly would read the
     * republish's instead.
     */
    void sendTheStructureTheWayTheBoardDoes()
    {
        republish.setOutbox(&outbox);
        republish.setStoredSensorDescriber(&startupScan);
        startupScan.setRepublishModuleStructureUseCase(&republish);
        loopService.setRepublishModuleStructureUseCase(&republish);
    }

    /** @brief Frames the three ports the registry knows nothing about have seen. */
    size_t freePortFrames() const
    {
        return port1.sentFrames.size() + port2.sentFrames.size() + port3.sentFrames.size();
    }

    size_t framesOnTheLine() const
    {
        return port0.sentFrames.size() + port1.sentFrames.size() + port2.sentFrames.size() +
               port3.sentFrames.size();
    }

    /** @brief One task, mirroring main.cpp: loop() runs the loop service and sleeps a period. */
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

    /** @brief When the last piece of discovery work left the line, or 0 if none ever ran. */
    uint64_t discoveryEndedAtMs() const
    {
        uint64_t last = 0;
        for (size_t i = 0; i < busWindows.size(); i++)
        {
            if (busWindows[i].discovery && busWindows[i].endedAtMs > last)
            {
                last = busWindows[i].endedAtMs;
            }
        }
        return last;
    }
};

void assertRunIsSound(ModuleUnderTest& module, CadenceRecorder& cadence)
{
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_FALSE_MESSAGE(module.busLock.deadlockDetected(),
                              "a task waited for the bus lock in a way that could never be served");
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");
}

/**
 * @brief Run the module until the pass the restart armed is over.
 *
 * On a free port nothing answers cheaply, that pass asks the module's address range and writes
 * the port's address - the identification sequence, tens of seconds a port - so a stand that means
 * "after the restart" waits for the pass to end rather than assuming how long it takes.
 */
void runUntilTheRestartsPassIsOver(ModuleUnderTest& module)
{
    for (int second = 0; second < 1200 && module.recordingStartupScan.finishedAtMs() == 0; second++)
    {
        module.scheduler.runForMs(1000);
    }
    TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                             "the pass the restart armed did not end in twenty minutes");
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The boot spends no line time, and the sensors the module already knows are reported long
//    before the discovery of the free ports is over.
// --------------------------------------------------------------------------------------------

void test_s18_the_known_sensors_are_reported_while_the_discovery_is_still_running(void)
{
    ModuleUnderTest module;

    module.boot();
    printf("  s18: the boot itself took %lu ms and put %u frames on the line\n",
           (unsigned long)module.clock.nowMs(), (unsigned)module.framesOnTheLine());

    // The claim this test rests on: not one frame reached the line during the boot.
    char bootMessage[160];
    snprintf(bootMessage, sizeof(bootMessage),
             "the boot put %u frames on the RS-485 line before the control loop had run once",
             (unsigned)module.framesOnTheLine());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.framesOnTheLine(), bootMessage);
    // What the boot does with the line is begin its two UARTs, under the lock, once - and give the
    // ports of the stored sensors their power, once each, under the same lock and without a frame.
    std::vector<int> storedPorts;
    const std::vector<SensorEntity> rows = module.sensorRepository.getAll();
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (rows[i].protocol == "ModBus" &&
            std::find(storedPorts.begin(), storedPorts.end(), rows[i].pin) == storedPorts.end())
        {
            storedPorts.push_back(rows[i].pin);
        }
    }
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1 + storedPorts.size(), module.busLock.totalTakes(),
                                     "the boot took the RS-485 lock for more than beginning the line "
                                     "and powering the stored sensors' ports");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.lineIsBegun() && module.port2.lineIsBegun(),
                             "the boot left a UART of the line unbegun");

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    // The discovery really happened: every free port was walked and nothing answered on any.
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.startupScanStepCount() >= 1,
                             "no discovery step ever reached the acquisition task");
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isRunning(),
                              "the discovery did not finish inside the observation window");
    TEST_ASSERT_TRUE_MESSAGE(module.port1.sentFrames.size() >= 1,
                             "the free ports were never probed - the discovery did not happen");

    const uint64_t firstOnline = module.server.firstOnlineUpdateMs();
    const uint64_t discoveryEnd = module.discoveryEndedAtMs();

    char message[256];
    snprintf(message, sizeof(message),
             "the module first reported its known sensor as online at t=%lu ms and the discovery "
             "ended at t=%lu ms - the telemetry did not start before the discovery was over",
             (unsigned long)firstOnline, (unsigned long)discoveryEnd);
    TEST_ASSERT_TRUE_MESSAGE(firstOnline > 0, "the known sensor was never reported online at all");
    TEST_ASSERT_TRUE_MESSAGE(discoveryEnd > 0, "the discovery never ran");
    TEST_ASSERT_TRUE_MESSAGE(firstOnline < discoveryEnd, message);

    // The figures themselves, so that "sooner" has a size: what it costs in setup() is the whole
    // discovery, spent before the first of these two moments rather than after it.
    printf("  s18: first online reading at t=%lu ms, discovery over at t=%lu ms after %lu steps; "
           "worst control-loop step %lu ms\n",
           (unsigned long)firstOnline, (unsigned long)discoveryEnd,
           (unsigned long)module.acquisition.startupScanStepCount(),
           (unsigned long)cadence.worstGapMs());
}

// --------------------------------------------------------------------------------------------
// 2. The control loop is not what pays for the discovery.
// --------------------------------------------------------------------------------------------

void test_s18_the_loop_stays_bounded_while_the_discovery_walks_the_free_ports(void)
{
    ModuleUnderTest module;
    module.boot();
    // The boot's own take: it begins the line from setup, before any task exists.
    const uint64_t takesOfTheBoot = module.busLock.totalTakes();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.startupScanStepCount() >= 1,
                             "no discovery step ever reached the acquisition task");

    char message[288];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms) while the "
             "discovery walked three free ports against a two-model catalog",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(), message);

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.takesBy(LOOP_TASK_NAME),
                                     "the control loop took the RS-485 lock for the discovery");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(module.busLock.totalTakes() - takesOfTheBoot,
                                     module.busLock.takesBy(SensorAcquisitionService::TASK_NAME),
                                     "somebody other than the acquisition task used the line");
}

// --------------------------------------------------------------------------------------------
// 3. A ScanSensors command arriving while the discovery is running takes its turn on the line
//    rather than sharing it.
// --------------------------------------------------------------------------------------------

void test_s18_a_command_and_the_discovery_take_turns_on_the_line(void)
{
    ModuleUnderTest module;
    module.boot();

    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-1", "ScanSensors", "{\"pin\":2}"));

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // The signal arrives on the first iterations, while the discovery is still walking port 1.
    module.mqttManager.deliver(scanSensorsSignalJson());
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    // Both really ran, and both on the acquisition task.
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.startupScanStepCount() >= 1,
                             "the discovery never reached the acquisition task");
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= 1,
                             "the command never reached the acquisition task");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.busLock.takesBy(LOOP_TASK_NAME),
                                     "the control loop took the RS-485 lock");

    // Neither ever had the line while the other did. The windows are recorded around the two
    // pieces of work themselves, so an overlap here would be two owners of one line.
    bool commandRan = false;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        if (!module.busWindows[i].discovery)
        {
            commandRan = true;
        }
        for (size_t j = i + 1; j < module.busWindows.size(); j++)
        {
            const bool overlap = module.busWindows[i].startedAtMs < module.busWindows[j].endedAtMs &&
                                 module.busWindows[j].startedAtMs < module.busWindows[i].endedAtMs;
            char message[256];
            snprintf(message, sizeof(message),
                     "two pieces of bus work overlapped: %s held the line from %lu to %lu and %s "
                     "from %lu to %lu",
                     module.busWindows[i].discovery ? "the discovery" : "a command",
                     (unsigned long)module.busWindows[i].startedAtMs,
                     (unsigned long)module.busWindows[i].endedAtMs,
                     module.busWindows[j].discovery ? "the discovery" : "a command",
                     (unsigned long)module.busWindows[j].startedAtMs,
                     (unsigned long)module.busWindows[j].endedAtMs);
            TEST_ASSERT_FALSE_MESSAGE(overlap, message);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(commandRan, "the ScanSensors command never executed its bus half");

    // And the command was still answered, with the same two reports the backend has always seen.
    TEST_ASSERT_TRUE_MESSAGE(module.server.moduleCommandReports.size() >= 2,
                             "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("InProgress",
                             module.server.moduleCommandReports[0][0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("scan-1", module.server.moduleCommandReports[0][0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Done", module.server.moduleCommandReports[module.server.moduleCommandReports.size() - 1][0]
                    .status.c_str());

    // The discovery finished too - a command did not cost the module its discovery.
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isRunning(),
                              "the discovery never finished after the command took the line");
}

// --------------------------------------------------------------------------------------------
// 4. Adoption happens while the registry is being read by the sweep and the telemetry.
// --------------------------------------------------------------------------------------------

void test_s18_a_sensor_found_during_a_sweep_joins_the_registry_and_gets_polled(void)
{
    ModuleUnderTest module;

    // A second device, on a port the registry knows nothing about, at the address the scan offers.
    SimRs485Bus::Device found;
    found.address = MODBUS_ADDRESS_MIN + 1;
    found.registerValues.assign(1, 300);
    module.port1.addDevice(found);

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    printf("  s18: the ladder made %u cheap probe(s), %u presence poll(s) and %u identification "
           "step(s) in %u cheap pass(es); %u sensor(s) adopted, %u port(s) unidentified\n",
           (unsigned)module.startupScan.cheapProbeCount(),
           (unsigned)module.startupScan.presencePollCount(),
           (unsigned)module.startupScan.identifyStepCount(),
           (unsigned)module.startupScan.freePortPassCount(),
           (unsigned)module.startupScan.adoptedCount(),
           (unsigned)module.startupScan.unidentifiedPinCount());

    // The registry gained the sensor, with the status a scan gives it.
    const std::string adoptedUid = std::string(MODULE_UID) + ".01";
    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(adoptedUid.c_str());
    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "the device on the free port was never written into the registry");
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_NEW, adopted.value().status,
                              "the adopted sensor did not get NEW status");

    // The sweep picked it up without a restart: it is polled and reported like any other.
    TEST_ASSERT_TRUE_MESSAGE(module.server.updates.size() >= 2,
                             "the module never reported anything");
    bool reportedTwoOnline = false;
    for (size_t i = 0; i < module.server.updates.size(); i++)
    {
        if (module.server.updates[i].onlineCount >= 2)
        {
            reportedTwoOnline = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(reportedTwoOnline,
                             "the adopted sensor never appeared in the telemetry - adoption did "
                             "not reach the sweep");

    // The loop never blocked for any of it.
    char message[256];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms) while a "
             "sensor was adopted mid-sweep",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(), message);
}

// --------------------------------------------------------------------------------------------
// 5. noScanSensors: the boot that carries the flag puts no discovery on the line at all.
// --------------------------------------------------------------------------------------------

void test_s18_a_no_scan_boot_puts_nothing_of_the_discovery_on_the_line(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(120000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    // One step, and it is the preparation: the flag skips the discovery, not the register writes
    // the sensors the module already holds are owed.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, module.acquisition.startupScanStepCount(),
                                     "the stored sensors were not prepared on a no-scan boot");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.port1.sentFrames.size(),
                                     "a free port was probed although noScanSensors was set");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.port2.sentFrames.size(),
                                     "a free port was probed although noScanSensors was set");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.port3.sentFrames.size(),
                                     "a free port was probed although noScanSensors was set");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)(module.port1.writes.size() +
                                                   module.port2.writes.size() +
                                                   module.port3.writes.size()),
                                     "an address was written into a free port although "
                                     "noScanSensors was set");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, module.settings.getNoScanSensors(),
                                    "the flag was not consumed by the boot that used it");

    // The known sensor is polled and reported as usual - the fast path is only about discovery.
    TEST_ASSERT_TRUE_MESSAGE(module.server.firstOnlineUpdateMs() > 0,
                             "the known sensor was never reported on a no-scan boot");
}

// --------------------------------------------------------------------------------------------
// 6. What the ports nothing is plugged into cost the module.
//
// Three free ports, a two-model catalog, and nothing on any of them: the commonest module in the
// field, whose spare ports are spare. The question the line is asked about such a port is "is
// anything here", and it is asked in the order of what it costs: one read budget per model at each
// address of the module's range and at the model's own, then the restart's window - the broadcast
// poll, the module's range for every model again with the paired port dark, and, with nothing
// answering any of it, the address writes. Those writes
// are most of what the pass costs; what bounds it is that the window runs once per free port per
// restart, and that it is the identification sequence and not more.
//
// And these are the ports of the board, not four independent buses: port 1 is on the same wire as
// the sensor working on port 0. What that costs is measured in the test after this one.
// --------------------------------------------------------------------------------------------

// The window on an empty port of this stand, from the identification procedure's timings: the poll (two 300 ms
// queries and the power the isolation moves), then per model the range (four read budgets) and the
// blind address sequence - neither model of this stand has a write code of its own. This stand
// puts no register write on the wire (SimRs485Bus writes like the board only when asked), so a
// write costs the reader's 300 ms pause and nothing of the driver's.
const uint64_t STAND_WINDOW_ON_AN_EMPTY_PORT_MS =
    (2 * 300 + 2 * 100) +
    2 * (4 * 300 + (StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS
                        ? 3 * 2 * (300 + (200 + 10 + 700 + 10 + 1200)) + 2 * 500
                        : 0));

// The cheap rung of the restart's pass on one free port: per model its own address and the four of
// the range, each a read budget and the turn the task that owns the line idles before it takes the
// next step.
const uint64_t STAND_CHEAP_RUNG_ON_AN_EMPTY_PORT_MS =
    2 * (1 + 4) * (300 + SensorAcquisitionService::IDLE_STEP_MS);

// Three windows and three cheap rungs, the ten seconds the ports are given to come up before the
// first question and after every window that took the power of a port still to be asked, and the
// ping of the stored sensor and the preparation around them. The identification procedure walks the whole
// catalog on each of the same three ports and costs several times this.
const uint64_t FREE_PORT_PASS_BUDGET_MS =
    3 * (STAND_WINDOW_ON_AN_EMPTY_PORT_MS + STAND_CHEAP_RUNG_ON_AN_EMPTY_PORT_MS) +
    4 * StartupSensorScanUseCase::PORT_WAKE_UP_MS + 10000;

// One frame per cheap probe - per model its own address and the four of the range, less the one
// address port 1 does not ask on the live wire because the sensor of port 0 holds it - two per poll,
// and per model four reads of the range and the blind sequence's three silent pings after each of
// its six writes: 56 per free port with this catalog, 54 on port 1.
const size_t FREE_PORT_FRAME_BUDGET =
    3 * (2 * (1 + 4) + 2 + 2 * (4 + (StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS ? 3 * 2 * 3 : 0))) -
    2;

void test_s18_the_free_ports_cost_their_windows_and_no_more(void)
{
    ModuleUnderTest module;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const size_t freePortFrames = module.recordingStartupScan.framesAtFinish();
    const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
    const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();

    printf("  s18: the pass over three free ports ran from t=%lu ms to t=%lu ms after %u frames "
           "on them\n",
           (unsigned long)startedAtMs, (unsigned long)finishedAtMs, (unsigned)freePortFrames);

    // The premise: the free ports really were walked, and the module really did settle.
    TEST_ASSERT_TRUE_MESSAGE(module.freePortFrames() > 0,
                             "no request ever went out on a free port - nothing was measured");
    TEST_ASSERT_TRUE_MESSAGE(finishedAtMs > 0,
                             "the pass a restart arms never finished inside the observation window");

    // The boot arms the pass; the ladder leaves the line alone until the boot's own peaks are
    // over, and only then does the pass begin. What it costs is measured from there.
    TEST_ASSERT_TRUE_MESSAGE(startedAtMs >= StartupSensorScanUseCase::BOOT_SETTLE_MS,
                             "the discovery had the line inside the boot's own minute");

    char message[256];
    snprintf(message, sizeof(message),
             "the pass over three free ports took %lu ms (budget %lu ms) and put %u frames on them",
             (unsigned long)(finishedAtMs - startedAtMs), (unsigned long)FREE_PORT_PASS_BUDGET_MS,
             (unsigned)freePortFrames);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(FREE_PORT_PASS_BUDGET_MS,
                                           (unsigned int)(finishedAtMs - startedAtMs), message);

    snprintf(message, sizeof(message),
             "the module put %u frames on three empty ports (budget %u) - the pass walked more "
             "than one window per port",
             (unsigned)freePortFrames, (unsigned)FREE_PORT_FRAME_BUDGET);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(FREE_PORT_FRAME_BUDGET, (unsigned int)freePortFrames,
                                           message);

    // And the ports were answered, not skipped: each of them is an empty port in the structure.
    TEST_ASSERT_TRUE_MESSAGE(module.moduleStructure.nodes.size() == 1,
                             "the boot did not build a node");
    int emptyPortRecords = 0;
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        if (module.moduleStructure.nodes[0].sensors[i].type == "none")
        {
            emptyPortRecords++;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(3, emptyPortRecords,
                              "the free ports were not all described as empty ports");
}

// --------------------------------------------------------------------------------------------
// 6a. The free port on the wire of a working sensor.
//
// Ports 0 and 1 are one UART. The sensor the module reads on port 0 answers anything it can hear,
// and a broadcast poll is the one question that is addressed to nobody in particular - so unless
// the poll takes port 0's power down for its own length, "is anything on port 1" is answered by
// the sensor on port 0, and the module spends the whole identification procedure on an empty port.
// That is the layout of every module in the field with one sensor in it.
// --------------------------------------------------------------------------------------------

// One sweep interval and the poll that ran inside it: the isolation costs the sensor on the paired
// port a power cycle, and what must not happen is that it costs it a reading the module never
// takes. Two intervals is the bound - one for the sweep the poll displaced, one for the next.
const uint64_t WORST_UPDATE_GAP_MS = 2 * 15000 + 1000;

// A scan asked for by hand is the one pass that spends the identification procedure, and the procedure
// takes the power off the other port of the wire it walks. What the sensor on that port may lose to
// it is what it loses to the scan the ScanSensors command has always run on its own: one stretch
// of silence per model the procedure walks on the port it was given. Anything beyond that is the
// module's discovery costing the module its telemetry.
//
// Two, for the two models of this stand's catalog, and it is measured at one: the room between
// them is deliberate, so that a catalog with another model in it fails this test only when the
// ladder has changed. The same measurement on the revision before the ladder gives seven.
const size_t OFFLINE_UPDATES_A_HAND_ASKED_SCAN_MAY_COST = 2;

// The gap of the window a command runs in is bounded by the command's own work rather than by the
// sweep: the identification procedure it runs on the port it named holds the line for one model scan per
// catalog model - 13 s each on this stand - and a sensor on another port has no power for the
// length of it. Two of those, and the sweep's own bound on top. Measured at 30 000 ms; the same
// measurement gives 120 000 ms on the revision before the ladder.
const uint64_t WORST_UPDATE_GAP_ACROSS_A_HAND_ASKED_SCAN_MS = 2 * 13000 + WORST_UPDATE_GAP_MS;

void test_s18_a_free_port_on_the_wire_of_a_working_sensor_is_honestly_empty(void)
{
    ModuleUnderTest module;
    // The stored sensor takes the ten seconds of power this firmware believes a sensor takes
    // (PORT_WAKE_UP_MS). A neighbour that answers the instant its power comes back cannot show
    // what a power cycle costs, and every port cycle in this test would be free.
    module.port0.device(KNOWN_ADDRESS)->wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    printf("  s18: on the board's wiring the restart's pass was over at t=%lu ms after %u frames "
           "on the free ports; %u presence poll(s), %u identification step(s)\n",
           (unsigned long)module.recordingStartupScan.finishedAtMs(),
           (unsigned)module.recordingStartupScan.framesAtFinish(),
           (unsigned)module.startupScan.presencePollCount(),
           (unsigned)module.startupScan.identifyStepCount());

    // Port 1 is empty and the module says so, having asked the line once.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, (uint32_t)module.startupScan.presencePollCount(),
                                     "the three free ports were not each asked once whether "
                                     "anything is on them");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.startupScan.identifyStepCount(),
                                     "the identification procedure ran on a module whose free "
                                     "ports are empty - the poll heard the sensor of the paired "
                                     "port and took its answer for an occupant");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.startupScan.adoptedCount(),
                                     "the module adopted something on an empty port");

    bool portOneIsAnEmptyPort = false;
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        const Sensor& record = module.moduleStructure.nodes[0].sensors[i];
        if (record.uid == std::string(MODULE_UID) + ".01" && record.type == "none")
        {
            portOneIsAnEmptyPort = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(portOneIsAnEmptyPort,
                             "the free port paired with the working sensor is not described as empty");

    // The sensor on the other end of that wire went on being a sensor: the poll takes its power
    // for as long as it runs and hands it straight back.
    TEST_ASSERT_TRUE_MESSAGE(module.port0.isPowered(),
                             "the working sensor was left without power after a presence poll");
    TEST_ASSERT_EQUAL_MESSAGE(module.port0.powerOnTransitions, module.port0.powerOffTransitions + 1,
                              "the power the presence poll took off the working port was not "
                              "given back the same number of times");

    uint64_t worstUpdateGapMs = 0;
    uint64_t previousMs = 0;
    for (size_t i = 0; i < module.server.updates.size(); i++)
    {
        if (i > 0 && (module.server.updates[i].atMs - previousMs) > worstUpdateGapMs)
        {
            worstUpdateGapMs = module.server.updates[i].atMs - previousMs;
        }
        previousMs = module.server.updates[i].atMs;
    }

    printf("  s18: the sensor sharing the wire was reported %u time(s), the longest gap %lu ms; "
           "its port lost power %u time(s) to a presence poll\n",
           (unsigned)module.server.updates.size(), (unsigned long)worstUpdateGapMs,
           (unsigned)module.port0.powerOffTransitions);

    char message[224];
    snprintf(message, sizeof(message),
             "the sensor sharing the wire with the free ports went %lu ms without being reported "
             "(limit %lu ms)",
             (unsigned long)worstUpdateGapMs, (unsigned long)WORST_UPDATE_GAP_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(WORST_UPDATE_GAP_MS, (unsigned int)worstUpdateGapMs,
                                           message);
    TEST_ASSERT_TRUE_MESSAGE(module.server.firstOnlineUpdateMs() > 0 &&
                                 module.server.firstOnlineUpdateMs() < 60000,
                             "the sensor sharing the wire was not reported in the first minute");

    // What the restart's window on the free port of this wire costs this sensor in the telemetry.
    // Its port is dark from the window's first step to its last and for the warm-up after, and the
    // sweep leaves it alone for all of that: not read, and not a miss. The sensor goes on being
    // reported with its last reading, so the bound is the module's own, not the window's.
    const TelemetrySeen acrossTheRestart =
        telemetryBetween(module.server, module.recordingStartupScan.startedAtMs(),
                         module.recordingStartupScan.finishedAtMs() + WORST_UPDATE_GAP_MS);
    printf("  s18: across the restart's pass the sensor sharing the wire was online in %u of %u "
           "update(s), the longest online gap %lu ms; the sweep left it unread %lu time(s) while "
           "its port was held dark\n",
           (unsigned)acrossTheRestart.online, (unsigned)acrossTheRestart.sends,
           (unsigned long)acrossTheRestart.worstOnlineGapMs,
           (unsigned long)module.acquisition.heldDarkSkipCount());
    snprintf(message, sizeof(message),
             "the sensor sharing the wire went %lu ms without an online reading across the "
             "restart's pass (limit %lu ms)",
             (unsigned long)acrossTheRestart.worstOnlineGapMs, (unsigned long)WORST_UPDATE_GAP_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE((unsigned int)WORST_UPDATE_GAP_MS,
                                           (unsigned int)acrossTheRestart.worstOnlineGapMs, message);

    // And now the half of "any pass" the module does not make on its own: somebody presses scan.
    // The identification procedure the command runs takes the power off the other port of the wire it
    // walks, this sensor's, so the pass the ladder adds behind it is measured on the sensor -
    // not on the free ports it walks.
    const uint64_t commandAtMs = module.clock.nowMs();
    const uint32_t neighbourCyclesBeforeTheCommand = (uint32_t)module.port0.powerOffTransitions;
    // Pin 3 in the command is port 1 of the board: the options carry the app's numbering, and
    // ModuleCommandExecutionUseCase::convertPinFromJson() turns 4/3/2/1 into 0/1/2/3.
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-neighbour", "ScanSensors", "{\"pin\":3}"));
    module.mqttManager.deliver(scanSensorsSignalJson());
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const TelemetrySeen acrossTheCommand =
        telemetryBetween(module.server, commandAtMs, module.clock.nowMs());

    printf("  s18: across a ScanSensors command the module sent %u update(s), %u of them carrying "
           "the stored sensor as online, the longest online gap %lu ms; its port lost power %u "
           "time(s) to the command and the pass behind it\n",
           (unsigned)acrossTheCommand.sends, (unsigned)acrossTheCommand.online,
           (unsigned long)acrossTheCommand.worstOnlineGapMs,
           (unsigned)(module.port0.powerOffTransitions - neighbourCyclesBeforeTheCommand));

    TEST_ASSERT_TRUE_MESSAGE(acrossTheCommand.sends >= 39,
                             "the module stopped talking to the server after a ScanSensors "
                             "command - there is nothing to measure");

    const size_t offlineSends = acrossTheCommand.sends - acrossTheCommand.online;
    snprintf(message, sizeof(message),
             "a ScanSensors command cost the working sensor %u of the %u updates that followed it "
             "(budget %u) - the discovery is taking the module's telemetry down with it",
             (unsigned)offlineSends, (unsigned)acrossTheCommand.sends,
             (unsigned)OFFLINE_UPDATES_A_HAND_ASKED_SCAN_MAY_COST);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(OFFLINE_UPDATES_A_HAND_ASKED_SCAN_MAY_COST,
                                           (unsigned int)offlineSends, message);

    snprintf(message, sizeof(message),
             "the working sensor went %lu ms without a reading the server was told about while a "
             "ScanSensors command ran (limit %lu ms)",
             (unsigned long)acrossTheCommand.worstOnlineGapMs,
             (unsigned long)WORST_UPDATE_GAP_ACROSS_A_HAND_ASKED_SCAN_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(WORST_UPDATE_GAP_ACROSS_A_HAND_ASKED_SCAN_MS,
                                           (unsigned int)acrossTheCommand.worstOnlineGapMs, message);
}

// --------------------------------------------------------------------------------------------
// 6b. What the free ports are between two passes: dark, the way the boot left them.
//
// A port with nothing configured on it has no power - ModbusRS485 starts every SENSOR_POWER pin
// low and the preparation puts back down every port no stored sensor uses - and whatever somebody
// plugs into it is electrically dead until the module asks it something. The ladder asks it
// something every half minute, so it raises that power; what it must not do is leave it up, or
// the device nobody could identify sits on the working sensor's wire for as long as the module
// runs.
// --------------------------------------------------------------------------------------------

void test_s18_the_free_ports_are_dark_between_two_passes(void)
{
    ModuleUnderTest module;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    // Between two passes, not in the middle of one: a pass that is still walking has its ports
    // powered on purpose, and that is not what this is about.
    for (int round = 0; round < 5 && module.startupScan.aPassIsInFlight(); round++)
    {
        module.scheduler.runForMs(StartupSensorScanUseCase::PORT_WAKE_UP_MS);
    }
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    printf("  s18: after ten minutes and %u cheap pass(es) the free ports are %s / %s / %s; they "
           "had power for %lu / %lu / %lu ms of %lu, longest stretch %lu ms\n",
           (unsigned)module.startupScan.freePortPassCount(),
           module.port1.isPowered() ? "powered" : "dark",
           module.port2.isPowered() ? "powered" : "dark",
           module.port3.isPowered() ? "powered" : "dark",
           (unsigned long)module.port1.poweredMs(), (unsigned long)module.port2.poweredMs(),
           (unsigned long)module.port3.poweredMs(), (unsigned long)module.clock.nowMs(),
           (unsigned long)module.port1.longestPoweredStretchMs());

    // The passes really ran - otherwise "dark" would mean nothing.
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() >= 5,
                             "the cheap rung stopped running - there is nothing to measure");

    TEST_ASSERT_FALSE_MESSAGE(module.port1.isPowered(),
                              "a free port was left under power between two passes - whatever is "
                              "plugged into it sits on the working sensor's wire for ever");
    TEST_ASSERT_FALSE_MESSAGE(module.port2.isPowered(), "a free port was left under power");
    TEST_ASSERT_FALSE_MESSAGE(module.port3.isPowered(), "a free port was left under power");

    // And it is not powered for most of the window either: a pass is seconds, the interval
    // between two of them is half a minute.
    const uint64_t windowMs = module.clock.nowMs();
    char message[224];
    snprintf(message, sizeof(message),
             "a free port had power for %lu ms of the %lu ms window - the passes are not what it "
             "is powered for",
             (unsigned long)module.port1.poweredMs(), (unsigned long)windowMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE((unsigned int)(windowMs / 2),
                                           (unsigned int)module.port1.poweredMs(), message);

    // No stretch of power outlives the pass that took it. The restart's pass holds the free ports
    // for as long as its windows take; a pass after it for less than two intervals.
    const uint64_t restartsPassMs =
        module.recordingStartupScan.finishedAtMs() - module.recordingStartupScan.startedAtMs();
    const uint64_t longestPassMs =
        (restartsPassMs > 2 * StartupSensorScanUseCase::FREE_PORT_PROBE_INTERVAL_MS)
            ? restartsPassMs
            : 2 * StartupSensorScanUseCase::FREE_PORT_PROBE_INTERVAL_MS;
    snprintf(message, sizeof(message),
             "a free port kept its power for %lu ms in one stretch - longer than the pass that "
             "raised it (%lu ms)",
             (unsigned long)module.port1.longestPoweredStretchMs(), (unsigned long)longestPassMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE((unsigned int)longestPassMs,
                                           (unsigned int)module.port1.longestPoweredStretchMs(),
                                           message);
}

// --------------------------------------------------------------------------------------------
// 7. A sensor plugged into a free port after the boot, with nobody touching the module.
//
// This is what the product's app asks a person to reboot for, and it is the whole point of the
// cheap rung being periodic rather than a thing a restart does once: the port is asked again at
// its interval, for ever, and a device that has arrived on it answers the next time it is asked.
// --------------------------------------------------------------------------------------------

// One full transaction: the reader's read budget (300 ms) plus the 100 ms a port pays once when
// its power comes up. The same bound S7 holds a bus command to, and the claim is the same one -
// a cheap probe hearing nothing is one request with one timeout, not a procedure.
const uint64_t ONE_TRANSACTION_MS = 400;

// The presence poll is two of those transactions - the two wildcard query frames the
// address discovery sends - plus the power it takes off the paired port of the same wire. On the
// restart's pass the poll opens the port's window, and the rest of the window is steps of its own,
// one catalog model each, bounded below.
const uint64_t PRESENCE_POLL_BUDGET_MS = 2 * ONE_TRANSACTION_MS;

// A step of the window that heard nothing: one model read at the four addresses of the module's
// range - four transactions.
const uint64_t RANGE_STEP_BUDGET_MS = (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1) * ONE_TRANSACTION_MS;

// And one model's address writes into a port with nothing on it - this stand's models have no
// write code of their own, so the blind sequence: three attempts of two writes, each verified by a
// silent three-attempt ping, 500 ms between the attempts. What the identification procedure spends on the
// address of one model, and nothing that grows with the catalog.
const uint64_t WRITE_STEP_BUDGET_MS = 3 * 2 * (300 + (200 + 10 + 700 + 10 + 1200)) + 2 * 500 + 100;

// What a cheap probe costs when it does hear something: the model scan that decides whether to
// adopt is the identification procedure - isolate the port, ten seconds of warm-up, a three-attempt
// ping - and that is the price of refusing a device that answered from the paired port of the
// same wire at the address the model was asked for.
const uint64_t ONE_MODEL_SCAN_MS = 13000;

/** @brief The device somebody plugs into a free port, holding the address it left the factory on. */
SimRs485Bus::Device makeFactoryFreshDevice(uint8_t factoryAddress)
{
    SimRs485Bus::Device device;
    device.address = factoryAddress;
    device.registerValues.assign(1, 250);
    return device;
}

void test_s18_a_sensor_plugged_in_after_the_boot_is_found_without_a_restart(void)
{
    ModuleUnderTest module;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // Two minutes of an ordinary module, and the pass a restart armed over: it has described all
    // three free ports as empty.
    module.scheduler.runForMs(120000);
    runUntilTheRestartsPassIsOver(module);

    const std::string plugged = std::string(MODULE_UID) + ".02";
    TEST_ASSERT_FALSE_MESSAGE((bool)module.sensorRepository.get(plugged.c_str()),
                              "the port was not empty before the sensor was plugged in");
    const uint64_t pluggedInAtMs = module.clock.nowMs();
    const uint32_t identifyStepsBefore = module.startupScan.identifyStepCount();

    // The person plugs a sensor into port 2. Nothing else happens: no restart, no command.
    module.port2.addDevice(makeFactoryFreshDevice(1));

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS - 120000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(plugged.c_str());
    printf("  s18: the sensor plugged in at t=%lu ms was %s; the ladder had made %u cheap probe(s) "
           "in %u pass(es) and %u identification step(s)\n",
           (unsigned long)pluggedInAtMs, adopted ? "adopted" : "never found",
           (unsigned)module.startupScan.cheapProbeCount(),
           (unsigned)module.startupScan.freePortPassCount(),
           (unsigned)module.startupScan.identifyStepCount());

    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a sensor plugged into a free port was never found - the module still "
                             "needs a restart or a command to notice one");
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_NEW, adopted.value().status,
                              "the sensor found on a free port did not get NEW status");
    TEST_ASSERT_EQUAL_MESSAGE(2, adopted.value().pin,
                              "the sensor was adopted on a port it was not plugged into");

    // Found by the cheap rung: a device holding its factory address answers one request, and the
    // expensive procedure is not what found it.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(identifyStepsBefore,
                                     (uint32_t)module.startupScan.identifyStepCount(),
                                     "the module ran the identification procedure to find a sensor "
                                     "sitting at its own factory address");

    // And the module told the server about it, which is what makes it appear in the app.
    bool reportedTwoOnline = false;
    for (size_t i = 0; i < module.server.updates.size(); i++)
    {
        if (module.server.updates[i].atMs > pluggedInAtMs && module.server.updates[i].onlineCount >= 2)
        {
            reportedTwoOnline = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(reportedTwoOnline,
                             "the sensor was adopted but never reported - the app would not show it");

    // What one cheap probe costs the line while every free port is empty, which is the answer on
    // every port that stays empty: one request, one budget.
    const uint64_t worstProbeOnEmptyPortsMs =
        worstSilentWindowMs(module.busWindows, StartupScanStep::PROBE_CANDIDATE, pluggedInAtMs);
    // And afterwards, when one of the two ports on the second wire has a device on it at the very
    // address the models are asked for: the probe of its paired port hears it and pays the model
    // scan that refuses to adopt it there.
    const uint64_t worstProbeMs = worstSilentWindowMs(module.busWindows,
                                                      StartupScanStep::PROBE_CANDIDATE);
    const uint64_t worstPresenceMs = worstSilentWindowMs(module.busWindows,
                                                         StartupScanStep::PROBE_PRESENCE);
    const uint64_t worstRangeMs = worstSilentWindowMs(module.busWindows, StartupScanStep::PROBE_RANGE);
    const uint64_t worstWriteMs =
        worstSilentWindowMs(module.busWindows, StartupScanStep::WRITE_PORT_ADDRESS);
    printf("  s18: the longest silent cheap probe held the line %lu ms while the ports were empty "
           "and %lu ms once one of them was not; the longest silent presence poll %lu ms, range "
           "step %lu ms, write step %lu ms\n",
           (unsigned long)worstProbeOnEmptyPortsMs, (unsigned long)worstProbeMs,
           (unsigned long)worstPresenceMs, (unsigned long)worstRangeMs, (unsigned long)worstWriteMs);

    char message[288];
    snprintf(message, sizeof(message),
             "a cheap probe that heard nothing held the line %lu ms (limit %lu ms = one transaction)",
             (unsigned long)worstProbeOnEmptyPortsMs, (unsigned long)ONE_TRANSACTION_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(ONE_TRANSACTION_MS,
                                           (unsigned int)worstProbeOnEmptyPortsMs, message);

    snprintf(message, sizeof(message),
             "a cheap probe that heard the device on the paired port of the same wire held the "
             "line %lu ms (limit %lu ms = the model scan that refuses it)",
             (unsigned long)worstProbeMs, (unsigned long)ONE_MODEL_SCAN_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(ONE_MODEL_SCAN_MS, (unsigned int)worstProbeMs, message);

    snprintf(message, sizeof(message),
             "a presence poll that heard nothing held the line %lu ms (limit %lu ms = two of them)",
             (unsigned long)worstPresenceMs, (unsigned long)PRESENCE_POLL_BUDGET_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(PRESENCE_POLL_BUDGET_MS, (unsigned int)worstPresenceMs,
                                           message);
    snprintf(message, sizeof(message),
             "a range step that heard nothing held the line %lu ms (limit %lu ms = four reads)",
             (unsigned long)worstRangeMs, (unsigned long)RANGE_STEP_BUDGET_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(RANGE_STEP_BUDGET_MS, (unsigned int)worstRangeMs, message);
    snprintf(message, sizeof(message),
             "a write step that heard nothing held the line %lu ms (limit %lu ms = one model's "
             "address writes)",
             (unsigned long)worstWriteMs, (unsigned long)WRITE_STEP_BUDGET_MS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(WRITE_STEP_BUDGET_MS, (unsigned int)worstWriteMs, message);

    // The device on the paired port was never adopted twice: what the probe heard on port 3 is
    // the sensor on port 2, and the model scan is what refuses it.
    TEST_ASSERT_FALSE_MESSAGE((bool)module.sensorRepository.get(
                                  (std::string(MODULE_UID) + ".03").c_str()),
                              "the free port paired with the one the sensor was plugged into "
                              "adopted a sensor of its own");
}

// --------------------------------------------------------------------------------------------
// 7a. The same, with a device that takes its time to come up.
//
// A sensor is not ready the moment its port's power arrives - this firmware's own belief is that
// it takes ten seconds, which is what it waits after every power transition it makes before it
// talks to anything. A pass that raises a port's power and asks it something 300 ms later hears
// nothing whatever is plugged in, so it would leave every port it powers looking empty for ever,
// and the periodic rung would be a promise the board does not keep. This is that promise, on a
// device that behaves the way the firmware expects one to.
// --------------------------------------------------------------------------------------------

void test_s18_a_sensor_that_takes_time_to_come_up_is_found_too(void)
{
    ModuleUnderTest module;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(120000);
    // The promise is the periodic pass's, so the device arrives after the restart's is over.
    runUntilTheRestartsPassIsOver(module);

    const uint64_t pluggedInAtMs = module.clock.nowMs();

    SimRs485Bus::Device slow = makeFactoryFreshDevice(1);
    slow.wakeUpMs = 10000; // what SensorReader::scanModbusSensor waits for after a power change
    module.port2.addDevice(slow);

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS - 120000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const std::string plugged = std::string(MODULE_UID) + ".02";
    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(plugged.c_str());

    printf("  s18: the sensor that needs %u ms of power was %s after being plugged in at t=%lu ms\n",
           (unsigned)slow.wakeUpMs, adopted ? "adopted" : "never found",
           (unsigned long)pluggedInAtMs);

    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a sensor that needs its power for a few seconds before it answers "
                             "was never found - the pass asks the port before what is plugged into "
                             "it has come up");
    TEST_ASSERT_EQUAL_MESSAGE(2, adopted.value().pin,
                              "the sensor was adopted on a port it was not plugged into");
}

// --------------------------------------------------------------------------------------------
// 8. A port with something on it that matches no model of the catalog.
//
// It answers a broadcast, so the port is not empty; no model of the catalog claims it, so there is
// nothing to adopt. The module cannot tell the difference between a sensor it has never heard of,
// a device of another vendor sharing the line and a probe with a corrupted register map - and it
// does not have to: what it must not do is spend the identification procedure on it again and
// again for as long as it runs. One pass, a line in the log, a counter, the port described as
// empty - and the way back is the person's, through ScanSensors.
// --------------------------------------------------------------------------------------------

/** @brief A device that answers when addressed, at an address no model of the catalog knows. */
SimRs485Bus::Device makeUnidentifiableDevice(uint8_t address)
{
    SimRs485Bus::Device device;
    device.address = address;
    device.registerValues.assign(1, 250);
    return device;
}

void test_s18_a_port_that_matches_no_model_gets_one_identification_pass(void)
{
    ModuleUnderTest module;
    // Not at any factory address of the catalog (1 and 2), not in the module's own range
    // (0x70-0x73): the cheap rung cannot name it and the identification procedure cannot move it.
    module.port1.addDevice(makeUnidentifiableDevice(7));

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    // The restart's pass, and five minutes of the ladder running after it.
    runUntilTheRestartsPassIsOver(module);
    module.scheduler.runForMs(300000);

    const uint32_t identifyStepsAfterTheFirstPass = module.startupScan.identifyStepCount();
    const uint32_t unidentifiedAfterTheFirstPass = module.startupScan.unidentifiedOccupantCount();

    printf("  s18: the unidentifiable port cost %u identification step(s) in the restart's pass and "
           "five minutes after it, over %u cheap pass(es); %u port(s) given up on\n",
           (unsigned)identifyStepsAfterTheFirstPass,
           (unsigned)module.startupScan.freePortPassCount(),
           (unsigned)unidentifiedAfterTheFirstPass);

    // The port was heard, and the whole catalog was tried on it - once.
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.presencePollCount() >= 1,
                             "the port was never asked whether anything is on it");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, identifyStepsAfterTheFirstPass,
                                     "the identification rung did not try the two models of the "
                                     "catalog exactly once on the port that answered");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, unidentifiedAfterTheFirstPass,
                                     "the module did not record that a port answers and matches nothing");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() >= 5,
                             "the cheap passes stopped running - the module would not notice the "
                             "device leaving either");

    // The port is described as empty, exactly as a port with nothing on it is: the structure has
    // no way to say "something is here that I cannot name", and inventing one is a contract change.
    bool describedAsEmpty = false;
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        const Sensor& record = module.moduleStructure.nodes[0].sensors[i];
        if (record.uid == std::string(MODULE_UID) + ".01" && record.type == "none")
        {
            describedAsEmpty = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(describedAsEmpty,
                             "the port nothing could be identified on is not described as empty");

    // And it stays at one pass for as long as nobody asks: five more minutes of the module
    // running, with the device still answering every presence poll.
    module.scheduler.runForMs(300000);

    char message[224];
    snprintf(message, sizeof(message),
             "the identification procedure ran %u time(s) on a port that answers and matches "
             "nothing, in ten minutes of a module nobody touched",
             (unsigned)module.startupScan.identifyStepCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(identifyStepsAfterTheFirstPass,
                                     (uint32_t)module.startupScan.identifyStepCount(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.startupScan.unidentifiedPinCount(),
                                     "the port stopped being counted as unidentifiable while its "
                                     "device was still answering");

    // The way back: the command a person sends from the app. The identification procedure it runs is the
    // command's own - the same walk of the whole catalog on that pin the module has always done
    // for a ScanSensors - and the pass the discovery adds behind it drops what the last one
    // concluded and looks again.
    const size_t framesOnThePortBefore = module.port1.sentFrames.size();
    const uint32_t unidentifiedBefore = module.startupScan.unidentifiedOccupantCount();
    const uint64_t commandAtMs = module.clock.nowMs();
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-zombie", "ScanSensors", "{\"pin\":3}"));
    module.mqttManager.deliver(scanSensorsSignalJson());
    module.scheduler.runForMs(300000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    // What the command itself spent on that port: the identification procedure, held as one stretch of
    // line time - the isolation and its ten seconds of warm-up are inside it.
    uint64_t longestCommandWindowMs = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& window = module.busWindows[i];
        if (!window.discovery && window.startedAtMs >= commandAtMs &&
            window.lengthMs() > longestCommandWindowMs)
        {
            longestCommandWindowMs = window.lengthMs();
        }
    }

    printf("  s18: after a ScanSensors command the port had been asked %u more frame(s) over a "
           "%lu ms procedure of the command's own; the ladder's procedure had run %u time(s) in "
           "total and %u port(s) had been given up on (%u rescan request(s))\n",
           (unsigned)(module.port1.sentFrames.size() - framesOnThePortBefore),
           (unsigned long)longestCommandWindowMs,
           (unsigned)module.startupScan.identifyStepCount(),
           (unsigned)module.startupScan.unidentifiedOccupantCount(),
           (unsigned)module.startupScan.rescanRequestCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.startupScan.rescanRequestCount(),
                                     "the ScanSensors command did not reach the discovery");
    // The port really was walked again, and by the procedure rather than by the two cheap
    // questions of the pass: those are four frames and no isolation, this is the whole catalog
    // tried on the port with the port's power held throughout.
    TEST_ASSERT_TRUE_MESSAGE(longestCommandWindowMs > StartupSensorScanUseCase::PORT_WAKE_UP_MS,
                             "a ScanSensors command did not make the module try the port again - "
                             "there is no way back from an unidentified port");
    TEST_ASSERT_TRUE_MESSAGE((module.port1.sentFrames.size() - framesOnThePortBefore) > 8,
                             "the port was not asked more than the cheap questions of one pass");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(unidentifiedBefore + 1,
                                     (uint32_t)module.startupScan.unidentifiedOccupantCount(),
                                     "the discovery did not look at the port again after the "
                                     "command - the note the first pass left was never dropped");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.startupScan.unidentifiedPinCount(),
                                     "the port is held out of the identification rung more than "
                                     "once, or not at all, after a command");
}

// --------------------------------------------------------------------------------------------
// 8a. And what the module still owes that port: the cheap questions.
//
// The rule the pass over a working sensor's wire follows is about the broadcast poll and only it -
// that is the question that costs the sensor on the other end of the wire its power. A request
// naming an address costs it nothing, so the port goes on being asked one, which is what makes it
// a port somebody can still plug a sensor into. Without that, one unnameable device is enough to
// take a port out of the module for as long as it runs.
// --------------------------------------------------------------------------------------------

void test_s18_a_sensor_put_where_the_module_gave_up_is_found_without_a_command(void)
{
    ModuleUnderTest module;
    // Port 1 is on the same wire as the sensor the module reads on port 0.
    module.port1.addDevice(makeUnidentifiableDevice(7));

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    // Two minutes of the ladder running: the boot's pass starts when the boot's own minute is
    // over, and the port has to have been given up on before the sensor is swapped.
    module.scheduler.runForMs(StartupSensorScanUseCase::BOOT_SETTLE_MS + 120000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.startupScan.unidentifiedPinCount(),
                                     "the boot's pass did not give up on the port with something "
                                     "unnameable on it - there is nothing to measure");
    const uint32_t identifyStepsBefore = module.startupScan.identifyStepCount();
    const uint32_t neighbourPowerCyclesBefore = (uint32_t)module.port0.powerOffTransitions;
    const uint64_t replacedAtMs = module.clock.nowMs();

    // The person takes out whatever it was and plugs an ordinary sensor into the same port.
    module.port1.removeDevice(7);
    module.port1.addDevice(makeFactoryFreshDevice(1));

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS - 120000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const std::string replaced = std::string(MODULE_UID) + ".01";
    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(replaced.c_str());
    printf("  s18: the sensor put at t=%lu ms where the module had given up was %s; %u cheap "
           "probe(s), %u identification step(s), %u presence poll(s)\n",
           (unsigned long)replacedAtMs, adopted ? "adopted" : "never found",
           (unsigned)module.startupScan.cheapProbeCount(),
           (unsigned)module.startupScan.identifyStepCount(),
           (unsigned)module.startupScan.presencePollCount());

    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a sensor put where the module had given up identifying was never "
                             "found - one unnameable device took the port out of the module");
    TEST_ASSERT_EQUAL_MESSAGE(1, adopted.value().pin,
                              "the sensor was adopted on a port it was not plugged into");
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_NEW, adopted.value().status,
                              "the sensor found on the port did not get NEW status");

    // Found by the rung that costs the neighbour nothing: no broadcast poll went out on that wire,
    // and the working sensor was power-cycled only by the model scan that confirmed the new one.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(identifyStepsBefore,
                                     (uint32_t)module.startupScan.identifyStepCount(),
                                     "the identification procedure ran again on a port the module "
                                     "had already given up on");
    const unsigned neighbourCyclesForTheAdoption =
        (unsigned)module.port0.powerOffTransitions - (unsigned)neighbourPowerCyclesBefore;
    printf("  s18: the sensor sharing the wire lost its power %u time(s) before and %u after\n",
           (unsigned)neighbourPowerCyclesBefore, (unsigned)module.port0.powerOffTransitions);

    // What adopting the new sensor may cost the one already working on that wire: the model scan
    // that names it takes the other port of its wire down, and so does the one address write that
    // moves it off its model's address. A third cycle is a second write into a device that has already been
    // asked once, and this number is what shows it.
    char message[288];
    snprintf(message, sizeof(message),
             "adopting a sensor on the free port of the wire cost the working sensor %u power "
             "cycle(s) - one model scan and one address write is what it may cost",
             neighbourCyclesForTheAdoption);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(2, neighbourCyclesForTheAdoption, message);

    // And it is reported, which is what makes it appear in the app.
    bool reportedTwoOnline = false;
    for (size_t i = 0; i < module.server.updates.size(); i++)
    {
        if (module.server.updates[i].atMs > replacedAtMs && module.server.updates[i].onlineCount >= 2)
        {
            reportedTwoOnline = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(reportedTwoOnline,
                             "the sensor was adopted but never reported - the app would not show it");
}

// --------------------------------------------------------------------------------------------
// 8b. A device slower than the window a pass holds a port's power for.
//
// Every cheap answer of the ladder is bounded by PORT_WAKE_UP_MS: a pass raises the free ports'
// power, waits that long and asks. A device needing longer answers nothing, and its port is
// described as empty - by the boot's pass and by every periodic one. That bound is deliberate,
// and it is what buys the seconds-instead-of-minutes of the boot; what it must not do is leave
// the module with no way to such a device at all. The way is the identification procedure, which holds a
// port's power for as long as it walks the catalog - that is what a ScanSensors command runs on
// the port it names - and the pass the discovery adds behind that command, which asks the port
// the cheap questions once more at the end of that window, with the device by then awake.
// --------------------------------------------------------------------------------------------

// Between the two reaches, measured on this stand with the device below: a pass of cheap
// questions finds one needing up to 12 s and no more, a ScanSensors command finds one needing up
// to 31 s - the length of the procedure it holds the port's power for, which is a catalog of two
// models here and minutes against a field catalog. Anything in between is found by the command
// and by nothing else, which is what this test is about.
//
// The restart's own pass reaches further than a pass after it: it holds the free ports' power for
// as long as the windows of the ports walked before this one take. So the device here arrives
// after that pass, and what it meets is the passes the module makes on its own from then on.
const uint32_t SLOWER_THAN_A_PASS_MS = 24000;

void test_s18_a_sensor_slower_than_a_pass_is_found_by_the_command(void)
{
    ModuleUnderTest module;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    runUntilTheRestartsPassIsOver(module);

    // At the factory address of the first model of the catalog, on a free port.
    SimRs485Bus::Device slow = makeFactoryFreshDevice(1);
    slow.wakeUpMs = SLOWER_THAN_A_PASS_MS;
    module.port2.addDevice(slow);
    const uint64_t longestStretchOfTheRestartMs = module.port2.longestPoweredStretchMs();

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    const std::string plugged = std::string(MODULE_UID) + ".02";
    printf("  s18: after %u pass(es) over the free ports, the device needing %u ms of power was "
           "%s; the free port had power for at most %lu ms in one stretch (%lu ms in the "
           "restart's pass)\n",
           (unsigned)module.startupScan.freePortPassCount(), (unsigned)slow.wakeUpMs,
           module.sensorRepository.get(plugged.c_str()) ? "adopted" : "not found",
           (unsigned long)module.port2.longestPoweredStretchMs(),
           (unsigned long)longestStretchOfTheRestartMs);

    // The envelope, named: no pass the module makes on its own reaches this device.
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() >= 5,
                             "the passes over the free ports stopped - there is nothing to measure");
    TEST_ASSERT_FALSE_MESSAGE((bool)module.sensorRepository.get(plugged.c_str()),
                              "a pass of cheap questions found a device that needs its power for "
                              "longer than a pass holds it - the envelope is not what this test "
                              "says it is, and the escape below is not what finds it");

    // The person asks, which is the way back this ladder has always had.
    const uint32_t identifyStepsBeforeTheCommand = module.startupScan.identifyStepCount();
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-slow", "ScanSensors", "{\"pin\":2}"));
    module.mqttManager.deliver(scanSensorsSignalJson());
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(plugged.c_str());
    printf("  s18: after a ScanSensors command the same device was %s; the port had power for up "
           "to %lu ms in one stretch and the ladder's own procedure ran %u time(s)\n",
           adopted ? "adopted" : "still not found",
           (unsigned long)module.port2.longestPoweredStretchMs(),
           (unsigned)module.startupScan.identifyStepCount());

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.startupScan.rescanRequestCount(),
                                     "the ScanSensors command did not reach the discovery");
    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a ScanSensors command did not find a device that needs its power for "
                             "longer than a pass holds it - the module has no way to a slow sensor "
                             "at all, and the boot scan this replaced had one");
    TEST_ASSERT_EQUAL_MESSAGE(2, adopted.value().pin,
                              "the sensor was adopted on a port it was not plugged into");

    // By what: the command's own procedure held this port's power far past the wake-up the device
    // needs - the ladder's passes after the restart hold it for PORT_WAKE_UP_MS and no more - and
    // the cheap rung asked the port again at the end of that window. The stretch that counts began
    // after the restart's pass, so it is the one longer than that pass's too.
    const uint64_t neededMs = (longestStretchOfTheRestartMs > SLOWER_THAN_A_PASS_MS)
                                  ? longestStretchOfTheRestartMs
                                  : SLOWER_THAN_A_PASS_MS;
    char message[224];
    snprintf(message, sizeof(message),
             "the port's longest stretch of power was %lu ms, no longer than %lu ms - then it "
             "answered something this test is not about",
             (unsigned long)module.port2.longestPoweredStretchMs(), (unsigned long)neededMs);
    TEST_ASSERT_TRUE_MESSAGE(module.port2.longestPoweredStretchMs() > neededMs, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(identifyStepsBeforeTheCommand,
                                     (uint32_t)module.startupScan.identifyStepCount(),
                                     "the pass behind the command walked the identification "
                                     "procedure a second time on the port the command had just "
                                     "walked - every model of it costs the other ports their power");
}

// --------------------------------------------------------------------------------------------
// 8c. A ScanSensors command the module refuses.
//
// The pin a scan is about arrives in the command's options and is validated where every other
// complex command is validated. A command whose pin is missing, or names a port this module does
// not have, is answered with an error and executes nothing - and the discovery must hear about it
// exactly as much: a pass over the free ports is the identification procedure, minutes of it, and a
// mistyped field on the backend must not be able to order one.
// --------------------------------------------------------------------------------------------

/** @brief The reports the module sent for one command uid, in the order it sent them. */
std::vector<ModuleCommandUpdateDto> reportsFor(const SimServer& server, const char* commandUid)
{
    std::vector<ModuleCommandUpdateDto> found;
    for (size_t i = 0; i < server.moduleCommandReports.size(); i++)
    {
        for (size_t j = 0; j < server.moduleCommandReports[i].size(); j++)
        {
            if (server.moduleCommandReports[i][j].uid == commandUid)
            {
                found.push_back(server.moduleCommandReports[i][j]);
            }
        }
    }
    return found;
}

void test_s18_a_scan_command_the_module_refuses_asks_the_line_for_nothing(void)
{
    ModuleUnderTest module;
    module.port0.device(KNOWN_ADDRESS)->wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;
    // Something on a free port that matches no model: the port the module has given up on is the
    // one a pass would spend the procedure on, so this is the layout that makes a refused command
    // as expensive as it can be.
    module.port1.addDevice(makeUnidentifiableDevice(7));

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    const uint32_t identifyStepsBefore = module.startupScan.identifyStepCount();
    const uint32_t neighbourCyclesBefore = (uint32_t)module.port0.powerOffTransitions;
    const uint64_t commandAtMs = module.clock.nowMs();

    // Pin 9 on a four-port module: the module has no such port, and the command is answered
    // "Missing or invalid pin in options" - the same answer it has always given.
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-bad-pin", "ScanSensors", "{\"pin\":9}"));
    module.mqttManager.deliver(scanSensorsSignalJson());
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const std::vector<ModuleCommandUpdateDto> reports = reportsFor(module.server, "scan-bad-pin");
    const TelemetrySeen acrossTheCommand =
        telemetryBetween(module.server, commandAtMs, module.clock.nowMs());

    printf("  s18: a ScanSensors naming a pin the module has not was reported %s; it cost %u "
           "identification step(s), %u rescan request(s) and %u power cycle(s) of the working "
           "sensor's port\n",
           reports.empty() ? "not at all" : reports.back().status.c_str(),
           (unsigned)(module.startupScan.identifyStepCount() - identifyStepsBefore),
           (unsigned)module.startupScan.rescanRequestCount(),
           (unsigned)(module.port0.powerOffTransitions - neighbourCyclesBefore));

    // The module answered the way it always did.
    TEST_ASSERT_FALSE_MESSAGE(reports.empty(),
                              "the module never reported the command it refused");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Error", reports.back().status.c_str(),
                                     "a ScanSensors naming a pin the module has not was not "
                                     "refused");

    // And it ordered nothing: not a pass, not a step of the procedure, not a power cycle of the
    // sensor the module reads.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.startupScan.rescanRequestCount(),
                                     "a command the module refused still reached the discovery and "
                                     "ordered a pass over the free ports");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(identifyStepsBefore,
                                     (uint32_t)module.startupScan.identifyStepCount(),
                                     "a command the module refused still spent the identification "
                                     "procedure");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(neighbourCyclesBefore,
                                     (uint32_t)module.port0.powerOffTransitions,
                                     "a command the module refused still cost the working sensor "
                                     "its power");
    TEST_ASSERT_TRUE_MESSAGE(acrossTheCommand.sends >= 39 &&
                                 acrossTheCommand.online == acrossTheCommand.sends,
                             "the working sensor was reported offline after a command the module "
                             "refused to execute");
}

// --------------------------------------------------------------------------------------------
// 9. The pathology the ladder must not make worse: every free port with something on it that
//    matches no model.
//
// This is the case where every cheap question is answered with silence and every expensive one has
// to be asked anyway - so the ladder pays the whole of what the discovery paid before it, plus the
// cheap rung on top. What must hold is that the "on top" is a rounding error against the
// procedure it decides about, and that the procedure itself runs exactly as many times as it did.
// --------------------------------------------------------------------------------------------

void test_s18_the_worst_case_costs_no_more_than_the_procedure_it_rations(void)
{
    ModuleUnderTest module;
    module.port1.addDevice(makeUnidentifiableDevice(7));
    module.port2.addDevice(makeUnidentifiableDevice(8));
    module.port3.addDevice(makeUnidentifiableDevice(9));
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        static_cast<SimRs485Bus*>(module.ports[i])->recordSendTimes(true);
    }

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const uint64_t identifyMs = totalWindowMs(module.busWindows, StartupScanStep::SCAN_CANDIDATE);
    const uint64_t cheapMs = totalWindowMs(module.busWindows, StartupScanStep::PROBE_CANDIDATE);
    const uint64_t presenceMs = totalWindowMs(module.busWindows, StartupScanStep::PROBE_PRESENCE);

    printf("  s18: three unidentifiable ports cost %lu ms of identification, %lu ms of cheap "
           "probes and %lu ms of presence polls over ten minutes\n",
           (unsigned long)identifyMs, (unsigned long)cheapMs, (unsigned long)presenceMs);

    // The expensive rung ran exactly the matrix the discovery has always run on three free ports:
    // every model of the catalog on every one of them, once.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3 * 2, (uint32_t)module.startupScan.identifyStepCount(),
                                     "the identification procedure did not run exactly the matrix "
                                     "of models by free ports");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, (uint32_t)module.startupScan.unidentifiedOccupantCount(),
                                     "the three ports that answered and matched nothing were not "
                                     "all recorded");
    TEST_ASSERT_TRUE_MESSAGE(identifyMs > 0, "the procedure never ran - there is nothing to compare");

    // What the ladder adds on top of it. Two numbers rather than one, because the two costs are of
    // different kinds: the procedure runs once per port and the cheap rungs run every interval for
    // as long as the module does, so their ratio is a measure of the observation window and not of
    // either of them.
    //
    // Per pass, against the procedure it decides about: this is the "rounding error" claim, and it
    // is the one that does not depend on how long anybody watched. A pass asks every free port the
    // module's range beside the models' own addresses - five questions per model - and that is a few
    // percent of the procedure, not one.
    char message[320];
    const uint64_t passes = module.startupScan.freePortPassCount() + 1; // and the restart's own
    const uint64_t ladderMsPerPass = (cheapMs + presenceMs) / passes;
    const uint64_t perPassPercent = ladderMsPerPass * 100 / identifyMs;
    snprintf(message, sizeof(message),
             "one pass of the cheap rungs costs %lu ms against the %lu ms of the identification "
             "procedure it decides about - %lu%% of it",
             (unsigned long)ladderMsPerPass, (unsigned long)identifyMs,
             (unsigned long)perPassPercent);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(5, (unsigned int)perPassPercent, message);

    // And in the steady state: what share of the module's own time the ladder holds the line for,
    // on the worst module there is - three ports with something on them that matches nothing.
    const uint64_t windowMs = module.clock.nowMs();
    const uint64_t sharePercent = (cheapMs + presenceMs) * 100 / windowMs;
    snprintf(message, sizeof(message),
             "the ladder held the line for %lu ms of the %lu ms the module ran - %lu%% of it, on "
             "three ports it has already given up identifying",
             (unsigned long)(cheapMs + presenceMs), (unsigned long)windowMs,
             (unsigned long)sharePercent);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(10, (unsigned int)sharePercent, message);

    // And the module went on being a module throughout: the stored sensor kept being read and
    // reported while the line was busy with all of it.
    uint64_t worstUpdateGapMs = 0;
    uint64_t previousMs = 0;
    for (size_t i = 0; i < module.server.updates.size(); i++)
    {
        if (i > 0 && (module.server.updates[i].atMs - previousMs) > worstUpdateGapMs)
        {
            worstUpdateGapMs = module.server.updates[i].atMs - previousMs;
        }
        previousMs = module.server.updates[i].atMs;
    }

    printf("  s18: the module sent %u update(s) while all that ran, the longest gap %lu ms\n",
           (unsigned)module.server.updates.size(), (unsigned long)worstUpdateGapMs);

    TEST_ASSERT_TRUE_MESSAGE(module.server.firstOnlineUpdateMs() > 0 &&
                                 module.server.firstOnlineUpdateMs() < 60000,
                             "the stored sensor was not reported in the first minute");
    TEST_ASSERT_TRUE_MESSAGE(module.server.updates.size() >= 20,
                             "the telemetry of the stored sensor dried up while the ladder ran");

    // And no step of a pass asked a port before it had come up: the passes after the restart poll
    // ports 2 and 3 one after the other - each poll takes the other port of its wire down and gives
    // it back - and the questions to that port wait for its wake-up without holding the line to
    // wait (the cost above). The steps' own frames: when the sweep first reads the stored sensor is
    // the boot's matter (test_s18_a_stored_sensor_answers_the_first_read_of_the_first_sweep).
    unsigned early = 0;
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        const SimRs485Bus* port = static_cast<const SimRs485Bus*>(module.ports[i]);
        for (size_t f = 0; f < port->sentAtUs.size() && f < port->sentAfterPowerForMs.size(); f++)
        {
            const uint64_t atMs = port->sentAtUs[f] / 1000;
            bool ofAStep = false;
            for (size_t w = 0; w < module.busWindows.size() && !ofAStep; w++)
            {
                ofAStep = module.busWindows[w].discovery && atMs >= module.busWindows[w].startedAtMs &&
                          atMs < module.busWindows[w].endedAtMs;
            }
            if (!ofAStep || port->sentAfterPowerForMs[f] >= StartupSensorScanUseCase::PORT_WAKE_UP_MS)
            {
                continue;
            }
            early++;
            if (early == 1)
            {
                snprintf(message, sizeof(message),
                         "a frame went into port %u at t=%lu ms after %lu ms of power - before the "
                         "%lu ms a sensor is given to come up",
                         (unsigned)i, (unsigned long)(port->sentAtUs[f] / 1000),
                         (unsigned long)port->sentAfterPowerForMs[f],
                         (unsigned long)StartupSensorScanUseCase::PORT_WAKE_UP_MS);
            }
        }
    }
    printf("  s18: %u frame(s) of a step went into a port before its wake-up\n", early);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, early, message);
}

// --------------------------------------------------------------------------------------------
// 10. The module a person builds with the app: a sensor added after the restart keeps the factory
//     address of its model, and the port sharing its wire is free.
//
// That is not a corner: ModuleCommandExecutionUseCase::resolveModbusAddress() tries the model's own
// address first and leaves the sensor on it whenever nobody else holds it, so the first sensor of
// each model a person adds sits there until the next restart moves it. Meanwhile the cheap rung
// asks the free port of that wire for that very address twice a minute, and is answered - by the
// sensor of the paired port. Refusing to adopt on that is right and the confirming scan is what
// makes the refusal safe, but the confirmation takes the power off the other port of the wire, the
// working sensor's.
//
// So the module may pay it once, to find out where the answer comes from, and then it must know.
// --------------------------------------------------------------------------------------------

/** The address the app leaves a sensor of the first catalog model on. */
const uint8_t A_MODEL_FACTORY_ADDRESS = 1;

/** The register a model that takes a new address keeps it in, as its catalog row would say. */
const uint16_t ADDRESS_STORE_REGISTER = 0x0064;

// A module nobody is touching may lose a working port its power for a discovery it started -
// once, to learn something it did not know. Twice a minute for ten minutes is not that.
const unsigned POWER_CYCLES_A_QUIET_MODULE_MAY_PAY = 1;

/** @brief A sensor of the same model as the stored one, on another port, at the given address. */
SensorEntity makeStoredSensorOn(int pin, uint8_t address)
{
    SensorEntity sensor = makeKnownSensor();
    char uid[32];
    snprintf(uid, sizeof(uid), "%s.%02d", MODULE_UID, pin);
    sensor.uid = uid;
    sensor.pin = pin;
    sensor.modbusDeviceAddress = address;
    return sensor;
}

/** @brief The device such a sensor is: it answers, and it needs its ten seconds of power first. */
SimRs485Bus::Device makeWorkingDevice(uint8_t address)
{
    SimRs485Bus::Device device;
    device.address = address;
    device.registerValues.assign(1, 250);
    device.wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;
    return device;
}

/** @brief How many times the module wrote the register a device keeps its ModBus address in. */
unsigned addressStoreWrites(const SimRs485Bus& port, uint16_t storeRegister)
{
    unsigned count = 0;
    for (size_t i = 0; i < port.writes.size(); i++)
    {
        if (port.writes[i].reg == (int)storeRegister)
        {
            count++;
        }
    }
    return count;
}

/** @brief Updates of a window, and how many of them carried at least this many sensors online. */
struct OnlineSeen {
    size_t sends = 0;
    size_t carrying = 0;
};

OnlineSeen updatesCarrying(const SimServer& server, size_t onlineAtLeast, uint64_t fromMs,
                           uint64_t untilMs)
{
    OnlineSeen seen;
    for (size_t i = 0; i < server.updates.size(); i++)
    {
        if (server.updates[i].atMs < fromMs || server.updates[i].atMs > untilMs)
        {
            continue;
        }
        seen.sends++;
        if (server.updates[i].onlineCount >= onlineAtLeast)
        {
            seen.carrying++;
        }
    }
    return seen;
}

void test_s18_a_free_port_pays_once_for_an_answer_that_came_from_its_wire(void)
{
    ModuleUnderTest module;
    module.port0.device(KNOWN_ADDRESS)->wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(120000);

    // The person adds a sensor from the app: the row appears on port 2 holding the factory address
    // of the model, and the device behind it is one whose address no write of the module's ever
    // takes - the model the address phase of a restart cannot move either.
    module.sensorRepository.save(makeStoredSensorOn(2, A_MODEL_FACTORY_ADDRESS));
    module.port2.addDevice(makeWorkingDevice(A_MODEL_FACTORY_ADDRESS));

    // Long enough for the discovery to hear the new sensor from port 3 and find out what it is.
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    // And now the ten minutes that are measured: a settled module, nobody touching it.
    const uint64_t steadyFromMs = module.clock.nowMs();
    const unsigned knownPortCyclesBefore = (unsigned)module.port0.powerOffTransitions;
    const unsigned addedPortCyclesBefore = (unsigned)module.port2.powerOffTransitions;
    const unsigned passesBefore = (unsigned)module.startupScan.freePortPassCount();

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const unsigned knownPortCycles =
        (unsigned)module.port0.powerOffTransitions - knownPortCyclesBefore;
    const unsigned addedPortCycles =
        (unsigned)module.port2.powerOffTransitions - addedPortCyclesBefore;
    const unsigned passes = (unsigned)module.startupScan.freePortPassCount() - passesBefore;
    const OnlineSeen steady = updatesCarrying(module.server, 2, steadyFromMs, module.clock.nowMs());

    printf("  s18: in ten settled minutes with a sensor on a factory address the free ports made "
           "%u pass(es); the working ports lost power %u / %u time(s); %u of %u update(s) carried "
           "both sensors\n",
           passes, knownPortCycles, addedPortCycles,
           (unsigned)steady.carrying, (unsigned)steady.sends);

    // The premise: the passes really went on running, and the module really was reporting. A
    // pass every half minute is fourteen or fifteen of them in ten minutes; five is where "the
    // cheap rung is still running" ends, and it is deliberately far below what a pass costs, so
    // that a module paying the confirming scan again and again fails on what it costs rather
    // than on how few passes it got through.
    TEST_ASSERT_TRUE_MESSAGE(passes >= 5,
                             "the cheap rung stopped running - there is nothing to measure");
    TEST_ASSERT_TRUE_MESSAGE(steady.sends >= 30, "the module stopped talking to the server");

    char message[288];
    snprintf(message, sizeof(message),
             "the port of the working sensor lost its power %u time(s) in ten minutes of a module "
             "nobody touched (budget %u) - the free port of its wire is paying the confirming scan "
             "at every pass for an answer the module has already accounted for",
             knownPortCycles, POWER_CYCLES_A_QUIET_MODULE_MAY_PAY);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(POWER_CYCLES_A_QUIET_MODULE_MAY_PAY, knownPortCycles,
                                           message);

    snprintf(message, sizeof(message),
             "the port of the sensor on the factory address lost its power %u time(s) in ten "
             "settled minutes (budget %u)",
             addedPortCycles, POWER_CYCLES_A_QUIET_MODULE_MAY_PAY);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(POWER_CYCLES_A_QUIET_MODULE_MAY_PAY, addedPortCycles,
                                           message);

    snprintf(message, sizeof(message),
             "only %u of the %u updates of a settled module carried both its sensors - the "
             "discovery is taking the telemetry down with it",
             (unsigned)steady.carrying, (unsigned)steady.sends);
    TEST_ASSERT_TRUE_MESSAGE(steady.carrying + 2 >= steady.sends, message);

    // The port is not written off, either: it is the address that is spoken for, and the other
    // model of the catalog is still asked of that port at every pass.
    TEST_ASSERT_TRUE_MESSAGE(module.port3.sentFrames.size() > 0,
                             "the free port of the shared wire was never asked anything at all");
}

// --------------------------------------------------------------------------------------------
// 11. What the module does about it: it moves the sensor off the factory address, and the free
//     port goes back to being asked.
//
// The note of test 10 is the cheap half of the answer and it is not the whole of it: while the
// sensor holds the factory address, a second device of the same model plugged into the free port
// of that wire answers at the same address and is not told apart from it. The address phase of a
// restart would move the sensor - every address outside the module's own range is moved - and this
// is that same act, performed when the module finds out rather than at the next restart: the same
// writeSensorAddress() sequence, the same allocator, the address of the sensor's own port.
//
// What it buys is measured here by plugging the twin in afterwards.
// --------------------------------------------------------------------------------------------

void test_s18_the_sensor_a_free_port_keeps_hearing_is_moved_off_the_factory_address(void)
{
    ModuleUnderTest module;
    module.port0.device(KNOWN_ADDRESS)->wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(120000);
    // Added after the restart's pass: the move this test is about is a pass's, not the restart's
    // address phase.
    runUntilTheRestartsPassIsOver(module);

    // The same sensor added from the app as in test 10, except that this one is a model whose
    // address the module can write: it keeps it in a holding register and moves when that register
    // is written, which is what the address write does.
    SensorEntity added = makeStoredSensorOn(2, A_MODEL_FACTORY_ADDRESS);
    added.registerToStoreModbusDeviceAddress = ADDRESS_STORE_REGISTER;
    added.writeRegistersCommand = 6;
    module.sensorRepository.save(added);

    SimRs485Bus::Device device = makeWorkingDevice(A_MODEL_FACTORY_ADDRESS);
    device.addressIsWritable = true;
    device.addressStoreRegister = ADDRESS_STORE_REGISTER;
    module.port2.addDevice(device);

    const uint32_t structuresBefore = module.server.structureSends;
    const uint64_t addedAtMs = module.clock.nowMs();

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    Utils::Optional<SensorEntity> moved = module.sensorRepository.get(added.uid.c_str());
    const OnlineSeen acrossTheMove =
        updatesCarrying(module.server, 2, addedAtMs, module.clock.nowMs());

    printf("  s18: the sensor added at 0x%02X is now at 0x%02X under uid %s; %u of %u update(s) "
           "carried both sensors while it moved; the module sent %u structure(s) and wrote the "
           "address register of that device %u time(s)\n",
           A_MODEL_FACTORY_ADDRESS,
           moved ? (unsigned)moved.value().modbusDeviceAddress : 0,
           moved ? moved.value().uid.c_str() : "(gone)",
           (unsigned)acrossTheMove.carrying, (unsigned)acrossTheMove.sends,
           (unsigned)(module.server.structureSends - structuresBefore),
           addressStoreWrites(module.port2, ADDRESS_STORE_REGISTER));

    // The row is the same row - the uid is what the server knows a sensor by, and an address is
    // not part of it - and it is on the address of its own port now.
    TEST_ASSERT_TRUE_MESSAGE((bool)moved, "the sensor added from the app left the registry");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(added.uid.c_str(), moved.value().uid.c_str(),
                                     "the move changed the uid the server knows the sensor by");
    char message[288];
    snprintf(message, sizeof(message),
             "the sensor the free port kept hearing is still on address 0x%02X - the module heard "
             "the same answer at every pass and never took the address back",
             (unsigned)moved.value().modbusDeviceAddress);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 2,
                                   (unsigned)moved.value().modbusDeviceAddress, message);

    // One move, and the pass that made it had two reasons to: the registry finds the row on a
    // factory address when the pass opens, and the free port of its wire is answered from that row
    // during the same pass. A write per reason would isolate the wire a second time
    // and aim at the address the first write has already taken the device off. What the line saw
    // first, then what the module counted.
    snprintf(message, sizeof(message),
             "the address register of the device was written %u time(s) - one attempt is one write "
             "to the device and at most one broadcast retry of it",
             addressStoreWrites(module.port2, ADDRESS_STORE_REGISTER));
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(2, addressStoreWrites(module.port2, ADDRESS_STORE_REGISTER),
                                           message);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)module.startupScan.factoryAddressMoveCount(),
                                   "the module moved one row off its factory address more than "
                                   "once");

    // It went on being a sensor across its own move: what the move costs it is one isolation and
    // the warm-up after it - two updates of the forty, measured. Five is that with room, and far
    // below the nine a pass costs when the confirming scan is paid at every one of them.
    TEST_ASSERT_TRUE_MESSAGE(acrossTheMove.carrying + 5 >= acrossTheMove.sends,
                             "the module lost the telemetry of a sensor while moving it off the "
                             "factory address");

    // And the app is not told anything new about it: the uid is unchanged, so the record is
    // replaced rather than added, and nothing but the sixty-second debounce of the liveness review
    // may send a structure at all.
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(1, (unsigned)(module.server.structureSends - structuresBefore),
                                           "the module described itself to the server again for an "
                                           "address change nothing outside it can see");
    int recordsOfTheMovedRow = 0;
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        if (module.moduleStructure.nodes[0].sensors[i].uid == added.uid)
        {
            recordsOfTheMovedRow++;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, recordsOfTheMovedRow,
                              "the moved sensor is in the structure more than once");

    // The point of moving it: the factory address is nobody's again, so a second device of the
    // same model - the twin this module could not have told from its neighbour - is found on the
    // free port of that same wire, with nobody touching the module.
    const size_t rowsBeforeTheTwin = module.sensorRepository.getAll().size();
    module.port3.addDevice(makeFactoryFreshDevice(A_MODEL_FACTORY_ADDRESS));
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const std::vector<SensorEntity> rows = module.sensorRepository.getAll();
    bool twinOnPin3 = false;
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (rows[i].pin == 3)
        {
            twinOnPin3 = true;
        }
    }

    printf("  s18: after the move the registry went from %u to %u row(s) when a twin of the same "
           "model was plugged into the free port of that wire\n",
           (unsigned)rowsBeforeTheTwin, (unsigned)rows.size());

    TEST_ASSERT_EQUAL_UINT_MESSAGE(rowsBeforeTheTwin + 1, (unsigned)rows.size(),
                                   "a second device of the same model, plugged into the free port "
                                   "of the wire, was not adopted after the first one had been "
                                   "moved off the factory address");
    TEST_ASSERT_TRUE_MESSAGE(twinOnPin3, "the adopted row is not on the port the twin is on");
}

// --------------------------------------------------------------------------------------------
// 12. The other half of the trade, stated as a test: what the note costs while it holds.
//
// A model whose address the module cannot write keeps the note instead of moving, and while the
// note holds, that port is not asked that address - so a second device of the same model plugged
// into it is not found by the passes the module makes on its own. That is the same as this module
// before the ladder existed, which had no periodic pass at all; what makes it acceptable is that
// the way back is the one a person already uses. This test holds both halves: the note hides the
// twin, and the ScanSensors command finds it.
// --------------------------------------------------------------------------------------------

void test_s18_a_hand_asked_scan_finds_a_twin_the_note_holds_off(void)
{
    ModuleUnderTest module;
    module.port0.device(KNOWN_ADDRESS)->wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(120000);
    runUntilTheRestartsPassIsOver(module);

    // What the restart cost the working port, so that what the row costs it can be told apart.
    const unsigned cyclesBeforeTheRow = (unsigned)module.port0.powerOffTransitions;

    // The sensor a person added, on a model whose address no write of this module's ever takes.
    module.sensorRepository.save(makeStoredSensorOn(2, A_MODEL_FACTORY_ADDRESS));
    module.port2.addDevice(makeWorkingDevice(A_MODEL_FACTORY_ADDRESS));
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    // A twin of it goes into the free port of the same wire, at the same factory address.
    const size_t rowsBeforeTheTwin = module.sensorRepository.getAll().size();
    const unsigned cyclesAfterTheRowWindow = (unsigned)module.port0.powerOffTransitions;
    module.port3.addDevice(makeFactoryFreshDevice(A_MODEL_FACTORY_ADDRESS));
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    const size_t rowsBeforeTheCommand = module.sensorRepository.getAll().size();
    const unsigned cyclesWhileTheNoteHeld = (unsigned)module.port0.powerOffTransitions;
    const unsigned cyclesForTheAttempt = cyclesAfterTheRowWindow - cyclesBeforeTheRow;
    const unsigned cyclesWithTheTwinPluggedIn = cyclesWhileTheNoteHeld - cyclesAfterTheRowWindow;

    printf("  s18: with the note holding, the registry went from %u to %u row(s) in the ten "
           "minutes after a twin was plugged into the free port of the wire; the working port lost "
           "power %u time(s) for the attempt and %u after it\n",
           (unsigned)rowsBeforeTheTwin, (unsigned)rowsBeforeTheCommand, cyclesForTheAttempt,
           cyclesWithTheTwinPluggedIn);

    // What the ten minutes around the single attempt cost the sensor of another port: the address
    // write isolates the wire it writes on, and the free port of that sensor's wire may cost it
    // one presence poll. Two reasons put this row in the plan of that one pass - the registry finds
    // it on a factory address when the pass opens, and the free port of its wire is answered from
    // it during the pass - and if the pass wrote once per reason, the sensor would pay a third
    // cycle for a second write aimed at an address the first one has already left.
    char message[288];
    snprintf(message, sizeof(message),
             "moving one row off its factory address cost the working sensor of another port %u "
             "power cycle(s) - one address write and one presence poll is what it may cost",
             cyclesForTheAttempt);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(2, cyclesForTheAttempt, message);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(rowsBeforeTheTwin, (unsigned)rowsBeforeTheCommand,
                                   "the passes the module makes on its own adopted a device at an "
                                   "address a sensor of the same wire is known to answer at - the "
                                   "note is not holding what it is there to hold");

    // The person presses scan on that port. Pin 1 of the app's numbering is port 3 of the board.
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-twin", "ScanSensors", "{\"pin\":1}"));
    module.mqttManager.deliver(scanSensorsSignalJson());
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const std::vector<SensorEntity> rows = module.sensorRepository.getAll();
    bool twinOnPin3 = false;
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (rows[i].pin == 3)
        {
            twinOnPin3 = true;
        }
    }

    printf("  s18: after a ScanSensors on that port the registry holds %u row(s); the working "
           "port lost power %u time(s) while the note held and %u across the command\n",
           (unsigned)rows.size(), cyclesWhileTheNoteHeld,
           (unsigned)module.port0.powerOffTransitions - cyclesWhileTheNoteHeld);

    TEST_ASSERT_TRUE_MESSAGE(twinOnPin3,
                             "a ScanSensors on the port the twin is in did not find it - the note "
                             "is holding off the pass a person asked for, which is the one way "
                             "back this trade rests on");
}

// --------------------------------------------------------------------------------------------
// 14. The invariant itself, on a module with nothing free: a row that kept the factory address of
//     its model is moved off it as soon as the module has read it, and that needs no free port.
//
// The two mechanisms above both start from a free port hearing something. This one starts from the
// registry: a row on the address its model leaves the factory with is a row a second device of that
// model answers for, whether or not there is a free port on its wire to hear it. Every port of this
// module is taken - the wire the row sits on has no free port at all, so no cheap probe, no
// presence poll and no confirming scan happens anywhere - and the move still happens, between two
// sweeps, on the same WRITE_ADDRESS step the restart's address phase uses.
//
// The scenario is the one a person makes: the sensor is added in the app before it is plugged in
// (the row is written, the device is not there), and plugged in minutes later.
// --------------------------------------------------------------------------------------------

/** @brief A stored row of a model the module can move: it keeps its address in a register. */
SensorEntity makeMovableRowOn(int pin, uint8_t address)
{
    SensorEntity row = makeStoredSensorOn(pin, address);
    row.registerToStoreModbusDeviceAddress = ADDRESS_STORE_REGISTER;
    row.writeRegistersCommand = 6;
    return row;
}

/** @brief The device such a row is: it answers, takes an address, and needs its power first. */
SimRs485Bus::Device makeMovableDevice(uint8_t address)
{
    SimRs485Bus::Device device = makeWorkingDevice(address);
    device.addressIsWritable = true;
    device.addressStoreRegister = ADDRESS_STORE_REGISTER;
    return device;
}

void test_s18_a_row_that_kept_the_factory_address_is_moved_off_it_with_nothing_free(void)
{
    ModuleUnderTest module;
    module.port0.device(KNOWN_ADDRESS)->wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;

    // A module with a sensor on every port: ports 1 and 3 fill the two wires, so there is no free
    // port anywhere and nothing can be heard for anybody.
    module.sensorRepository.save(makeStoredSensorOn(1, MODBUS_ADDRESS_MIN + 1));
    module.sensorRepository.save(makeStoredSensorOn(3, MODBUS_ADDRESS_MIN + 3));
    module.port1.addDevice(makeWorkingDevice(MODBUS_ADDRESS_MIN + 1));
    module.port3.addDevice(makeWorkingDevice(MODBUS_ADDRESS_MIN + 3));

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    // Sampled with the line idle: the counts below are about what the ladder asks once every port
    // is taken, and a pass caught in the middle of its walk still owes the step it planned while
    // the port was free.
    for (int guard = 0; guard < 60 && module.startupScan.aPassIsInFlight(); guard++)
    {
        module.scheduler.runForMs(1000);
    }

    // The person adds the fourth sensor in the app and goes to fetch it: the row is written on the
    // factory address of its model and there is nothing on the port yet.
    SensorEntity added = makeMovableRowOn(2, A_MODEL_FACTORY_ADDRESS);
    module.sensorRepository.save(added);
    const unsigned cyclesBeforeTheDevice = (unsigned)module.port0.powerOffTransitions;
    // From this moment every port of the module is taken. What the ladder asked the free ports
    // before it - port 2 was one of them - is what the numbers below are counted from.
    const unsigned probesWhileAPortWasFree = (unsigned)module.startupScan.cheapProbeCount();
    const unsigned pollsWhileAPortWasFree = (unsigned)module.startupScan.presencePollCount();

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    Utils::Optional<SensorEntity> beforeThePlugIn = module.sensorRepository.get(added.uid.c_str());
    TEST_ASSERT_TRUE_MESSAGE((bool)beforeThePlugIn, "the row the app added left the registry");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(A_MODEL_FACTORY_ADDRESS,
                                   (unsigned)beforeThePlugIn.value().modbusDeviceAddress,
                                   "the module moved a row nothing had ever answered for");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.startupScan.factoryAddressMoveCount(),
                                   "the module wrote an address into a port with nothing on it");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.startupScan.factoryAddressMoveRefusedCount(),
                                   "the module spent an address write on a row nothing answers for");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(cyclesBeforeTheDevice, (unsigned)module.port0.powerOffTransitions,
                                   "the working ports paid an isolation for a device that is not there");

    // They come back and plug it in.
    const uint64_t pluggedInAtMs = module.clock.nowMs();
    const uint32_t structuresBefore = module.server.structureSends;
    module.port2.addDevice(makeMovableDevice(A_MODEL_FACTORY_ADDRESS));

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    Utils::Optional<SensorEntity> moved = module.sensorRepository.get(added.uid.c_str());
    const OnlineSeen acrossTheMove =
        updatesCarrying(module.server, 4, pluggedInAtMs, module.clock.nowMs());

    printf("  s18: with every port taken, the row plugged in at t=%lu ms went from 0x%02X to 0x%02X; "
           "the ladder made %u cheap probe(s) and %u presence poll(s) after the last port was "
           "taken; %u of %u update(s) carried all four sensors; %u structure(s)\n",
           (unsigned long)pluggedInAtMs, A_MODEL_FACTORY_ADDRESS,
           moved ? (unsigned)moved.value().modbusDeviceAddress : 0,
           (unsigned)module.startupScan.cheapProbeCount() - probesWhileAPortWasFree,
           (unsigned)module.startupScan.presencePollCount() - pollsWhileAPortWasFree,
           (unsigned)acrossTheMove.carrying, (unsigned)acrossTheMove.sends,
           (unsigned)(module.server.structureSends - structuresBefore));

    TEST_ASSERT_TRUE_MESSAGE((bool)moved, "the row the app added left the registry");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 2,
                                   (unsigned)moved.value().modbusDeviceAddress,
                                   "the row is still on the address its model leaves the factory "
                                   "on - nothing but a restart would ever take it off");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(added.uid.c_str(), moved.value().uid.c_str(),
                                     "the move changed the uid the server knows the sensor by");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)module.startupScan.factoryAddressMoveCount(),
                                   "the row was moved by something other than the pass that owes "
                                   "the move, or moved more than once");

    // What did not happen: no port was free, so no rung of the ladder ran at all. The move is the
    // registry's own, and it needs neither a free port nor a device answering for one.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(probesWhileAPortWasFree,
                                   (unsigned)module.startupScan.cheapProbeCount(),
                                   "a module with every port taken put a cheap probe on the line");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(pollsWhileAPortWasFree,
                                   (unsigned)module.startupScan.presencePollCount(),
                                   "a module with every port taken broadcast a presence poll");

    // And the module went on being a module across it: the sweep kept reading all four rows, and
    // the address is not something the structure carries, so the server hears about the move only
    // as the row coming online - one structure, held to the liveness debounce.
    TEST_ASSERT_TRUE_MESSAGE(acrossTheMove.carrying + 5 >= acrossTheMove.sends,
                             "the module lost the telemetry of its sensors while moving a row off "
                             "the factory address");
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(1, (unsigned)(module.server.structureSends - structuresBefore),
                                           "the module described itself to the server more often "
                                           "than the liveness of one row can account for");
    int recordsOfTheMovedRow = 0;
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        if (module.moduleStructure.nodes[0].sensors[i].uid == added.uid)
        {
            recordsOfTheMovedRow++;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, recordsOfTheMovedRow,
                              "the moved sensor is in the structure more than once");
}

// --------------------------------------------------------------------------------------------
// 15. The other entrance to the same invariant: a sensor added from the app.
//
// The command is where a sensor usually enters the registry, and it must not leave the device on
// the address it answered at just because nobody else holds it. Here the whole path runs - the server
// command, the bus half on the acquisition task, the real ModbusSensorReader over the simulated
// line, the isolate-write-verify sequence - and what is checked is not only the row but
// the device: it answers at the module's address and nowhere else afterwards.
//
// The ladder is switched off for this module (noScanSensors), so nothing but the command can
// touch the line: the invariant does not depend on the periodic pass existing.
// --------------------------------------------------------------------------------------------

void test_s18_a_sensor_added_from_the_app_lands_on_an_address_of_the_module(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);

    // A model the module can address - it keeps its ModBus address in a holding register - and a
    // device of it, fresh from the factory, on port 2.
    SensorType movable = makeCatalogType("type-movable", "S18-MOVABLE", A_MODEL_FACTORY_ADDRESS);
    movable.registerToStoreModbusDeviceAddress = ADDRESS_STORE_REGISTER;
    movable.writeRegistersCommand = 6;
    module.sensorTypeRepository.addSensorType(movable);
    module.port2.addDevice(makeMovableDevice(A_MODEL_FACTORY_ADDRESS));

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(120000);

    // The person picks the model in the app for the port they have just plugged it into.
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("set-model-1", "SetSensorModel", "{\"pin\":2,\"uid\":\"type-movable\"}"));
    module.mqttManager.deliver(scanSensorsSignalJson());

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const std::string added = std::string(MODULE_UID) + ".02";
    Utils::Optional<SensorEntity> row = module.sensorRepository.get(added.c_str());
    const bool deviceAnswersAtTheModulesAddress =
        module.port2.device(MODBUS_ADDRESS_MIN + 2) != nullptr;
    const bool deviceStillAtTheFactoryAddress =
        module.port2.device(A_MODEL_FACTORY_ADDRESS) != nullptr;

    printf("  s18: the sensor added from the app on port 2 is stored at 0x%02X, and the device "
           "answers at 0x%02X / 0x%02X (module address / factory address)\n",
           row ? (unsigned)row.value().modbusDeviceAddress : 0,
           deviceAnswersAtTheModulesAddress ? MODBUS_ADDRESS_MIN + 2 : 0,
           deviceStillAtTheFactoryAddress ? A_MODEL_FACTORY_ADDRESS : 0);

    TEST_ASSERT_TRUE_MESSAGE((bool)row, "the command did not save the sensor at all");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 2, (unsigned)row.value().modbusDeviceAddress,
                                   "the row was saved on the address the model leaves the factory "
                                   "on - it would keep it until the next restart");
    // The row and the device agree, and that is the half no mock can show: the write really went
    // down the wire as the identification sequence, and the device took it.
    TEST_ASSERT_TRUE_MESSAGE(deviceAnswersAtTheModulesAddress,
                             "the device does not answer at the address the row was saved on");
    TEST_ASSERT_FALSE_MESSAGE(deviceStillAtTheFactoryAddress,
                              "the device still answers at the factory address, so the next device "
                              "of this model on that wire cannot be told from it");

    // And it is a sensor from then on: the sweep reads it at the new address and reports it.
    bool reportedTwoOnline = false;
    for (size_t i = 0; i < module.server.updates.size(); i++)
    {
        if (module.server.updates[i].onlineCount >= 2)
        {
            reportedTwoOnline = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(reportedTwoOnline,
                             "the sensor was saved but never reported as online - the move left it "
                             "at an address nothing reads");
}

// --------------------------------------------------------------------------------------------
// 16. The same person, one restart earlier: the sensor was added in the app and the module came
//     up again before it was plugged in.
//
// The row is then in the registry when the module boots, on the factory address of its model, with
// nothing on its port. The address phase of that boot writes into it all the same - the address is
// outside the range of the module - and gets no answer, because there is nothing there to give one.
//
// That silence is not a device refusing to move, and the module must not read it as one. The note a
// refusal leaves holds off every pass after it, so a row carrying one would keep the factory address
// for as long as the module runs - which is the state this whole invariant exists to end, reached
// through the one door that leads to it.
// --------------------------------------------------------------------------------------------

void test_s18_a_row_whose_device_was_not_there_at_the_restart_is_moved_when_it_arrives(void)
{
    ModuleUnderTest module;
    module.port0.device(KNOWN_ADDRESS)->wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;

    // The row is in the registry before the module comes up - added from the app on an earlier run -
    // and the port has nothing on it: the person went to fetch the sensor.
    SensorEntity added = makeMovableRowOn(2, A_MODEL_FACTORY_ADDRESS);
    module.sensorRepository.save(added);

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(120000);
    runUntilTheRestartsPassIsOver(module);

    const unsigned writesInTheRestart = addressStoreWrites(module.port2, ADDRESS_STORE_REGISTER);
    const unsigned refusalsInTheRestart = (unsigned)module.startupScan.factoryAddressMoveRefusedCount();

    // The premise of the scenario: the boot really did write into that port. Without it there is
    // nothing here to be mistaken for a refusal and nothing to measure.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(1, writesInTheRestart,
                                              "the boot did not write an address into the row at "
                                              "all - the scenario is not the one being measured");

    // They come back and plug it in. Port 3 shares the wire and is free, so the cheap rung is
    // answered from this device at the factory address; the sweep reads the row at that same
    // address. Either fact owes the row a move, and a note would hold off both.
    const uint64_t pluggedInAtMs = module.clock.nowMs();
    module.port2.addDevice(makeMovableDevice(A_MODEL_FACTORY_ADDRESS));

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS * 2);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    Utils::Optional<SensorEntity> moved = module.sensorRepository.get(added.uid.c_str());
    const bool answersAtTheModulesAddress = module.port2.device(MODBUS_ADDRESS_MIN + 2) != nullptr;
    const bool stillAtTheFactoryAddress = module.port2.device(A_MODEL_FACTORY_ADDRESS) != nullptr;

    printf("  s18: the row whose device was missing at the restart took %u address-register "
           "write(s) then; plugged in at t=%lu ms it is stored at 0x%02X and the device answers at "
           "0x%02X / 0x%02X (module address / factory address)\n",
           writesInTheRestart, (unsigned long)pluggedInAtMs,
           moved ? (unsigned)moved.value().modbusDeviceAddress : 0,
           answersAtTheModulesAddress ? MODBUS_ADDRESS_MIN + 2 : 0,
           stillAtTheFactoryAddress ? A_MODEL_FACTORY_ADDRESS : 0);

    TEST_ASSERT_TRUE_MESSAGE((bool)moved, "the row left the registry");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 2,
                                   (unsigned)moved.value().modbusDeviceAddress,
                                   "the row is still on the address its model leaves the factory "
                                   "on - the boot's own write was taken for a refusal and every "
                                   "pass after it left the row alone");
    // The device, not only the row: this runs on the real reader over the simulated line, so the
    // identification sequence either moved the device or it did not.
    TEST_ASSERT_TRUE_MESSAGE(answersAtTheModulesAddress,
                             "the device does not answer at the address the row is stored on");
    TEST_ASSERT_FALSE_MESSAGE(stillAtTheFactoryAddress,
                              "the device still answers at the factory address, so the next device "
                              "of this model on that wire cannot be told from it");
    // And the reason it could move: the boot wrote into an empty port and read the silence for what
    // it was. This is the fact the two mechanisms above stand on - both of them look at the note.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, refusalsInTheRestart,
                                   "the module counted the silence of a port with nothing on it as "
                                   "a device refusing the address it was written");
}

// --------------------------------------------------------------------------------------------
// 17. And the other side of the same note: what it promises to the device that did refuse.
//
// One attempt per row is a promise about every pass, not about the cheap ones. A pass that adopted
// a sensor resolves the whole registry for its address phase - the same question a restart asks -
// and the row that refused an address is in that registry like any other. Somebody plugging an
// ordinary new sensor into a free port of another wire must not cost the refusing device the
// whole identification sequence again, nor cost every working port of the module the power it takes.
// --------------------------------------------------------------------------------------------

void test_s18_a_pass_that_adopts_a_sensor_leaves_the_refusing_device_alone(void)
{
    ModuleUnderTest module;
    module.port0.device(KNOWN_ADDRESS)->wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;

    // The row that will not move: on port 2, on the address its model leaves the factory with. The
    // catalog says the model keeps its address in a register, so the write goes down the wire in
    // full; the device behind it answers everything and keeps the address it had all the same.
    module.sensorRepository.save(makeMovableRowOn(2, A_MODEL_FACTORY_ADDRESS));
    module.port2.addDevice(makeWorkingDevice(A_MODEL_FACTORY_ADDRESS));

    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    const unsigned writesBeforeTheNewSensor = addressStoreWrites(module.port2, ADDRESS_STORE_REGISTER);
    const unsigned cyclesBeforeTheNewSensor = (unsigned)module.port0.powerOffTransitions;
    const size_t rowsBeforeTheNewSensor = module.sensorRepository.getAll().size();

    // The premise: the module did try, was refused, and wrote the note.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)module.startupScan.factoryAddressMoveRefusedCount(),
                                   "the device that answers and refuses the address was not tried "
                                   "once, or the refusal was not noted - the scenario is not the "
                                   "one being measured");

    // An ordinary new sensor goes into port 1 - the other wire, nothing to do with the refusing
    // device. The pass that finds it adopts it, and a pass that adopted resolves the whole registry.
    module.port1.addDevice(makeFactoryFreshDevice(A_MODEL_FACTORY_ADDRESS));

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS * 2);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    const unsigned writesAfter =
        addressStoreWrites(module.port2, ADDRESS_STORE_REGISTER) - writesBeforeTheNewSensor;
    const unsigned cyclesAfter =
        (unsigned)module.port0.powerOffTransitions - cyclesBeforeTheNewSensor;
    const size_t rowsAfter = module.sensorRepository.getAll().size();

    printf("  s18: with the note standing, adopting a sensor on another wire cost the refusing "
           "device %u address-register write(s) and the working port %u power cycle(s); the "
           "registry went from %u to %u row(s)\n",
           writesAfter, cyclesAfter, (unsigned)rowsBeforeTheNewSensor, (unsigned)rowsAfter);

    // The premise of the second half: the pass really did adopt, so the full resolution of the
    // registry really did run.
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE((unsigned)rowsBeforeTheNewSensor, (unsigned)rowsAfter,
                                          "the new sensor was never adopted - nothing made the pass "
                                          "resolve the whole registry, and there is nothing to "
                                          "measure");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writesAfter,
                                   "the pass that adopted a sensor elsewhere wrote an address into "
                                   "the device that had already refused one");
    // A write into the refusing device would not reach the working port: the isolation takes the
    // power off the other port of the wire being written, and port 2 is on the other wire. What is
    // held here is the adoption's cost - its wire is the one with the new sensor on it - so the
    // budget is the adoption's, not zero; the write itself is held by the count above.
    char message[288];
    snprintf(message, sizeof(message),
             "adopting one sensor cost the working port %u power cycle(s) - the identification of "
             "the new sensor is what it may cost, not that plus the identification sequence of a device "
             "the module has already been refused by",
             cyclesAfter);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(2, cyclesAfter, message);
}

// --------------------------------------------------------------------------------------------
// 18. The restart's window on a free port nothing answered cheaply.
//
// The wildcard frames of the presence poll are answered only by the models they were written for.
// An RY-WS301 answers the address the module once gave it and nothing else, so a restart that took
// silence for an empty port never found it. What the identification procedure did on every free port
// instead - ask the port every address the module hands out, write the port's own address into
// whatever is there - is what the restart's window does now: the poll opens it, the wire stays
// dark, and the rest is asked one catalog model per step, the sweep and the other wire getting the
// line back between two of them.
//
// The stand is a module whose sensors.json has been erased: no row, the catalog the server sends -
// three models it lets a module scan, two of them writing their address with FC06 - and register
// writes put on the wire the way ModbusRS485 puts them, because what the line looks like while
// the module writes an address is part of what these tests look at.
// --------------------------------------------------------------------------------------------

namespace {

const char* const XF700A_UID = "5ec10934-c2d9-4eff-a637-e7e320ef855c";
const char* const RY_WS12S_UID = "a54de3e2-b492-4262-8da2-2313f564d84f";
const char* const GENERIC_MODBUS_UID = "2065c111-b0a8-4183-9430-40ad08e60eaa";
const char* const RY_WS301_UID = "4719a986-a300-48d5-bd1f-e34a1329798a";

/** @brief One channel of a model, as the catalog carries it. */
ModbusRegisterConfig makeChannel(uint16_t reg, int channel, const char* name, float multiplier,
                                 float offset, float minValue, float maxValue)
{
    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = reg;
    config.numRegisters = 1;
    config.multiplier = multiplier;
    config.offset = offset;
    config.channel = channel;
    config.channelName = name;
    config.sensorType = name;
    config.canConnectMinValue = minValue;
    config.canConnectMaxValue = maxValue;
    return config;
}

/** @brief A ModBus model with the addressing fields the catalog gives it. */
SensorType makeModel(const char* uid, const char* name, bool canScan, int factoryAddress,
                     uint8_t writeCommand, uint16_t storeRegister, uint16_t pingRegister)
{
    SensorType type;
    type.uid = uid;
    type.name = name;
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = canScan;
    type.isSupported = true;
    type.baudRate = 9600;
    type.modbusDeviceAddress = factoryAddress;
    type.writeRegistersCommand = writeCommand;
    type.registerToStoreModbusDeviceAddress = storeRegister;
    type.modbusPingAddress = pingRegister;
    return type;
}

/** @brief The RY-WS301 row of the server's catalog: not scannable, its address in holding register 1. */
SensorType makeRyWs301Model()
{
    SensorType type = makeModel(RY_WS301_UID, "ry_ws301", false, 255, 16, 0x0001, 0x0000);
    type.modbusRegisterConfig.push_back(makeChannel(0, 1, "air_temp", 0.01f, -40.0f, 0.0f, 105.0f));
    type.modbusRegisterConfig.push_back(makeChannel(1, 2, "humidity", 0.01f, 0.0f, 0.0f, 100.0f));
    return type;
}

/**
 * @brief The catalog the server sends, in the order of its ids - which is the order a module walks
 *        it in. Three models a module may scan: XF700A (FC06 into register 0, pinged at register
 *        9), RY-WS12S (FC06 into register 0x00FF) and the generic ModBus sensor (FC16 into
 *        register 1). The values are the backend's: UpdateSensorModelsAndTypes for the models and
 *        their channels, SyncSensorModelsAndTypesFromDev for the address fields and the RY-WS12S
 *        channel, SyncSensorModelConfigsFromDev for the XF700A channels.
 */
std::vector<SensorType> theServersCatalog()
{
    std::vector<SensorType> catalog;

    catalog.push_back(makeRyWs301Model());

    SensorType mp508c = makeModel("144f6c6d-d210-44bf-a9c3-23c5f26472fe", "mp-508c", false, 1, 16,
                                  0x0001, 0x0000);
    mp508c.modbusRegisterConfig.push_back(
        makeChannel(0, 1, "soil_moisture_dig", 0.1f, 0.0f, 0.0f, 100.0f));
    mp508c.modbusRegisterConfig.push_back(makeChannel(1, 2, "soil_temp", 0.01f, -20.0f, 10.0f, 40.0f));
    catalog.push_back(mp508c);

    SensorType xf700a = makeModel(XF700A_UID, "XF700A", true, 255, 6, 0x0000, 0x0009);
    xf700a.modbusRegisterConfig.push_back(makeChannel(9, 0, "air_temp", 0.01f, -40.0f, 0.0f, 100.0f));
    xf700a.modbusRegisterConfig.push_back(makeChannel(10, 1, "humidity", 0.01f, 0.0f, 0.0f, 100.0f));
    xf700a.modbusRegisterConfig.push_back(
        makeChannel(11, 2, "atmospheric_pressure_dig", 0.1f, 0.0f, 0.0f, 10000.0f));
    xf700a.modbusRegisterConfig.push_back(makeChannel(12, 3, "wind_speed_dig", 0.01f, 0.0f, 0.0f, 100.0f));
    xf700a.modbusRegisterConfig.push_back(
        makeChannel(13, 4, "wind_direction_dig", 0.1f, 0.0f, 0.0f, 360.0f));
    xf700a.modbusRegisterConfig.push_back(makeChannel(14, 5, "rainfall_dig", 10.0f, 0.0f, 0.0f, 1000.0f));
    xf700a.modbusRegisterConfig.push_back(makeChannel(16, 7, "lux", 10.0f, 0.0f, 0.0f, 100000.0f));
    catalog.push_back(xf700a);

    SensorType ryWs12s = makeModel(RY_WS12S_UID, "RY-WS12S", true, 1, 6, 0x00FF, 0x0000);
    ryWs12s.modbusRegisterConfig.push_back(makeChannel(0, 0, "water_temp", 0.01f, -50.0f, 0.0f, 100.0f));
    catalog.push_back(ryWs12s);

    SensorType generic = makeModel(GENERIC_MODBUS_UID, "ModBus", true, 255, 16, 0x0001, 0x0000);
    generic.modbusRegisterConfig.push_back(makeChannel(0, 0, "UNKNOWN", 1.0f, 0.0f, 0.0f, 100000.0f));
    catalog.push_back(generic);

    SensorType mp508e = makeModel("66ce7fc0-8b4d-4f87-b6e9-feeed78c7cec", "MP-508E-TH", false, 255,
                                  16, 0x0001, 0x0000);
    mp508e.modbusRegisterConfig.push_back(makeChannel(0, 0, "soil_temp", 0.1f, 0.0f, 0.0f, 100.0f));
    catalog.push_back(mp508e);

    return catalog;
}

/**
 * @brief The two models main.cpp seeds into an empty catalog: both scannable, neither with a write
 *        code, so the window writes them nothing but the blind sequence.
 */
std::vector<SensorType> theSeededCatalog()
{
    SensorType ryWs301 = makeModel(RY_WS301_UID, "ry_ws301", true, 255, 0, 0x0000, 0x0000);
    ryWs301.modbusRegisterConfig.push_back(makeChannel(0, 1, "air_temp", 0.01f, -40.0f, 10.0f, 40.0f));
    ryWs301.modbusRegisterConfig.push_back(makeChannel(1, 2, "humidity", 0.01f, 0.0f, 30.0f, 70.0f));

    SensorType mp508c = makeModel("144f6c6d-d210-44bf-a9c3-23c5f26472fe", "mp-508c", true, 1, 0,
                                  0x0000, 0x0000);
    mp508c.modbusRegisterConfig.push_back(makeChannel(0, 1, "soil_moisture", 0.1f, 0.0f, 0.0f, 50.0f));
    mp508c.modbusRegisterConfig.push_back(makeChannel(1, 2, "soil_temp", 0.01f, -20.0f, 10.0f, 30.0f));

    std::vector<SensorType> catalog;
    catalog.push_back(ryWs301);
    catalog.push_back(mp508c);
    return catalog;
}

/**
 * @brief An RY-WS301: 25 degrees and 50 % in its registers 0 and 1, nothing else, its address kept
 *        in holding register 1, silent to the wildcard frames and to a read of a register
 *        it does not have, and ten seconds from power to its first answer.
 */
SimRs485Bus::Device makeLoggedRyWs301(uint8_t address)
{
    SimRs485Bus::Device device;
    device.address = address;
    device.registerValues.push_back(6500); // (25 + 40) / 0.01
    device.registerValues.push_back(5000); // 50 / 0.01
    device.registersAreAddressed = true;
    device.answersRegistersItDoesNotHave = false;
    device.answersTheWildcard = false;
    device.addressIsWritable = true;
    device.addressStoreRegister = 0x0001;
    device.wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;
    return device;
}

/**
 * @brief A module whose sensors.json has been erased: no row, nothing on any port, the given
 *        catalog, and every register write made the way ModbusRS485 makes it, with the time of
 *        every frame.
 */
void eraseTheRegistry(ModuleUnderTest& module, const std::vector<SensorType>& catalog)
{
    module.sensorRepository.remove(makeKnownSensor().uid.c_str());
    module.port0.removeDevice(KNOWN_ADDRESS);
    module.sensorTypeRepository.removeAll();
    for (size_t i = 0; i < catalog.size(); i++)
    {
        module.sensorTypeRepository.addSensorType(catalog[i]);
    }
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        SimRs485Bus* port = static_cast<SimRs485Bus*>(module.ports[i]);
        port->setWritesLikeTheBoard(true);
        port->recordSendTimes(true);
    }
}

std::string uidOfPin(int pin)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%s.%02d", MODULE_UID, pin);
    return std::string(uid);
}

SimRs485Bus& portOf(ModuleUnderTest& module, int pin)
{
    return *static_cast<SimRs485Bus*>(module.ports[static_cast<size_t>(pin)]);
}

/** @brief One frame the master put on a port, and when. */
struct TimedFrame {
    std::vector<uint8_t> bytes;
    uint64_t atUs = 0;
    /** Where it stood in the sequence it was taken from. */
    size_t index = 0;
};

std::vector<TimedFrame> timedFramesOf(const SimRs485Bus& port, uint64_t fromMs = 0,
                                      uint64_t untilMs = (uint64_t)-1)
{
    std::vector<TimedFrame> frames;
    for (size_t i = 0; i < port.sentFrames.size() && i < port.sentAtUs.size(); i++)
    {
        const uint64_t atMs = port.sentAtUs[i] / 1000;
        if (atMs < fromMs || atMs > untilMs)
        {
            continue;
        }
        TimedFrame frame;
        frame.bytes = port.sentFrames[i];
        frame.atUs = port.sentAtUs[i];
        frame.index = i;
        frames.push_back(frame);
    }
    return frames;
}

/**
 * @brief The address write into a port of its own: writeSensorAddress() as the
 *        identification rung calls it, from the model's factory address to the given one.
 */
std::vector<TimedFrame> theVendorsAddressWrite(const SimRs485Bus::Device* device,
                                               const SensorType& model, int newAddress)
{
    VirtualClock clock;
    SimArduino arduino(clock);
    SimRs485Bus port(clock, "vendor-port");
    port.setPortPowerLikeTheBoard(true);
    port.setWritesLikeTheBoard(true);
    port.recordSendTimes(true);
    port.startUnpowered();
    if (device != nullptr)
    {
        port.addDevice(*device);
    }
    std::vector<IModbusRS485*> ports;
    ports.push_back(&port);
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    // On a line that has been begun, as the boot begins it: the sequence is the procedure's
    // frames, and a UART nobody began carries none of them.
    reader.init();
    reader.writeSensorAddress(model.modbusDeviceAddress, newAddress, 0,
                              model.registerToStoreModbusDeviceAddress, model.writeRegistersCommand,
                              model.modbusPingAddress, model.applyAddressAfterPowerCycle);
    return timedFramesOf(port);
}

/**
 * @brief Of the identification sequence, what an address write that cannot direct its first frame
 *        sends: everything from the first frame addressed to 0x00 on, less what is addressed to
 *        the model's factory address (the write to it and the pings of it) and less the wildcard
 *        frames of its fallback.
 */
std::vector<TimedFrame> theBroadcastPartOf(const std::vector<TimedFrame>& vendor, const SensorType& model)
{
    std::vector<TimedFrame> part;
    bool started = false;
    for (size_t i = 0; i < vendor.size(); i++)
    {
        const std::vector<uint8_t>& bytes = vendor[i].bytes;
        if (bytes.empty())
        {
            continue;
        }
        if (!started && bytes[0] != 0x00)
        {
            continue;
        }
        started = true;
        if (bytes[0] == 0xFF || bytes[0] == static_cast<uint8_t>(model.modbusDeviceAddress))
        {
            continue;
        }
        part.push_back(vendor[i]);
    }
    return part;
}

std::string hexOf(const std::vector<uint8_t>& bytes)
{
    std::string text;
    char byte[4];
    for (size_t i = 0; i < bytes.size(); i++)
    {
        snprintf(byte, sizeof(byte), "%02X ", bytes[i]);
        text += byte;
    }
    return text;
}

/**
 * @brief The frames are those of the full identification sequence: the same bytes in the same order, and the same pause before
 *        every frame that follows its predecessor directly in that sequence.
 */
void assertTheVendorsFramesAndPauses(const std::vector<TimedFrame>& ours,
                                     const std::vector<TimedFrame>& theirs, size_t count,
                                     const char* what)
{
    char message[320];
    snprintf(message, sizeof(message), "%s: the module put %u frame(s) on the wire, %u expected",
             what, (unsigned)ours.size(), (unsigned)count);
    TEST_ASSERT_TRUE_MESSAGE(ours.size() >= count, message);
    snprintf(message, sizeof(message), "%s: the identification sequence has %u frame(s), %u expected",
             what, (unsigned)theirs.size(), (unsigned)count);
    TEST_ASSERT_TRUE_MESSAGE(theirs.size() >= count, message);

    for (size_t i = 0; i < count; i++)
    {
        snprintf(message, sizeof(message), "%s, frame %u: the module sent %s, the identification sequence sends %s",
                 what, (unsigned)i, hexOf(ours[i].bytes).c_str(), hexOf(theirs[i].bytes).c_str());
        TEST_ASSERT_TRUE_MESSAGE(ours[i].bytes == theirs[i].bytes, message);
        if (i == 0 || theirs[i].index != theirs[i - 1].index + 1)
        {
            continue;
        }
        const uint64_t ourPauseMs = (ours[i].atUs - ours[i - 1].atUs) / 1000;
        const uint64_t theirPauseMs = (theirs[i].atUs - theirs[i - 1].atUs) / 1000;
        snprintf(message, sizeof(message),
                 "%s, frame %u: the module paused %lu ms before it, the identification sequence pauses %lu ms",
                 what, (unsigned)i, (unsigned long)ourPauseMs, (unsigned long)theirPauseMs);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(theirPauseMs, ourPauseMs, message);
    }
}

/** @brief The steps of one kind the discovery put on the line for one port, in order. */
std::vector<BusWindow> stepsOf(const ModuleUnderTest& module, StartupScanStep::Kind kind, int pin)
{
    std::vector<BusWindow> steps;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& window = module.busWindows[i];
        if (window.discovery && window.kind == kind && window.pin == pin)
        {
            steps.push_back(window);
        }
    }
    return steps;
}

/** @brief The frames one WRITE_PORT_ADDRESS step of this port put on it, for this model. */
std::vector<TimedFrame> framesOfTheWriteStep(ModuleUnderTest& module, int pin, int model)
{
    const std::vector<BusWindow> writes = stepsOf(module, StartupScanStep::WRITE_PORT_ADDRESS, pin);
    for (size_t i = 0; i < writes.size(); i++)
    {
        if (writes[i].model == model)
        {
            return timedFramesOf(portOf(module, pin), writes[i].startedAtMs, writes[i].endedAtMs);
        }
    }
    return std::vector<TimedFrame>();
}

int indexOfModel(const std::vector<SensorType>& catalog, const char* uid)
{
    for (size_t i = 0; i < catalog.size(); i++)
    {
        if (catalog[i].uid == uid)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/** @brief Boot the stand and run it until the pass the restart armed is over. */
void bootAndRunTheRestartsPass(ModuleUnderTest& module, CadenceRecorder& cadence)
{
    module.boot();
    module.startArduinoLoopTask(cadence);
    runUntilTheRestartsPassIsOver(module);
    cadence.closeAt(module.clock.nowMs());
    assertRunIsSound(module, cadence);
}

// What the identification procedure's own steps cost, out of its timings: the reader's read budget, the driver's wait
// for a write's acknowledgement plus the reader's pause after it, a silent three-attempt ping, a
// power transition, and the blind sequence of three attempts of two writes, 500 ms between them.
const uint64_t A_READ_MS = 300;
const uint64_t A_WRITE_MS = 300 + 300;
const uint64_t A_SILENT_PING_MS = 200 + 10 + 700 + 10 + 1200;
const uint64_t A_POWER_TRANSITION_MS = 100;
const uint64_t THE_BLIND_SEQUENCE_MS = 3 * 2 * (A_WRITE_MS + A_SILENT_PING_MS) + 2 * 500;

// One step of a window, bounded by what one model costs and by nothing else: the poll is two
// transactions and the transition of the isolation, a model's range four reads, a model's writes
// its own write and the blind sequence - what the identification procedure spends on the address of one
// model. None of these grows with the catalog.
const uint64_t THE_POLL_STEP_MS = A_POWER_TRANSITION_MS + 2 * A_READ_MS;
const uint64_t A_RANGE_STEP_MS = (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1) * A_READ_MS;
const uint64_t A_WRITE_STEP_MS = A_WRITE_MS + A_SILENT_PING_MS + THE_BLIND_SEQUENCE_MS;
const uint64_t THE_CLOSING_STEP_MS = A_POWER_TRANSITION_MS;

// And the bound on any step of the discovery that no catalog and no layout may cross, from the
// module's own thresholds: a sensor the sweep cannot read for DISCONNECT_THRESHOLD_MS has its port
// power-cycled by the recovery, and the sweep reads it every SWEEP_INTERVAL_MS - so a step longer
// than the difference turns one missed read into a recovery of a sensor that is fine.
const uint64_t ANY_STEP_OF_THE_DISCOVERY_MS =
    SensorRecoveryUseCase::DISCONNECT_THRESHOLD_MS - SensorAcquisitionService::SWEEP_INTERVAL_MS;

/** @brief The line time of a window on a port nothing is plugged into, from those steps. */
uint64_t windowOnAnEmptyPortMs(const std::vector<SensorType>& catalog)
{
    uint64_t total = THE_POLL_STEP_MS + THE_CLOSING_STEP_MS;
    for (size_t i = 0; i < catalog.size(); i++)
    {
        const SensorType& type = catalog[i];
        if (!type.canScan || !type.isSupported || !type.isModbusSensor())
        {
            continue;
        }
        total += A_RANGE_STEP_MS;
        if (!StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS)
        {
            continue;
        }
        if (type.writeRegistersCommand == 6 || type.writeRegistersCommand == 16)
        {
            total += A_WRITE_MS + A_SILENT_PING_MS;
        }
        total += THE_BLIND_SEQUENCE_MS;
    }
    return total;
}

} // namespace

// (a) A sensor answers at the address the module gave it, and only there.
void test_s18_a_sensor_the_module_once_addressed_is_found_where_it_answers(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port0.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN));

    CadenceRecorder cadence;
    bootAndRunTheRestartsPass(module, cadence);

    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(uidOfPin(0).c_str());
    printf("  s18: on the erased module the RY-WS301 at 0x%02X on port 0 was %s as %s by the "
           "restart's pass (over at t=%lu ms); %u adopted, %u window(s), %u identification step(s), "
           "%u write(s) on its port\n",
           MODBUS_ADDRESS_MIN, adopted ? "adopted" : "not found",
           adopted ? adopted.value().model.c_str() : "-",
           (unsigned long)module.recordingStartupScan.finishedAtMs(),
           (unsigned)module.startupScan.adoptedCount(),
           (unsigned)module.startupScan.presencePollCount(),
           (unsigned)module.startupScan.identifyStepCount(), (unsigned)module.port0.writes.size());

    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "the restart called port 0 empty although a sensor answers there at "
                             "0x70 - it is silent to the wildcard, and nothing else was asked");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, (unsigned)adopted.value().modbusDeviceAddress,
                                   "the sensor was adopted at an address it does not answer at");
    // Named by the first model of the catalog, in the catalog's order, whose scan it passes: the
    // XF700A reads a register it does not have and gets no answer, the RY-WS12S reads register 0
    // and takes 25 degrees of air for 15 degrees of water. The person corrects the model in the
    // app; the port and the address are right.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(RY_WS12S_UID, adopted.value().sensorTypeUid.c_str(),
                                     "the sensor was not adopted as the first model of the catalog "
                                     "whose scan it passes");
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_NEW, adopted.value().status,
                              "the sensor found by the restart did not get NEW status");

    // Found where it answers, with no write: a device in the module's own range is named by a read.
    // And the writes the windows of the other ports put on the wire - to 0x00, into the register
    // this device keeps its address in - never reached it: its port was dark while they went out.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.port0.writes.size(),
                                   "the module wrote into the port of a sensor that answered a read");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr,
                             "the sensor no longer answers at 0x70 - an address write of another "
                             "port's window reached it");
}

// (b) Two sensors of one wire on one address: each is found with the other dark, and the address
//     phase parts them.
void test_s18_two_sensors_at_one_address_of_one_wire_are_both_found_and_parted(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port0.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN + 1));
    module.port1.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN + 1));

    CadenceRecorder cadence;
    bootAndRunTheRestartsPass(module, cadence);

    Utils::Optional<SensorEntity> onPort0 = module.sensorRepository.get(uidOfPin(0).c_str());
    Utils::Optional<SensorEntity> onPort1 = module.sensorRepository.get(uidOfPin(1).c_str());
    printf("  s18: two sensors at 0x%02X on the wire of ports 0 and 1: port 0 %s at 0x%02X, port 1 "
           "%s at 0x%02X; the devices answer at 0x%02X / 0x%02X; restart's pass over at t=%lu ms\n",
           MODBUS_ADDRESS_MIN + 1, onPort0 ? "adopted" : "not found",
           onPort0 ? (unsigned)onPort0.value().modbusDeviceAddress : 0,
           onPort1 ? "adopted" : "not found",
           onPort1 ? (unsigned)onPort1.value().modbusDeviceAddress : 0,
           module.port0.device(MODBUS_ADDRESS_MIN) != nullptr ? MODBUS_ADDRESS_MIN
                                                               : MODBUS_ADDRESS_MIN + 1,
           module.port1.device(MODBUS_ADDRESS_MIN + 1) != nullptr ? MODBUS_ADDRESS_MIN + 1 : 0,
           (unsigned long)module.recordingStartupScan.finishedAtMs());

    TEST_ASSERT_TRUE_MESSAGE((bool)onPort0 && (bool)onPort1,
                             "not both sensors of the wire were found - two devices answering one "
                             "address are one collision to anything that asks with both powered");

    // The address phase keeps the address with the row whose port it belongs to (per_port_match)
    // and moves the other onto its own port's address, which is free.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 1, (unsigned)onPort1.value().modbusDeviceAddress,
                                   "port 1 lost the address that is its own port's");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, (unsigned)onPort0.value().modbusDeviceAddress,
                                   "port 0 was not moved onto its own port's address");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr,
                             "the device on port 0 does not answer at the address its row says");
    TEST_ASSERT_TRUE_MESSAGE(module.port1.device(MODBUS_ADDRESS_MIN + 1) != nullptr,
                             "the device on port 1 does not answer at the address its row says");

    // Found by a read of the range, as the one on port 0 was - not by the verification of an
    // address written into it. The window of port 0 took port 1's power, and a window that asked
    // port 1 before its device had come up would find nothing to read and fall through to writing.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.port1.writes.size(),
                                   "an address was written into port 1 although its device answers "
                                   "a read of the range - it was asked before it had come up");
}

// (c) A device on an address nobody knows, which takes the address its model writes with FC06 to
//     0x00 - the RY-WS12S's own register, which no other write of the window touches.
void test_s18_a_device_on_an_address_nobody_knows_takes_the_one_its_model_writes(void)
{
    if (!StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS)
    {
        TEST_IGNORE_MESSAGE("this build's window writes no address (THE_WINDOW_WRITES_THE_PORT_ADDRESS)");
    }
    const uint8_t AN_ADDRESS_NOBODY_KNOWS = 5;
    SimRs485Bus::Device device = makeLoggedRyWs301(AN_ADDRESS_NOBODY_KNOWS);
    device.addressStoreRegister = 0x00FF;

    const std::vector<SensorType> catalog = theServersCatalog();
    ModuleUnderTest module;
    eraseTheRegistry(module, catalog);
    module.port0.addDevice(device);

    CadenceRecorder cadence;
    bootAndRunTheRestartsPass(module, cadence);

    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(uidOfPin(0).c_str());
    printf("  s18: the device at %u that takes the RY-WS12S's FC06 write was %s; it answers at "
           "0x%02X now\n",
           (unsigned)AN_ADDRESS_NOBODY_KNOWS, adopted ? "adopted" : "not found",
           module.port0.device(MODBUS_ADDRESS_MIN) != nullptr ? MODBUS_ADDRESS_MIN
                                                               : AN_ADDRESS_NOBODY_KNOWS);

    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a device on an address outside the module's range, silent to the "
                             "wildcard, was not found - nobody wrote it an address");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, (unsigned)adopted.value().modbusDeviceAddress,
                                   "the device was not adopted at its port's address");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(RY_WS12S_UID, adopted.value().sensorTypeUid.c_str(),
                                     "the device was not adopted as the model whose write it took");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr,
                             "the device does not answer at the address it was adopted at");

    // Every frame of both FC06 models' steps is the identification sequence's, and so is every pause. The XF700A's
    // step is the whole of its sequence on a device that takes none of it - its own write to
    // register 0 and the blind sequence, every verification a ping of register 9. The RY-WS12S's
    // begins with the write that moved the device and the ping that found it there.
    const int xf700a = indexOfModel(catalog, XF700A_UID);
    const int ryWs12s = indexOfModel(catalog, RY_WS12S_UID);
    assertTheVendorsFramesAndPauses(
        framesOfTheWriteStep(module, 0, xf700a),
        theBroadcastPartOf(theVendorsAddressWrite(&device, catalog[xf700a], MODBUS_ADDRESS_MIN),
                           catalog[xf700a]),
        4 + 3 * 8, "the XF700A's write step");
    assertTheVendorsFramesAndPauses(
        framesOfTheWriteStep(module, 0, ryWs12s),
        theBroadcastPartOf(theVendorsAddressWrite(&device, catalog[ryWs12s], MODBUS_ADDRESS_MIN),
                           catalog[ryWs12s]),
        2, "the RY-WS12S's write to 0x00 and the ping that found the device");
}

// (c2) The same device, taking nothing but the blind FC16 to register 1 - which the first model's
//      blind sequence sends.
void test_s18_a_device_that_takes_only_the_blind_write_is_found_too(void)
{
    if (!StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS)
    {
        TEST_IGNORE_MESSAGE("this build's window writes no address (THE_WINDOW_WRITES_THE_PORT_ADDRESS)");
    }
    const uint8_t AN_ADDRESS_NOBODY_KNOWS = 5;
    SimRs485Bus::Device device = makeLoggedRyWs301(AN_ADDRESS_NOBODY_KNOWS);

    const std::vector<SensorType> catalog = theServersCatalog();
    ModuleUnderTest module;
    eraseTheRegistry(module, catalog);
    module.port0.addDevice(device);

    CadenceRecorder cadence;
    bootAndRunTheRestartsPass(module, cadence);

    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(uidOfPin(0).c_str());
    printf("  s18: the device at %u that takes only the blind FC16 to register 1 was %s as %s\n",
           (unsigned)AN_ADDRESS_NOBODY_KNOWS, adopted ? "adopted" : "not found",
           adopted ? adopted.value().model.c_str() : "-");

    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a device that takes only the blind write was not found - the window "
                             "stopped at the model's own write");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, (unsigned)adopted.value().modbusDeviceAddress,
                                   "the device was not adopted at its port's address");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr,
                             "the device does not answer at the address it was adopted at");

    // The XF700A's step moved it: its own write to register 0 and three silent pings of register 9,
    // then tryLegacyBroadcastLoop()'s first attempt - FC06 to register 0, three pings, FC16 to
    // register 1, and the ping that found the device. Frame for frame and pause for pause.
    const int xf700a = indexOfModel(catalog, XF700A_UID);
    assertTheVendorsFramesAndPauses(
        framesOfTheWriteStep(module, 0, xf700a),
        theBroadcastPartOf(theVendorsAddressWrite(&device, catalog[xf700a], MODBUS_ADDRESS_MIN),
                           catalog[xf700a]),
        4 + 4 + 2, "the XF700A's write step");
}

// (d) What the window costs a port nothing is plugged into, on the two catalogs a module meets.
//     Once per free port per restart; one catalog model per step; every write the identification sequence's.
void test_s18_what_the_window_costs_a_port_nothing_is_plugged_into(void)
{
    const char* names[2] = {"the server's catalog", "the seeded catalog"};
    std::vector<SensorType> catalogs[2] = {theServersCatalog(), theSeededCatalog()};

    for (int c = 0; c < 2; c++)
    {
        ModuleUnderTest module;
        eraseTheRegistry(module, catalogs[c]);
        SimTaskWatchdog taskWatchdog(module.scheduler, module.clock, (uint32_t)WDT_TIMEOUT_S * 1000UL);
        taskWatchdog.start();

        CadenceRecorder cadence;
        bootAndRunTheRestartsPass(module, cadence);
        const uint64_t passMs =
            module.recordingStartupScan.finishedAtMs() - module.recordingStartupScan.startedAtMs();

        // Per port: the line time of its window, the wall time from the poll to the close, and the
        // longest single step of it.
        uint64_t lineMs[4] = {0, 0, 0, 0};
        uint64_t wallMs[4] = {0, 0, 0, 0};
        uint64_t openedAtMs[4] = {0, 0, 0, 0};
        uint64_t longestStepMs = 0;
        unsigned windows = 0;
        char message[320];
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (!w.discovery)
            {
                continue;
            }
            snprintf(message, sizeof(message),
                     "%s: a step of the discovery held the line %lu ms - over the %lu ms after which "
                     "the module power-cycles a sensor the sweep could not read",
                     names[c], (unsigned long)w.lengthMs(), (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS);
            TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(ANY_STEP_OF_THE_DISCOVERY_MS, w.lengthMs(), message);

            uint64_t bound = 0;
            switch (w.kind)
            {
            case StartupScanStep::PROBE_PRESENCE: bound = THE_POLL_STEP_MS; break;
            case StartupScanStep::PROBE_RANGE: bound = A_RANGE_STEP_MS; break;
            case StartupScanStep::WRITE_PORT_ADDRESS: bound = A_WRITE_STEP_MS; break;
            case StartupScanStep::RELEASE_LINE: bound = THE_CLOSING_STEP_MS; break;
            default: continue;
            }
            snprintf(message, sizeof(message),
                     "%s: a %s step on port %d held the line %lu ms, one model's worth is %lu ms",
                     names[c], BackgroundActs::ladderStepName(w.kind), w.pin,
                     (unsigned long)w.lengthMs(), (unsigned long)bound);
            TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(bound, w.lengthMs(), message);
            if (w.lengthMs() > longestStepMs)
            {
                longestStepMs = w.lengthMs();
            }
            if (w.pin < 0 || w.pin > 3)
            {
                continue;
            }
            lineMs[w.pin] += w.lengthMs();
            if (w.kind == StartupScanStep::PROBE_PRESENCE)
            {
                windows++;
                openedAtMs[w.pin] = w.startedAtMs;
            }
            if (w.kind == StartupScanStep::RELEASE_LINE)
            {
                wallMs[w.pin] = w.endedAtMs - openedAtMs[w.pin];
            }
        }
        const uint64_t budgetMs = windowOnAnEmptyPortMs(catalogs[c]);

        // A settled module afterwards: ten more minutes of passes over the same four free ports.
        module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
        cadence.closeAt(module.clock.nowMs());
        assertRunIsSound(module, cadence);

        printf("  s18: on %s a window on an empty port held the line %lu / %lu / %lu / %lu ms "
               "(from the identification procedure's timings %lu ms) in steps of at most %lu ms, over %lu / %lu / %lu "
               "/ %lu ms of wall time; the restart's pass over four empty ports took %lu ms; %u "
               "window(s) after ten more minutes\n",
               names[c], (unsigned long)lineMs[0], (unsigned long)lineMs[1],
               (unsigned long)lineMs[2], (unsigned long)lineMs[3], (unsigned long)budgetMs,
               (unsigned long)longestStepMs, (unsigned long)wallMs[0], (unsigned long)wallMs[1],
               (unsigned long)wallMs[2], (unsigned long)wallMs[3], (unsigned long)passMs,
               (unsigned)module.startupScan.presencePollCount());
        printf("  s18: on %s the task that owns the line went %lu ms at most between two feeds of "
               "its watchdog (budget %lu ms)\n",
               names[c], (unsigned long)taskWatchdog.worstGapMs(SensorAcquisitionService::TASK_NAME),
               (unsigned long)WDT_SENSOR_ACQ_LEGAL_PAUSE_S * 1000UL);

        snprintf(message, sizeof(message),
                 "%s: %u windows over four free ports - there is one per free port per restart",
                 names[c], windows);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(4, windows, message);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(4, (unsigned)module.startupScan.presencePollCount(),
                                       "a pass after the restart opened a window of its own");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.startupScan.adoptedCount(),
                                       "something was adopted on four empty ports");
        for (int pin = 0; pin < 4; pin++)
        {
            snprintf(message, sizeof(message),
                     "%s: the window on empty port %d held the line %lu ms, the identification procedure's timings "
                     "make it %lu ms",
                     names[c], pin, (unsigned long)lineMs[pin], (unsigned long)budgetMs);
            TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(budgetMs + 100, lineMs[pin], message);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT64_MESSAGE(budgetMs - 100, lineMs[pin], message);
        }

        TEST_ASSERT_FALSE_MESSAGE(taskWatchdog.fired(), "the task watchdog fired during the pass");
        snprintf(message, sizeof(message),
                 "%s: the task that owns the line went %lu ms without feeding its watchdog (budget "
                 "%lu ms)",
                 names[c], (unsigned long)taskWatchdog.worstGapMs(SensorAcquisitionService::TASK_NAME),
                 (unsigned long)WDT_SENSOR_ACQ_LEGAL_PAUSE_S * 1000UL);
        TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE((uint64_t)WDT_SENSOR_ACQ_LEGAL_PAUSE_S * 1000ULL,
                                                 taskWatchdog.worstGapMs(SensorAcquisitionService::TASK_NAME),
                                                 message);
        if (!StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS)
        {
            continue;
        }

        // Every write step is the identification sequence's, frame for frame and pause for pause, model by model:
        // the model's write to 0x00 when it has a write code and its three silent pings, then the
        // whole blind sequence - three attempts of two writes, three silent pings after each,
        // 500 ms between the attempts - every ping at the model's own ping register.
        for (size_t model = 0; model < catalogs[c].size(); model++)
        {
            const SensorType& type = catalogs[c][model];
            if (!type.canScan || !type.isSupported || !type.isModbusSensor())
            {
                continue;
            }
            const size_t frames =
                ((type.writeRegistersCommand == 6 || type.writeRegistersCommand == 16) ? 4 : 0) + 3 * 8;
            snprintf(message, sizeof(message), "%s: the write step of %s on port 0", names[c],
                     type.name.c_str());
            assertTheVendorsFramesAndPauses(
                framesOfTheWriteStep(module, 0, static_cast<int>(model)),
                theBroadcastPartOf(theVendorsAddressWrite(nullptr, type, MODBUS_ADDRESS_MIN), type),
                frames, message);
        }
    }
}

// (e) A pass after the restart is the cheap rung - the module's range and the factory addresses - and
//     nothing of the window.
void test_s18_a_pass_after_the_restart_asks_the_range_but_neither_writes_nor_opens_a_window(void)
{
    ModuleUnderTest module;
    // Something nobody can name on port 3, a wire with no stored sensor: the restart gives up on
    // it, and every pass after the restart polls it to see whether it has gone. That poll is the
    // one a pass after the restart opens, so it is where a window of its own would show.
    module.port3.addDevice(makeUnidentifiableDevice(9));
    module.boot();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    runUntilTheRestartsPassIsOver(module);
    for (int guard = 0; guard < 120 && module.startupScan.aPassIsInFlight(); guard++)
    {
        module.scheduler.runForMs(1000);
    }

    SimRs485Bus* freePorts[3] = {&module.port1, &module.port2, &module.port3};
    size_t framesBefore[3];
    size_t writesBefore[3];
    for (int i = 0; i < 3; i++)
    {
        framesBefore[i] = freePorts[i]->sentFrames.size();
        writesBefore[i] = freePorts[i]->writes.size();
    }
    const uint32_t passesBefore = module.startupScan.freePortPassCount();

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());
    assertRunIsSound(module, cadence);

    unsigned rangeFrames = 0;
    unsigned rangeFramesOfThePort[3] = {0, 0, 0};
    unsigned broadcastFrames = 0;
    unsigned writes = 0;
    for (int i = 0; i < 3; i++)
    {
        for (size_t f = framesBefore[i]; f < freePorts[i]->sentFrames.size(); f++)
        {
            const uint8_t slave = freePorts[i]->sentFrames[f][0];
            if (slave >= MODBUS_ADDRESS_MIN && slave <= MODBUS_ADDRESS_MAX)
            {
                rangeFrames++;
                rangeFramesOfThePort[i]++;
            }
            if (slave == 0x00)
            {
                broadcastFrames++;
            }
        }
        writes += (unsigned)(freePorts[i]->writes.size() - writesBefore[i]);
    }

    printf("  s18: %u pass(es) after the restart's put %u frame(s) to the module's range, %u to "
           "0x00 and %u register write(s) on the free ports\n",
           (unsigned)(module.startupScan.freePortPassCount() - passesBefore), rangeFrames,
           broadcastFrames, writes);

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() - passesBefore >= 5,
                             "the passes after the restart stopped - there is nothing to measure");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.presencePollCount() > 3,
                             "no pass after the restart polled the port it gave up on - the one "
                             "place a window of its own would show was never reached");
    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(rangeFramesOfThePort[i] >= module.startupScan.freePortPassCount() - passesBefore,
                                 "a pass after the restart did not ask a free port the module's range");
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, broadcastFrames,
                                   "a pass after the restart put a broadcast frame on a free port");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, writes,
                                   "a pass after the restart wrote an address into a free port");
}

namespace {

/** @brief A stored RY-WS301 row, as the module wrote it when it adopted the sensor. */
SensorEntity makeStoredRyWs301(int pin)
{
    const SensorType model = makeRyWs301Model();
    SensorEntity row;
    row.uid = uidOfPin(pin);
    row.pin = pin;
    row.protocol = "ModBus";
    row.model = model.name;
    row.sensorType = model.name;
    row.sensorTypeUid = model.uid;
    row.isActive = true;
    row.isError = false;
    row.status = SENSOR_STATUS_NEW;
    row.baudRate = 9600;
    row.modbusDeviceAddress = MODBUS_ADDRESS_MIN + pin;
    row.registerToStoreModbusDeviceAddress = model.registerToStoreModbusDeviceAddress;
    row.writeRegistersCommand = model.writeRegistersCommand;
    row.modbusPingAddress = model.modbusPingAddress;
    row.modbusRegisterConfigs = model.modbusRegisterConfig;
    return row;
}

/**
 * @brief What the sweep did for one stored sensor while the restart's pass ran.
 *
 * Sampled once a second off the value model, where a reading lands when the control loop applies
 * it, and off the sensor's port, whose power says whether the sweep could have read it at all: a
 * sensor needs PORT_WAKE_UP_MS of power before it answers anything.
 */
struct SweepOfOneSensor {
    int pin = -1;
    std::string uid;
    uint64_t lastReadMs = 0;
    uint64_t lastSampleMs = 0;
    /** The longest time between two readings, whatever the port was doing. */
    uint64_t worstGapMs = 0;
    /** The longest time the sensor went unread while its port could have answered. */
    uint64_t worstUnreadWhileAwakeMs = 0;
    uint64_t unreadWhileAwakeMs = 0;
    unsigned readings = 0;
};

void sample(ModuleUnderTest& module, SweepOfOneSensor& sensor)
{
    const uint64_t nowMs = module.clock.nowMs();
    const uint64_t readMs = module.sensorValueModel.getLastReadTime(sensor.uid);
    if (readMs != 0 && readMs != sensor.lastReadMs)
    {
        if (sensor.lastReadMs != 0 && readMs - sensor.lastReadMs > sensor.worstGapMs)
        {
            sensor.worstGapMs = readMs - sensor.lastReadMs;
        }
        sensor.lastReadMs = readMs;
        sensor.readings++;
        sensor.unreadWhileAwakeMs = 0;
    }
    else if (sensor.readings > 0 &&
             portOf(module, sensor.pin).currentPowerStretchMs() >= StartupSensorScanUseCase::PORT_WAKE_UP_MS)
    {
        sensor.unreadWhileAwakeMs += nowMs - sensor.lastSampleMs;
        if (sensor.unreadWhileAwakeMs > sensor.worstUnreadWhileAwakeMs)
        {
            sensor.worstUnreadWhileAwakeMs = sensor.unreadWhileAwakeMs;
        }
    }
    sensor.lastSampleMs = nowMs;
}

/** @brief How the structure describes a stored row's first channel: its type, or "none". */
std::string describedTypeOf(const ModuleUnderTest& module, const std::string& rowUid)
{
    if (module.moduleStructure.nodes.empty())
    {
        return "";
    }
    const std::string channelUid = rowUid + ".01";
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        if (module.moduleStructure.nodes[0].sensors[i].uid == channelUid)
        {
            return module.moduleStructure.nodes[0].sensors[i].type;
        }
    }
    return "";
}

/**
 * @brief Frames the sweep put on this port while it had had its power for less than
 *        PORT_WAKE_UP_MS - reads of a sensor that cannot answer yet - from this moment on.
 *
 * Read off the stand-in: when each frame went out and how long the port had been up at that moment.
 * A frame inside a step of the discovery or a command is that step's, not the sweep's.
 */
unsigned sweepReadsBeforeTheWakeUp(ModuleUnderTest& module, int pin, uint64_t fromMs)
{
    const SimRs485Bus& port = portOf(module, pin);
    unsigned early = 0;
    for (size_t f = 0; f < port.sentAtUs.size() && f < port.sentAfterPowerForMs.size(); f++)
    {
        const uint64_t atMs = port.sentAtUs[f] / 1000;
        if (atMs < fromMs || port.sentAfterPowerForMs[f] >= StartupSensorScanUseCase::PORT_WAKE_UP_MS)
        {
            continue;
        }
        bool ofAStep = false;
        for (size_t i = 0; i < module.busWindows.size() && !ofAStep; i++)
        {
            ofAStep = atMs >= module.busWindows[i].startedAtMs && atMs < module.busWindows[i].endedAtMs;
        }
        if (!ofAStep)
        {
            early++;
        }
    }
    return early;
}

// What each stored port's power is taken down for on the restart that these layouts make: the poll
// that opens a window on the free port of its wire takes the port down once. The model scan that
// verifies the other stored row takes the other port of that row's wire down, and none of these
// layouts has a stored row there. The window adds none - its steps hold the port dark between them,
// they do not take it down again.
struct Layout {
    const char* name;
    int stored[2];
    size_t storedCount;
    int powerOffsOfTheStoredPorts[2];
};

} // namespace

// (f) The sensors the module already reads, across the restart's windows.
//
// Stored devices that need their ten seconds of power, the server's catalog, three layouts: a
// stored sensor on each wire, one on the first wire, one on the second. The window keeps the port
// sharing its wire dark from its first step to its last, and the sweep leaves that port alone for
// that long and for its warm-up after - not a read, not a miss. Everything the sweep may read it
// reads on time, whatever the size of the catalog: no port recovery, no power cycle but the one the
// window of its wire makes, no stored row described as an empty port, no device re-addressed.
void test_s18_the_sensors_the_module_reads_come_through_the_restarts_windows(void)
{
    const Layout layouts[3] = {
        {"stored on ports 0 and 2", {0, 2}, 2, {1, 1}},
        {"stored on port 0", {0, -1}, 1, {1, 0}},
        {"stored on port 2", {2, -1}, 1, {1, 0}},
    };

    // Every layout is run and said before any of them is judged, so that a failing one still shows
    // what the others did.
    struct Outcome {
        std::vector<SweepOfOneSensor> sensors;
        std::string described[2];
        int powerOffs[2];
        unsigned readsBeforeTheWakeUp[2];
        bool stillAtTheirAddress[2];
        uint64_t recoveryActions = 0;
        uint64_t longestStepMs = 0;
    };
    Outcome outcomes[3];

    for (int l = 0; l < 3; l++)
    {
        const Layout& layout = layouts[l];
        Outcome& outcome = outcomes[l];
        ModuleUnderTest module;
        eraseTheRegistry(module, theServersCatalog());

        for (size_t i = 0; i < layout.storedCount; i++)
        {
            const int pin = layout.stored[i];
            module.sensorRepository.save(makeStoredRyWs301(pin));
            portOf(module, pin).addDevice(makeLoggedRyWs301((uint8_t)(MODBUS_ADDRESS_MIN + pin)));
            SweepOfOneSensor sensor;
            sensor.pin = pin;
            sensor.uid = uidOfPin(pin);
            outcome.sensors.push_back(sensor);
        }

        module.boot();
        CadenceRecorder cadence;
        module.startArduinoLoopTask(cadence);
        for (int second = 0; second < 1200 && module.recordingStartupScan.finishedAtMs() == 0; second++)
        {
            module.scheduler.runForMs(1000);
            for (size_t i = 0; i < outcome.sensors.size(); i++)
            {
                sample(module, outcome.sensors[i]);
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                                 "the pass the restart armed did not end in twenty minutes");

        // Read right when the pass is over: this is the structure the pass sends.
        for (size_t i = 0; i < outcome.sensors.size(); i++)
        {
            outcome.described[i] = describedTypeOf(module, outcome.sensors[i].uid);
        }

        // Two more minutes, so that whatever the pass left behind - a recovery owed, a read that
        // fails - has had its sweeps to show.
        for (int second = 0; second < 120; second++)
        {
            module.scheduler.runForMs(1000);
            for (size_t i = 0; i < outcome.sensors.size(); i++)
            {
                sample(module, outcome.sensors[i]);
            }
        }
        cadence.closeAt(module.clock.nowMs());
        assertRunIsSound(module, cadence);

        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            if (module.busWindows[i].discovery && module.busWindows[i].lengthMs() > outcome.longestStepMs)
            {
                outcome.longestStepMs = module.busWindows[i].lengthMs();
            }
        }
        outcome.recoveryActions = module.acquisition.recoveryActionCount();
        for (size_t i = 0; i < outcome.sensors.size(); i++)
        {
            const int pin = outcome.sensors[i].pin;
            outcome.powerOffs[i] = portOf(module, pin).powerOffTransitions;
            outcome.readsBeforeTheWakeUp[i] =
                sweepReadsBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs());
            outcome.stillAtTheirAddress[i] =
                portOf(module, pin).device((uint8_t)(MODBUS_ADDRESS_MIN + pin)) != nullptr;
            printf("  s18: %s - the sensor on port %d: longest gap between readings %lu ms, longest "
                   "unread while awake %lu ms, %u reading(s); its port lost power %d time(s); "
                   "%u sweep read(s) before the wake-up; "
                   "described as '%s' when the pass ended at t=%lu ms\n",
                   layout.name, pin, (unsigned long)outcome.sensors[i].worstGapMs,
                   (unsigned long)outcome.sensors[i].worstUnreadWhileAwakeMs,
                   outcome.sensors[i].readings, outcome.powerOffs[i],
                   outcome.readsBeforeTheWakeUp[i], outcome.described[i].c_str(),
                   (unsigned long)module.recordingStartupScan.finishedAtMs());
        }
        printf("  s18: %s - %lu port recovery action(s), longest step of the discovery %lu ms\n",
               layout.name, (unsigned long)outcome.recoveryActions, (unsigned long)outcome.longestStepMs);
    }

    char message[320];
    for (int l = 0; l < 3; l++)
    {
        const Layout& layout = layouts[l];
        const Outcome& outcome = outcomes[l];

        snprintf(message, sizeof(message),
                 "%s: the port recovery acted %lu time(s) - a sensor that is fine was taken for one "
                 "that stopped answering",
                 layout.name, (unsigned long)outcome.recoveryActions);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, outcome.recoveryActions, message);

        snprintf(message, sizeof(message),
                 "%s: a step of the discovery held the line %lu ms - over the %lu ms after which the "
                 "module power-cycles a sensor the sweep could not read",
                 layout.name, (unsigned long)outcome.longestStepMs,
                 (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS);
        TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(ANY_STEP_OF_THE_DISCOVERY_MS, outcome.longestStepMs,
                                                 message);

        for (size_t i = 0; i < outcome.sensors.size(); i++)
        {
            const SweepOfOneSensor& sensor = outcome.sensors[i];
            snprintf(message, sizeof(message),
                     "%s: the sensor on port %d went %lu ms unread while its port was awake (limit "
                     "%lu ms)",
                     layout.name, sensor.pin, (unsigned long)sensor.worstUnreadWhileAwakeMs,
                     (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS);
            TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(ANY_STEP_OF_THE_DISCOVERY_MS,
                                                     sensor.worstUnreadWhileAwakeMs, message);

            snprintf(message, sizeof(message),
                     "%s: the port of the sensor on port %d lost its power %d time(s), the steps "
                     "that take it account for %d",
                     layout.name, sensor.pin, outcome.powerOffs[i], layout.powerOffsOfTheStoredPorts[i]);
            TEST_ASSERT_EQUAL_MESSAGE(layout.powerOffsOfTheStoredPorts[i], outcome.powerOffs[i], message);

            snprintf(message, sizeof(message),
                     "%s: the sweep read the sensor on port %d %u time(s) before its port had come "
                     "up again - reads nobody can answer",
                     layout.name, sensor.pin, outcome.readsBeforeTheWakeUp[i]);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, outcome.readsBeforeTheWakeUp[i], message);

            snprintf(message, sizeof(message),
                     "%s: the NEW row on port %d was described as '%s' in the structure the pass "
                     "sent - the backend drops its readings until a structure says otherwise",
                     layout.name, sensor.pin, outcome.described[i].c_str());
            TEST_ASSERT_TRUE_MESSAGE(outcome.described[i] != "none" && !outcome.described[i].empty(),
                                     message);

            snprintf(message, sizeof(message),
                     "%s: the device on port %d no longer answers at 0x%02X - an address write of a "
                     "window reached it",
                     layout.name, sensor.pin, MODBUS_ADDRESS_MIN + sensor.pin);
            TEST_ASSERT_TRUE_MESSAGE(outcome.stillAtTheirAddress[i], message);
        }
    }
}

// (g) A command a person sends while a window is open waits for the window to close.
//
// Any command that reaches the line powers the port it talks through, and PingPin powers its own
// port and darkens the others: served in the middle of a window, it would give the dark port back
// its power just before the window's next write to 0x00 - and the sensor working on that port keeps
// its address in the very register that write sets. So the command is held until the window's
// closing step, and served before the next step of the discovery.
void test_s18_a_command_sent_during_a_window_waits_for_it_to_close(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.sensorRepository.save(makeStoredRyWs301(0));
    module.port0.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN));

    module.boot();
    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // The pass walks port 1 first, on the wire of the working sensor; the moment the sensor's port
    // goes dark is the moment that port's window has opened.
    bool windowOpened = false;
    for (int tick = 0; tick < 6000 && !windowOpened; tick++)
    {
        module.scheduler.runForMs(100);
        windowOpened = module.recordingStartupScan.startedAtMs() > 0 && !module.port0.isPowered();
    }
    TEST_ASSERT_TRUE_MESSAGE(windowOpened, "the working sensor's wire never had a window opened on it");
    const uint64_t sentAtMs = module.clock.nowMs();

    // The person pings the port of the working sensor. Pin 4 of the app's numbering is port 0.
    module.server.pendingModuleCommands.push_back(makeServerCommand("ping-0", "PingPin", "{\"pin\":4}"));
    module.mqttManager.deliver(scanSensorsSignalJson());

    runUntilTheRestartsPassIsOver(module);
    module.scheduler.runForMs(60000);
    cadence.closeAt(module.clock.nowMs());
    assertRunIsSound(module, cadence);

    uint64_t commandStartedAtMs = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        if (!module.busWindows[i].discovery && module.busWindows[i].startedAtMs >= sentAtMs)
        {
            commandStartedAtMs = module.busWindows[i].startedAtMs;
            break;
        }
    }
    // The window of port 1: from the poll that was on the line when the command was sent to the
    // step that closed it - the poll itself, on a module whose poll gives the power straight back.
    uint64_t windowClosedAtMs = 0;
    bool afterThePoll = false;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (!w.discovery || w.pin != 1)
        {
            continue;
        }
        if (w.kind == StartupScanStep::PROBE_PRESENCE && w.startedAtMs <= sentAtMs)
        {
            windowClosedAtMs = w.endedAtMs;
            afterThePoll = true;
            continue;
        }
        if (afterThePoll && w.kind == StartupScanStep::RELEASE_LINE)
        {
            windowClosedAtMs = w.endedAtMs;
            break;
        }
    }
    const std::vector<ModuleCommandUpdateDto> reports = reportsFor(module.server, "ping-0");

    printf("  s18: a PingPin sent at t=%lu ms, inside the window of port 1, reached the line at "
           "t=%lu ms, after the window closed at t=%lu ms; reported %s; the working sensor answers "
           "at 0x%02X\n",
           (unsigned long)sentAtMs, (unsigned long)commandStartedAtMs,
           (unsigned long)windowClosedAtMs, reports.empty() ? "nothing" : reports.back().status.c_str(),
           module.port0.device(MODBUS_ADDRESS_MIN) != nullptr ? MODBUS_ADDRESS_MIN : 0);

    TEST_ASSERT_TRUE_MESSAGE(commandStartedAtMs > 0, "the command never reached the line");
    TEST_ASSERT_TRUE_MESSAGE(windowClosedAtMs > 0, "the window of port 1 never closed");
    char message[256];
    snprintf(message, sizeof(message),
             "the command reached the line at t=%lu ms, inside the window that closed at t=%lu ms",
             (unsigned long)commandStartedAtMs, (unsigned long)windowClosedAtMs);
    TEST_ASSERT_TRUE_MESSAGE(commandStartedAtMs >= windowClosedAtMs, message);
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr,
                             "the working sensor no longer answers at 0x70 - the command gave its "
                             "port power in the middle of a window, and a write of the window "
                             "re-addressed it");
    TEST_ASSERT_FALSE_MESSAGE(reports.empty(), "the command was never reported");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", reports.back().status.c_str(),
                                     "the command did not finish after the window");
}

// --------------------------------------------------------------------------------------------
// 19. The line is begun at boot, before its first frame.
//
// A UART nobody began carries nothing: the board's HardwareSerial has no driver until begin(), and
// what it is asked to write goes nowhere (the stand-in of the line does the same, SimRs485Bus::
// begin()). Beginning the ports is part of a model's scan and of the preparation of stored ModBus
// sensors, and the first frame of a boot comes from neither: the sweep reads a stored sensor from
// its first second, the discovery asks a free port with plain reads and the raw adapter, a command
// from the backend goes straight to the line. So setup begins the line itself, before anything
// else can reach it - ModuleSetupService::setupSensorBus() - and these stands hold the boot to it
// in the four situations where nothing else would: a fresh module, a stored sensor's first read, a
// raw command ahead of any scan, and a boot with noScanSensors or in the board-test mode.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief A logged RY-WS301 that answers the moment its port has power: what is measured is the line. */
SimRs485Bus::Device makeAwakeRyWs301(uint8_t address)
{
    SimRs485Bus::Device device = makeLoggedRyWs301(address);
    device.wakeUpMs = 0;
    return device;
}

/** @brief The raw read the backend sends for register 0 of the device at 0x70 on port 0. */
std::string rawReadOfRegisterZeroAt0x70Json()
{
    char buffer[384];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"ModbusRead\",\"options\":{\"channel\":0,\"address\":%u,"
             "\"function\":3,\"register\":0,\"count\":1}}",
             MODULE_UID, (unsigned)MODBUS_ADDRESS_MIN);
    return std::string(buffer);
}

/** @brief That read as it goes on the wire: 70 03 00 00 00 01 8E EB. */
std::vector<uint8_t> rawReadOfRegisterZeroAt0x70Frame()
{
    std::vector<uint8_t> frame;
    frame.push_back(MODBUS_ADDRESS_MIN);
    frame.push_back(0x03);
    frame.push_back(0x00);
    frame.push_back(0x00);
    frame.push_back(0x00);
    frame.push_back(0x01);
    const uint16_t crc = SimRs485Bus::crc16(&frame[0], 6);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

/** @brief Frames offered to the module's line before a UART was begun, over the four ports. */
unsigned framesDroppedBeforeBegin(ModuleUnderTest& module)
{
    unsigned dropped = 0;
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        dropped += portOf(module, static_cast<int>(i)).framesDroppedBeforeBegin;
    }
    return dropped;
}

/**
 * @brief Every UART of the module was begun, and begun before the first frame offered to any of
 *        its ports: on the tape, the "Initialized at 9600 baud" lines come before the first
 *        "Sending request".
 */
void assertTheLineWasBegunBeforeItsFirstFrame(ModuleUnderTest& module, const char* stand)
{
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        SimRs485Bus& port = portOf(module, static_cast<int>(i));
        char message[256];
        snprintf(message, sizeof(message),
                 "%s: port %u was offered %u frame(s) while its UART had not been begun - none of "
                 "them reached the wire",
                 stand, (unsigned)i, (unsigned)port.framesDroppedBeforeBegin);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, port.framesDroppedBeforeBegin, message);
        snprintf(message, sizeof(message), "%s: the UART of port %u was never begun", stand,
                 (unsigned)i);
        TEST_ASSERT_TRUE_MESSAGE(port.lineIsBegun(), message);
        if (!port.sentAtUs.empty())
        {
            snprintf(message, sizeof(message),
                     "%s: port %u put a frame on the wire at t=%lu ms, before its UART was begun at "
                     "t=%lu ms",
                     stand, (unsigned)i, (unsigned long)(port.sentAtUs[0] / 1000),
                     (unsigned long)(port.lineBegunAtUs() / 1000));
            TEST_ASSERT_TRUE_MESSAGE(port.lineBegunAtUs() <= port.sentAtUs[0], message);
        }
    }
}

/** @brief Run until the raw read has been answered to the server, or two minutes have passed. */
void runUntilTheRawReadIsReported(ModuleUnderTest& module)
{
    for (int tick = 0; tick < 1200 && module.server.rawIoReports.empty(); tick++)
    {
        module.scheduler.runForMs(100);
    }
}

/** @brief When the discovery's first step was on the line, or 0 if none was. */
uint64_t firstDiscoveryStepOnTheLineMs(ModuleUnderTest& module)
{
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        if (module.busWindows[i].discovery)
        {
            return module.busWindows[i].startedAtMs;
        }
    }
    return 0;
}

/**
 * @brief Run until the discovery's first step has been on the line, for two minutes at most.
 *
 * A raw read does not wait for the discovery: the command asks the task that owns the line to exist,
 * and the discovery's first step comes when its own gate lets it.
 */
void runUntilTheDiscoveryHasStepped(ModuleUnderTest& module)
{
    for (int tick = 0; tick < 1200 && firstDiscoveryStepOnTheLineMs(module) == 0; tick++)
    {
        module.scheduler.runForMs(100);
    }
}

/** @brief When the first frame reached port 0's wire, or 0 if none did. */
uint64_t firstFrameOnPort0Ms(ModuleUnderTest& module)
{
    return module.port0.sentAtUs.empty() ? 0 : module.port0.sentAtUs[0] / 1000;
}

} // namespace

// (a) A fresh module - no row, the server's catalog, an RY-WS301 on 0x70 that does not answer the
//     wildcard - asks the port with its window and finds the sensor. Every question of the window
//     is a frame, and a line nobody began answers none of them.
void test_s18_a_fresh_module_begins_its_line_before_the_first_frame_and_finds_its_sensor(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port0.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN));

    CadenceRecorder cadence;
    bootAndRunTheRestartsPass(module, cadence);

    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get(uidOfPin(0).c_str());
    printf("  s18: a fresh module - UARTs begun at t=%lu/%lu ms, first frame at t=%lu ms, %u frame(s) "
           "on the wire, %u offered before a UART was begun; %u adopted, port 0 %s\n",
           (unsigned long)(module.port0.lineBegunAtUs() / 1000),
           (unsigned long)(module.port2.lineBegunAtUs() / 1000),
           (unsigned long)firstFrameOnPort0Ms(module), (unsigned)module.framesOnTheLine(),
           framesDroppedBeforeBegin(module), (unsigned)module.startupScan.adoptedCount(),
           adopted ? "adopted at its address" : "called empty");

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)module.startupScan.adoptedCount(),
                                   "the restart's pass adopted nothing on a fresh module with a "
                                   "sensor on port 0 - its window asked into a line that carries "
                                   "nothing");
    TEST_ASSERT_TRUE_MESSAGE((bool)adopted, "the sensor adopted is not the one on port 0");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, (unsigned)adopted.value().modbusDeviceAddress,
                                   "the sensor was adopted at an address it does not answer at");
    assertTheLineWasBegunBeforeItsFirstFrame(module, "a fresh module");
}

namespace {

/**
 * @brief How far into the boot the logged module begins its sensor bus
 *        (device log: "setupSensorBus done" at 5 819 ms) - the
 *        configuration, mostly the time adapter, before it. The rest of the stand's setup is
 *        instant, so its first sweep comes at the 15 s mark; the logged one came at 15.4 s, once its
 *        setup was over. Either way the boot leaves the port less than
 *        PortPower::WAKE_UP_MS of power before the first visit: 9.3 s here, 9.5 s in the log.
 */
const uint64_t LOGGED_SENSOR_BUS_AT_MS = 5819;

/** @brief What the first read of a stored sensor on port 0 looked like. */
struct FirstRead {
    /** When the setup had begun the sensor bus. */
    uint64_t busBegunAtMs = 0;
    uint64_t poweredAtMs = 0;
    uint64_t firstVisitAtMs = 0;
    uint64_t firstFrameAtMs = 0;
    uint64_t discoveryStartedAtMs = 0;
    uint64_t polls = 0;
    ModbusSensorReader::ReadStats stats;
};

/**
 * @brief Boot a module with one stored RY-WS301 on port 0 - instantly, or with the logged boot
 *        timing - and run until the sweep has read it, or a minute has passed.
 */
FirstRead bootAndReadTheStoredSensor(const SimRs485Bus::Device& device, bool loggedTiming)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.sensorRepository.save(makeStoredRyWs301(0));
    module.port0.addDevice(device);
    module.port0.recordPowerOnTimes(true);

    if (loggedTiming)
    {
        // The setup alone, as main.cpp runs it: nothing in src calls SensorService::init(), so the
        // first sweep is due 15 s after the module's power-on, not after the setup.
        module.clock.advanceMs(LOGGED_SENSOR_BUS_AT_MS);
        module.setupService.setupModule();
    }
    else
    {
        module.boot();
    }
    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    FirstRead read;
    read.busBegunAtMs = module.port0.lineBegunAtUs() / 1000;
    for (int tick = 0; tick < 600 && module.acquisition.pollCount() == 0; tick++)
    {
        module.scheduler.runForMs(100);
        if (read.firstVisitAtMs == 0 &&
            module.acquisition.pollCount() + module.acquisition.heldDarkSkipCount() > 0)
        {
            read.firstVisitAtMs = module.clock.nowMs();
        }
    }
    read.poweredAtMs = module.port0.powerOnAtUs.empty() ? 0 : module.port0.powerOnAtUs[0] / 1000;
    read.firstFrameAtMs = firstFrameOnPort0Ms(module);
    read.discoveryStartedAtMs = module.recordingStartupScan.startedAtMs();
    read.polls = module.acquisition.pollCount();
    read.stats = module.modbusSensorReader.getReadStats();
    assertTheLineWasBegunBeforeItsFirstFrame(module, "a stored sensor");
    return read;
}

/**
 * @brief The boot gave the port its power right after it began the sensor bus. The first read is
 *        the first visit's - or, when the port has not had PortPower::WAKE_UP_MS of power by then,
 *        the first moment it has: not a sweep later. No frame of it went unanswered, and the
 *        discovery had not started.
 */
void assertTheFirstReadCameAsSoonAsItCould(const FirstRead& read, const char* stand)
{
    char powered[200];
    snprintf(powered, sizeof(powered),
             "%s: the sensor bus was begun at t=%lu ms and the stored sensor's port came up at t=%lu "
             "ms - not by the boot",
             stand, (unsigned long)read.busBegunAtMs, (unsigned long)read.poweredAtMs);
    // One port, switched on with the settling of ModbusRS485::powerOnPort() after the bus.
    TEST_ASSERT_TRUE_MESSAGE(read.poweredAtMs > 0 && read.poweredAtMs <= read.busBegunAtMs + 200,
                             powered);
    // The visit is sampled every 100 ms; a read of two channels takes well under 300 ms more.
    const uint64_t dueMs = read.poweredAtMs + PortPower::WAKE_UP_MS > read.firstVisitAtMs
                               ? read.poweredAtMs + PortPower::WAKE_UP_MS
                               : read.firstVisitAtMs;
    char message[320];
    snprintf(message, sizeof(message),
             "%s: the port had its power at t=%lu ms and the sweep came at t=%lu ms, so the sensor "
             "could be read by t=%lu ms; its first frame went at t=%lu ms",
             stand, (unsigned long)read.poweredAtMs, (unsigned long)read.firstVisitAtMs,
             (unsigned long)dueMs, (unsigned long)read.firstFrameAtMs);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, read.polls, message);
    TEST_ASSERT_TRUE_MESSAGE(read.poweredAtMs > 0 && read.firstVisitAtMs > 0, message);
    TEST_ASSERT_TRUE_MESSAGE(read.firstFrameAtMs > 0 && read.firstFrameAtMs <= dueMs + 300, message);
    snprintf(message, sizeof(message),
             "%s: the first read timed out %u time(s) - a frame went to a sensor that could not "
             "answer it yet",
             stand, (unsigned)read.stats.timeout);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, read.stats.timeout, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, read.stats.accepted, "the first read did not take both channels");
    snprintf(message, sizeof(message),
             "%s: the discovery had started at t=%lu ms, before the first read - the read measured "
             "here is not the boot's",
             stand, (unsigned long)read.discoveryStartedAtMs);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, read.discoveryStartedAtMs, message);
}

} // namespace

// (b) A stored sensor that answers from the moment it has power - the logged RY-WS301 does, on the
//     first frame after its pin goes up (device log) - is read at the sweep's first visit,
//     about 15 s after the boot, with the boot instant
//     and with the logged boot timing alike. The boot gives its port the power right after the
//     sensor bus is begun.
void test_s18_a_stored_sensor_answers_the_first_read_of_the_first_sweep(void)
{
    const bool timings[2] = {false, true};
    for (int t = 0; t < 2; t++)
    {
        const char* stand = timings[t] ? "the logged boot" : "an instant boot";
        const FirstRead read = bootAndReadTheStoredSensor(makeAwakeRyWs301(MODBUS_ADDRESS_MIN), timings[t]);
        printf("  s18: %s - the stored RY-WS301 that answers at once: port powered at t=%lu ms, first "
               "visit at t=%lu ms, first frame at t=%lu ms; %u channel read(s) accepted, %u timed out\n",
               stand, (unsigned long)read.poweredAtMs, (unsigned long)read.firstVisitAtMs,
               (unsigned long)read.firstFrameAtMs, (unsigned)read.stats.accepted,
               (unsigned)read.stats.timeout);
        assertTheFirstReadCameAsSoonAsItCould(read, stand);
    }
}

// (b) The same with the RY-WS301 as this firmware believes it to be - ten seconds from power to its
//     first answer, PortPower::WAKE_UP_MS, a constant of the firmware and not a measurement: read as
//     soon as its port has had them, and not a frame before.
void test_s18_a_stored_sensor_that_needs_its_warm_up_is_read_before_the_discovery_starts(void)
{
    const bool timings[2] = {false, true};
    for (int t = 0; t < 2; t++)
    {
        const char* stand = timings[t] ? "the logged boot" : "an instant boot";
        const FirstRead read = bootAndReadTheStoredSensor(makeLoggedRyWs301(MODBUS_ADDRESS_MIN), timings[t]);
        printf("  s18: %s - the stored RY-WS301 with a 10 s warm-up: port powered at t=%lu ms, first "
               "visit at t=%lu ms, first frame at t=%lu ms; %u channel read(s) accepted, %u timed out\n",
               stand, (unsigned long)read.poweredAtMs, (unsigned long)read.firstVisitAtMs,
               (unsigned long)read.firstFrameAtMs, (unsigned)read.stats.accepted,
               (unsigned)read.stats.timeout);
        assertTheFirstReadCameAsSoonAsItCould(read, stand);
    }
}

// (c) A raw read from the backend on a fresh module, before any scan: it is served before the
//     first step of the discovery, and it is the first frame the line carries.
void test_s18_a_raw_read_before_any_scan_reaches_the_device(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port0.addDevice(makeAwakeRyWs301(MODBUS_ADDRESS_MIN));

    module.boot();
    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.mqttManager.deliver(rawReadOfRegisterZeroAt0x70Json());
    runUntilTheRawReadIsReported(module);
    // And until the discovery's first step has been on the line too.
    runUntilTheDiscoveryHasStepped(module);

    const uint64_t firstDiscoveryStepMs = firstDiscoveryStepOnTheLineMs(module);
    const std::vector<uint8_t> raw = rawReadOfRegisterZeroAt0x70Frame();
    const bool firstFrameIsTheRawRead = !module.port0.sentFrames.empty() && module.port0.sentFrames[0] == raw;

    printf("  s18: a raw read sent at boot reached the wire at t=%lu ms (first discovery step at t=%lu "
           "ms); %u frame(s) on port 0, %u offered before a UART was begun; reported %s\n",
           (unsigned long)firstFrameOnPort0Ms(module), (unsigned long)firstDiscoveryStepMs,
           (unsigned)module.port0.sentFrames.size(), framesDroppedBeforeBegin(module),
           module.server.rawIoReports.empty()
               ? "nothing"
               : (module.server.rawIoReports[0].isError ? module.server.rawIoReports[0].errorMessage.c_str()
                                                        : module.server.rawIoReports[0].dataHex.c_str()));

    TEST_ASSERT_TRUE_MESSAGE(firstFrameIsTheRawRead,
                             "the raw read is not the first frame on port 0's wire - it went into a "
                             "UART nobody had begun");
    TEST_ASSERT_TRUE_MESSAGE(firstDiscoveryStepMs > 0, "the discovery never took a step on the line");
    TEST_ASSERT_TRUE_MESSAGE(firstFrameOnPort0Ms(module) <= firstDiscoveryStepMs,
                             "the raw read reached the wire only after the discovery had begun");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.rawIoReports.size(),
                                     "the raw read was never answered to the server");
    TEST_ASSERT_FALSE_MESSAGE(module.server.rawIoReports[0].isError,
                              "the raw read was answered with an error");
    TEST_ASSERT_FALSE_MESSAGE(module.server.rawIoReports[0].dataHex.empty(),
                              "the raw read came back with no data");
    assertTheLineWasBegunBeforeItsFirstFrame(module, "a raw read before any scan");
}

// (d) Two boots nothing else would begin the line on: noScanSensors with an empty registry, whose
//     only work on the line is a preparation with nothing to prepare, and the board-test mode.
void test_s18_a_no_scan_boot_and_a_board_test_boot_begin_the_line_too(void)
{
    {
        ModuleUnderTest module;
        eraseTheRegistry(module, theServersCatalog());
        module.settings.setNoScanSensors(1);
        module.port0.addDevice(makeAwakeRyWs301(MODBUS_ADDRESS_MIN));

        module.boot();
        CadenceRecorder cadence;
        module.startArduinoLoopTask(cadence);
        module.mqttManager.deliver(rawReadOfRegisterZeroAt0x70Json());
        runUntilTheRawReadIsReported(module);

        printf("  s18: noScanSensors - the raw read reached the wire at t=%lu ms; %u frame(s) offered "
               "before a UART was begun\n",
               (unsigned long)firstFrameOnPort0Ms(module), framesDroppedBeforeBegin(module));

        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.startupScan.presencePollCount(),
                                       "the premise: a noScanSensors boot asks the line nothing of "
                                       "its own");
        TEST_ASSERT_FALSE_MESSAGE(module.server.rawIoReports.empty(),
                                  "noScanSensors: the raw read was never answered to the server");
        TEST_ASSERT_FALSE_MESSAGE(module.server.rawIoReports[0].isError,
                                  "noScanSensors: the raw read went into a UART nobody had begun");
        assertTheLineWasBegunBeforeItsFirstFrame(module, "noScanSensors");
    }
    {
        ModuleUnderTest module;
        module.settings.setConfigured(ModuleModeEnum::AP_TEST_MODE);

        module.setupService.setupModule();

        TEST_ASSERT_TRUE_MESSAGE(module.port0.lineIsBegun() && module.port2.lineIsBegun(),
                                 "the setup of the board-test mode left a UART of the line unbegun");
    }
}

// --------------------------------------------------------------------------------------------
// 20. The restart's pass asks every free port the cheap rung before it opens any window, and a
//     sensor it adopts goes out in a structure as it is adopted.
//
// A device at its factory address answers the cheap rung in seconds; a port's window is a minute
// and more of line time. Asked port by port, a device on the last port waits for the windows of
// every port before it - and the app shows a sensor only once a structure describes it. So the
// pass asks the cheap rung everywhere first, and every sensor it adopts goes out in a structure
// there and then: one structure on its way at a time and one owed, the owed one built from the
// registry when it goes. These stands send the structure the way the board does - the discovery
// asks the republish, the republish builds it from the registry and hands it to the outbox - and
// the server records every one it takes.
// --------------------------------------------------------------------------------------------

namespace {

/**
 * @brief An RY-WS12S as it leaves the factory: at address 1, 15 degrees of water in register 0, its
 *        address kept in register 0x00FF, ten seconds from power to its first answer.
 */
SimRs485Bus::Device makeFactoryRyWs12s()
{
    SimRs485Bus::Device device;
    device.address = 1;
    device.registerValues.push_back(6500); // (15 + 50) / 0.01
    device.registersAreAddressed = true;
    device.addressIsWritable = true;
    device.addressStoreRegister = 0x00FF;
    device.wakeUpMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS;
    return device;
}

/**
 * @brief Does this structure describe the row of this port as a sensor - and at which address: the
 *        group of a ModBus record is its row's address.
 */
bool describesTheSensorOf(const std::vector<Sensor>& records, int pin, int* group = nullptr)
{
    const std::string prefix = uidOfPin(pin) + ".";
    for (size_t i = 0; i < records.size(); i++)
    {
        if (records[i].uid.compare(0, prefix.size(), prefix) == 0 && records[i].type != "none")
        {
            if (group != nullptr)
            {
                *group = records[i].group;
            }
            return true;
        }
    }
    return false;
}

/** @brief Does this structure carry any record for this port at all - a sensor or an empty port? */
bool saysAnythingAbout(const std::vector<Sensor>& records, int pin)
{
    const std::string prefix = uidOfPin(pin);
    for (size_t i = 0; i < records.size(); i++)
    {
        if (records[i].uid.compare(0, prefix.size(), prefix) == 0)
        {
            return true;
        }
    }
    return false;
}

/** @brief When the server first had a structure describing the sensor of this port; 0 if never. */
uint64_t firstStructureDescribing(const SimServer& server, int pin)
{
    for (size_t i = 0; i < server.structures.size(); i++)
    {
        if (describesTheSensorOf(server.structures[i].sensors, pin))
        {
            return server.structures[i].atMs;
        }
    }
    return 0;
}

/** @brief Structures the server had taken by this moment. */
size_t structuresTakenBy(const SimServer& server, uint64_t untilMs)
{
    size_t taken = 0;
    for (size_t i = 0; i < server.structures.size(); i++)
    {
        if (server.structures[i].atMs <= untilMs)
        {
            taken++;
        }
    }
    return taken;
}

/** @brief When the pass opened its first window: the first presence poll of any port; 0 if none. */
uint64_t firstWindowOpenedAtMs(const ModuleUnderTest& module)
{
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        if (module.busWindows[i].discovery && module.busWindows[i].kind == StartupScanStep::PROBE_PRESENCE)
        {
            return module.busWindows[i].startedAtMs;
        }
    }
    return 0;
}

/** @brief What the restart's pass did about the structure, sampled while it ran. */
struct StructureWatch {
    /** When each port's row first appeared in the registry; 0 while it has not. */
    uint64_t adoptedAtMs[MODULE_PIN_COUNT] = {0, 0, 0, 0};
    /** The most structures handed to the outbox and not yet taken by the server, at any moment. */
    uint32_t mostInFlight = 0;
};

void sampleTheStructure(ModuleUnderTest& module, StructureWatch& watch)
{
    for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
    {
        if (watch.adoptedAtMs[pin] == 0 && (bool)module.sensorRepository.get(uidOfPin(pin).c_str()))
        {
            watch.adoptedAtMs[pin] = module.clock.nowMs();
        }
    }
    const uint32_t handedOver = module.republish.submittedCount();
    const uint32_t taken = static_cast<uint32_t>(module.server.structures.size());
    const uint32_t inFlight = handedOver > taken ? handedOver - taken : 0;
    if (inFlight > watch.mostInFlight)
    {
        watch.mostInFlight = inFlight;
    }
}

/** @brief Sample the stand every 100 ms for this long, and let the stored sensors be watched too. */
void runWatching(ModuleUnderTest& module, StructureWatch& watch, uint64_t forMs,
                 std::vector<SweepOfOneSensor>* sensors = nullptr)
{
    for (uint64_t spent = 0; spent < forMs; spent += 100)
    {
        module.scheduler.runForMs(100);
        sampleTheStructure(module, watch);
        for (size_t i = 0; sensors != nullptr && i < sensors->size(); i++)
        {
            if (spent % 1000 == 0)
            {
                sample(module, (*sensors)[i]);
            }
        }
    }
}

/**
 * @brief Boot a stand that sends its structure the way the board does, run the restart's pass to
 *        its end, and then long enough for what it still owes the server to go out.
 */
void runTheRestartsPassAndItsStructures(ModuleUnderTest& module, StructureWatch& watch,
                                        CadenceRecorder& cadence,
                                        std::vector<SweepOfOneSensor>* sensors = nullptr,
                                        void (*eachSecondOfThePass)(ModuleUnderTest&) = nullptr)
{
    module.sendTheStructureTheWayTheBoardDoes();
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        portOf(module, static_cast<int>(i)).recordPowerOffTimes(true);
    }
    module.boot();
    module.startArduinoLoopTask(cadence);
    for (int second = 0; second < 1200 && module.recordingStartupScan.finishedAtMs() == 0; second++)
    {
        runWatching(module, watch, 1000, sensors);
        if (eachSecondOfThePass != nullptr)
        {
            eachSecondOfThePass(module);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                             "the pass the restart armed did not end in twenty minutes");
    // One structure may still be on its way when the pass ends and one owed behind it.
    runWatching(module, watch, 10000 + 2 * (uint64_t)module.server.structurePostMs, sensors);
    cadence.closeAt(module.clock.nowMs());
    assertRunIsSound(module, cadence);
}

/**
 * @brief An uplink slow enough that the next sensor is adopted while the structure of the one before
 *        is still on its way: a structure takes a minute to reach the server - an attempt that runs
 *        out and a second one. The cheap rung adopts on this stand every 45 s or so.
 */
const uint32_t A_STRUCTURE_THAT_TAKES_A_MINUTE_MS = 60000;

} // namespace

// (a) A sensor at its factory address on port 3, ports 0 to 2 empty: the cheap rung finds it before
//     any window opens, and the server has a structure with it before the pass is over.
void test_s18_a_factory_sensor_on_the_last_port_is_found_and_shown_before_the_windows(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port3.addDevice(makeFactoryRyWs12s());

    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
    const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
    const uint64_t firstWindowMs = firstWindowOpenedAtMs(module);
    const uint64_t shownAtMs = firstStructureDescribing(module.server, 3);
    int lastGroup = -1;
    const bool lastDescribesIt = !module.server.structures.empty() &&
                                 describesTheSensorOf(module.server.structures.back().sensors, 3, &lastGroup);

    printf("  s18: a factory sensor on port 3 - adopted %lu ms into the pass, first window at %lu ms, "
           "in a structure the server had at %lu ms, pass over at %lu ms; %u structure(s) by then, "
           "%u in all; the last one says 0x%02X\n",
           (unsigned long)(watch.adoptedAtMs[3] > 0 ? watch.adoptedAtMs[3] - startedAtMs : 0),
           (unsigned long)(firstWindowMs > 0 ? firstWindowMs - startedAtMs : 0),
           (unsigned long)(shownAtMs > 0 ? shownAtMs - startedAtMs : 0),
           (unsigned long)(finishedAtMs - startedAtMs),
           (unsigned)structuresTakenBy(module.server, finishedAtMs),
           (unsigned)module.server.structures.size(), (unsigned)(lastGroup < 0 ? 0 : lastGroup));

    TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[3] > 0,
                             "the sensor at its factory address on port 3 was never adopted");
    char message[288];
    snprintf(message, sizeof(message),
             "the sensor on port 3 was adopted %lu ms into the pass, the first window opened at %lu "
             "ms - it waited for the windows of the ports walked before it",
             (unsigned long)(watch.adoptedAtMs[3] - startedAtMs),
             (unsigned long)(firstWindowMs > 0 ? firstWindowMs - startedAtMs : 0));
    TEST_ASSERT_TRUE_MESSAGE(firstWindowMs > 0 && watch.adoptedAtMs[3] < firstWindowMs, message);

    snprintf(message, sizeof(message),
             "the server first had a structure with the sensor of port 3 %lu ms into the pass, which "
             "ended at %lu ms - for the whole pass the app had nothing to show",
             (unsigned long)(shownAtMs > 0 ? shownAtMs - startedAtMs : 0),
             (unsigned long)(finishedAtMs - startedAtMs));
    TEST_ASSERT_TRUE_MESSAGE(shownAtMs > 0 && shownAtMs < finishedAtMs, message);

    TEST_ASSERT_TRUE_MESSAGE(stepsOf(module, StartupScanStep::PROBE_PRESENCE, 3).empty(),
                             "the port the cheap rung named was given a window all the same");
    TEST_ASSERT_TRUE_MESSAGE(lastDescribesIt,
                             "the last structure the server had does not describe the sensor");
    TEST_ASSERT_EQUAL_INT_MESSAGE(MODBUS_ADDRESS_MIN + 3, lastGroup,
                                  "the last structure does not carry the address the pass moved the "
                                  "sensor to");
}

// (b) Two sensors the cheap rung finds one after the other, while a structure takes a minute to reach
//     the server: never more than one structure on its way, the first sensor in a structure before
//     the pass is over, and the last structure carrying both at the addresses the pass left them on.
void test_s18_two_sensors_found_cheaply_go_out_one_structure_at_a_time(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port0.addDevice(makeFactoryRyWs12s());
    module.port2.addDevice(makeFactoryRyWs12s());
    module.server.structurePostMs = A_STRUCTURE_THAT_TAKES_A_MINUTE_MS;

    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
    const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
    int group0 = -1;
    int group2 = -1;
    const bool lastHasPort0 = !module.server.structures.empty() &&
                              describesTheSensorOf(module.server.structures.back().sensors, 0, &group0);
    const bool lastHasPort2 = !module.server.structures.empty() &&
                              describesTheSensorOf(module.server.structures.back().sensors, 2, &group2);

    printf("  s18: two factory sensors, a structure a minute on its way - adopted %lu / %lu ms into the "
           "pass, first in a structure at %lu / %lu ms, pass over at %lu ms; %u structure(s) handed "
           "over, %u taken by the pass's end, %u in all, at most %u on its way at once; the last "
           "says 0x%02X / 0x%02X\n",
           (unsigned long)(watch.adoptedAtMs[0] > 0 ? watch.adoptedAtMs[0] - startedAtMs : 0),
           (unsigned long)(watch.adoptedAtMs[2] > 0 ? watch.adoptedAtMs[2] - startedAtMs : 0),
           (unsigned long)(firstStructureDescribing(module.server, 0) > 0
                               ? firstStructureDescribing(module.server, 0) - startedAtMs
                               : 0),
           (unsigned long)(firstStructureDescribing(module.server, 2) > 0
                               ? firstStructureDescribing(module.server, 2) - startedAtMs
                               : 0),
           (unsigned long)(finishedAtMs - startedAtMs), (unsigned)module.republish.submittedCount(),
           (unsigned)structuresTakenBy(module.server, finishedAtMs),
           (unsigned)module.server.structures.size(), (unsigned)watch.mostInFlight,
           (unsigned)(group0 < 0 ? 0 : group0), (unsigned)(group2 < 0 ? 0 : group2));

    TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[0] > 0 && watch.adoptedAtMs[2] > 0,
                             "not both sensors at their factory address were adopted");

    char message[256];
    snprintf(message, sizeof(message),
             "%u structures were on their way to the server at once - one was handed over while "
             "another was still in flight",
             (unsigned)watch.mostInFlight);
    TEST_ASSERT_TRUE_MESSAGE(watch.mostInFlight <= 1, message);

    const uint64_t firstShownMs = firstStructureDescribing(module.server, 0);
    snprintf(message, sizeof(message),
             "the server first had a structure with the sensor of port 0 %lu ms into the pass, which "
             "ended at %lu ms",
             (unsigned long)(firstShownMs > 0 ? firstShownMs - startedAtMs : 0),
             (unsigned long)(finishedAtMs - startedAtMs));
    TEST_ASSERT_TRUE_MESSAGE(firstShownMs > 0 && firstShownMs < finishedAtMs, message);

    TEST_ASSERT_TRUE_MESSAGE(lastHasPort0 && lastHasPort2,
                             "the last structure the server had does not carry both sensors");
    TEST_ASSERT_EQUAL_INT_MESSAGE(MODBUS_ADDRESS_MIN + 0, group0,
                                  "the last structure does not carry port 0's sensor at its own "
                                  "port's address");
    TEST_ASSERT_EQUAL_INT_MESSAGE(MODBUS_ADDRESS_MIN + 2, group2,
                                  "the last structure does not carry port 2's sensor at its own "
                                  "port's address");
}

// (c) A sensor the cheap rung names on port 0 gets no window of its own, and the steps of the ports
//     after it - port 1's is on its wire - take its power twice and no more: for port 1's confirming
//     scan (port 1 hears the sensor at its factory address) and for port 1's window. The darkness
//     they leave is not the sensor's failure, and the port recovery does not act on it.
const int POWER_OFFS_OF_A_PORT_THE_CHEAP_RUNG_NAMED = 2;

void test_s18_a_port_the_cheap_rung_named_gets_no_window_and_no_more_darkness(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port0.addDevice(makeFactoryRyWs12s());

    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
    unsigned windowsOfItsWire = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::PROBE_PRESENCE && w.pin == 1)
        {
            windowsOfItsWire++;
        }
    }
    printf("  s18: a factory sensor on port 0 - adopted %lu ms into the pass; %u window(s) of its own, "
           "%u of port 1 on its wire; its port lost its power %d time(s), %lu port recovery "
           "action(s)\n",
           (unsigned long)(watch.adoptedAtMs[0] > 0 ? watch.adoptedAtMs[0] - startedAtMs : 0),
           (unsigned)stepsOf(module, StartupScanStep::PROBE_PRESENCE, 0).size(), windowsOfItsWire,
           module.port0.powerOffTransitions, (unsigned long)module.acquisition.recoveryActionCount());

    TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[0] > 0, "the sensor on port 0 was never adopted");
    TEST_ASSERT_TRUE_MESSAGE(stepsOf(module, StartupScanStep::PROBE_PRESENCE, 0).empty(),
                             "the port the cheap rung named was given a window all the same");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, windowsOfItsWire,
                                   "the free port sharing its wire was not given its one window");
    char message[224];
    snprintf(message, sizeof(message),
             "the port of the sensor the cheap rung named lost its power %d time(s), the steps "
             "that take it account for %d",
             module.port0.powerOffTransitions, POWER_OFFS_OF_A_PORT_THE_CHEAP_RUNG_NAMED);
    TEST_ASSERT_TRUE_MESSAGE(module.port0.powerOffTransitions <= POWER_OFFS_OF_A_PORT_THE_CHEAP_RUNG_NAMED,
                             message);
    snprintf(message, sizeof(message),
             "the port recovery acted %lu time(s) on a module whose sensor answers throughout",
             (unsigned long)module.acquisition.recoveryActionCount());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.acquisition.recoveryActionCount(), message);
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr,
                             "the sensor does not answer at its own port's address after the pass");
}

namespace {

/** @brief A layout of the restart's pass with cheap finds in it, and the bounds it is held to. */
struct CheapFindsLayout {
    const char* name;
    /** The stored RY-WS301 the module reads, or -1. */
    int stored;
    /** Factory RY-WS12S devices, one per port listed; -1 ends the list. */
    int factory[3];
    /** Times the steps of the pass take the stored sensor's power, step by step (below). */
    int storedPowerOffsByThePass;
};

// What the steps of the restart's pass take from the stored sensor on port 1: only steps about
// port 0, the port sharing its wire. Factory on 2: the poll that opens port 0's window - one; the
// confirming scans on ports 2 and 3 and the address write of port 2's sensor are on the other wire.
// Factory on 0 and 2: the confirming scan of port 0's find and the address write that moves port
// 0's sensor into the module's range - two.
const int STORED_POWER_OFFS_BY_THE_PASS_FACTORY_ON_2 = 1;
const int STORED_POWER_OFFS_BY_THE_PASS_FACTORY_ON_0_AND_2 = 2;

/**
 * @brief Power-offs of this port made by a step of the discovery - not the recovery's.
 *
 * A step's own power-off is followed by the 100 ms the pin settles, inside the step; what the sweep
 * does right after a step - the recovery's power-off included - happens at the moment the step
 * ended, before the clock moves.
 */
int powerOffsByTheDiscovery(const ModuleUnderTest& module, int pin)
{
    const SimRs485Bus& port = *static_cast<const SimRs485Bus*>(module.ports[static_cast<size_t>(pin)]);
    int count = 0;
    for (size_t o = 0; o < port.powerOffAtUs.size(); o++)
    {
        const uint64_t atMs = port.powerOffAtUs[o] / 1000;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (w.discovery && atMs >= w.startedAtMs && atMs < w.endedAtMs)
            {
                count++;
                break;
            }
        }
    }
    return count;
}

/** @brief Which wire a port is on, as wireThePortsTheWayTheBoardWiresThem() wires the ports. */
int wireOf(int pin)
{
    return pin / 2;
}

/**
 * @brief Power-offs of this port made by a step of the discovery about a port of the other wire.
 *
 * Such a step puts its frames on the other wire, and nothing plugged into this one hears them - the
 * two UARTs are two lines on this stand, as on the board. None of those steps has anything to keep
 * off its wire by taking this port's power.
 */
int powerOffsByTheOtherWire(const ModuleUnderTest& module, int pin)
{
    const SimRs485Bus& port = *static_cast<const SimRs485Bus*>(module.ports[static_cast<size_t>(pin)]);
    int count = 0;
    for (size_t o = 0; o < port.powerOffAtUs.size(); o++)
    {
        const uint64_t atMs = port.powerOffAtUs[o] / 1000;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (w.discovery && atMs >= w.startedAtMs && atMs < w.endedAtMs)
            {
                count += (w.pin >= 0 && wireOf(w.pin) != wireOf(pin)) ? 1 : 0;
                break;
            }
        }
    }
    return count;
}

/**
 * @brief How each port's row was described, structure by structure, as the server took them: 's'
 *        a sensor, 'n' an empty port, '-' no record.
 */
std::string descriptionsOf(const SimServer& server, int pin)
{
    std::string seen;
    for (size_t i = 0; i < server.structures.size(); i++)
    {
        if (describesTheSensorOf(server.structures[i].sensors, pin))
        {
            seen += 's';
        }
        else if (saysAnythingAbout(server.structures[i].sensors, pin))
        {
            seen += 'n';
        }
        else
        {
            seen += '-';
        }
    }
    return seen;
}

} // namespace

// (d) Cheap finds in a row: every confirming scan takes the power of the port sharing the wire of
//     the one it asks and leaves it so, and on the restart's pass the scans come one after the
//     other, before the windows. Stored and adopted sensors need their ten seconds of power, as the
//     logged ones do. Held here, layout by layout: the steps of the pass take the stored sensor's power
//     only as often as the steps listed above and never for a step of the other wire, the sweep
//     reads no port before it has come up again, the port
//     recovery does not act - the darkness is the pass's, not a sensor's failure - and no sensor
//     the server was shown as a sensor is shown as an empty port afterwards. How long the stored
//     sensor goes unread is printed, not bounded.
void test_s18_cheap_finds_in_a_row_take_the_stored_sensors_power_no_more_and_show_no_sensor_as_empty(void)
{
    const CheapFindsLayout layouts[3] = {
        {"stored on 1, factory on 2", 1, {2, -1, -1}, STORED_POWER_OFFS_BY_THE_PASS_FACTORY_ON_2},
        {"stored on 1, factory on 0 and 2", 1, {0, 2, -1}, STORED_POWER_OFFS_BY_THE_PASS_FACTORY_ON_0_AND_2},
        {"factory on 0 and 2", -1, {0, 2, -1}, 0},
    };

    struct Outcome {
        uint64_t passMs = 0;
        uint64_t recoveryActions = 0;
        int powerOffs[MODULE_PIN_COUNT] = {0, 0, 0, 0};
        int powerOffsByTheDiscovery[MODULE_PIN_COUNT] = {0, 0, 0, 0};
        int powerOffsByTheOtherWire[MODULE_PIN_COUNT] = {0, 0, 0, 0};
        unsigned readsBeforeTheWakeUp[MODULE_PIN_COUNT] = {0, 0, 0, 0};
        SweepOfOneSensor stored;
        std::string described[MODULE_PIN_COUNT];
    };
    Outcome outcomes[3];

    for (int l = 0; l < 3; l++)
    {
        const CheapFindsLayout& layout = layouts[l];
        Outcome& outcome = outcomes[l];
        ModuleUnderTest module;
        eraseTheRegistry(module, theServersCatalog());
        std::vector<SweepOfOneSensor> sensors;
        if (layout.stored >= 0)
        {
            module.sensorRepository.save(makeStoredRyWs301(layout.stored));
            portOf(module, layout.stored)
                .addDevice(makeLoggedRyWs301((uint8_t)(MODBUS_ADDRESS_MIN + layout.stored)));
            SweepOfOneSensor sensor;
            sensor.pin = layout.stored;
            sensor.uid = uidOfPin(layout.stored);
            sensors.push_back(sensor);
        }
        for (int i = 0; i < 3 && layout.factory[i] >= 0; i++)
        {
            portOf(module, layout.factory[i]).addDevice(makeFactoryRyWs12s());
        }

        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
        // And two more minutes, for whatever the pass left behind to show in the structure.
        runWatching(module, watch, 120000, &sensors);

        outcome.passMs = module.recordingStartupScan.finishedAtMs() - module.recordingStartupScan.startedAtMs();
        outcome.recoveryActions = module.acquisition.recoveryActionCount();
        if (layout.stored >= 0)
        {
            outcome.stored = sensors[0];
        }
        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            outcome.described[pin] = descriptionsOf(module.server, pin);
            outcome.powerOffs[pin] = portOf(module, pin).powerOffTransitions;
            outcome.powerOffsByTheDiscovery[pin] = powerOffsByTheDiscovery(module, pin);
            outcome.powerOffsByTheOtherWire[pin] = powerOffsByTheOtherWire(module, pin);
            outcome.readsBeforeTheWakeUp[pin] =
                sweepReadsBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs());
        }
        printf("  s18: %s - pass %lu ms, %lu port recovery action(s); power-offs per port %d/%d/%d/%d, "
               "by a step of the discovery %d/%d/%d/%d, of the other wire %d/%d/%d/%d; "
               "sweep reads before the wake-up %u/%u/%u/%u; "
               "the stored sensor: longest gap between "
               "readings %lu ms, longest unread while awake %lu ms; described per structure: port 0 "
               "'%s', port 1 '%s', port 2 '%s', port 3 '%s'\n",
               layout.name, (unsigned long)outcome.passMs, (unsigned long)outcome.recoveryActions,
               outcome.powerOffs[0], outcome.powerOffs[1], outcome.powerOffs[2], outcome.powerOffs[3],
               outcome.powerOffsByTheDiscovery[0], outcome.powerOffsByTheDiscovery[1],
               outcome.powerOffsByTheDiscovery[2], outcome.powerOffsByTheDiscovery[3],
               outcome.powerOffsByTheOtherWire[0], outcome.powerOffsByTheOtherWire[1],
               outcome.powerOffsByTheOtherWire[2], outcome.powerOffsByTheOtherWire[3],
               outcome.readsBeforeTheWakeUp[0], outcome.readsBeforeTheWakeUp[1],
               outcome.readsBeforeTheWakeUp[2], outcome.readsBeforeTheWakeUp[3],
               (unsigned long)outcome.stored.worstGapMs,
               (unsigned long)outcome.stored.worstUnreadWhileAwakeMs, outcome.described[0].c_str(),
               outcome.described[1].c_str(), outcome.described[2].c_str(),
               outcome.described[3].c_str());
    }

    char message[320];
    for (int l = 0; l < 3; l++)
    {
        const CheapFindsLayout& layout = layouts[l];
        const Outcome& outcome = outcomes[l];

        snprintf(message, sizeof(message),
                 "%s: the port recovery acted %lu time(s) on sensors that answer throughout",
                 layout.name, (unsigned long)outcome.recoveryActions);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, outcome.recoveryActions, message);

        snprintf(message, sizeof(message),
                 "%s: the sweep read a port before it had come up again %u/%u/%u/%u time(s) - "
                 "reads nobody can answer",
                 layout.name, outcome.readsBeforeTheWakeUp[0], outcome.readsBeforeTheWakeUp[1],
                 outcome.readsBeforeTheWakeUp[2], outcome.readsBeforeTheWakeUp[3]);
        TEST_ASSERT_TRUE_MESSAGE(outcome.readsBeforeTheWakeUp[0] + outcome.readsBeforeTheWakeUp[1] +
                                         outcome.readsBeforeTheWakeUp[2] +
                                         outcome.readsBeforeTheWakeUp[3] ==
                                     0,
                                 message);

        if (layout.stored >= 0)
        {
            snprintf(message, sizeof(message),
                     "%s: steps of the discovery took the stored sensor's power %d time(s), the steps "
                     "that take it account for %d",
                     layout.name, outcome.powerOffsByTheDiscovery[layout.stored],
                     layout.storedPowerOffsByThePass);
            TEST_ASSERT_TRUE_MESSAGE(outcome.powerOffsByTheDiscovery[layout.stored] <=
                                         layout.storedPowerOffsByThePass,
                                     message);
            snprintf(message, sizeof(message),
                     "%s: steps about ports of the other wire took the stored sensor's power %d time(s)",
                     layout.name, outcome.powerOffsByTheOtherWire[layout.stored]);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, outcome.powerOffsByTheOtherWire[layout.stored], message);
        }

        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            const std::string& seen = outcome.described[pin];
            const size_t shown = seen.find('s');
            snprintf(message, sizeof(message),
                     "%s: port %d was shown to the server as a sensor and then as an empty port "
                     "(structure by structure: '%s')",
                     layout.name, pin, seen.c_str());
            TEST_ASSERT_TRUE_MESSAGE(shown == std::string::npos ||
                                         seen.find('n', shown) == std::string::npos,
                                     message);
        }
    }
}

// (e) What a structure sent in the middle of the pass says about the ports the pass has not reached
//     yet: nothing. It is built from the registry (RepublishModuleStructureUseCase::execute() clears
//     the node and asks describeStoredSensors()), a free port has no row, so it has no record - not a
//     sensor, not an empty port - exactly as in the structure the pass ends with. And a stored sensor
//     is described by what the line answers for it, not by where the walk is: a sensor the pass keeps
//     taking the power of is a sensor in every structure.
void test_s18_a_structure_sent_mid_pass_says_nothing_of_the_ports_the_pass_has_not_reached(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.sensorRepository.save(makeStoredRyWs301(2));
    module.port2.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN + 2));
    module.port0.addDevice(makeFactoryRyWs12s());

    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
    const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
    size_t midPass = module.server.structures.size();
    for (size_t i = 0; i < module.server.structures.size(); i++)
    {
        if (module.server.structures[i].atMs < finishedAtMs &&
            describesTheSensorOf(module.server.structures[i].sensors, 0))
        {
            midPass = i;
            break;
        }
    }
    printf("  s18: stored on 2, factory on 0 - %u structure(s); the one sent mid-pass at %lu ms of a "
           "pass of %lu ms; described per structure: port 0 '%s', port 1 '%s', port 2 '%s', port 3 "
           "'%s'\n",
           (unsigned)module.server.structures.size(),
           (unsigned long)(midPass < module.server.structures.size()
                               ? module.server.structures[midPass].atMs - startedAtMs
                               : 0),
           (unsigned long)(finishedAtMs - startedAtMs), descriptionsOf(module.server, 0).c_str(),
           descriptionsOf(module.server, 1).c_str(), descriptionsOf(module.server, 2).c_str(),
           descriptionsOf(module.server, 3).c_str());

    TEST_ASSERT_TRUE_MESSAGE(midPass < module.server.structures.size(),
                             "no structure with the sensor adopted on port 0 reached the server "
                             "before the pass was over");
    const std::vector<Sensor>& atAdoption = module.server.structures[midPass].sensors;
    TEST_ASSERT_TRUE_MESSAGE(describesTheSensorOf(atAdoption, 2),
                             "the structure sent mid-pass does not describe the stored sensor as a "
                             "sensor");
    TEST_ASSERT_FALSE_MESSAGE(saysAnythingAbout(atAdoption, 1) || saysAnythingAbout(atAdoption, 3),
                              "the structure sent mid-pass says something about a free port the pass "
                              "had not finished with");

    char message[256];
    for (size_t i = 0; i < module.server.structures.size(); i++)
    {
        const std::vector<Sensor>& records = module.server.structures[i].sensors;
        snprintf(message, sizeof(message),
                 "structure %u (at %lu ms of the pass) does not describe the stored sensor as a sensor",
                 (unsigned)i, (unsigned long)(module.server.structures[i].atMs - startedAtMs));
        TEST_ASSERT_TRUE_MESSAGE(describesTheSensorOf(records, 2), message);
        snprintf(message, sizeof(message),
                 "structure %u (at %lu ms of the pass) carries a record for a free port",
                 (unsigned)i, (unsigned long)(module.server.structures[i].atMs - startedAtMs));
        TEST_ASSERT_FALSE_MESSAGE(saysAnythingAbout(records, 1) || saysAnythingAbout(records, 3), message);
    }
}

// (f) Two sensors of one model, both at their factory address, on the two ports of one wire. Asked
//     at that address with both powered, they answer at once - a collision, which no reader takes -
//     so the cheap rung names neither and both are left to the windows: each window writes its own
//     port's address with the other port dark, and each sensor is found there.
void test_s18_twins_at_their_factory_address_on_one_wire_are_both_found(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port0.addDevice(makeFactoryRyWs12s());
    module.port1.addDevice(makeFactoryRyWs12s());

    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    Utils::Optional<SensorEntity> onPort0 = module.sensorRepository.get(uidOfPin(0).c_str());
    Utils::Optional<SensorEntity> onPort1 = module.sensorRepository.get(uidOfPin(1).c_str());
    const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
    printf("  s18: twins at the factory address on ports 0 and 1 - port 0 %s at 0x%02X (%lu ms into the "
           "pass), port 1 %s at 0x%02X (%lu ms); %u / %u write(s) into their ports; pass over at %lu ms\n",
           onPort0 ? "adopted" : "not found", onPort0 ? (unsigned)onPort0.value().modbusDeviceAddress : 0,
           (unsigned long)(watch.adoptedAtMs[0] > 0 ? watch.adoptedAtMs[0] - startedAtMs : 0),
           onPort1 ? "adopted" : "not found", onPort1 ? (unsigned)onPort1.value().modbusDeviceAddress : 0,
           (unsigned long)(watch.adoptedAtMs[1] > 0 ? watch.adoptedAtMs[1] - startedAtMs : 0),
           (unsigned)module.port0.writes.size(), (unsigned)module.port1.writes.size(),
           (unsigned long)(module.recordingStartupScan.finishedAtMs() - startedAtMs));

    TEST_ASSERT_TRUE_MESSAGE((bool)onPort0 && (bool)onPort1, "not both twins were found");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, (unsigned)onPort0.value().modbusDeviceAddress,
                                   "port 0's sensor does not hold its own port's address");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 1, (unsigned)onPort1.value().modbusDeviceAddress,
                                   "port 1's sensor does not hold its own port's address");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr &&
                                 module.port1.device(MODBUS_ADDRESS_MIN + 1) != nullptr,
                             "a twin does not answer at the address its row says");
}

namespace {

/** @brief What a stand has plugged in: factory RY-WS12S, RY-WS301 at its port's address, stored rows. */
struct PluggedIn {
    const char* name;
    int factory[4];
    int atThePortsAddress[4];
    int stored[4];
};

bool listed(const int* pins, int pin)
{
    for (int i = 0; i < 4 && pins[i] >= 0; i++)
    {
        if (pins[i] == pin)
        {
            return true;
        }
    }
    return false;
}

/** @brief A module with an erased registry and these devices on it, sending its structure as the board does. */
void plugIn(ModuleUnderTest& module, const PluggedIn& layout, std::vector<SweepOfOneSensor>* sensors = nullptr)
{
    eraseTheRegistry(module, theServersCatalog());
    for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
    {
        if (listed(layout.factory, pin))
        {
            portOf(module, pin).addDevice(makeFactoryRyWs12s());
        }
        if (listed(layout.atThePortsAddress, pin) || listed(layout.stored, pin))
        {
            portOf(module, pin).addDevice(makeLoggedRyWs301((uint8_t)(MODBUS_ADDRESS_MIN + pin)));
        }
        if (listed(layout.stored, pin))
        {
            module.sensorRepository.save(makeStoredRyWs301(pin));
            if (sensors != nullptr)
            {
                SweepOfOneSensor sensor;
                sensor.pin = pin;
                sensor.uid = uidOfPin(pin);
                sensors->push_back(sensor);
            }
        }
    }
}

} // namespace

// (g) A confirming scan takes the power of the port sharing its wire and leaves it so, and on the
//     restart's pass it is followed by the cheap questions of the other free ports. Each of them
//     gets its power back and its ten seconds before it is asked - not only the one asked next. A
//     factory sensor two ports after a cheap find is found by the cheap rung, before any window,
//     with no identification step and no address written into its port but the one that moves it
//     into the module's range.
void test_s18_a_factory_sensor_two_ports_after_a_cheap_find_is_found_by_the_cheap_rung(void)
{
    const PluggedIn layouts[2] = {
        {"factory on 0 and 3", {0, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory on 1 and 3", {1, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    };
    for (int l = 0; l < 2; l++)
    {
        ModuleUnderTest module;
        plugIn(module, layouts[l]);
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence);

        const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
        const uint64_t firstWindowMs = firstWindowOpenedAtMs(module);
        unsigned identifySteps = 0;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            if (module.busWindows[i].discovery && module.busWindows[i].kind == StartupScanStep::SCAN_CANDIDATE)
            {
                identifySteps++;
            }
        }
        Utils::Optional<SensorEntity> onPort3 = module.sensorRepository.get(uidOfPin(3).c_str());
        printf("  s18: %s - port 3 adopted at %lu ms (first window at %lu ms), %u identification step(s), "
               "%u write(s) into port 3, at 0x%02X; pass over at %lu ms\n",
               layouts[l].name,
               (unsigned long)(watch.adoptedAtMs[3] > 0 ? watch.adoptedAtMs[3] - startedAtMs : 0),
               (unsigned long)(firstWindowMs > 0 ? firstWindowMs - startedAtMs : 0), identifySteps,
               (unsigned)module.port3.writes.size(),
               onPort3 ? (unsigned)onPort3.value().modbusDeviceAddress : 0,
               (unsigned long)(module.recordingStartupScan.finishedAtMs() - startedAtMs));

        char message[256];
        snprintf(message, sizeof(message),
                 "%s: port 3 was adopted at %lu ms, the first window opened at %lu ms - its cheap "
                 "question went to a port a confirming scan had left dark",
                 layouts[l].name, (unsigned long)(watch.adoptedAtMs[3] > 0 ? watch.adoptedAtMs[3] - startedAtMs : 0),
                 (unsigned long)(firstWindowMs > 0 ? firstWindowMs - startedAtMs : 0));
        TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[3] > 0 && firstWindowMs > 0 &&
                                     watch.adoptedAtMs[3] < firstWindowMs,
                                 message);
        snprintf(message, sizeof(message), "%s: %u identification step(s) on a module whose sensors "
                 "all hold their factory address", layouts[l].name, identifySteps);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, identifySteps, message);
        snprintf(message, sizeof(message), "%s: %u address write(s) into port 3, one moves its "
                 "sensor into the module's range", layouts[l].name, (unsigned)module.port3.writes.size());
        TEST_ASSERT_TRUE_MESSAGE(module.port3.writes.size() <= 1, message);
        TEST_ASSERT_TRUE_MESSAGE(onPort3 && onPort3.value().modbusDeviceAddress == MODBUS_ADDRESS_MIN + 3,
                                 "port 3's sensor does not hold its own port's address");
    }
}

// (h) The same for a window: a port the cheap rung left to the rest of the ladder, darkened by the
//     confirming scan of a find after it, is given its power and its ten seconds before its window.
//     A sensor at an address of the module's range is then found by a read of the range - no write
//     into its port.
void test_s18_a_window_after_a_cheap_find_asks_a_port_with_its_power_up(void)
{
    const PluggedIn layout = {"at 0x70 on 0, factory on 1", {1, -1, -1, -1}, {0, -1, -1, -1}, {-1, -1, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
    Utils::Optional<SensorEntity> onPort0 = module.sensorRepository.get(uidOfPin(0).c_str());
    printf("  s18: %s - port 0 %s at 0x%02X at %lu ms, port 1 at %lu ms, %u write(s) into port 0; pass "
           "over at %lu ms\n",
           layout.name, onPort0 ? "adopted" : "not found",
           onPort0 ? (unsigned)onPort0.value().modbusDeviceAddress : 0,
           (unsigned long)(watch.adoptedAtMs[0] > 0 ? watch.adoptedAtMs[0] - startedAtMs : 0),
           (unsigned long)(watch.adoptedAtMs[1] > 0 ? watch.adoptedAtMs[1] - startedAtMs : 0),
           (unsigned)module.port0.writes.size(),
           (unsigned long)(module.recordingStartupScan.finishedAtMs() - startedAtMs));

    TEST_ASSERT_TRUE_MESSAGE((bool)onPort0, "the sensor at 0x70 on port 0 was not found");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, (unsigned)onPort0.value().modbusDeviceAddress,
                                   "the sensor on port 0 was not adopted at the address it answers at");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.port0.writes.size(),
                                   "an address was written into port 0 although its device answers a "
                                   "read of the range - it was asked before it had come up");
}

namespace {

/**
 * @brief Frames anybody sent into this port while it had had its power for less than
 *        PORT_WAKE_UP_MS, from `fromMs` on - the sweep, a step, a command; the first one described.
 */
unsigned framesBeforeTheWakeUp(ModuleUnderTest& module, int pin, uint64_t fromMs, std::string& first)
{
    const SimRs485Bus& port = portOf(module, pin);
    unsigned early = 0;
    for (size_t f = 0; f < port.sentAtUs.size() && f < port.sentAfterPowerForMs.size(); f++)
    {
        const uint64_t atMs = port.sentAtUs[f] / 1000;
        if (atMs < fromMs || port.sentAfterPowerForMs[f] >= StartupSensorScanUseCase::PORT_WAKE_UP_MS)
        {
            continue;
        }
        early++;
        if (early > 1)
        {
            continue;
        }
        const char* whose = "the sweep";
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (atMs >= w.startedAtMs && atMs < w.endedAtMs)
            {
                whose = w.discovery ? BackgroundActs::ladderStepName(w.kind)
                                    : (w.raw ? "a raw command" : "a bus command");
                break;
            }
        }
        char text[128];
        snprintf(text, sizeof(text), "port %d at %lu ms after %lu ms of power, by %s", pin,
                 (unsigned long)atMs, (unsigned long)port.sentAfterPowerForMs[f], whose);
        first = text;
    }
    return early;
}

} // namespace

// (i) The rule itself, on the stand-in: no cheap question and no presence poll of the restart's pass
//     goes to a port that has had its power for less than PORT_WAKE_UP_MS - the time this firmware
//     gives a sensor to come up. Layouts with finds before, between and after the quiet ports, and
//     with the stored sensors beside them. And the same rule for every frame, whoever sends it, over
//     the pass and the two minutes after it: no read, write or scan of a port before it has come up
//     again - and so no port recovery on a sensor that answers, no address write the device never
//     heard, and every row where its device answers, at the address of its port.
void test_s18_the_restarts_pass_asks_no_port_before_it_has_come_up(void)
{
    const PluggedIn layouts[] = {
        {"nothing plugged in", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory on 3", {3, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory on 0 and 2", {0, 2, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory on 0 and 3", {0, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory on 1 and 2", {1, 2, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory on 1 and 3", {1, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory on 0, 1, 2", {0, 1, 2, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"at 0x70 on 0, factory on 1", {1, -1, -1, -1}, {0, -1, -1, -1}, {-1, -1, -1, -1}},
        {"at 0x73 on 3, factory on 0", {0, -1, -1, -1}, {3, -1, -1, -1}, {-1, -1, -1, -1}},
        {"stored on 1, factory on 2", {2, -1, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}},
        {"stored on 0, factory on 3", {3, -1, -1, -1}, {-1, -1, -1, -1}, {0, -1, -1, -1}},
        {"stored on 0 and 2", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, 2, -1, -1}},
        {"stored on 0", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, -1, -1, -1}},
        {"stored on 2", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {2, -1, -1, -1}},
        {"stored on 1, factory on 0 and 2", {0, 2, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}},
        {"factory on 0", {0, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"stored on 0, factory on 1", {1, -1, -1, -1}, {-1, -1, -1, -1}, {0, -1, -1, -1}},
        {"stored on 2, factory on 3 and 0", {3, 0, -1, -1}, {-1, -1, -1, -1}, {2, -1, -1, -1}},
        {"stored on 0 and 2, factory on 1 and 3", {1, 3, -1, -1}, {-1, -1, -1, -1}, {0, 2, -1, -1}},
        {"at 0x70 on 0", {-1, -1, -1, -1}, {0, -1, -1, -1}, {-1, -1, -1, -1}},
        {"at 0x71 on 1", {-1, -1, -1, -1}, {1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"stored at 0x70 on 0, at 0x71 on 1", {-1, -1, -1, -1}, {1, -1, -1, -1}, {0, -1, -1, -1}},
        {"twins on 0 and 1", {0, 1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"twins on 0 and 1, twins on 2 and 3", {0, 1, 2, 3}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);

    char message[288];
    std::string failures;
    for (size_t l = 0; l < count; l++)
    {
        ModuleUnderTest module;
        plugIn(module, layouts[l]);
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence);
        runWatching(module, watch, 120000);

        const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
        unsigned sweepReadsEarly = 0;
        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            sweepReadsEarly +=
                sweepReadsBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs());
        }
        const uint64_t recoveryActions = module.acquisition.recoveryActionCount();
        if (sweepReadsEarly > 0 || recoveryActions > 0)
        {
            snprintf(message, sizeof(message), " [%s: %u sweep read(s) before the wake-up, %lu port "
                     "recovery action(s)]",
                     layouts[l].name, sweepReadsEarly, (unsigned long)recoveryActions);
            failures += message;
        }
        unsigned framesEarly = 0;
        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            std::string first;
            const unsigned early =
                framesBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs(), first);
            if (early > 0 && framesEarly == 0)
            {
                snprintf(message, sizeof(message), " [%s: a frame into %s]", layouts[l].name,
                         first.c_str());
                failures += message;
            }
            framesEarly += early;
        }

        // Where the pass leaves every sensor: one row per port something is plugged into, at the
        // address of that port, and the device answering there.
        const std::vector<SensorEntity> rows = module.sensorRepository.getAll();
        std::string addresses;
        unsigned plugged = 0;
        unsigned rowsRight = 0;
        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            const bool somethingThere = listed(layouts[l].factory, pin) ||
                                        listed(layouts[l].atThePortsAddress, pin) ||
                                        listed(layouts[l].stored, pin);
            plugged += somethingThere ? 1 : 0;
            for (size_t r = 0; r < rows.size(); r++)
            {
                if (rows[r].pin != pin)
                {
                    continue;
                }
                const int address = rows[r].modbusDeviceAddress;
                const bool answersThere = portOf(module, pin).device((uint8_t)address) != nullptr;
                snprintf(message, sizeof(message), " %d:0x%02X%s", pin, (unsigned)address,
                         answersThere ? "" : "(nobody there)");
                addresses += message;
                if (address == MODBUS_ADDRESS_MIN + pin && answersThere)
                {
                    rowsRight++;
                }
            }
        }
        const uint32_t refusedMoves = module.startupScan.factoryAddressMoveRefusedCount();
        if (rows.size() != plugged || rowsRight != plugged || refusedMoves > 0)
        {
            snprintf(message, sizeof(message), " [%s: rows%s for %u port(s) with a sensor, %u "
                     "address move(s) refused]",
                     layouts[l].name, addresses.c_str(), plugged, (unsigned)refusedMoves);
            failures += message;
        }
        unsigned questions = 0;
        unsigned early = 0;
        uint64_t leastPowerMs = (uint64_t)-1;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (!w.discovery || w.startedAtMs > finishedAtMs ||
                (w.kind != StartupScanStep::PROBE_CANDIDATE && w.kind != StartupScanStep::PROBE_PRESENCE))
            {
                continue;
            }
            questions++;
            if (w.pinPoweredForMs < leastPowerMs)
            {
                leastPowerMs = w.pinPoweredForMs;
            }
            if (w.pinPoweredForMs < StartupSensorScanUseCase::PORT_WAKE_UP_MS)
            {
                early++;
                if (early == 1)
                {
                    snprintf(message, sizeof(message), " [%s: %s on port %d at %lu ms after %lu ms of power]",
                             layouts[l].name, BackgroundActs::ladderStepName(w.kind), w.pin,
                             (unsigned long)(w.startedAtMs - module.recordingStartupScan.startedAtMs()),
                             (unsigned long)w.pinPoweredForMs);
                    failures += message;
                }
            }
        }
        printf("  s18: %s - %u cheap question(s) and poll(s), %u to a port with less than %lu ms of "
               "power, the least power %lu ms; pass over at %lu ms; %u sweep read(s) and %u frame(s) "
               "in all before the wake-up, %lu port recovery action(s); rows%s, %u move(s) refused\n",
               layouts[l].name, questions, early, (unsigned long)StartupSensorScanUseCase::PORT_WAKE_UP_MS,
               (unsigned long)(leastPowerMs == (uint64_t)-1 ? 0 : leastPowerMs),
               (unsigned long)(finishedAtMs - module.recordingStartupScan.startedAtMs()),
               sweepReadsEarly, framesEarly, (unsigned long)recoveryActions, addresses.c_str(),
               (unsigned)refusedMoves);
    }
    const std::string verdict = "a port was asked before it had come up:" + failures;
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), verdict.c_str());
}

// When the sensors a restart finds reach the server, and how many structures the pass sends: a
// factory sensor on the first port and on the last, a sensor at an address of the module's range,
// nothing at all, the layouts with a quiet port or a window after a cheap find, a sensor of the range
// on the second port of a wire - alone and beside a stored one - and twins on a wire. A table of
// figures, printed; what it holds is that the pass sends a structure and that the last one
// describes every sensor it found.
void test_s18_when_the_sensor_a_restart_finds_reaches_the_server(void)
{
    const PluggedIn layouts[] = {
        {"a factory sensor on port 0", {0, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"a factory sensor on port 3", {3, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"a sensor at 0x70 on port 0", {-1, -1, -1, -1}, {0, -1, -1, -1}, {-1, -1, -1, -1}},
        {"nothing on any port", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory sensors on ports 0 and 3", {0, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"factory sensors on ports 1 and 3", {1, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"a sensor at 0x70 on port 0, a factory sensor on port 1", {1, -1, -1, -1}, {0, -1, -1, -1},
         {-1, -1, -1, -1}},
        {"a sensor at 0x71 on port 1", {-1, -1, -1, -1}, {1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"a stored sensor at 0x70 on port 0, a sensor at 0x71 on port 1", {-1, -1, -1, -1},
         {1, -1, -1, -1}, {0, -1, -1, -1}},
        {"twins on ports 0 and 1", {0, 1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"twins on ports 0 and 1, twins on ports 2 and 3", {0, 1, 2, 3}, {-1, -1, -1, -1},
         {-1, -1, -1, -1}},
    };

    std::string missing;
    char item[224];
    for (size_t l = 0; l < sizeof(layouts) / sizeof(layouts[0]); l++)
    {
        const PluggedIn& layout = layouts[l];
        ModuleUnderTest module;
        plugIn(module, layout);
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence);

        const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
        const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
        std::string perPort;
        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            if (!listed(layout.factory, pin) && !listed(layout.atThePortsAddress, pin))
            {
                continue;
            }
            const uint64_t shownAtMs = firstStructureDescribing(module.server, pin);
            snprintf(item, sizeof(item),
                     "; port %d adopted at %lu ms, first in a structure the server had at %lu ms, %u "
                     "address write(s) into it, %u window(s), %u identification step(s)",
                     pin, (unsigned long)(watch.adoptedAtMs[pin] > 0 ? watch.adoptedAtMs[pin] - startedAtMs : 0),
                     (unsigned long)(shownAtMs > 0 ? shownAtMs - startedAtMs : 0),
                     (unsigned)portOf(module, pin).writes.size(),
                     (unsigned)stepsOf(module, StartupScanStep::PROBE_PRESENCE, pin).size(),
                     (unsigned)stepsOf(module, StartupScanStep::SCAN_CANDIDATE, pin).size());
            perPort += item;
        }
        const uint64_t firstWindowMs = firstWindowOpenedAtMs(module);
        printf("  s18: %s - pass over at %lu ms of the pass (%lu ms after the boot), first window at %lu "
               "ms, %u structure(s) by its end, %u in all, %lu port recovery action(s)%s\n",
               layout.name, (unsigned long)(finishedAtMs - startedAtMs), (unsigned long)finishedAtMs,
               (unsigned long)(firstWindowMs > 0 ? firstWindowMs - startedAtMs : 0),
               (unsigned)structuresTakenBy(module.server, finishedAtMs),
               (unsigned)module.server.structures.size(),
               (unsigned long)module.acquisition.recoveryActionCount(), perPort.c_str());

        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            if (!listed(layout.factory, pin) && !listed(layout.atThePortsAddress, pin))
            {
                continue;
            }
            if (watch.adoptedAtMs[pin] == 0 || module.server.structures.empty() ||
                !describesTheSensorOf(module.server.structures.back().sensors, pin))
            {
                snprintf(item, sizeof(item), " [%s: port %d]", layout.name, pin);
                missing += item;
            }
        }
        if (module.server.structures.empty())
        {
            snprintf(item, sizeof(item), " [%s: no structure at all]", layout.name);
            missing += item;
        }
    }

    // Every layout is printed before any is judged, so that one that fails still shows the rest.
    char message[480];
    snprintf(message, sizeof(message),
             "a sensor the pass found was never adopted or is not described by the last structure:%s",
             missing.c_str());
    TEST_ASSERT_TRUE_MESSAGE(missing.empty(), message);
}

// --------------------------------------------------------------------------------------------
// 21. A port a step of the discovery or a command left dark is not read until it has come up, and
//     its darkness is not counted against the sensor on it.
//
// A model scan and an address write power their own port and take the power of every other one,
// and neither gives it back (SensorReader::scanModbusSensor(),
// ModbusSensorReader::isolatePortForAddressWrite()); a window holds the wire of its port dark for
// minutes. A sensor behind such a port answers nothing until it has had its power for
// PORT_WAKE_UP_MS again, and a read of it before that is a miss the port recovery counts towards a
// power cycle. So the sweep gives such a port its power, leaves it alone for that long, and the
// recovery leaves the time out. What the recovery still sees is what it is for: a sensor that is
// silent while its port could answer.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief When the port recovery - not a step of the discovery or a command - took this port's power. */
std::vector<uint64_t> recoveryPowerOffsOf(ModuleUnderTest& module, int pin)
{
    const SimRs485Bus& port = portOf(module, pin);
    std::vector<uint64_t> offs;
    for (size_t o = 0; o < port.powerOffAtUs.size(); o++)
    {
        const uint64_t atMs = port.powerOffAtUs[o] / 1000;
        bool ofAStep = false;
        for (size_t i = 0; i < module.busWindows.size() && !ofAStep; i++)
        {
            ofAStep = atMs >= module.busWindows[i].startedAtMs && atMs < module.busWindows[i].endedAtMs;
        }
        if (!ofAStep)
        {
            offs.push_back(atMs);
        }
    }
    return offs;
}

/**
 * @brief How long, from `fromMs` to `untilMs`, this port had had its power for PORT_WAKE_UP_MS or
 *        more - the time what is on it could have answered - read off the stand-in's own
 *        transitions: the port starts dark, as on the board.
 */
uint64_t timeThePortCouldAnswerMs(const SimRs485Bus& port, uint64_t fromMs, uint64_t untilMs)
{
    std::vector<std::pair<uint64_t, bool> > transitions;
    for (size_t i = 0; i < port.powerOnAtUs.size(); i++)
    {
        transitions.push_back(std::make_pair(port.powerOnAtUs[i], true));
    }
    for (size_t i = 0; i < port.powerOffAtUs.size(); i++)
    {
        transitions.push_back(std::make_pair(port.powerOffAtUs[i], false));
    }
    std::sort(transitions.begin(), transitions.end());

    const uint64_t fromUs = fromMs * 1000ULL;
    const uint64_t untilUs = untilMs * 1000ULL;
    uint64_t totalUs = 0;
    const auto add = [&](uint64_t aUs, uint64_t bUs) {
        aUs = aUs > fromUs ? aUs : fromUs;
        bUs = bUs < untilUs ? bUs : untilUs;
        totalUs += bUs > aUs ? bUs - aUs : 0;
    };
    const uint64_t wakeUpUs = StartupSensorScanUseCase::PORT_WAKE_UP_MS * 1000ULL;
    bool on = false;
    uint64_t upAtUs = 0;
    for (size_t i = 0; i < transitions.size(); i++)
    {
        if (transitions[i].second)
        {
            on = true;
            upAtUs = transitions[i].first;
            continue;
        }
        if (on)
        {
            add(upAtUs + wakeUpUs, transitions[i].first);
        }
        on = false;
    }
    if (on)
    {
        add(upAtUs + wakeUpUs, untilUs);
    }
    return totalUs / 1000ULL;
}

/**
 * @brief The first frame put into this port from `fromMs` on, after the port had had its power for
 *        PORT_WAKE_UP_MS - the first question a silent sensor on it fails; 0 if there was none.
 */
uint64_t firstFrameTheSensorCouldAnswerMs(const SimRs485Bus& port, uint64_t fromMs)
{
    for (size_t f = 0; f < port.sentAtUs.size() && f < port.sentAfterPowerForMs.size(); f++)
    {
        if (port.sentAtUs[f] / 1000 >= fromMs &&
            port.sentAfterPowerForMs[f] >= StartupSensorScanUseCase::PORT_WAKE_UP_MS)
        {
            return port.sentAtUs[f] / 1000;
        }
    }
    return 0;
}

/** @brief The board's way of sending the structure, power transitions recorded, booted, the loop running. */
void bootRecordingThePower(ModuleUnderTest& module, CadenceRecorder& cadence)
{
    module.sendTheStructureTheWayTheBoardDoes();
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        portOf(module, static_cast<int>(i)).recordPowerOffTimes(true);
        portOf(module, static_cast<int>(i)).recordPowerOnTimes(true);
    }
    module.boot();
    module.startArduinoLoopTask(cadence);
}

/** @brief Run in steps of 100 ms until the condition holds, or until the time is up. */
bool runUntil(ModuleUnderTest& module, uint64_t atMostMs, const std::function<bool()>& condition)
{
    for (uint64_t spent = 0; spent < atMostMs; spent += 100)
    {
        if (condition())
        {
            return true;
        }
        module.scheduler.runForMs(100);
    }
    return condition();
}

bool storedRowIsInError(ModuleUnderTest& module, int pin)
{
    Utils::Optional<SensorEntity> row = module.sensorRepository.get(uidOfPin(pin).c_str());
    return row && row.value().isError;
}

} // namespace

// (a) A sensor that stops answering with its port up is power-cycled - when it stops in the middle
//     of the restart's pass, and when it stops after it. Only the time its port could not answer is
//     left out of its silence, so the power cycle comes after a minute of silence it could have
//     broken - measured on the stand-in, from its own frames and power transitions, not by the
//     firmware: from the first question it failed with its port up for the wake-up, whatever of the
//     time after it the port was up for the wake-up too - and after the pass is over no later than
//     DISCONNECT_THRESHOLD_MS, two sweep intervals and a warm-up past the later of the silence and
//     the end of the pass.
void test_s18_a_sensor_silent_with_its_port_up_is_still_power_cycled(void)
{
    struct Silence {
        const char* name;
        bool fromTheEndOfThePass;
        uint64_t intoThePassMs;
    };
    const Silence silences[2] = {
        {"stored on 1, factory on 2, silent from 30 s into the pass", false, 30000},
        {"stored on 1, factory on 2, silent from the end of the pass", true, 0},
    };
    const PluggedIn layout = {"", {2, -1, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}};
    const uint64_t latestAfterBoth = SensorRecoveryUseCase::DISCONNECT_THRESHOLD_MS +
                                     2 * SensorAcquisitionService::SWEEP_INTERVAL_MS +
                                     StartupSensorScanUseCase::PORT_WAKE_UP_MS;

    struct Outcome {
        uint64_t silentFromMs = 0;
        uint64_t passEndedAtMs = 0;
        std::vector<uint64_t> offsOfTheSilent;
        std::vector<uint64_t> offsOfTheAdopted;
        /**
         * The first question the sensor failed with its port up, and of the time from it to the
         * recovery first taking the power, what the port could answer in.
         */
        uint64_t firstFailedMs = 0;
        uint64_t couldAnswerMs = 0;
    };
    Outcome outcomes[2];

    for (int s = 0; s < 2; s++)
    {
        ModuleUnderTest module;
        plugIn(module, layout);
        CadenceRecorder cadence;
        bootRecordingThePower(module, cadence);

        const bool silenced = runUntil(module, 1200000, [&]() {
            const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
            if (silences[s].fromTheEndOfThePass)
            {
                return module.recordingStartupScan.finishedAtMs() > 0;
            }
            return startedAtMs > 0 && module.clock.nowMs() >= startedAtMs + silences[s].intoThePassMs;
        });
        TEST_ASSERT_TRUE_MESSAGE(silenced, "the moment the sensor stops answering never came");
        portOf(module, 1).device(MODBUS_ADDRESS_MIN + 1)->silent = true;
        outcomes[s].silentFromMs = module.clock.nowMs();

        runUntil(module, 1200000, [&]() { return module.recordingStartupScan.finishedAtMs() > 0; });
        TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                                 "the pass the restart armed did not end in twenty minutes");
        outcomes[s].passEndedAtMs = module.recordingStartupScan.finishedAtMs();
        module.scheduler.runForMs(180000);
        cadence.closeAt(module.clock.nowMs());
        assertRunIsSound(module, cadence);

        outcomes[s].offsOfTheSilent = recoveryPowerOffsOf(module, 1);
        outcomes[s].offsOfTheAdopted = recoveryPowerOffsOf(module, 2);
        outcomes[s].firstFailedMs =
            firstFrameTheSensorCouldAnswerMs(module.port1, outcomes[s].silentFromMs);
        if (!outcomes[s].offsOfTheSilent.empty() && outcomes[s].firstFailedMs > 0)
        {
            outcomes[s].couldAnswerMs = timeThePortCouldAnswerMs(
                module.port1, outcomes[s].firstFailedMs, outcomes[s].offsOfTheSilent[0]);
        }
        std::string offs;
        char at[32];
        for (size_t i = 0; i < outcomes[s].offsOfTheSilent.size() && i < 6; i++)
        {
            snprintf(at, sizeof(at), " %lu", (unsigned long)outcomes[s].offsOfTheSilent[i]);
            offs += at;
        }
        printf("  s18: %s - silent from t=%lu ms, pass over at t=%lu ms; the port recovery took the "
               "silent sensor's power %u time(s), at t=%s ms, the first after %lu ms of silence its "
               "port could have broken since the first question it failed at t=%lu ms; the adopted "
               "sensor's %u time(s)\n",
               silences[s].name, (unsigned long)outcomes[s].silentFromMs,
               (unsigned long)outcomes[s].passEndedAtMs, (unsigned)outcomes[s].offsOfTheSilent.size(),
               offs.c_str(), (unsigned long)outcomes[s].couldAnswerMs,
               (unsigned long)outcomes[s].firstFailedMs, (unsigned)outcomes[s].offsOfTheAdopted.size());
    }

    char message[320];
    for (int s = 0; s < 2; s++)
    {
        const Outcome& outcome = outcomes[s];
        const uint64_t firstOffAfter = outcome.offsOfTheSilent.empty() ? 0 : outcome.offsOfTheSilent[0];
        const uint64_t latestMs =
            (outcome.silentFromMs > outcome.passEndedAtMs ? outcome.silentFromMs : outcome.passEndedAtMs) +
            latestAfterBoth;

        snprintf(message, sizeof(message),
                 "%s: the port recovery took the sensor's power at t=%lu ms, after %lu ms of silence "
                 "its port could have broken since t=%lu ms - less than %lu ms",
                 silences[s].name, (unsigned long)firstOffAfter, (unsigned long)outcome.couldAnswerMs,
                 (unsigned long)outcome.firstFailedMs,
                 (unsigned long)SensorRecoveryUseCase::DISCONNECT_THRESHOLD_MS);
        TEST_ASSERT_TRUE_MESSAGE(firstOffAfter == 0 ||
                                     outcome.couldAnswerMs >= SensorRecoveryUseCase::DISCONNECT_THRESHOLD_MS,
                                 message);

        snprintf(message, sizeof(message),
                 "%s: a sensor silent with its port up since t=%lu ms was not power-cycled by t=%lu ms "
                 "(first time: t=%lu ms)",
                 silences[s].name, (unsigned long)outcome.silentFromMs, (unsigned long)latestMs,
                 (unsigned long)firstOffAfter);
        TEST_ASSERT_TRUE_MESSAGE(firstOffAfter > 0 && firstOffAfter <= latestMs, message);

        snprintf(message, sizeof(message),
                 "%s: the port recovery took the power of the adopted sensor, which answers "
                 "throughout, %u time(s)",
                 silences[s].name, (unsigned)outcome.offsOfTheAdopted.size());
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)outcome.offsOfTheAdopted.size(), message);
    }
}

// (b) A sensor that had already missed reads when a step took its port's power, and answers again
//     by the time the port is back: the dark stretch is not added to its silence, and it is not
//     power-cycled the moment it is read again. The dark stretch of a window of the port sharing its
//     wire, of the confirming scan of a find on that port, and of the address write of a find on that
//     port - late in the pass, with the preparation after it giving the port its power back before
//     the sweep comes to it, so that the sweep never finds the port dark.
void test_s18_a_silence_that_began_before_the_dark_is_not_finished_by_it(void)
{
    struct Layout21 {
        const char* name;
        PluggedIn plugged;
        int stored;
        /** When the sensor stops answering, and the moment before which a power-off is not its dark. */
        uint64_t silentFromMs;
        uint64_t darkNotBeforeMs;
    };
    // Silent from 40 s, before the pass: the sweep misses the sensor at least once before the pass
    // takes its port's power, and the pass starts at BOOT_SETTLE_MS. Or silent late in the pass, less
    // than DISCONNECT_THRESHOLD_MS before the address write of the factory sensor on port 0 takes its
    // port down - some 260 s after the boot on this stand. The confirming scan of that sensor's find,
    // inside the first 90 s, is the only step before it that takes the port's power.
    const Layout21 layouts[4] = {
        {"stored on 0, the window of port 1 on its wire",
         {"", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, -1, -1, -1}}, 0, 40000, 0},
        {"stored on 1, the confirming scan of a factory sensor on 0",
         {"", {0, -1, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}}, 1, 40000, 0},
        {"stored on 1 silent from 215 s, the address write of a factory sensor on 0",
         {"", {0, -1, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}}, 1, 215000, 250000},
        {"stored on 1 silent from 230 s, the address write of a factory sensor on 0",
         {"", {0, -1, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}}, 1, 230000, 250000},
    };

    struct Outcome {
        bool missedBeforeTheDark = false;
        uint64_t darkAtMs = 0;
        uint64_t readAgainAtMs = 0;
        uint64_t recoveryActions = 0;
        std::vector<uint64_t> offs;
        unsigned readsBeforeTheWakeUp = 0;
    };
    Outcome outcomes[4];

    for (int l = 0; l < 4; l++)
    {
        const Layout21& layout = layouts[l];
        Outcome& outcome = outcomes[l];
        ModuleUnderTest module;
        plugIn(module, layout.plugged);
        CadenceRecorder cadence;
        bootRecordingThePower(module, cadence);
        SimRs485Bus& port = portOf(module, layout.stored);
        const std::string uid = uidOfPin(layout.stored);

        runUntil(module, layout.silentFromMs, [&]() { return module.clock.nowMs() >= layout.silentFromMs; });
        port.device((uint8_t)(MODBUS_ADDRESS_MIN + layout.stored))->silent = true;

        // The first step after the pass began, and not before the layout's moment, that takes the
        // stored sensor's port down.
        const bool dark = runUntil(module, 1200000, [&]() {
            return module.recordingStartupScan.startedAtMs() > 0 &&
                   module.clock.nowMs() >= layout.darkNotBeforeMs && !port.isPowered();
        });
        TEST_ASSERT_TRUE_MESSAGE(dark, "no step of the pass took the stored sensor's port down");
        outcome.darkAtMs = module.clock.nowMs();
        outcome.missedBeforeTheDark = storedRowIsInError(module, layout.stored);
        port.device((uint8_t)(MODBUS_ADDRESS_MIN + layout.stored))->silent = false;

        const uint32_t lastReadBeforeMs = module.sensorValueModel.getLastReadTime(uid);
        runUntil(module, 1200000, [&]() { return module.recordingStartupScan.finishedAtMs() > 0; });
        TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                                 "the pass the restart armed did not end in twenty minutes");
        runUntil(module, 120000, []() { return false; });
        cadence.closeAt(module.clock.nowMs());
        assertRunIsSound(module, cadence);

        const uint32_t lastReadMs = module.sensorValueModel.getLastReadTime(uid);
        outcome.readAgainAtMs = lastReadMs != lastReadBeforeMs ? lastReadMs : 0;
        outcome.recoveryActions = module.acquisition.recoveryActionCount();
        outcome.offs = recoveryPowerOffsOf(module, layout.stored);
        outcome.readsBeforeTheWakeUp =
            sweepReadsBeforeTheWakeUp(module, layout.stored, module.recordingStartupScan.startedAtMs());
        printf("  s18: %s - silent from t=%lu ms to t=%lu ms, when a step took its port down (%s "
               "before it); last read at t=%lu ms; %lu port recovery action(s), the power taken by "
               "the recovery %u time(s), first at t=%lu ms; %u sweep read(s) before the wake-up\n",
               layout.name, (unsigned long)layout.silentFromMs, (unsigned long)outcome.darkAtMs,
               outcome.missedBeforeTheDark ? "missed" : "NOT missed",
               (unsigned long)outcome.readAgainAtMs, (unsigned long)outcome.recoveryActions,
               (unsigned)outcome.offs.size(), (unsigned long)(outcome.offs.empty() ? 0 : outcome.offs[0]),
               outcome.readsBeforeTheWakeUp);
    }

    char message[288];
    for (int l = 0; l < 4; l++)
    {
        const Outcome& outcome = outcomes[l];
        snprintf(message, sizeof(message),
                 "%s: the sensor had not missed a read by the time its port was taken - there is "
                 "no silence before the dark to test",
                 layouts[l].name);
        TEST_ASSERT_TRUE_MESSAGE(outcome.missedBeforeTheDark, message);
        snprintf(message, sizeof(message), "%s: the sensor was never read again after the dark",
                 layouts[l].name);
        TEST_ASSERT_TRUE_MESSAGE(outcome.readAgainAtMs > outcome.darkAtMs, message);
        snprintf(message, sizeof(message),
                 "%s: the port recovery acted %lu time(s) on a sensor that answers from the moment "
                 "its port is back",
                 layouts[l].name, (unsigned long)outcome.recoveryActions);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, outcome.recoveryActions, message);
    }
}

// (c) A ScanSensors command a person sends to a free port, on a module with sensors on both wires:
//     the command's model scans take the power of the port sharing the wire of the port it names and
//     leave it so, and the pass the discovery adds behind it polls that wire. The stored sensor there
//     is not read until its port has come up again, and the port recovery does not act.
void test_s18_a_scan_command_leaves_the_stored_sensors_nothing_to_recover_from(void)
{
    const PluggedIn layout = {"", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, 2, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    CadenceRecorder cadence;
    bootRecordingThePower(module, cadence);
    runUntil(module, 1200000, [&]() { return module.recordingStartupScan.finishedAtMs() > 0; });
    TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                             "the pass the restart armed did not end in twenty minutes");
    module.scheduler.runForMs(60000);

    const uint64_t commandAtMs = module.clock.nowMs();
    const uint64_t recoveryBefore = module.acquisition.recoveryActionCount();
    const int offs0Before = module.port0.powerOffTransitions;
    const int offs2Before = module.port2.powerOffTransitions;
    // Pin 3 in the command is port 1 of the board (ModuleCommandExecutionUseCase::convertPinFromJson()).
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-free-1", "ScanSensors", "{\"pin\":3}"));
    module.mqttManager.deliver(scanSensorsSignalJson());
    module.scheduler.runForMs(300000);
    cadence.closeAt(module.clock.nowMs());
    assertRunIsSound(module, cadence);

    bool commandRan = false;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        commandRan = commandRan ||
                     (!module.busWindows[i].discovery && module.busWindows[i].startedAtMs >= commandAtMs);
    }
    const uint64_t recoveryActions = module.acquisition.recoveryActionCount() - recoveryBefore;
    const unsigned early0 = sweepReadsBeforeTheWakeUp(module, 0, commandAtMs);
    const unsigned early2 = sweepReadsBeforeTheWakeUp(module, 2, commandAtMs);
    const Utils::Optional<SensorEntity> row0 = module.sensorRepository.get(uidOfPin(0).c_str());
    const Utils::Optional<SensorEntity> row2 = module.sensorRepository.get(uidOfPin(2).c_str());

    printf("  s18: a ScanSensors on port 1 with sensors stored on 0 and 2 - the stored ports lost "
           "their power %d / %d time(s) to the command and the pass behind it; %u / %u sweep read(s) "
           "before the wake-up; %lu port recovery action(s); in error at the end %d / %d\n",
           module.port0.powerOffTransitions - offs0Before, module.port2.powerOffTransitions - offs2Before,
           early0, early2, (unsigned long)recoveryActions, row0 && row0.value().isError ? 1 : 0,
           row2 && row2.value().isError ? 1 : 0);

    TEST_ASSERT_TRUE_MESSAGE(commandRan, "the ScanSensors command never reached the line");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.powerOffTransitions > offs0Before,
                             "the command did not take the power of the stored sensor on its wire - there "
                             "is no dark to test");
    char message[256];
    snprintf(message, sizeof(message),
             "the sweep read the stored sensors %u / %u time(s) before their ports had come up again "
             "after the command",
             early0, early2);
    TEST_ASSERT_TRUE_MESSAGE(early0 + early2 == 0, message);
    snprintf(message, sizeof(message),
             "the port recovery acted %lu time(s) after a ScanSensors command on sensors that answer "
             "throughout",
             (unsigned long)recoveryActions);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, recoveryActions, message);
}

// (d) A raw read a person sends to a stored sensor's port that a ScanSensors command has just left
//     dark. The frame waits for the port to come up, whoever raised it, and the device answers it;
//     and the sweep, which comes to the port after the raw read has raised it, does not read it
//     before the wake-up either. No frame from anybody into that port before its wake-up, and no
//     port recovery.
void test_s18_a_raw_read_into_a_port_a_command_left_dark_waits_for_it_to_come_up(void)
{
    const PluggedIn layout = {"", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, 2, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    CadenceRecorder cadence;
    bootRecordingThePower(module, cadence);
    runUntil(module, 1200000, [&]() { return module.recordingStartupScan.finishedAtMs() > 0; });
    TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                             "the pass the restart armed did not end in twenty minutes");
    module.scheduler.runForMs(60000);

    const uint64_t commandAtMs = module.clock.nowMs();
    const uint64_t recoveryBefore = module.acquisition.recoveryActionCount();
    // Pin 3 in the command is port 1 of the board (ModuleCommandExecutionUseCase::convertPinFromJson()).
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-free-1", "ScanSensors", "{\"pin\":3}"));
    module.mqttManager.deliver(scanSensorsSignalJson());
    const bool dark = runUntil(module, 120000, [&]() { return !module.port0.isPowered(); });
    TEST_ASSERT_TRUE_MESSAGE(dark, "the command did not take port 0's power - there is no dark to test");
    const uint64_t darkAtMs = module.clock.nowMs();

    module.mqttManager.deliver(rawReadOfRegisterZeroAt0x70Json());
    runUntilTheRawReadIsReported(module);
    module.scheduler.runForMs(180000);
    cadence.closeAt(module.clock.nowMs());
    assertRunIsSound(module, cadence);

    const std::vector<uint8_t> raw = rawReadOfRegisterZeroAt0x70Frame();
    uint64_t rawSentAtMs = 0;
    uint64_t rawSentAfterPowerForMs = 0;
    for (size_t f = 0; f < module.port0.sentFrames.size() && f < module.port0.sentAtUs.size(); f++)
    {
        if (module.port0.sentAtUs[f] / 1000 >= darkAtMs && module.port0.sentFrames[f] == raw)
        {
            rawSentAtMs = module.port0.sentAtUs[f] / 1000;
            rawSentAfterPowerForMs = module.port0.sentAfterPowerForMs[f];
            break;
        }
    }
    std::string firstEarly;
    const unsigned framesEarly = framesBeforeTheWakeUp(module, 0, commandAtMs, firstEarly);
    const unsigned sweepEarly = sweepReadsBeforeTheWakeUp(module, 0, commandAtMs);
    const uint64_t recoveryActions = module.acquisition.recoveryActionCount() - recoveryBefore;
    const bool reported = !module.server.rawIoReports.empty();

    printf("  s18: a raw read into port 0 after a ScanSensors left it dark at t=%lu ms - on the wire at "
           "t=%lu ms after %lu ms of power, reported %s; %u frame(s) into port 0 before its wake-up "
           "(%u of the sweep)%s%s; %lu port recovery action(s)\n",
           (unsigned long)darkAtMs, (unsigned long)rawSentAtMs, (unsigned long)rawSentAfterPowerForMs,
           !reported ? "nothing"
                     : (module.server.rawIoReports[0].isError ? module.server.rawIoReports[0].errorMessage.c_str()
                                                              : module.server.rawIoReports[0].dataHex.c_str()),
           framesEarly, sweepEarly, framesEarly > 0 ? ", the first: " : "", firstEarly.c_str(),
           (unsigned long)recoveryActions);

    TEST_ASSERT_TRUE_MESSAGE(rawSentAtMs > 0, "the raw read never reached port 0's wire");
    TEST_ASSERT_TRUE_MESSAGE(reported, "the raw read was never answered to the server");
    TEST_ASSERT_FALSE_MESSAGE(module.server.rawIoReports[0].isError,
                              "the raw read was answered with an error - it went into a port whose "
                              "device had not come up");
    char message[256];
    snprintf(message, sizeof(message),
             "%u frame(s) went into port 0 before it had come up after the command (%u of the sweep): %s",
             framesEarly, sweepEarly, firstEarly.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, framesEarly, message);
    snprintf(message, sizeof(message),
             "the port recovery acted %lu time(s) on sensors that answer throughout",
             (unsigned long)recoveryActions);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, recoveryActions, message);
}

// (e) The same as (b), with the power given back by the stored port's own model scan: a sensor that
//     has missed reads, a ScanSensors on the free port of its wire that takes its port down - from
//     then on the sensor answers again - and a PingPin on the stored port straight after, whose scan
//     switches the port on and waits for it itself. The sweep never finds that port dark; the stretch
//     it could not answer in is left out all the same, and the sensor is not power-cycled.
void test_s18_a_ping_that_gives_a_silent_sensors_port_back_does_not_finish_its_silence(void)
{
    const PluggedIn layout = {"", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, 2, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    CadenceRecorder cadence;
    bootRecordingThePower(module, cadence);
    runUntil(module, 1200000, [&]() { return module.recordingStartupScan.finishedAtMs() > 0; });
    TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                             "the pass the restart armed did not end in twenty minutes");
    module.scheduler.runForMs(60000);

    SimRs485Bus::Device* sensor = module.port0.device(MODBUS_ADDRESS_MIN);
    TEST_ASSERT_NOT_NULL(sensor);
    const uint64_t recoveryBefore = module.acquisition.recoveryActionCount();
    const uint64_t silentFromMs = module.clock.nowMs();
    sensor->silent = true;
    // Long enough for the sweep to have missed it, short of the recovery's minute.
    module.scheduler.runForMs(40000);
    const bool missed = storedRowIsInError(module, 0);

    // Pin 3 in the command is port 1 of the board, pin 4 is port 0
    // (ModuleCommandExecutionUseCase::convertPinFromJson()).
    module.server.pendingModuleCommands.push_back(
        makeServerCommand("scan-free-1", "ScanSensors", "{\"pin\":3}"));
    module.server.pendingModuleCommands.push_back(makeServerCommand("ping-0", "PingPin", "{\"pin\":4}"));
    module.mqttManager.deliver(scanSensorsSignalJson());
    const bool dark = runUntil(module, 120000, [&]() { return !module.port0.isPowered(); });
    const uint64_t darkAtMs = module.clock.nowMs();
    sensor->silent = false;

    module.scheduler.runForMs(300000);
    cadence.closeAt(module.clock.nowMs());
    assertRunIsSound(module, cadence);

    uint64_t pingAtMs = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (!w.discovery && !w.raw && w.startedAtMs >= darkAtMs)
        {
            pingAtMs = w.startedAtMs;
        }
    }
    const uint64_t recoveryActions = module.acquisition.recoveryActionCount() - recoveryBefore;
    const unsigned sweepEarly = sweepReadsBeforeTheWakeUp(module, 0, silentFromMs);
    const std::vector<uint64_t> offs = recoveryPowerOffsOf(module, 0);
    printf("  s18: stored on 0 silent from t=%lu ms (%s by t=%lu ms), its port taken down by a "
           "ScanSensors on port 1 at t=%lu ms and answering from then on, the last command on the "
           "line from t=%lu ms; %lu port recovery action(s), the first power-off by the recovery at "
           "t=%lu ms; %u sweep read(s) before the wake-up\n",
           (unsigned long)silentFromMs, missed ? "missed" : "NOT missed",
           (unsigned long)(silentFromMs + 40000), (unsigned long)darkAtMs, (unsigned long)pingAtMs,
           (unsigned long)recoveryActions, (unsigned long)(offs.empty() ? 0 : offs[0]), sweepEarly);

    TEST_ASSERT_TRUE_MESSAGE(missed, "the sensor had not missed a read before its port was taken - "
                                     "there is no silence before the dark to test");
    TEST_ASSERT_TRUE_MESSAGE(dark, "the command did not take port 0's power - there is no dark to test");
    TEST_ASSERT_TRUE_MESSAGE(pingAtMs > darkAtMs, "the PingPin never reached the line");
    char message[256];
    snprintf(message, sizeof(message),
             "the port recovery acted %lu time(s) on a sensor that answers from the moment its port "
             "is back, given back by its own ping",
             (unsigned long)recoveryActions);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, recoveryActions, message);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, sweepEarly, "the sweep read port 0 before it had come up");
}

// --------------------------------------------------------------------------------------------
// 22. The telemetry carries no record of a sensor the module has not read.
//
// The sweep reads on its own task, and the iteration of the control loop that hands it the
// sensors builds the sensor update of the same tick - on a boot with a stored sensor, the boot's
// first update, and after the discovery adopts a sensor, the first update that has it in the
// registry. Neither can carry a reading of that sensor yet, so neither may carry a record of it:
// not one under the sensor's own uid, which the structure does not declare (it declares the
// channels), and not a channel "online" with a value nothing measured. The sensor enters the
// telemetry with the reading that gives it its values.
//
// The order of events is the logged one: the boot's
// first update is built first, the first reading is applied next, and the next update
// is the first that can carry the channels.
// --------------------------------------------------------------------------------------------

namespace {

bool isARecordOf(const std::string& recordUid, const std::string& rowUid)
{
    return recordUid == rowUid ||
           (recordUid.size() > rowUid.size() && recordUid.compare(0, rowUid.size(), rowUid) == 0 &&
            recordUid[rowUid.size()] == '.');
}

std::string describeUpdate(const SimServer::Update& update)
{
    if (update.sensors.empty())
    {
        return "[]";
    }
    std::string text = "[";
    for (size_t i = 0; i < update.sensors.size(); i++)
    {
        char item[128];
        snprintf(item, sizeof(item), "%s%s=%.2f/%s", i == 0 ? "" : " ",
                 update.sensors[i].uid.c_str(), update.sensors[i].value,
                 update.sensors[i].state.c_str());
        text += item;
    }
    return text + "]";
}

/** @brief Run until the server has taken the given number of sensor updates, or a minute passed. */
void runUntilTheServerHasUpdates(ModuleUnderTest& module, size_t updates)
{
    const uint64_t untilMs = module.clock.nowMs() + 60000;
    while (module.server.updates.size() < updates && module.clock.nowMs() < untilMs)
    {
        module.scheduler.runForMs(100);
    }
}

/**
 * @brief No update carries a record of the row before its reading: no record under the row's own
 *        uid, every channel online carries the value the module read for it (the twins hold their
 *        registers, so the last reading is every reading), and the first update with any record of
 *        the row carries every channel of its register configs online.
 * @return index of the first update that carried the row
 */
size_t assertTheRowIsReportedOnlyWithItsReading(ModuleUnderTest& module, const std::string& rowUid,
                                                const char* stand)
{
    char message[400];
    Utils::Optional<SensorEntity> row = module.sensorRepository.get(rowUid.c_str());
    snprintf(message, sizeof(message), "%s: the registry has no row %s", stand, rowUid.c_str());
    TEST_ASSERT_TRUE_MESSAGE((bool)row, message);
    snprintf(message, sizeof(message), "%s: the module never read %s", stand, rowUid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.sensorValueModel.hasChannelValues(rowUid), message);

    size_t firstCarrying = module.server.updates.size();
    for (size_t u = 0; u < module.server.updates.size(); u++)
    {
        const SimServer::Update& update = module.server.updates[u];
        const std::string text = describeUpdate(update);
        for (size_t i = 0; i < update.sensors.size(); i++)
        {
            const SensorDto& record = update.sensors[i];
            if (!isARecordOf(record.uid, rowUid))
            {
                continue;
            }
            if (firstCarrying == module.server.updates.size())
            {
                firstCarrying = u;
            }
            snprintf(message, sizeof(message),
                     "%s: update %u (t=%lu ms) carries a record under the sensor's own uid, which "
                     "the structure does not declare: %s",
                     stand, (unsigned)u, (unsigned long)update.atMs, text.c_str());
            TEST_ASSERT_TRUE_MESSAGE(record.uid != rowUid, message);
            if (record.state != "online")
            {
                continue;
            }
            const float read = module.sensorValueModel.getChannelValue(rowUid, record.uid);
            snprintf(message, sizeof(message),
                     "%s: update %u (t=%lu ms) says %s is online with %.2f, the module read %.2f: %s",
                     stand, (unsigned)u, (unsigned long)update.atMs, record.uid.c_str(),
                     record.value, read, text.c_str());
            TEST_ASSERT_TRUE_MESSAGE(record.value > read - 0.05f && record.value < read + 0.05f,
                                     message);
        }
    }

    snprintf(message, sizeof(message), "%s: no update ever carried the sensor (%u updates)", stand,
             (unsigned)module.server.updates.size());
    TEST_ASSERT_TRUE_MESSAGE(firstCarrying < module.server.updates.size(), message);
    const SimServer::Update& first = module.server.updates[firstCarrying];
    size_t channelsOnline = 0;
    for (size_t i = 0; i < first.sensors.size(); i++)
    {
        if (isARecordOf(first.sensors[i].uid, rowUid) && first.sensors[i].state == "online")
        {
            channelsOnline++;
        }
    }
    snprintf(message, sizeof(message),
             "%s: the first update with the sensor (t=%lu ms) is not its reading: %s", stand,
             (unsigned long)first.atMs, describeUpdate(first).c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE((unsigned)row.value().modbusRegisterConfigs.size(),
                                   (unsigned)channelsOnline, message);
    return firstCarrying;
}

} // namespace

// A stored RY-WS301 on port 0, a boot - instant, and with the logged boot timing as main.cpp runs
// it - and the first sensor updates the server takes: the boot's first carries nothing of the
// sensor, the next its two channels with the values the module read.
void test_s18_the_first_telemetry_of_a_boot_carries_no_record_the_module_has_not_read(void)
{
    const bool timings[2] = {false, true};
    for (int t = 0; t < 2; t++)
    {
        const char* stand = timings[t] ? "the logged boot" : "an instant boot";
        ModuleUnderTest module;
        eraseTheRegistry(module, theServersCatalog());
        module.sensorRepository.save(makeStoredRyWs301(0));
        module.port0.addDevice(makeAwakeRyWs301(MODBUS_ADDRESS_MIN));

        if (timings[t])
        {
            module.clock.advanceMs(LOGGED_SENSOR_BUS_AT_MS);
            module.setupService.setupModule();
        }
        else
        {
            module.boot();
        }
        CadenceRecorder cadence;
        module.startArduinoLoopTask(cadence);
        runUntilTheServerHasUpdates(module, 3);

        printf("  s18: %s - sensor updates taken by the server:", stand);
        for (size_t u = 0; u < module.server.updates.size(); u++)
        {
            printf(" t=%lu %s", (unsigned long)module.server.updates[u].atMs,
                   describeUpdate(module.server.updates[u]).c_str());
        }
        printf("\n");

        char message[320];
        snprintf(message, sizeof(message), "%s: the server took %u sensor update(s) in a minute", stand,
                 (unsigned)module.server.updates.size());
        TEST_ASSERT_TRUE_MESSAGE(module.server.updates.size() >= 3, message);

        const SimServer::Update& first = module.server.updates[0];
        snprintf(message, sizeof(message),
                 "%s: the boot's first update (t=%lu ms) carries a record the module had not read: %s",
                 stand, (unsigned long)first.atMs, describeUpdate(first).c_str());
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)first.sensors.size(), message);

        const size_t firstCarrying =
            assertTheRowIsReportedOnlyWithItsReading(module, uidOfPin(0), stand);
        snprintf(message, sizeof(message),
                 "%s: the sensor first went out in update %u, not in the one after the boot's first",
                 stand, (unsigned)firstCarrying);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)firstCarrying, message);
    }
}

// A fresh module: no row, the server's catalog, the logged RY-WS301 on port 0. The restart's pass
// adopts it while the module is already reporting; no update carries it before its first reading,
// and the updates after that carry its channels.
void test_s18_a_sensor_the_discovery_adopts_is_not_reported_before_its_first_reading(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.port0.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN));

    CadenceRecorder cadence;
    bootAndRunTheRestartsPass(module, cadence);
    // The pass ends with the adoption; a minute more lets the sweep read the sensor and report it.
    module.scheduler.runForMs(60000);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)module.startupScan.adoptedCount(),
                                   "the premise: the restart's pass adopted the sensor on port 0");

    size_t emptyBefore = 0;
    const size_t firstCarrying =
        assertTheRowIsReportedOnlyWithItsReading(module, uidOfPin(0), "a fresh module");
    for (size_t u = 0; u < firstCarrying; u++)
    {
        if (module.server.updates[u].sensors.empty())
        {
            emptyBefore++;
        }
    }
    printf("  s18: a fresh module - %u sensor update(s) before the adopted sensor first went out, %u of "
           "them empty; the first with it at t=%lu ms: %s; the one before it: %s\n",
           (unsigned)firstCarrying, (unsigned)emptyBefore,
           (unsigned long)module.server.updates[firstCarrying].atMs,
           describeUpdate(module.server.updates[firstCarrying]).c_str(),
           firstCarrying > 0 ? describeUpdate(module.server.updates[firstCarrying - 1]).c_str() : "-");
}

// --------------------------------------------------------------------------------------------
// 23. The restart's cheap rung asks the module's range too, and a port sharing its wire with a find
//     is asked the cheap rung again.
//
// After a restart a free port most often has on it a device the module once gave an address: a
// sensor moved from another port, or one whose row went with the registry - a module or a sensor
// deleted in the app. It answers at that address and nowhere else, so the cheap rung reads the range
// on the live wire before the factory addresses, with the same read, the same confirming scan and the
// same adoption; an address a sensor of the paired port holds is left to the window, where that port
// is dark, when that sensor answered the pass - and asked like any other when its ping heard nothing.
// And a find made by the rest of the ladder moves the device it found off the wire as it stood, so the
// port sharing that wire is asked the cheap rung again before its own window.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief When every free port had been asked the cheap rung: the first window, or the pass's end. */
uint64_t cheapRungOverAtMs(const ModuleUnderTest& module)
{
    const uint64_t firstWindowMs = firstWindowOpenedAtMs(module);
    return firstWindowMs > 0 ? firstWindowMs : module.recordingStartupScan.finishedAtMs();
}

/** @brief Steps of one kind the restart's pass put on the line for one port. */
unsigned stepsOfThePass(const ModuleUnderTest& module, StartupScanStep::Kind kind, int pin)
{
    const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
    unsigned count = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == kind && w.pin == pin && w.startedAtMs <= finishedAtMs)
        {
            count++;
        }
    }
    return count;
}

/** @brief Cheap probes of the restart's pass on one port at one address, and how many were heard. */
unsigned cheapProbesAt(const ModuleUnderTest& module, int pin, int address, unsigned* heard = nullptr)
{
    const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
    unsigned count = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (!w.discovery || w.kind != StartupScanStep::PROBE_CANDIDATE || w.pin != pin ||
            w.address != address || w.startedAtMs > finishedAtMs)
        {
            continue;
        }
        count++;
        if (heard != nullptr && w.heardBeforeTheWireWasIsolated)
        {
            (*heard)++;
        }
    }
    return count;
}

/** @brief Cheap probes of the restart's pass, on any port, heard and not confirmed. */
unsigned heardOnTheLiveWire(const ModuleUnderTest& module)
{
    const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
    unsigned heard = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.heardBeforeTheWireWasIsolated && w.startedAtMs <= finishedAtMs)
        {
            heard++;
        }
    }
    return heard;
}

/** @brief The address a port's row holds after the pass, or 0 without a row. */
unsigned rowAddressOf(ModuleUnderTest& module, int pin)
{
    Utils::Optional<SensorEntity> row = module.sensorRepository.get(uidOfPin(pin).c_str());
    return row ? (unsigned)row.value().modbusDeviceAddress : 0;
}

/** @brief Milliseconds into the pass, or 0 for a moment that never came. */
unsigned long intoThePass(const ModuleUnderTest& module, uint64_t atMs)
{
    const uint64_t startedAtMs = module.recordingStartupScan.startedAtMs();
    return (unsigned long)(atMs > startedAtMs ? atMs - startedAtMs : 0);
}

} // namespace

// (a) The logged case: an RY-WS301 at 0x70 on port 0 of a module whose registry has gone, the
//     other ports empty. The cheap rung finds it at the address it answers at - before any window
//     opens, with no address written into its port - and the server has a structure with it while
//     the pass is still walking the ports that have nothing on them.
void test_s18_a_sensor_at_an_address_of_the_range_is_found_by_the_cheap_rung(void)
{
    const PluggedIn layout = {"at 0x70 on 0", {-1, -1, -1, -1}, {0, -1, -1, -1}, {-1, -1, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    const uint64_t firstWindowMs = firstWindowOpenedAtMs(module);
    const uint64_t shownAtMs = firstStructureDescribing(module.server, 0);
    const uint64_t finishedAtMs = module.recordingStartupScan.finishedAtMs();
    printf("  s18: %s - adopted at %lu ms (first window at %lu ms), in a structure the server had at "
           "%lu ms, %u write(s) into port 0, %u window(s) on it, at 0x%02X; pass over at %lu ms\n",
           layout.name, intoThePass(module, watch.adoptedAtMs[0]), intoThePass(module, firstWindowMs),
           intoThePass(module, shownAtMs), (unsigned)module.port0.writes.size(),
           stepsOfThePass(module, StartupScanStep::PROBE_PRESENCE, 0), rowAddressOf(module, 0),
           intoThePass(module, finishedAtMs));

    char message[256];
    snprintf(message, sizeof(message),
             "the sensor at 0x70 on port 0 was adopted at %lu ms into the pass, the first window "
             "opened at %lu ms - it waited for the windows",
             intoThePass(module, watch.adoptedAtMs[0]), intoThePass(module, firstWindowMs));
    TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[0] > 0 && firstWindowMs > 0 &&
                                 watch.adoptedAtMs[0] < firstWindowMs,
                             message);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.port0.writes.size(),
                                   "an address was written into port 0 although its device answers a "
                                   "read of the range");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, stepsOfThePass(module, StartupScanStep::PROBE_PRESENCE, 0),
                                   "the port the cheap rung named was given a window all the same");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, rowAddressOf(module, 0),
                                   "the sensor was not adopted at the address it answers at");
    TEST_ASSERT_TRUE_MESSAGE(shownAtMs > 0 && shownAtMs < finishedAtMs,
                             "no structure with the sensor reached the server before the pass was over");
}

// (b) The same sensor and a factory sensor on the port sharing its wire. Port 0 is asked the range
//     first and its sensor is adopted there; port 1 is not asked 0x70 on the live wire any more, and
//     its sensor is adopted at its factory address. Both before any window, and nothing is written
//     into port 0.
void test_s18_a_sensor_of_the_range_and_a_factory_sensor_on_one_wire_are_both_found_by_the_cheap_rung(void)
{
    const PluggedIn layout = {"at 0x70 on 0, factory on 1", {1, -1, -1, -1}, {0, -1, -1, -1},
                              {-1, -1, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    const uint64_t firstWindowMs = firstWindowOpenedAtMs(module);
    printf("  s18: %s - port 0 adopted at %lu ms, port 1 at %lu ms (first window at %lu ms); %u / %u "
           "write(s) into ports 0 / 1; at 0x%02X / 0x%02X; pass over at %lu ms\n",
           layout.name, intoThePass(module, watch.adoptedAtMs[0]), intoThePass(module, watch.adoptedAtMs[1]),
           intoThePass(module, firstWindowMs), (unsigned)module.port0.writes.size(),
           (unsigned)module.port1.writes.size(), rowAddressOf(module, 0), rowAddressOf(module, 1),
           intoThePass(module, module.recordingStartupScan.finishedAtMs()));

    char message[256];
    for (int pin = 0; pin < 2; pin++)
    {
        snprintf(message, sizeof(message),
                 "port %d was adopted at %lu ms into the pass, the first window opened at %lu ms",
                 pin, intoThePass(module, watch.adoptedAtMs[pin]), intoThePass(module, firstWindowMs));
        TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[pin] > 0 && firstWindowMs > 0 &&
                                     watch.adoptedAtMs[pin] < firstWindowMs,
                                 message);
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.port0.writes.size(),
                                   "an address was written into port 0 although its device answers a "
                                   "read of the range");
    TEST_ASSERT_TRUE_MESSAGE(module.port1.writes.size() <= 1,
                             "more than the one write that moves port 1's sensor into the range");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, rowAddressOf(module, 0), "port 0's row is not at 0x70");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 1, rowAddressOf(module, 1),
                                   "port 1's row is not at its own port's address");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, cheapProbesAt(module, 1, MODBUS_ADDRESS_MIN),
                                   "port 1 was asked, on the live wire, the address port 0's sensor was "
                                   "adopted at in this pass");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, heardOnTheLiveWire(module),
                                   "a confirming scan found a free port silent - an echo of a sensor this "
                                   "pass had adopted");
}

// (c) A stored sensor at 0x70 on port 0 and a device at 0x71 on port 1, one wire. The stored sensor
//     answers its ping, so port 1 is not asked 0x70 on the live wire - the stored sensor would answer
//     it and the confirming scan would take the power of every port to prove as much - and its own
//     device is found by the cheap rung.
//     Ports 2 and 3 are on the other wire, which the stored sensor cannot answer, and are asked 0x70
//     like any other address. What the pass takes of the stored sensor is one step: the confirming
//     scan of port 1's find.
const int STORED_POWER_OFFS_WHEN_ITS_NEIGHBOUR_IS_FOUND = 1;

void test_s18_an_address_the_sensor_of_the_paired_port_holds_is_not_asked_on_the_live_wire(void)
{
    const PluggedIn layout = {"stored at 0x70 on 0, at 0x71 on 1", {-1, -1, -1, -1}, {1, -1, -1, -1},
                              {0, -1, -1, -1}};
    ModuleUnderTest module;
    std::vector<SweepOfOneSensor> sensors;
    plugIn(module, layout, &sensors);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
    runWatching(module, watch, 120000, &sensors);

    const unsigned heldAskedOnPort1 = cheapProbesAt(module, 1, MODBUS_ADDRESS_MIN);
    const int storedOffsByThePass = powerOffsByTheDiscovery(module, 0);
    printf("  s18: %s - 0x70 asked on port 1's live wire %u time(s); port 1 adopted at %lu ms (first "
           "window at %lu ms), at 0x%02X, %u write(s) into it; the stored sensor lost its power %d "
           "time(s), %d of them to a step of the discovery; %lu port recovery action(s); its longest gap "
           "between readings %lu ms, longest unread while awake %lu ms; pass over at %lu ms\n",
           layout.name, heldAskedOnPort1, intoThePass(module, watch.adoptedAtMs[1]),
           intoThePass(module, firstWindowOpenedAtMs(module)), rowAddressOf(module, 1),
           (unsigned)module.port1.writes.size(), module.port0.powerOffTransitions, storedOffsByThePass,
           (unsigned long)module.acquisition.recoveryActionCount(), (unsigned long)sensors[0].worstGapMs,
           (unsigned long)sensors[0].worstUnreadWhileAwakeMs,
           intoThePass(module, module.recordingStartupScan.finishedAtMs()));

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, heldAskedOnPort1,
                                   "port 1 was asked, on the live wire, the address the stored sensor of "
                                   "port 0 holds");
    TEST_ASSERT_TRUE_MESSAGE(cheapProbesAt(module, 2, MODBUS_ADDRESS_MIN) > 0 &&
                                 cheapProbesAt(module, 3, MODBUS_ADDRESS_MIN) > 0,
                             "the ports of the other wire, which the stored sensor cannot answer, were "
                             "not asked 0x70 cheaply");
    TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[1] > 0 && watch.adoptedAtMs[1] < cheapRungOverAtMs(module),
                             "port 1's device was not found by the cheap rung");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 1, rowAddressOf(module, 1),
                                   "port 1's device was not adopted at the address it answers at");
    char message[200];
    snprintf(message, sizeof(message),
             "steps of the discovery took the stored sensor's power %d time(s), the steps that take it "
             "account for %d",
             storedOffsByThePass, STORED_POWER_OFFS_WHEN_ITS_NEIGHBOUR_IS_FOUND);
    TEST_ASSERT_TRUE_MESSAGE(storedOffsByThePass <= STORED_POWER_OFFS_WHEN_ITS_NEIGHBOUR_IS_FOUND, message);
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr,
                             "the stored sensor no longer answers at 0x70");
}

// (d) Two sensors of one model at their factory address on the two ports of one wire answer every
//     cheap question together, and a collision is no answer: both ports are left to the rest of the
//     ladder. The first twin is found by its port's window and the identification after it, which
//     moves it onto its port's address. The second twin, alone at the factory address from then on,
//     is asked the cheap rung again before its own window and found by it: no window, no
//     identification step, and no write into its port but the one that moves it into the range.
void test_s18_the_second_of_two_twins_on_a_wire_is_found_by_the_cheap_rung_asked_again(void)
{
    const PluggedIn layouts[3] = {
        {"twins on 0 and 1", {0, 1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"twins on 2 and 3", {2, 3, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
        {"twins on 0 and 1, twins on 2 and 3", {0, 1, 2, 3}, {-1, -1, -1, -1}, {-1, -1, -1, -1}},
    };

    std::string failures;
    char item[288];
    for (int l = 0; l < 3; l++)
    {
        ModuleUnderTest module;
        plugIn(module, layouts[l]);
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence);

        std::string perPort;
        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            if (!listed(layouts[l].factory, pin))
            {
                continue;
            }
            snprintf(item, sizeof(item),
                     "; port %d adopted at %lu ms, %u window(s), %u identification step(s), %u write(s), "
                     "at 0x%02X",
                     pin, intoThePass(module, watch.adoptedAtMs[pin]),
                     stepsOfThePass(module, StartupScanStep::PROBE_PRESENCE, pin),
                     stepsOfThePass(module, StartupScanStep::SCAN_CANDIDATE, pin),
                     (unsigned)portOf(module, pin).writes.size(), rowAddressOf(module, pin));
            perPort += item;
        }
        printf("  s18: %s - pass over at %lu ms%s\n", layouts[l].name,
               intoThePass(module, module.recordingStartupScan.finishedAtMs()), perPort.c_str());

        for (int pin = 1; pin < MODULE_PIN_COUNT; pin += 2)
        {
            if (!listed(layouts[l].factory, pin))
            {
                continue;
            }
            const unsigned windows = stepsOfThePass(module, StartupScanStep::PROBE_PRESENCE, pin);
            const unsigned identified = stepsOfThePass(module, StartupScanStep::SCAN_CANDIDATE, pin);
            const size_t writes = portOf(module, pin).writes.size();
            if (watch.adoptedAtMs[pin] == 0 || windows != 0 || identified != 0 || writes > 1 ||
                rowAddressOf(module, pin) != (unsigned)(MODBUS_ADDRESS_MIN + pin))
            {
                snprintf(item, sizeof(item),
                         " [%s: port %d - %s, %u window(s), %u identification step(s), %u write(s), at "
                         "0x%02X]",
                         layouts[l].name, pin, watch.adoptedAtMs[pin] > 0 ? "adopted" : "not found",
                         windows, identified, (unsigned)writes, rowAddressOf(module, pin));
                failures += item;
            }
        }
    }

    char message[640];
    snprintf(message, sizeof(message),
             "the second twin of a wire was not found by the cheap rung asked again - it got a window, "
             "an identification step or more than one write:%s",
             failures.c_str());
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), message);
}

// (e) A sensor moved: its row says port 0 at 0x70, the device itself is on port 1 now, the port
//     sharing that wire, still at 0x70. The ping of port 0's row is asked with the rest of its wire
//     dark, port 1, and hears nothing - so port 1 is asked 0x70 on the live wire like any other
//     address, and the device is found by the cheap rung: no window, no write of a window, no
//     identification. The two rows are parted as soon as the device is adopted - the one whose port the
//     address belongs to keeps it, the moved sensor takes its own port's - while port 0 is still dark
//     from the confirming scan, so no read of port 0's row on the shared wire is ever answered by the
//     moved sensor. The row of port 0 stays in the registry, as it stays when its device is silent for
//     any other reason.
void test_s18_a_sensor_moved_to_the_port_sharing_its_wire_is_found_and_its_old_row_kept(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.sensorRepository.save(makeStoredRyWs301(0));
    module.port1.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN));
    std::vector<SweepOfOneSensor> oldRow(1);
    oldRow[0].pin = 0;
    oldRow[0].uid = uidOfPin(0);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &oldRow);
    runWatching(module, watch, 120000, &oldRow);

    const char* how = "not found";
    uint64_t movedAtMs = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.pin == 1 && w.kind == StartupScanStep::WRITE_ADDRESS)
        {
            movedAtMs = w.endedAtMs;
        }
        if (!w.discovery || w.pin != 1 || !w.answered || w.kind == StartupScanStep::WRITE_ADDRESS ||
            w.startedAtMs > watch.adoptedAtMs[1] || w.endedAtMs < watch.adoptedAtMs[1] - 1000)
        {
            continue;
        }
        how = BackgroundActs::ladderStepName(w.kind);
    }
    Utils::Optional<SensorEntity> oldEntity = module.sensorRepository.get(uidOfPin(0).c_str());
    printf("  s18: a sensor moved from port 0 to port 1 - 0x70 asked on port 1's live wire %u time(s); "
           "port 1 adopted at %lu ms (first window at %lu ms) by %s, at 0x%02X, %u window(s), %u "
           "identification step(s), %u address write step(s) of its window, %u of the address phase (over "
           "at %lu ms), %u register write(s) into port 1; port 0's row %s at 0x%02X, described per "
           "structure '%s', read %u time(s) (the last at %lu ms); pass over at %lu ms\n",
           cheapProbesAt(module, 1, MODBUS_ADDRESS_MIN), intoThePass(module, watch.adoptedAtMs[1]),
           intoThePass(module, firstWindowOpenedAtMs(module)), how, rowAddressOf(module, 1),
           stepsOfThePass(module, StartupScanStep::PROBE_PRESENCE, 1),
           stepsOfThePass(module, StartupScanStep::SCAN_CANDIDATE, 1),
           stepsOfThePass(module, StartupScanStep::WRITE_PORT_ADDRESS, 1),
           stepsOfThePass(module, StartupScanStep::WRITE_ADDRESS, 1), intoThePass(module, movedAtMs),
           (unsigned)module.port1.writes.size(), oldEntity ? "kept" : "gone",
           oldEntity ? (unsigned)oldEntity.value().modbusDeviceAddress : 0,
           descriptionsOf(module.server, 0).c_str(), oldRow[0].readings, intoThePass(module, oldRow[0].lastReadMs),
           intoThePass(module, module.recordingStartupScan.finishedAtMs()));

    TEST_ASSERT_TRUE_MESSAGE(cheapProbesAt(module, 1, MODBUS_ADDRESS_MIN) > 0,
                             "port 1 was not asked, on the live wire, the address of a row whose device was "
                             "silent to its ping");
    TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[1] > 0 && watch.adoptedAtMs[1] < cheapRungOverAtMs(module),
                             "the moved sensor was not found by the cheap rung");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, stepsOfThePass(module, StartupScanStep::PROBE_PRESENCE, 1),
                                   "port 1 got a window although the cheap rung found its device");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, stepsOfThePass(module, StartupScanStep::SCAN_CANDIDATE, 1),
                                   "the identification procedure ran on port 1");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, stepsOfThePass(module, StartupScanStep::WRITE_PORT_ADDRESS, 1),
                                   "an address was written into port 1 by a window");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, stepsOfThePass(module, StartupScanStep::WRITE_ADDRESS, 1),
                                   "the two rows were not parted by the one move of the address phase");
    TEST_ASSERT_TRUE_MESSAGE(oldEntity && oldEntity.value().modbusDeviceAddress == MODBUS_ADDRESS_MIN,
                             "port 0's row did not keep the address that is its port's own");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 1, rowAddressOf(module, 1),
                                   "port 1's row is not at its own port's address");
    TEST_ASSERT_TRUE_MESSAGE(module.port1.device(MODBUS_ADDRESS_MIN + 1) != nullptr,
                             "the moved sensor does not answer at the address its row says");
    TEST_ASSERT_TRUE_MESSAGE(movedAtMs > 0 && movedAtMs < cheapRungOverAtMs(module),
                             "the two rows were not parted before the cheap rung was over");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, oldRow[0].readings,
                                   "port 0's row was read - answered by the moved sensor on the shared wire");
}

// The echo of a sensor nobody has adopted yet: a device at 0x71 on port 1, port 0 empty. Port 0 is
// asked 0x71 on the live wire - no row holds it - and the first scannable model whose read the device
// answers there costs one confirming scan, which finds port 0 silent. That scan leaves every other
// port of the module dark, port 1 with it, so the models after it get no answer at 0x71 and nothing
// else port 0 is asked is heard. One scan is the price the range pays on the live wire for a
// neighbour the registry does not know yet; port 1's device is adopted by its own cheap rung after it.
void test_s18_a_sensor_of_the_range_on_the_paired_port_costs_one_confirming_scan(void)
{
    const PluggedIn layout = {"at 0x71 on 1", {-1, -1, -1, -1}, {1, -1, -1, -1}, {-1, -1, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    unsigned scannable = 0;
    const std::vector<SensorType> catalog = theServersCatalog();
    for (size_t i = 0; i < catalog.size(); i++)
    {
        if (catalog[i].canScan && catalog[i].isSupported && catalog[i].isModbusSensor())
        {
            scannable++;
        }
    }
    unsigned heardAt0x71 = 0;
    const unsigned askedAt0x71 = cheapProbesAt(module, 0, MODBUS_ADDRESS_MIN + 1, &heardAt0x71);
    unsigned heardAnywhere = 0;
    uint64_t heardMs = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.heardBeforeTheWireWasIsolated)
        {
            heardAnywhere++;
            heardMs += w.lengthMs();
        }
    }
    printf("  s18: %s - port 0 asked 0x71 on the live wire %u time(s), %u of them heard and not "
           "confirmed (%lu ms of line time, %u scannable model(s)); heard anywhere %u time(s); port 1 "
           "adopted at %lu ms (cheap rung over at %lu ms) at 0x%02X; pass over at %lu ms\n",
           layout.name, askedAt0x71, heardAt0x71, (unsigned long)heardMs, scannable, heardAnywhere,
           intoThePass(module, watch.adoptedAtMs[1]), intoThePass(module, cheapRungOverAtMs(module)),
           rowAddressOf(module, 1), intoThePass(module, module.recordingStartupScan.finishedAtMs()));

    TEST_ASSERT_EQUAL_UINT_MESSAGE(scannable, askedAt0x71, "port 0 was not asked 0x71 by every scannable model");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, heardAt0x71,
                                   "port 0's echo of its neighbour at 0x71 did not cost exactly one "
                                   "confirming scan");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(heardAt0x71, heardAnywhere,
                                   "a confirming scan found a free port silent somewhere else too");
    TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[1] > 0 && watch.adoptedAtMs[1] < cheapRungOverAtMs(module),
                             "port 1's device was not found by its cheap rung");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN + 1, rowAddressOf(module, 1),
                                   "port 1's device was not adopted at the address it answers at");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.port1.writes.size(), "an address was written into port 1");
}

// (f) What the range on the cheap rung costs the commonest restart - nothing plugged into any port:
//     per port and scannable model the four addresses of the range beside the model's own, each a
//     read budget and the turn the task that owns the line idles before the next step. The walk over
//     the cheap rung is bounded by that and the ten seconds the ports are given first.
void test_s18_what_the_range_on_the_cheap_rung_costs_a_module_with_nothing_plugged_in(void)
{
    const PluggedIn layout = {"nothing plugged in", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    unsigned scannable = 0;
    const std::vector<SensorType> catalog = theServersCatalog();
    for (size_t i = 0; i < catalog.size(); i++)
    {
        if (catalog[i].canScan && catalog[i].isSupported && catalog[i].isModbusSensor())
        {
            scannable++;
        }
    }
    const unsigned questionsPerPort = scannable * (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1 + 1);
    const uint64_t cheapRungMs = intoThePass(module, cheapRungOverAtMs(module));
    const uint64_t boundMs = StartupSensorScanUseCase::PORT_WAKE_UP_MS +
                             MODULE_PIN_COUNT * questionsPerPort * (A_READ_MS + SensorAcquisitionService::IDLE_STEP_MS) +
                             1000;
    unsigned questions = 0;
    for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
    {
        questions += stepsOfThePass(module, StartupScanStep::PROBE_CANDIDATE, pin);
    }
    printf("  s18: %s - %u cheap question(s), %u per port; the cheap rung over at %lu ms (bound %lu ms), "
           "the pass over at %lu ms; %u window(s)\n",
           layout.name, questions, questionsPerPort, (unsigned long)cheapRungMs, (unsigned long)boundMs,
           intoThePass(module, module.recordingStartupScan.finishedAtMs()),
           (unsigned)module.startupScan.presencePollCount());

    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODULE_PIN_COUNT * questionsPerPort, questions,
                                   "the cheap rung did not ask every port the range and the factory "
                                   "addresses of every scannable model, once each");
    char message[200];
    snprintf(message, sizeof(message), "the cheap rung over four empty ports took %lu ms, the reader's "
             "timings make it %lu ms at most", (unsigned long)cheapRungMs, (unsigned long)boundMs);
    TEST_ASSERT_TRUE_MESSAGE(cheapRungMs <= boundMs, message);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODULE_PIN_COUNT, (unsigned)module.startupScan.presencePollCount(),
                                   "every empty port did not get its one window");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.startupScan.adoptedCount(),
                                   "something was adopted on four empty ports");
}

namespace {

/**
 * @brief The rules' freshness window: a rule whose sensor was last read longer ago than this is held
 *        as sensor_unreachable. EvaluateRulesUseCase::SENSOR_FRESHNESS_WINDOW_MS is private, so its
 *        value is written here.
 */
const uint64_t RULES_FRESHNESS_WINDOW_MS = 300000;

/** @brief The steps of the discovery that took this port's power, in order, for the print. */
std::string stepsThatTookThePowerOf(const ModuleUnderTest& module, int pin)
{
    const SimRs485Bus& port = *static_cast<const SimRs485Bus*>(module.ports[static_cast<size_t>(pin)]);
    std::string steps;
    char item[96];
    for (size_t o = 0; o < port.powerOffAtUs.size(); o++)
    {
        const uint64_t atMs = port.powerOffAtUs[o] / 1000;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (w.discovery && atMs >= w.startedAtMs && atMs < w.endedAtMs)
            {
                snprintf(item, sizeof(item), "%s%s on %d at 0x%02X%s", steps.empty() ? "" : ", ",
                         BackgroundActs::ladderStepName(w.kind), w.pin, w.address,
                         w.heardBeforeTheWireWasIsolated ? " (heard)" : "");
                steps += item;
                break;
            }
        }
    }
    return steps;
}

/** @brief A layout with stored sensors, and how often the steps of the pass take each one's power. */
struct BesideStored {
    PluggedIn plugged;
    /** Power-offs of each stored port by a step of the discovery; -1 on a port that is not stored. */
    int offs[MODULE_PIN_COUNT];
    /** Cheap probes heard and not confirmed. */
    unsigned heard;
};

/** @brief What the pass and the two minutes after it did to the stored sensors of a layout. */
struct BesideStoredOutcome {
    int offs[MODULE_PIN_COUNT] = {0, 0, 0, 0};
    int offsByTheOtherWire[MODULE_PIN_COUNT] = {0, 0, 0, 0};
    std::string offSteps[MODULE_PIN_COUNT];
    std::string described[MODULE_PIN_COUNT];
    std::vector<SweepOfOneSensor> sensors;
    uint64_t recoveryActions = 0;
    unsigned heard = 0;
    unsigned readsBeforeTheWakeUp = 0;
};

BesideStoredOutcome runBesideStored(const BesideStored& layout)
{
    BesideStoredOutcome outcome;
    ModuleUnderTest module;
    plugIn(module, layout.plugged, &outcome.sensors);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &outcome.sensors);
    runWatching(module, watch, 120000, &outcome.sensors);

    outcome.recoveryActions = module.acquisition.recoveryActionCount();
    outcome.heard = heardOnTheLiveWire(module);
    std::string perPort;
    char item[400];
    for (size_t i = 0; i < outcome.sensors.size(); i++)
    {
        const int pin = outcome.sensors[i].pin;
        outcome.offs[pin] = powerOffsByTheDiscovery(module, pin);
        outcome.offsByTheOtherWire[pin] = powerOffsByTheOtherWire(module, pin);
        outcome.offSteps[pin] = stepsThatTookThePowerOf(module, pin);
        outcome.described[pin] = descriptionsOf(module.server, pin);
        outcome.readsBeforeTheWakeUp += sweepReadsBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs());
        snprintf(item, sizeof(item),
                 "; stored on %d: power taken %d time(s) by steps of the discovery [%s], longest gap between "
                 "readings %lu ms, longest unread while awake %lu ms, described '%s'",
                 pin, outcome.offs[pin], outcome.offSteps[pin].c_str(),
                 (unsigned long)outcome.sensors[i].worstGapMs,
                 (unsigned long)outcome.sensors[i].worstUnreadWhileAwakeMs, outcome.described[pin].c_str());
        perPort += item;
    }
    printf("  s18: %s - pass over at %lu ms, %u cheap probe(s) heard and not confirmed, %lu port recovery "
           "action(s), %u sweep read(s) before the wake-up%s\n",
           layout.plugged.name, intoThePass(module, module.recordingStartupScan.finishedAtMs()), outcome.heard,
           (unsigned long)outcome.recoveryActions, outcome.readsBeforeTheWakeUp, perPort.c_str());
    return outcome;
}

/** @brief Hold a layout's stored sensors to what the pass may cost them; say every failure. */
void judgeBesideStored(const BesideStored& layout, const BesideStoredOutcome& outcome, std::string& failures)
{
    char item[320];
    for (size_t i = 0; i < outcome.sensors.size(); i++)
    {
        const SweepOfOneSensor& sensor = outcome.sensors[i];
        const int pin = sensor.pin;
        const std::string& seen = outcome.described[pin];
        const size_t shown = seen.find('s');
        if (outcome.offs[pin] != layout.offs[pin] || outcome.offsByTheOtherWire[pin] != 0 ||
            sensor.worstUnreadWhileAwakeMs > ANY_STEP_OF_THE_DISCOVERY_MS ||
            sensor.worstGapMs >= RULES_FRESHNESS_WINDOW_MS ||
            (shown != std::string::npos && seen.find('n', shown) != std::string::npos))
        {
            snprintf(item, sizeof(item),
                     " [%s, stored on %d: power taken %d time(s), %d of them by steps of the other wire, the "
                     "steps that take it account for %d; unread while awake %lu ms (limit %lu); longest gap "
                     "%lu ms (limit %lu); described '%s']",
                     layout.plugged.name, pin, outcome.offs[pin], outcome.offsByTheOtherWire[pin], layout.offs[pin],
                     (unsigned long)sensor.worstUnreadWhileAwakeMs, (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS,
                     (unsigned long)sensor.worstGapMs, (unsigned long)RULES_FRESHNESS_WINDOW_MS, seen.c_str());
            failures += item;
        }
    }
    if (outcome.recoveryActions != 0 || outcome.readsBeforeTheWakeUp != 0 || outcome.heard != layout.heard)
    {
        snprintf(item, sizeof(item),
                 " [%s: %lu port recovery action(s), %u sweep read(s) before the wake-up, %u cheap probe(s) heard "
                 "and not confirmed (%u expected)]",
                 layout.plugged.name, (unsigned long)outcome.recoveryActions, outcome.readsBeforeTheWakeUp,
                 outcome.heard, layout.heard);
        failures += item;
    }
}

} // namespace

// The echo beside a stored sensor on the other wire. A sensor of the range nobody has adopted yet, on
// the port paired with a free one, answers that free port's cheap question at its address; the
// confirming scan that finds the free port silent takes the power of the port sharing the free
// port's wire, the very sensor that answered. A stored sensor on the other wire pays nothing for it:
// none of the steps about the echo's wire takes its power. Held exactly, layout by layout, with the
// steps that take each stored sensor's power named - all of them about its own wire:
//   - a sensor at 0x71 on port 1, port 0 free, stored on 2 (or 3): the poll that opens the window of
//     the stored port's pair (port 3, or port 2) - one;
//   - the same with stored sensors on both 2 and 3: the ping of the other stored sensor - one each;
//   - a sensor at 0x73 on port 3, port 2 free, stored on 0 (or 1): the first, mirrored;
//   - with a factory sensor on the free port as well: the window poll of the stored port's pair -
//     one. Here the echo takes the place of another: the port sharing the factory sensor's wire,
//     asked its model's factory address with that sensor live, would hear it - unless the range found
//     its own device first.
// And through all of it: no recovery, no read before the wake-up, no sensor shown and then taken
// back, never unread while awake for longer than a step of the discovery may hold the line, never
// unread for as long as the rules' freshness window.
void test_s18_an_echo_of_the_range_beside_a_stored_sensor_on_the_other_wire_costs_it_nothing(void)
{
    const BesideStored layouts[] = {
        {{"stored on 2, at 0x71 on 1", {-1, -1, -1, -1}, {1, -1, -1, -1}, {2, -1, -1, -1}}, {-1, -1, 1, -1}, 1},
        {{"stored on 3, at 0x71 on 1", {-1, -1, -1, -1}, {1, -1, -1, -1}, {3, -1, -1, -1}}, {-1, -1, -1, 1}, 1},
        {{"stored on 2 and 3, at 0x71 on 1", {-1, -1, -1, -1}, {1, -1, -1, -1}, {2, 3, -1, -1}}, {-1, -1, 1, 1}, 1},
        {{"stored on 0, at 0x73 on 3", {-1, -1, -1, -1}, {3, -1, -1, -1}, {0, -1, -1, -1}}, {1, -1, -1, -1}, 1},
        {{"stored on 1, at 0x73 on 3", {-1, -1, -1, -1}, {3, -1, -1, -1}, {1, -1, -1, -1}}, {-1, 1, -1, -1}, 1},
        {{"stored on 2, at 0x71 on 1, factory on 0", {0, -1, -1, -1}, {1, -1, -1, -1}, {2, -1, -1, -1}},
         {-1, -1, 1, -1}, 1},
        {{"stored on 0, factory on 2, at 0x73 on 3", {2, -1, -1, -1}, {3, -1, -1, -1}, {0, -1, -1, -1}},
         {1, -1, -1, -1}, 1},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);

    std::vector<BesideStoredOutcome> outcomes;
    for (size_t l = 0; l < count; l++)
    {
        outcomes.push_back(runBesideStored(layouts[l]));
    }
    std::string failures;
    for (size_t l = 0; l < count; l++)
    {
        judgeBesideStored(layouts[l], outcomes[l], failures);
    }
    char message[1400];
    snprintf(message, sizeof(message), "a stored sensor beside an echo of the range paid more or less than "
             "named:%s", failures.c_str());
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), message);
}

// The echo the cheap rung asked again can meet. A device that will not take the address the module
// writes it and is asleep through the cheap rung of every free port is found by its port's window and
// the identification after it - at its model's factory address, 0x01. The port sharing its wire is
// then asked the cheap rung again, and the factory addresses are asked on the live wire: the device
// just found answers 0x01, and the confirming scan finds the asking port silent. One confirming scan,
// one power-off more of the port sharing the asking port's wire - the device that answered; the
// asking port then gets its window as it would have. The stored sensor, on the other wire, pays for
// none of it: its power is taken once, by the poll that opens port 3's window. The two identification
// steps on port 0, the echo scan of port 1 and the address write the device on port 0 refuses are all
// on the wire of ports 0 and 1. The echo is paid earlier rather than paid more: without the cheap rung
// asked again, a pass after the restart's asks port 1 the same factory address on the same live wire
// and hears the same echo.
const int STORED_POWER_OFFS_BESIDE_A_FACTORY_FIND_ASKED_AGAIN = 1;

namespace {

/** @brief A wake-up no power stretch reaches: the device is asleep until wakeAtItsPortsWindow() wakes it. */
const uint32_t ASLEEP_UNTIL_ITS_PORTS_WINDOW_MS = 0xFFFFFFFFu;

/**
 * @brief Did a request this device answers go out on its wire while its port had already had this much
 *        power in a stretch before the present one?
 *
 * A request to its address, or to the wildcard when it answers that, put on the wire by either port of
 * it (ModuleUnderTest::wireThePortsTheWayTheBoardWiresThem() wires ports 0-1 and 2-3) at a moment the
 * device's port had had at least `wakeUpMs` of power. Such a device would have been awake for it and
 * answered. A stretch still in progress is left out - up to now it is shorter than its own length - and
 * only a port that has its power has one: on a dark port every stretch is over and every one counts.
 * Read off the records the port keeps when asked to - wakeAtItsPortsWindow() asks for them before the
 * boot.
 */
bool aRequestWouldHaveFoundItAwake(ModuleUnderTest& module, int pin, const SimRs485Bus::Device& device,
                                   uint64_t wakeUpMs)
{
    const SimRs485Bus& port = portOf(module, pin);
    if (port.powerOnAtUs.empty())
    {
        return false;
    }
    const uint64_t currentStretchFromUs = port.isPowered() ? port.powerOnAtUs.back() : UINT64_MAX;
    const int wire[2] = {pin, pin ^ 1};
    for (int w = 0; w < 2; w++)
    {
        const SimRs485Bus& sender = portOf(module, wire[w]);
        const size_t untimed = sender.sentFrames.size() - sender.sentAtUs.size();
        for (size_t k = 0; k < sender.sentAtUs.size(); k++)
        {
            const std::vector<uint8_t>& frame = sender.sentFrames[untimed + k];
            const uint64_t atUs = sender.sentAtUs[k];
            if (frame.empty() || atUs >= currentStretchFromUs ||
                !(frame[0] == device.address || (frame[0] == 0xFF && device.answersTheWildcard)))
            {
                continue;
            }
            // The stretch of the device's port the request fell into, if the port was up at all.
            uint64_t onUs = 0;
            bool up = false;
            for (size_t i = 0; i < port.powerOnAtUs.size() && port.powerOnAtUs[i] <= atUs; i++)
            {
                onUs = port.powerOnAtUs[i];
                up = true;
            }
            for (size_t i = 0; up && i < port.powerOffAtUs.size(); i++)
            {
                if (port.powerOffAtUs[i] > onUs && port.powerOffAtUs[i] <= atUs)
                {
                    up = false;
                }
            }
            if (up && atUs - onUs >= wakeUpMs * 1000ULL)
            {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief The device at this address of this port wakes as the window of its port opens - if a device
 *        could.
 *
 * The restart's pass asks every free port the cheap rung before it asks any port the rest, and the rest
 * of a port begins with its window's presence poll (StartupSensorScanUseCase::buildCandidateStep()). The
 * device takes the power its port has had by that poll as its wake-up - the most it may need and still
 * answer the poll - and from then on every power-up of the port costs it that long. That is a device
 * asleep through the cheap rung of every free port and awake for its window wherever the cheap rung puts
 * the two, as long as its port has had its power without a break since the pass gave it: the stretch
 * before the poll is then the only one, and no request found the device awake in it.
 *
 * When the port lost its power before the poll, an earlier stretch may have been long enough: a device
 * needing that much would have been awake for a request to it there (aRequestWouldHaveFoundItAwake()),
 * and one needing less would have been awake sooner still. No device then sleeps through the cheap rung
 * and answers the poll, so this one is not woken at all - and the pass does not find it, as it would not
 * find any device of a fixed wake-up there. Nor is it woken when the poll's step finds its port dark:
 * the hook runs before the poll's path switches the port on and waits out its warm-up
 * (RawBusAccessAdapter::letThePortComeUp()), so it does not know the power the device would have at the
 * poll's first frame, and leaves the device asleep - the test then says the scenario did not happen
 * rather than pass on a wake-up it made up. A device that warm-up would wake needs no more than
 * PORT_WAKE_UP_MS, and the pass asks no port the cheap rung before it has had that much: it would have
 * answered its own port's cheap questions.
 */
void wakeAtItsPortsWindow(ModuleUnderTest& module, int pin, uint8_t address)
{
    portOf(module, pin).recordPowerOnTimes(true);
    portOf(module, pin).recordPowerOffTimes(true);
    portOf(module, pin).recordSendTimes(true);
    portOf(module, pin ^ 1).recordSendTimes(true);
    bool decided = false;
    module.recordingStartupScan.beforeEachStep([&module, pin, address, decided](const StartupScanStep& step) mutable {
        SimRs485Bus::Device* device = portOf(module, pin).device(address);
        if (decided || device == nullptr || step.kind != StartupScanStep::PROBE_PRESENCE || step.pin != pin)
        {
            return;
        }
        decided = true;
        const uint64_t wakeUpMs = portOf(module, pin).currentPowerStretchMs();
        if (portOf(module, pin).isPowered() && !aRequestWouldHaveFoundItAwake(module, pin, *device, wakeUpMs))
        {
            device->wakeUpMs = static_cast<uint32_t>(wakeUpMs);
        }
    });
}

} // namespace

void test_s18_a_port_asked_again_beside_a_find_at_a_factory_address_pays_one_echo(void)
{
    BesideStoredOutcome outcome;
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    SimRs485Bus::Device device = makeFactoryRyWs12s();
    device.addressIsWritable = false;
    device.wakeUpMs = ASLEEP_UNTIL_ITS_PORTS_WINDOW_MS;
    module.port0.addDevice(device);
    wakeAtItsPortsWindow(module, 0, device.address);
    module.sensorRepository.save(makeStoredRyWs301(2));
    module.port2.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN + 2));
    SweepOfOneSensor stored;
    stored.pin = 2;
    stored.uid = uidOfPin(2);
    outcome.sensors.push_back(stored);

    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &outcome.sensors);
    runWatching(module, watch, 120000, &outcome.sensors);

    unsigned heardOnPort1At0x01 = 0;
    const unsigned askedOnPort1At0x01 = cheapProbesAt(module, 1, 1, &heardOnPort1At0x01);
    const unsigned identified = stepsOfThePass(module, StartupScanStep::SCAN_CANDIDATE, 0);
    const unsigned windowsOfPort1 = stepsOfThePass(module, StartupScanStep::PROBE_PRESENCE, 1);
    const int storedOffs = powerOffsByTheDiscovery(module, 2);
    const int storedOffsByTheOtherWire = powerOffsByTheOtherWire(module, 2);
    const uint64_t recoveryActions = module.acquisition.recoveryActionCount();
    uint64_t echoMs = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        if (module.busWindows[i].discovery && module.busWindows[i].heardBeforeTheWireWasIsolated)
        {
            echoMs += module.busWindows[i].lengthMs();
        }
    }
    const SimRs485Bus::Device* woken = module.port0.device(device.address);
    const uint32_t wakeUpMs = woken != nullptr ? woken->wakeUpMs : 0;
    printf("  s18: a factory sensor that takes no address and wakes late on 0, stored on 2 - awake at its "
           "window's poll after %lu ms of power; port 0 adopted at "
           "%lu ms at 0x%02X after %u identification step(s); port 1 asked 0x01 on the live wire %u time(s), "
           "%u heard and not confirmed (%u in all, %lu ms of line time), %u window(s) of port 1; the stored "
           "sensor's power taken %d time(s) by steps of the discovery [%s]; %lu port recovery action(s); "
           "longest gap %lu ms; pass over at %lu ms\n",
           wakeUpMs == ASLEEP_UNTIL_ITS_PORTS_WINDOW_MS ? 0UL : (unsigned long)wakeUpMs,
           intoThePass(module, watch.adoptedAtMs[0]), rowAddressOf(module, 0), identified, askedOnPort1At0x01,
           heardOnPort1At0x01, heardOnTheLiveWire(module), (unsigned long)echoMs, windowsOfPort1, storedOffs,
           stepsThatTookThePowerOf(module, 2).c_str(), (unsigned long)recoveryActions,
           (unsigned long)outcome.sensors[0].worstGapMs,
           intoThePass(module, module.recordingStartupScan.finishedAtMs()));

    TEST_ASSERT_TRUE_MESSAGE(watch.adoptedAtMs[0] > 0 && rowAddressOf(module, 0) == 1 && identified > 0,
                             "port 0's device was not found by the identification at its factory address - "
                             "the scenario did not happen");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, heardOnPort1At0x01,
                                   "port 1, asked again, did not pay exactly one confirming scan for the "
                                   "answer port 0's device gave at 0x01");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, heardOnTheLiveWire(module),
                                   "a confirming scan found a free port silent somewhere else too");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, windowsOfPort1, "port 1 did not get its window after the echo");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, storedOffsByTheOtherWire,
                                  "steps about the ports of the other wire took the stored sensor's power");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STORED_POWER_OFFS_BESIDE_A_FACTORY_FIND_ASKED_AGAIN, storedOffs,
                                  "the steps of the discovery took the stored sensor's power another number of "
                                  "times than the steps that take it");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, recoveryActions, "the port recovery acted");
    TEST_ASSERT_TRUE_MESSAGE(outcome.sensors[0].worstUnreadWhileAwakeMs <= ANY_STEP_OF_THE_DISCOVERY_MS,
                             "the stored sensor went unread while awake for longer than a step may hold the line");
}

// The same move beside a stored sensor on the other wire. The early parting is a write like the one
// the address phase would make at the end of the pass, moved, not added. The ping of the moved
// sensor's old row, the confirming scan of the find and the write that parts the two rows are all on
// the wire of ports 0 and 1: the stored sensor's power is taken once, by the poll that opens the
// window of its pair - and no read of the old row is answered by the moved sensor.
const int STORED_POWER_OFFS_BESIDE_A_MOVED_SENSOR = 1;

void test_s18_a_sensor_moved_beside_a_stored_sensor_is_parted_from_its_old_row_at_no_extra_cost(void)
{
    const int storedOn[2] = {2, 3};
    std::string failures;
    char item[400];
    for (int k = 0; k < 2; k++)
    {
        const int pin = storedOn[k];
        ModuleUnderTest module;
        eraseTheRegistry(module, theServersCatalog());
        module.sensorRepository.save(makeStoredRyWs301(0));
        module.port1.addDevice(makeLoggedRyWs301(MODBUS_ADDRESS_MIN));
        module.sensorRepository.save(makeStoredRyWs301(pin));
        portOf(module, pin).addDevice(makeLoggedRyWs301((uint8_t)(MODBUS_ADDRESS_MIN + pin)));
        std::vector<SweepOfOneSensor> sensors(2);
        sensors[0].pin = 0;
        sensors[0].uid = uidOfPin(0);
        sensors[1].pin = pin;
        sensors[1].uid = uidOfPin(pin);
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
        runWatching(module, watch, 120000, &sensors);

        const int offs = powerOffsByTheDiscovery(module, pin);
        const int offsByTheOtherWire = powerOffsByTheOtherWire(module, pin);
        printf("  s18: a sensor moved from port 0 to port 1, stored on %d - port 1 adopted at %lu ms (first window "
               "at %lu ms) at 0x%02X; port 0's row read %u time(s); the stored sensor's power taken %d time(s) by "
               "steps of the discovery [%s], longest gap %lu ms, described '%s'; %lu port recovery action(s); "
               "pass over at %lu ms\n",
               pin, intoThePass(module, watch.adoptedAtMs[1]), intoThePass(module, firstWindowOpenedAtMs(module)),
               rowAddressOf(module, 1), sensors[0].readings, offs, stepsThatTookThePowerOf(module, pin).c_str(),
               (unsigned long)sensors[1].worstGapMs, descriptionsOf(module.server, pin).c_str(),
               (unsigned long)module.acquisition.recoveryActionCount(),
               intoThePass(module, module.recordingStartupScan.finishedAtMs()));

        if (watch.adoptedAtMs[1] == 0 || watch.adoptedAtMs[1] >= cheapRungOverAtMs(module) ||
            rowAddressOf(module, 1) != (unsigned)(MODBUS_ADDRESS_MIN + 1) || sensors[0].readings != 0 ||
            offs != STORED_POWER_OFFS_BESIDE_A_MOVED_SENSOR || offsByTheOtherWire != 0)
        {
            snprintf(item, sizeof(item),
                     " [stored on %d: port 1 %s at 0x%02X, port 0's row read %u time(s), the stored sensor's power "
                     "taken %d time(s), %d of them by steps of the other wire, the steps that take it account for %d]",
                     pin, watch.adoptedAtMs[1] > 0 && watch.adoptedAtMs[1] < cheapRungOverAtMs(module)
                              ? "found by the cheap rung" : "not found by the cheap rung",
                     rowAddressOf(module, 1), sensors[0].readings, offs, offsByTheOtherWire,
                     STORED_POWER_OFFS_BESIDE_A_MOVED_SENSOR);
            failures += item;
        }
    }
    char message[1000];
    snprintf(message, sizeof(message), "a sensor moved beside a stored sensor was not parted from its old row "
             "as named:%s", failures.c_str());
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), message);
}

namespace {

/**
 * @brief The sensor stored on PIN answers the sweep before the restart's pass and after its ping, and
 *        is silent from the last second of the boot's settle window to the end of the ping - a sensor
 *        that missed its ping by chance.
 */
template <int PIN>
void missesItsPingByChance(ModuleUnderTest& module)
{
    SimRs485Bus::Device* device = portOf(module, PIN).device((uint8_t)(MODBUS_ADDRESS_MIN + PIN));
    if (device == nullptr || module.clock.nowMs() + 1000 < StartupSensorScanUseCase::BOOT_SETTLE_MS)
    {
        return;
    }
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::VERIFY_KNOWN && w.pin == PIN && w.endedAtMs > 0)
        {
            device->silent = false;
            return;
        }
    }
    device->silent = true;
}

/** @brief Did the ping of the restart's pass get an answer from the row stored on this port? */
bool answeredItsPing(const ModuleUnderTest& module, int pin)
{
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::VERIFY_KNOWN && w.pin == pin)
        {
            return w.answered;
        }
    }
    return false;
}

} // namespace

// A stored sensor that missed its ping by chance and answers after it. Its address is asked on the
// live wire of the port it shares, like any address of the range, and its answer is the echo the
// confirming scan exists for: the scan, with the rest of its wire dark, hears nothing from the asking
// port, and the module notes whose answer it was. The price is that one confirming scan - one
// power-off of the port sharing the asking port's wire, the silent sensor's own - and nothing is
// adopted. Held exactly, with the steps that take each stored sensor's power named:
//   - stored on 0 (or 1), the others empty: the echo scan, and the poll that opens the window of the
//     port sharing its wire - two;
//   - stored on 0 and on 2: for the sensor on 0 the same two; for the one on 2 the poll that opens the
//     window of port 3 - one. The ping of a stored sensor and the echo scan are steps of one wire, and
//     take no power on the other.
// The same layouts with the sensor answering its ping are held beside them: its address is not asked
// on the live wire, nothing is heard, and each stored sensor pays one.
void test_s18_a_stored_sensor_silent_to_its_ping_by_chance_costs_one_confirming_scan(void)
{
    struct SilentAtThePing {
        BesideStored layout;
        /** Makes the sensor miss its ping, or nullptr for a sensor that answers it. */
        void (*missesItsPing)(ModuleUnderTest&);
        /** The port of the sensor in question. */
        int pin;
    };
    const SilentAtThePing layouts[] = {
        {{{"stored on 0 silent to its ping", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, -1, -1, -1}}, {2, -1, -1, -1}, 1},
         missesItsPingByChance<0>, 0},
        {{{"stored on 1 silent to its ping", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}}, {-1, 2, -1, -1}, 1},
         missesItsPingByChance<1>, 1},
        {{{"stored on 0 silent to its ping, stored on 2", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, 2, -1, -1}},
          {2, -1, 1, -1}, 1},
         missesItsPingByChance<0>, 0},
        {{{"stored on 0 answering its ping", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, -1, -1, -1}}, {1, -1, -1, -1}, 0},
         nullptr, 0},
        {{{"stored on 0 answering its ping, stored on 2", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {0, 2, -1, -1}},
          {1, -1, 1, -1}, 0},
         nullptr, 0},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);

    std::string failures;
    char item[320];
    for (size_t l = 0; l < count; l++)
    {
        const BesideStored& layout = layouts[l].layout;
        BesideStoredOutcome outcome;
        ModuleUnderTest module;
        plugIn(module, layout.plugged, &outcome.sensors);
        const int storedPin = layouts[l].pin;
        const int askingPin = storedPin ^ 1;
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence, &outcome.sensors, layouts[l].missesItsPing);
        runWatching(module, watch, 120000, &outcome.sensors);

        const bool missedItsPing = layouts[l].missesItsPing != nullptr;
        TEST_ASSERT_EQUAL_MESSAGE(!missedItsPing, answeredItsPing(module, storedPin),
                                  "the ping of the sensor went otherwise than the layout says");
        outcome.recoveryActions = module.acquisition.recoveryActionCount();
        outcome.heard = heardOnTheLiveWire(module);
        unsigned heardAtItsAddress = 0;
        const unsigned askedAtItsAddress =
            cheapProbesAt(module, askingPin, MODBUS_ADDRESS_MIN + storedPin, &heardAtItsAddress);
        uint64_t echoMs = 0;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            if (module.busWindows[i].discovery && module.busWindows[i].heardBeforeTheWireWasIsolated)
            {
                echoMs += module.busWindows[i].lengthMs();
            }
        }
        std::string perPort;
        for (size_t i = 0; i < outcome.sensors.size(); i++)
        {
            const int pin = outcome.sensors[i].pin;
            outcome.offs[pin] = powerOffsByTheDiscovery(module, pin);
            outcome.offsByTheOtherWire[pin] = powerOffsByTheOtherWire(module, pin);
            outcome.offSteps[pin] = stepsThatTookThePowerOf(module, pin);
            outcome.described[pin] = descriptionsOf(module.server, pin);
            outcome.readsBeforeTheWakeUp +=
                sweepReadsBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs());
            snprintf(item, sizeof(item),
                     "; stored on %d: power taken %d time(s) by steps of the discovery [%s], longest gap %lu ms, "
                     "described '%s'",
                     pin, outcome.offs[pin], outcome.offSteps[pin].c_str(), (unsigned long)outcome.sensors[i].worstGapMs,
                     outcome.described[pin].c_str());
            perPort += item;
        }
        printf("  s18: %s - port %d asked 0x%02X on the live wire %u time(s), %u heard and not confirmed (%u in all, "
               "%lu ms of line time); %u adopted; %lu port recovery action(s); pass over at %lu ms%s\n",
               layout.plugged.name, askingPin, MODBUS_ADDRESS_MIN + storedPin, askedAtItsAddress, heardAtItsAddress,
               outcome.heard, (unsigned long)echoMs, (unsigned)module.startupScan.adoptedCount(),
               (unsigned long)outcome.recoveryActions, intoThePass(module, module.recordingStartupScan.finishedAtMs()),
               perPort.c_str());

        if ((missedItsPing ? (askedAtItsAddress == 0 || heardAtItsAddress != 1) : askedAtItsAddress != 0) ||
            module.startupScan.adoptedCount() != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: port %d asked the silent sensor's address %u time(s), %u heard; %u adopted]",
                     layout.plugged.name, askingPin, askedAtItsAddress, heardAtItsAddress,
                     (unsigned)module.startupScan.adoptedCount());
            failures += item;
        }
        for (size_t i = 0; i < outcome.sensors.size(); i++)
        {
            const int pin = outcome.sensors[i].pin;
            if (outcome.offs[pin] != layout.offs[pin] || outcome.offsByTheOtherWire[pin] != 0)
            {
                snprintf(item, sizeof(item), " [%s, stored on %d: power taken %d time(s), %d of them by steps of "
                         "the other wire, the steps that take it account for %d]",
                         layout.plugged.name, pin, outcome.offs[pin], outcome.offsByTheOtherWire[pin],
                         layout.offs[pin]);
                failures += item;
            }
        }
        if (outcome.heard != layout.heard || outcome.recoveryActions != 0)
        {
            snprintf(item, sizeof(item), " [%s: %u cheap probe(s) heard and not confirmed (%u expected), %lu port "
                     "recovery action(s)]",
                     layout.plugged.name, outcome.heard, layout.heard, (unsigned long)outcome.recoveryActions);
            failures += item;
        }
    }
    char message[1200];
    snprintf(message, sizeof(message), "a stored sensor silent to its ping by chance cost another price than "
             "named:%s", failures.c_str());
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), message);
}

// How long a stored sensor goes unread during the restart's pass. A port's window holds the port of
// its pair dark for its whole length, a model scan and an address write take the power of the port
// sharing their wire, and the sweep leaves a port alone until it has had its power for
// PORT_WAKE_UP_MS again - so a stored sensor beside the free ports goes minutes between two readings.
// What bounds it is the rules: a rule whose sensor has not been read for the freshness window is held
// as sensor_unreachable. No stored sensor of these layouts reaches it, and none loses its power to a
// step about the other wire.
void test_s18_the_restarts_pass_reads_every_stored_sensor_within_the_rules_freshness_window(void)
{
    const PluggedIn layouts[] = {
        {"stored on 1, factory on 2", {2, -1, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}},
        {"stored on 1, factory on 0 and 2", {0, 2, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}},
        {"stored on 0, twins on 2 and 3", {2, 3, -1, -1}, {-1, -1, -1, -1}, {0, -1, -1, -1}},
        {"stored on 1, twins on 2 and 3", {2, 3, -1, -1}, {-1, -1, -1, -1}, {1, -1, -1, -1}},
        {"stored on 2, twins on 0 and 1", {0, 1, -1, -1}, {-1, -1, -1, -1}, {2, -1, -1, -1}},
        {"stored on 3, twins on 0 and 1", {0, 1, -1, -1}, {-1, -1, -1, -1}, {3, -1, -1, -1}},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);

    std::string failures;
    char item[240];
    for (size_t l = 0; l < count; l++)
    {
        ModuleUnderTest module;
        std::vector<SweepOfOneSensor> sensors;
        plugIn(module, layouts[l], &sensors);
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
        runWatching(module, watch, 120000, &sensors);

        for (size_t i = 0; i < sensors.size(); i++)
        {
            printf("  s18: %s - the stored sensor on %d: longest gap between readings %lu ms (rules' freshness "
                   "window %lu ms), longest unread while awake %lu ms, power taken %d time(s) by steps of the "
                   "discovery, %d of them by steps of the other wire; pass over at %lu ms\n",
                   layouts[l].name, sensors[i].pin, (unsigned long)sensors[i].worstGapMs,
                   (unsigned long)RULES_FRESHNESS_WINDOW_MS, (unsigned long)sensors[i].worstUnreadWhileAwakeMs,
                   powerOffsByTheDiscovery(module, sensors[i].pin), powerOffsByTheOtherWire(module, sensors[i].pin),
                   intoThePass(module, module.recordingStartupScan.finishedAtMs()));
            const int byTheOtherWire = powerOffsByTheOtherWire(module, sensors[i].pin);
            if (sensors[i].worstGapMs >= RULES_FRESHNESS_WINDOW_MS || byTheOtherWire != 0)
            {
                snprintf(item, sizeof(item), " [%s, stored on %d: %lu ms, %d power-off(s) by steps of the other wire]",
                         layouts[l].name, sensors[i].pin, (unsigned long)sensors[i].worstGapMs, byTheOtherWire);
                failures += item;
            }
        }
    }
    char message[640];
    snprintf(message, sizeof(message), "a stored sensor went unread for the rules' freshness window (%lu ms), or "
             "lost its power to a step about the other wire:%s",
             (unsigned long)RULES_FRESHNESS_WINDOW_MS, failures.c_str());
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), message);
}

// --------------------------------------------------------------------------------------------
// 24. A device a port's window finds at the address a sensor of its wire holds as its own port's is
//     parted from that sensor inside the window.
//
// A stored sensor that answered its ping keeps its address off the live wire of the port sharing its
// wire: that port's window asks it, with the stored sensor's port dark. A second device at that address
// on the windowed port answers there and is adopted, and from then on two devices answer one address of
// one wire - once the window gives the stored sensor's port back, every read of it is a collision, which
// nobody answers. The address phase parts the two rows, the row whose port the address belongs to
// keeping it, but only when the pass is over: minutes in which the stored sensor goes unread with its
// port up, and the port recovery power-cycles a port that is fine. So the move is the window's next
// step, while the stored sensor's port is still dark - the same write, made there instead of at the end.
// --------------------------------------------------------------------------------------------

namespace {

/**
 * @brief Two RY-WS301 at one address on the two ports of one wire: the holder at its own port's
 *        address - stored, and answering its ping, or on a fresh module not yet known - and the second
 *        on the paired port. Another stored sensor may stand on the other wire.
 */
struct TwoAtOneAddress {
    const char* name;
    int holder;
    bool holderStored;
    int second;
    /** A stored sensor on the other wire, or -1. */
    int alsoStored;
    /** Times the steps of the pass take the holder's power, and the other stored sensor's (below). */
    int holderPowerOffs;
    int alsoStoredPowerOffs;
};

/** @brief The index, in the record of the line, of the first discovery step of this kind on this port; -1 if none. */
int indexOfTheStep(const ModuleUnderTest& module, StartupScanStep::Kind kind, int pin)
{
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == kind && w.pin == pin)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/** @brief Power-on transitions of a port from one moment up to, not including, another. */
unsigned powerOnsBetween(const SimRs485Bus& port, uint64_t fromMs, uint64_t untilMs)
{
    unsigned count = 0;
    for (size_t i = 0; i < port.powerOnAtUs.size(); i++)
    {
        if (port.powerOnAtUs[i] >= fromMs * 1000ULL && port.powerOnAtUs[i] < untilMs * 1000ULL)
        {
            count++;
        }
    }
    return count;
}

/** @brief The step of the discovery that adopted the device of this port: the one that answered as its row appeared. */
const char* theStepThatAdopted(const ModuleUnderTest& module, int pin, uint64_t adoptedAtMs)
{
    const char* how = "not found";
    for (size_t i = 0; adoptedAtMs > 0 && i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.pin == pin && w.answered && w.kind != StartupScanStep::WRITE_ADDRESS &&
            w.startedAtMs <= adoptedAtMs && w.endedAtMs + 1000 >= adoptedAtMs)
        {
            how = BackgroundActs::ladderStepName(w.kind);
        }
    }
    return how;
}

/** @brief A module with two RY-WS301 at one address on one wire and its stored rows, every power-on recorded. */
void plugInTwoAtOneAddress(ModuleUnderTest& module, const TwoAtOneAddress& layout, bool secondTakesAnAddress,
                           std::vector<SweepOfOneSensor>& sensors)
{
    eraseTheRegistry(module, theServersCatalog());
    const uint8_t address = (uint8_t)(MODBUS_ADDRESS_MIN + layout.holder);
    portOf(module, layout.holder).addDevice(makeLoggedRyWs301(address));
    SimRs485Bus::Device second = makeLoggedRyWs301(address);
    second.addressIsWritable = secondTakesAnAddress;
    portOf(module, layout.second).addDevice(second);
    if (layout.holderStored)
    {
        module.sensorRepository.save(makeStoredRyWs301(layout.holder));
    }
    if (layout.alsoStored >= 0)
    {
        module.sensorRepository.save(makeStoredRyWs301(layout.alsoStored));
        portOf(module, layout.alsoStored).addDevice(makeLoggedRyWs301((uint8_t)(MODBUS_ADDRESS_MIN + layout.alsoStored)));
    }
    const int watched[2] = {layout.holder, layout.alsoStored};
    for (int i = 0; i < 2; i++)
    {
        if (watched[i] < 0)
        {
            continue;
        }
        SweepOfOneSensor sensor;
        sensor.pin = watched[i];
        sensor.uid = uidOfPin(watched[i]);
        sensors.push_back(sensor);
    }
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        portOf(module, static_cast<int>(i)).recordPowerOnTimes(true);
    }
}

/**
 * @brief The restart's pass and the two minutes after it, as runTheRestartsPassAndItsStructures() and
 *        runWatching() run them, with the holder watched a second time from the moment the second
 *        device's row appears - what the pass costs it from the find on, apart from what the second
 *        device cost it before anything knew it was there - and the port recovery's count at that
 *        moment.
 */
void runTheRestartsPassWatchingFromTheFind(ModuleUnderTest& module, StructureWatch& watch, CadenceRecorder& cadence,
                                           std::vector<SweepOfOneSensor>& sensors, int second,
                                           SweepOfOneSensor& fromTheFind, uint64_t& recoveryActionsAtTheFind,
                                           int secondsAfterThePass = 120)
{
    module.sendTheStructureTheWayTheBoardDoes();
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        portOf(module, static_cast<int>(i)).recordPowerOffTimes(true);
    }
    module.boot();
    module.startArduinoLoopTask(cadence);

    const SweepOfOneSensor& holder = sensors[0];
    const auto oneSecond = [&]() {
        runWatching(module, watch, 1000, &sensors);
        if (fromTheFind.pin < 0 && watch.adoptedAtMs[second] > 0)
        {
            fromTheFind.pin = holder.pin;
            fromTheFind.uid = holder.uid;
            fromTheFind.lastReadMs = module.sensorValueModel.getLastReadTime(holder.uid);
            fromTheFind.lastSampleMs = module.clock.nowMs();
            fromTheFind.readings = 1;
            recoveryActionsAtTheFind = module.acquisition.recoveryActionCount();
        }
        if (fromTheFind.pin >= 0)
        {
            sample(module, fromTheFind);
        }
    };
    for (int s = 0; s < 1200 && module.recordingStartupScan.finishedAtMs() == 0; s++)
    {
        oneSecond();
    }
    TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                             "the pass the restart armed did not end in twenty minutes");
    // What the pass still owes the server, and two minutes more - or as long as asked.
    for (int s = 0; s < 10 + secondsAfterThePass; s++)
    {
        oneSecond();
    }
    cadence.closeAt(module.clock.nowMs());
    assertRunIsSound(module, cadence);
}

} // namespace

// (a) The holder answered its ping - or, on a fresh module, was adopted by its own window - and the
//     second device is found by the window of its port. The write that moves it onto its own port's
//     address is the window's next step: inside the window, the holder's port dark from the poll that
//     opened it to the step that closes it. From the find on one device answers each address, so the
//     holder is read once its port has come up: from that moment never unread with its port awake for
//     longer than a step of the discovery may hold the line, and no port recovery. Every row ends at its
//     own port's address with its device answering there, and the pass still asks no port before it has
//     come up. What the steps of the pass take of the holder's power is held exactly:
//   - stored on a port of its own wire's pair: the poll that opens the second device's window - one;
//   - with a stored sensor on the other wire as well: the same one - the ping of that sensor is a
//     step of the other wire; and that sensor loses its power only to the poll that opens the window
//     of its own pair - one: the holder's ping, the model scan of the window's find and the write that
//     parts the two rows are all steps of the holder's wire;
//   - on a fresh module the holder is adopted by its own window, and loses its power to the poll that
//     opens the second device's window - one.
// Not held, and printed: what the second device costs the holder before its window finds it. The pass
// gives every free port its power when it begins, and from the second device's warm-up to its window
// both devices answer every read of the holder.
void test_s18_a_window_find_at_its_wires_sensors_address_is_parted_from_it_inside_the_window(void)
{
    const TwoAtOneAddress layouts[] = {
        {"stored on 0, a second device at 0x70 on 1", 0, true, 1, -1, 1, 0},
        {"stored on 1, a second device at 0x71 on 0", 1, true, 0, -1, 1, 0},
        {"stored on 2, a second device at 0x72 on 3", 2, true, 3, -1, 1, 0},
        {"stored on 3, a second device at 0x73 on 2", 3, true, 2, -1, 1, 0},
        {"stored on 0 and 2, a second device at 0x70 on 1", 0, true, 1, 2, 1, 1},
        {"a fresh module, devices at 0x70 on 0 and 1", 0, false, 1, -1, 1, 0},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);

    std::string failures;
    char item[640];
    for (size_t l = 0; l < count; l++)
    {
        const TwoAtOneAddress& layout = layouts[l];
        ModuleUnderTest module;
        std::vector<SweepOfOneSensor> sensors;
        plugInTwoAtOneAddress(module, layout, true, sensors);
        StructureWatch watch;
        CadenceRecorder cadence;
        SweepOfOneSensor fromTheFind;
        uint64_t recoveryActionsAtTheFind = 0;
        runTheRestartsPassWatchingFromTheFind(module, watch, cadence, sensors, layout.second, fromTheFind,
                                              recoveryActionsAtTheFind);

        const int h = layout.holder;
        const int o = layout.second;
        const char* how = theStepThatAdopted(module, o, watch.adoptedAtMs[o]);
        const int opened = indexOfTheStep(module, StartupScanStep::PROBE_PRESENCE, o);
        const int moved = indexOfTheStep(module, StartupScanStep::WRITE_ADDRESS, o);
        const int closed = indexOfTheStep(module, StartupScanStep::RELEASE_LINE, o);
        const unsigned writes = stepsOfThePass(module, StartupScanStep::WRITE_ADDRESS, o);
        const unsigned holderLitInTheWindow =
            (opened >= 0 && closed >= 0) ? powerOnsBetween(portOf(module, h), module.busWindows[opened].startedAtMs,
                                                           module.busWindows[closed].startedAtMs)
                                         : 0;
        const SweepOfOneSensor& holder = sensors[0];
        const int holderOffs = powerOffsByTheDiscovery(module, h);
        const int alsoOffs = layout.alsoStored >= 0 ? powerOffsByTheDiscovery(module, layout.alsoStored) : 0;
        const uint64_t recoveryActions = module.acquisition.recoveryActionCount();
        const uint64_t recoveryFromTheFind = recoveryActions - recoveryActionsAtTheFind;
        unsigned framesEarly = 0;
        unsigned sweepEarly = 0;
        std::string firstEarly;
        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            std::string first;
            const unsigned early = framesBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs(), first);
            if (early > 0 && firstEarly.empty())
            {
                firstEarly = first;
            }
            framesEarly += early;
            sweepEarly += sweepReadsBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs());
        }
        const bool holderRight = rowAddressOf(module, h) == (unsigned)(MODBUS_ADDRESS_MIN + h) &&
                                 portOf(module, h).device((uint8_t)(MODBUS_ADDRESS_MIN + h)) != nullptr;
        const bool secondRight = rowAddressOf(module, o) == (unsigned)(MODBUS_ADDRESS_MIN + o) &&
                                 portOf(module, o).device((uint8_t)(MODBUS_ADDRESS_MIN + o)) != nullptr;
        const std::string described = descriptionsOf(module.server, h);
        const size_t shown = described.find('s');

        printf("  s18: %s - port %d adopted at %lu ms by %s, its window open from %lu to %lu ms, moved by the "
               "write %s (%u write(s), over at %lu ms) to 0x%02X; the holder on %d %s its ping: power taken %d "
               "time(s) by steps of the discovery [%s], %u time(s) given back inside the window, longest gap "
               "%lu ms, unread while awake %lu ms over the run and %lu ms from the find on, %u reading(s), "
               "described '%s', at 0x%02X; ",
               layout.name, o, intoThePass(module, watch.adoptedAtMs[o]), how,
               intoThePass(module, opened >= 0 ? module.busWindows[opened].startedAtMs : 0),
               intoThePass(module, closed >= 0 ? module.busWindows[closed].startedAtMs : 0),
               moved < 0 ? "never" : (moved > opened && moved < closed ? "inside the window" : "after the window"),
               writes, intoThePass(module, moved >= 0 ? module.busWindows[moved].endedAtMs : 0),
               rowAddressOf(module, o), h,
               !layout.holderStored ? "had no row for" : (answeredItsPing(module, h) ? "answered" : "missed"),
               holderOffs, stepsThatTookThePowerOf(module, h).c_str(), holderLitInTheWindow,
               (unsigned long)holder.worstGapMs, (unsigned long)holder.worstUnreadWhileAwakeMs,
               (unsigned long)fromTheFind.worstUnreadWhileAwakeMs, holder.readings, described.c_str(),
               rowAddressOf(module, h));
        if (layout.alsoStored >= 0)
        {
            printf("stored on %d: power taken %d time(s) [%s], unread while awake %lu ms; ", layout.alsoStored, alsoOffs,
                   stepsThatTookThePowerOf(module, layout.alsoStored).c_str(),
                   (unsigned long)sensors[1].worstUnreadWhileAwakeMs);
        }
        printf("%lu port recovery action(s), %lu of them from the find on, %u frame(s) and %u sweep read(s) "
               "before the wake-up, %u move(s) refused; pass over at %lu ms\n",
               (unsigned long)recoveryActions, (unsigned long)recoveryFromTheFind, framesEarly, sweepEarly,
               (unsigned)module.startupScan.factoryAddressMoveRefusedCount(),
               intoThePass(module, module.recordingStartupScan.finishedAtMs()));

        if (watch.adoptedAtMs[o] == 0 || std::string(how) != BackgroundActs::ladderStepName(StartupScanStep::PROBE_RANGE) ||
            (layout.holderStored && !answeredItsPing(module, h)))
        {
            snprintf(item, sizeof(item), " [%s: the scenario did not happen - port %d %s by %s, the holder %s its ping]",
                     layout.name, o, watch.adoptedAtMs[o] > 0 ? "adopted" : "not adopted", how,
                     !layout.holderStored ? "had no row for" : (answeredItsPing(module, h) ? "answered" : "missed"));
            failures += item;
            continue;
        }
        if (writes != 1 || !(opened < moved && moved < closed) || holderLitInTheWindow != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: %u write(s) to port %d, the first %s; the holder given its power back %u time(s) "
                     "inside the window]",
                     layout.name, writes, o,
                     moved < 0 ? "never made" : (opened < moved && moved < closed ? "inside its window" : "after its window"),
                     holderLitInTheWindow);
            failures += item;
        }
        if (fromTheFind.worstUnreadWhileAwakeMs > ANY_STEP_OF_THE_DISCOVERY_MS || recoveryFromTheFind != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: from the find on the holder unread while awake for %lu ms (limit %lu), %lu port "
                     "recovery action(s)]",
                     layout.name, (unsigned long)fromTheFind.worstUnreadWhileAwakeMs,
                     (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS, (unsigned long)recoveryFromTheFind);
            failures += item;
        }
        if (!holderRight || !secondRight || module.startupScan.factoryAddressMoveRefusedCount() != 0 ||
            framesEarly != 0 || sweepEarly != 0 ||
            (shown != std::string::npos && described.find('n', shown) != std::string::npos))
        {
            snprintf(item, sizeof(item),
                     " [%s: rows %d:0x%02X%s %d:0x%02X%s, %u move(s) refused; %u frame(s) before the wake-up%s%s, "
                     "%u of the sweep; the holder described '%s']",
                     layout.name, h, rowAddressOf(module, h), holderRight ? "" : "(wrong)", o, rowAddressOf(module, o),
                     secondRight ? "" : "(wrong)", (unsigned)module.startupScan.factoryAddressMoveRefusedCount(),
                     framesEarly, framesEarly > 0 ? ", the first: " : "", firstEarly.c_str(), sweepEarly,
                     described.c_str());
            failures += item;
        }
        if (holderOffs != layout.holderPowerOffs || alsoOffs != layout.alsoStoredPowerOffs)
        {
            snprintf(item, sizeof(item),
                     " [%s: the steps of the pass took the holder's power %d time(s) and the other stored "
                     "sensor's %d, the steps that take them account for %d and %d]",
                     layout.name, holderOffs, alsoOffs, layout.holderPowerOffs, layout.alsoStoredPowerOffs);
            failures += item;
        }
    }
    std::string verdict = "a device a window found at its wire's sensor's address was not parted from it inside "
                          "the window as named:" +
                          failures;
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), verdict.c_str());
}

// (b) The write that parts the two rows refused: the second device does not take the address the module
//     writes it. The refusal is counted as the address phase counts one, and nothing of this pass or of
//     the passes after it writes to that device again. One write, the window's. What becomes of the two
//     devices after the refusal is section 26's (b).
void test_s18_a_window_find_that_refuses_the_parting_write_is_written_to_once(void)
{
    const TwoAtOneAddress layout = {"stored on 0, a second device at 0x70 on 1 that takes no address", 0, true, 1,
                                    -1, 1, 0};
    ModuleUnderTest module;
    std::vector<SweepOfOneSensor> sensors;
    plugInTwoAtOneAddress(module, layout, false, sensors);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
    runWatching(module, watch, 120000, &sensors);

    const char* how = theStepThatAdopted(module, 1, watch.adoptedAtMs[1]);
    const int opened = indexOfTheStep(module, StartupScanStep::PROBE_PRESENCE, 1);
    const int moved = indexOfTheStep(module, StartupScanStep::WRITE_ADDRESS, 1);
    const int closed = indexOfTheStep(module, StartupScanStep::RELEASE_LINE, 1);
    unsigned writes = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        writes += (w.discovery && w.kind == StartupScanStep::WRITE_ADDRESS) ? 1 : 0;
    }
    printf("  s18: %s - port 1 adopted at %lu ms by %s, its window open from %lu to %lu ms; %u address write "
           "step(s) in the pass and the two minutes after it, the first %s, over at %lu ms; %u move(s) refused; "
           "rows 0:0x%02X 1:0x%02X; the stored sensor unread while awake %lu ms, longest gap %lu ms; %lu port "
           "recovery action(s); pass over at %lu ms\n",
           layout.name, intoThePass(module, watch.adoptedAtMs[1]), how,
           intoThePass(module, opened >= 0 ? module.busWindows[opened].startedAtMs : 0),
           intoThePass(module, closed >= 0 ? module.busWindows[closed].startedAtMs : 0), writes,
           moved < 0 ? "never made" : (opened < moved && moved < closed ? "inside the window" : "after the window"),
           intoThePass(module, moved >= 0 ? module.busWindows[moved].endedAtMs : 0),
           (unsigned)module.startupScan.factoryAddressMoveRefusedCount(), rowAddressOf(module, 0),
           rowAddressOf(module, 1), (unsigned long)sensors[0].worstUnreadWhileAwakeMs,
           (unsigned long)sensors[0].worstGapMs, (unsigned long)module.acquisition.recoveryActionCount(),
           intoThePass(module, module.recordingStartupScan.finishedAtMs()));

    TEST_ASSERT_TRUE_MESSAGE(answeredItsPing(module, 0) && watch.adoptedAtMs[1] > 0 &&
                                 std::string(how) == BackgroundActs::ladderStepName(StartupScanStep::PROBE_RANGE),
                             "the scenario did not happen: the stored sensor missed its ping, or port 1's device "
                             "was not found by its window");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, writes, "the device that refused an address was written to more than once");
    TEST_ASSERT_TRUE_MESSAGE(opened < moved && moved < closed, "the one write was not the window's");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)module.startupScan.factoryAddressMoveRefusedCount(),
                                   "the refusal was not kept");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(MODBUS_ADDRESS_MIN, rowAddressOf(module, 0),
                                   "the stored row lost the address that is its port's own");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN) != nullptr,
                             "the stored sensor no longer answers at 0x70");
}

// --------------------------------------------------------------------------------------------
// 25. A model scan and an address write take the power of the ports of their own wire and of no
//     other.
//
// Ports 0 and 1 share one UART and ports 2 and 3 the other (globals.h), so a frame put on a port is
// heard by what is plugged into its pair and by nothing on the other wire. The isolation a model scan
// and an address write make before their frames is the pair's: the other wire keeps its power, its
// sensors their warm-up, and the sweep goes on reading them.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief How many times this port's pin went down between two moments. */
int powerOffsBetween(const ModuleUnderTest& module, int pin, uint64_t fromMs, uint64_t untilMs)
{
    const SimRs485Bus& port = *static_cast<const SimRs485Bus*>(module.ports[static_cast<size_t>(pin)]);
    int count = 0;
    for (size_t o = 0; o < port.powerOffAtUs.size(); o++)
    {
        const uint64_t atMs = port.powerOffAtUs[o] / 1000;
        count += (atMs >= fromMs && atMs < untilMs) ? 1 : 0;
    }
    return count;
}

} // namespace

// (a) The confirming scan of a cheap find on port 0, on the restart's pass, beside a stored sensor on
//     port 2. The scan takes the power of port 1 - the free port sharing port 0's wire, which the pass
//     has given its power to ask it next - and leaves the other wire as it found it: the stored sensor
//     on port 2 and the free port 3 keep their power.
void test_s18_a_confirming_scan_takes_the_power_of_its_wire_and_of_no_other(void)
{
    const PluggedIn layout = {"stored on 2, factory on 0", {0, -1, -1, -1}, {-1, -1, -1, -1}, {2, -1, -1, -1}};
    ModuleUnderTest module;
    std::vector<SweepOfOneSensor> sensors;
    plugIn(module, layout, &sensors);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);

    BusWindow scan;
    bool found = false;
    for (size_t i = 0; i < module.busWindows.size() && !found; i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::PROBE_CANDIDATE && w.pin == 0 && w.answered)
        {
            scan = w;
            found = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "port 0's factory sensor was not found by the cheap rung - there is no "
                                    "confirming scan to look at");
    int offs[MODULE_PIN_COUNT];
    for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
    {
        offs[pin] = powerOffsBetween(module, pin, scan.startedAtMs, scan.endedAtMs);
    }
    printf("  s18: %s - the confirming scan of port 0's find (%lu..%lu ms) took the power of ports 0/1/2/3 "
           "%d/%d/%d/%d time(s); the stored sensor on 2: power taken %d time(s) by steps of the discovery, %d "
           "of them by steps of the other wire, longest gap between readings %lu ms; %lu port recovery "
           "action(s); pass over at %lu ms\n",
           layout.name, (unsigned long)scan.startedAtMs, (unsigned long)scan.endedAtMs, offs[0], offs[1], offs[2],
           offs[3], powerOffsByTheDiscovery(module, 2), powerOffsByTheOtherWire(module, 2),
           (unsigned long)sensors[0].worstGapMs, (unsigned long)module.acquisition.recoveryActionCount(),
           intoThePass(module, module.recordingStartupScan.finishedAtMs()));

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, offs[1], "the confirming scan of port 0 did not take the power of port 1, "
                                              "which shares its wire");
    char message[200];
    snprintf(message, sizeof(message),
             "the confirming scan of port 0 took the power of the stored sensor on port 2, on the other wire, "
             "%d time(s)",
             offs[2]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, offs[2], message);
    snprintf(message, sizeof(message),
             "the confirming scan of port 0 took the power of port 3, on the other wire, %d time(s)", offs[3]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, offs[3], message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, powerOffsByTheOtherWire(module, 2),
                                  "a step about the wire of ports 0 and 1 took the stored sensor's power");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.acquisition.recoveryActionCount(), "the port recovery acted");
}

// (c) A ScanSensors command on port 0 beside a stored sensor on port 3. The command's model scans
//     isolate port 0's wire, and so does the poll of the pass the discovery adds behind it; neither
//     takes the power of port 3, on the other wire. With a stored sensor on port 1 as well, that one -
//     on port 0's wire - loses its power to them, and is neither read before it has come up again nor
//     power-cycled.
void test_s18_a_scan_command_takes_the_power_of_its_wire_and_of_no_other(void)
{
    const PluggedIn layouts[2] = {
        {"stored on 3", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {3, -1, -1, -1}},
        {"stored on 1 and 3", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {1, 3, -1, -1}},
    };
    struct Outcome {
        bool commandRan = false;
        uint64_t commandMs = 0;
        int offs[MODULE_PIN_COUNT] = {0, 0, 0, 0};
        unsigned readsBeforeTheWakeUp[MODULE_PIN_COUNT] = {0, 0, 0, 0};
        uint64_t recoveryActions = 0;
        uint64_t worstGapOfPort3Ms = 0;
    };
    Outcome outcomes[2];

    for (int l = 0; l < 2; l++)
    {
        Outcome& outcome = outcomes[l];
        ModuleUnderTest module;
        std::vector<SweepOfOneSensor> sensors;
        plugIn(module, layouts[l], &sensors);
        CadenceRecorder cadence;
        bootRecordingThePower(module, cadence);
        runUntil(module, 1200000, [&]() { return module.recordingStartupScan.finishedAtMs() > 0; });
        TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                                 "the pass the restart armed did not end in twenty minutes");
        module.scheduler.runForMs(60000);

        const uint64_t commandAtMs = module.clock.nowMs();
        const uint64_t recoveryBefore = module.acquisition.recoveryActionCount();
        // Pin 4 in the command is port 0 of the board (ModuleCommandExecutionUseCase::convertPinFromJson()).
        module.server.pendingModuleCommands.push_back(
            makeServerCommand("scan-free-0", "ScanSensors", "{\"pin\":4}"));
        module.mqttManager.deliver(scanSensorsSignalJson());
        for (int second = 0; second < 300; second++)
        {
            module.scheduler.runForMs(1000);
            for (size_t i = 0; i < sensors.size(); i++)
            {
                sample(module, sensors[i]);
            }
        }
        cadence.closeAt(module.clock.nowMs());
        assertRunIsSound(module, cadence);

        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (!w.discovery && w.startedAtMs >= commandAtMs)
            {
                outcome.commandRan = true;
                outcome.commandMs += w.lengthMs();
            }
        }
        for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
        {
            outcome.offs[pin] = powerOffsBetween(module, pin, commandAtMs, module.clock.nowMs() + 1);
            outcome.readsBeforeTheWakeUp[pin] = sweepReadsBeforeTheWakeUp(module, pin, commandAtMs);
        }
        outcome.recoveryActions = module.acquisition.recoveryActionCount() - recoveryBefore;
        for (size_t i = 0; i < sensors.size(); i++)
        {
            if (sensors[i].pin == 3)
            {
                outcome.worstGapOfPort3Ms = sensors[i].worstGapMs;
            }
        }
        printf("  s18: a ScanSensors on port 0, %s - the command held the line %lu ms; power taken from ports "
               "0/1/2/3 %d/%d/%d/%d time(s) by the command and the pass behind it; sweep reads before the "
               "wake-up %u/%u/%u/%u; %lu port recovery action(s); the sensor on port 3: longest gap between "
               "readings %lu ms\n",
               layouts[l].name, (unsigned long)outcome.commandMs, outcome.offs[0], outcome.offs[1], outcome.offs[2],
               outcome.offs[3], outcome.readsBeforeTheWakeUp[0], outcome.readsBeforeTheWakeUp[1],
               outcome.readsBeforeTheWakeUp[2], outcome.readsBeforeTheWakeUp[3],
               (unsigned long)outcome.recoveryActions, (unsigned long)outcome.worstGapOfPort3Ms);
    }

    char message[256];
    for (int l = 0; l < 2; l++)
    {
        const Outcome& outcome = outcomes[l];
        snprintf(message, sizeof(message), "%s: the ScanSensors command never reached the line", layouts[l].name);
        TEST_ASSERT_TRUE_MESSAGE(outcome.commandRan, message);
        snprintf(message, sizeof(message),
                 "%s: a ScanSensors on port 0 took the power of the stored sensor on port 3, on the other wire, "
                 "%d time(s)",
                 layouts[l].name, outcome.offs[3]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, outcome.offs[3], message);
        snprintf(message, sizeof(message),
                 "%s: the sweep read a stored sensor before its port had come up again %u/%u/%u/%u time(s)",
                 layouts[l].name, outcome.readsBeforeTheWakeUp[0], outcome.readsBeforeTheWakeUp[1],
                 outcome.readsBeforeTheWakeUp[2], outcome.readsBeforeTheWakeUp[3]);
        TEST_ASSERT_TRUE_MESSAGE(outcome.readsBeforeTheWakeUp[1] + outcome.readsBeforeTheWakeUp[3] == 0, message);
        snprintf(message, sizeof(message), "%s: the port recovery acted %lu time(s)", layouts[l].name,
                 (unsigned long)outcome.recoveryActions);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, outcome.recoveryActions, message);
    }
    TEST_ASSERT_TRUE_MESSAGE(outcomes[1].offs[1] > 0,
                             "stored on 1 and 3: the command did not take the power of the stored sensor on port "
                             "0's wire - the isolation of the wire is not there to compare with");
}

// --------------------------------------------------------------------------------------------
// 26. Two devices at one address of one wire, around the window that finds the second of them.
//
// The restart's pass gives every free port its power as it begins, and on the live wire it does not
// ask a free port the address the stored sensor of its pair answered its ping at: the port's window
// asks it, with that sensor's port dark. Until the window, a device on the free port at that address
// is somebody nobody knows about, and from its warm-up on it answers every read of the stored sensor
// along with it - two answers at once, which is no answer. So the port whose cheap rung had to leave
// that address out is taken down after its cheap rung, and has its power and its warm-up back when the
// walk comes to it again. Two things follow the window's find: when the device it finds will not take
// the address that would part it from the stored sensor, it is not kept and its port stays dark; and
// when the address is the found device's own port's - the stored sensor holding it as a foreign one -
// the stored sensor is moved as soon as the window has given its port back, not at the end of the pass.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief Two RY-WS301 at one address on the two ports of one wire; either of them may refuse a new one. */
struct OneAddressTwoDevices {
    const char* name;
    int holder;
    int address;
    bool holderStored;
    bool holderTakesAnAddress;
    int second;
    bool secondTakesAnAddress;
};

void plugInOneAddressTwoDevices(ModuleUnderTest& module, const OneAddressTwoDevices& layout,
                                std::vector<SweepOfOneSensor>& sensors)
{
    eraseTheRegistry(module, theServersCatalog());
    SimRs485Bus::Device holder = makeLoggedRyWs301((uint8_t)layout.address);
    holder.addressIsWritable = layout.holderTakesAnAddress;
    portOf(module, layout.holder).addDevice(holder);
    SimRs485Bus::Device second = makeLoggedRyWs301((uint8_t)layout.address);
    second.addressIsWritable = layout.secondTakesAnAddress;
    portOf(module, layout.second).addDevice(second);
    if (layout.holderStored)
    {
        SensorEntity row = makeStoredRyWs301(layout.holder);
        row.modbusDeviceAddress = layout.address;
        module.sensorRepository.save(row);
    }
    SweepOfOneSensor sensor;
    sensor.pin = layout.holder;
    sensor.uid = uidOfPin(layout.holder);
    sensors.push_back(sensor);
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        portOf(module, static_cast<int>(i)).recordPowerOnTimes(true);
    }
}

/**
 * @brief The index, in the record of the line, of the first - or the last - step that took this one port
 *        down; -1 if none.
 */
int indexOfTheTakeDown(const ModuleUnderTest& module, int pin, bool last = false)
{
    int found = -1;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::RELEASE_FREE_PORTS && w.pin == pin)
        {
            found = static_cast<int>(i);
            if (!last)
            {
                return found;
            }
        }
    }
    return found;
}

/** @brief Frames any step or the sweep put on a port before it had come up, and the sweep's reads among them. */
void framesBeforeTheWakeUpOnAnyPort(ModuleUnderTest& module, unsigned& frames, unsigned& sweepReads, std::string& first)
{
    frames = 0;
    sweepReads = 0;
    for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
    {
        std::string firstOfPort;
        const unsigned early = framesBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs(), firstOfPort);
        if (early > 0 && first.empty())
        {
            first = firstOfPort;
        }
        frames += early;
        sweepReads += sweepReadsBeforeTheWakeUp(module, pin, module.recordingStartupScan.startedAtMs());
    }
}

/** @brief Does the last structure the server took describe the row of this port as a sensor? */
bool theLastStructureDescribes(const SimServer& server, int pin)
{
    return !server.structures.empty() && describesTheSensorOf(server.structures.back().sensors, pin);
}

} // namespace

// (a) A stored sensor that answered its ping, and a second device at its address on the port sharing
//     its wire - on either wire, on either port of it, with a stored sensor on the other wire as well,
//     and on a fresh module whose first device the window of its own port adopts. The second device's
//     port is taken down after its cheap rung, before any window opens, and is given its power once
//     more - and only once - before its own window polls it, long enough before it for what is on it
//     to have come up. Held over the whole of the pass and the two minutes after it: the stored sensor
//     is never unread with its port awake for longer than a step of the discovery may hold the line,
//     and the port recovery does not act. Every row ends at its own port's address with its device
//     answering there, and no port is asked before it has come up. On the fresh module nothing is
//     stored when the cheap rung asks, so nothing is left out and nothing is taken down. The length
//     of the pass is printed.
void test_s18_a_port_that_may_hold_its_wires_sensors_address_is_dark_until_its_window(void)
{
    const TwoAtOneAddress layouts[] = {
        {"stored on 0, a second device at 0x70 on 1", 0, true, 1, -1, 0, 0},
        {"stored on 1, a second device at 0x71 on 0", 1, true, 0, -1, 0, 0},
        {"stored on 2, a second device at 0x72 on 3", 2, true, 3, -1, 0, 0},
        {"stored on 3, a second device at 0x73 on 2", 3, true, 2, -1, 0, 0},
        {"stored on 0 and 2, a second device at 0x70 on 1", 0, true, 1, 2, 0, 0},
        {"a fresh module, devices at 0x70 on 0 and 1", 0, false, 1, -1, 0, 0},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);

    std::string failures;
    char item[640];
    for (size_t l = 0; l < count; l++)
    {
        const TwoAtOneAddress& layout = layouts[l];
        ModuleUnderTest module;
        std::vector<SweepOfOneSensor> sensors;
        plugInTwoAtOneAddress(module, layout, true, sensors);
        StructureWatch watch;
        CadenceRecorder cadence;
        SweepOfOneSensor fromTheFind;
        uint64_t recoveryActionsAtTheFind = 0;
        runTheRestartsPassWatchingFromTheFind(module, watch, cadence, sensors, layout.second, fromTheFind,
                                              recoveryActionsAtTheFind);

        const int h = layout.holder;
        const int o = layout.second;
        const int takenDown = indexOfTheTakeDown(module, o);
        const int polled = indexOfTheStep(module, StartupScanStep::PROBE_PRESENCE, o);
        SimRs485Bus& secondsPort = portOf(module, o);
        const unsigned litBeforeItsWindow =
            (takenDown >= 0 && polled >= 0) ? powerOnsBetween(secondsPort, module.busWindows[takenDown].endedAtMs,
                                                              module.busWindows[polled].startedAtMs)
                                            : 0;
        const uint64_t upAtThePoll = polled >= 0 ? module.busWindows[polled].pinPoweredForMs : 0;
        const SweepOfOneSensor& holder = sensors[0];
        const uint64_t recoveryActions = module.acquisition.recoveryActionCount();
        unsigned framesEarly = 0;
        unsigned sweepEarly = 0;
        std::string firstEarly;
        framesBeforeTheWakeUpOnAnyPort(module, framesEarly, sweepEarly, firstEarly);
        const bool holderRight = rowAddressOf(module, h) == (unsigned)(MODBUS_ADDRESS_MIN + h) &&
                                 portOf(module, h).device((uint8_t)(MODBUS_ADDRESS_MIN + h)) != nullptr;
        const bool secondRight = rowAddressOf(module, o) == (unsigned)(MODBUS_ADDRESS_MIN + o) &&
                                 secondsPort.device((uint8_t)(MODBUS_ADDRESS_MIN + o)) != nullptr;

        printf("  s18: %s - port %d taken down %s at %lu ms, given its power %u time(s) before its window polled it "
               "at %lu ms (up %lu ms), adopted at %lu ms; the holder on %d unread while awake %lu ms over the run "
               "(%lu ms from the find on), longest gap %lu ms, %u reading(s); %lu port recovery action(s); rows "
               "%d:0x%02X %d:0x%02X; %u frame(s) and %u sweep read(s) before the wake-up; pass over at %lu ms\n",
               layout.name, o, takenDown >= 0 ? "after its cheap rung" : "never",
               intoThePass(module, takenDown >= 0 ? module.busWindows[takenDown].startedAtMs : 0), litBeforeItsWindow,
               intoThePass(module, polled >= 0 ? module.busWindows[polled].startedAtMs : 0),
               (unsigned long)upAtThePoll, intoThePass(module, watch.adoptedAtMs[o]), h,
               (unsigned long)holder.worstUnreadWhileAwakeMs, (unsigned long)fromTheFind.worstUnreadWhileAwakeMs,
               (unsigned long)holder.worstGapMs, holder.readings, (unsigned long)recoveryActions, h,
               rowAddressOf(module, h), o, rowAddressOf(module, o), framesEarly, sweepEarly,
               intoThePass(module, module.recordingStartupScan.finishedAtMs()));

        if (watch.adoptedAtMs[o] == 0 || (layout.holderStored && !answeredItsPing(module, h)))
        {
            snprintf(item, sizeof(item), " [%s: the scenario did not happen - port %d %s, the holder %s its ping]",
                     layout.name, o, watch.adoptedAtMs[o] > 0 ? "adopted" : "not adopted",
                     !layout.holderStored ? "had no row for" : (answeredItsPing(module, h) ? "answered" : "missed"));
            failures += item;
            continue;
        }
        const bool takenDownAsNamed =
            layout.holderStored ? (takenDown >= 0 && takenDown < polled && litBeforeItsWindow == 1 &&
                                   upAtThePoll >= StartupSensorScanUseCase::PORT_WAKE_UP_MS)
                                : takenDown < 0;
        if (!takenDownAsNamed)
        {
            snprintf(item, sizeof(item),
                     " [%s: port %d taken down %s, given its power %u time(s) before its window, up %lu ms at the poll]",
                     layout.name, o, takenDown < 0 ? "never" : (takenDown < polled ? "before its window" : "after its window"),
                     litBeforeItsWindow, (unsigned long)upAtThePoll);
            failures += item;
        }
        if (holder.worstUnreadWhileAwakeMs > ANY_STEP_OF_THE_DISCOVERY_MS || recoveryActions != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: the holder unread while awake for %lu ms over the run (limit %lu), %lu port recovery "
                     "action(s)]",
                     layout.name, (unsigned long)holder.worstUnreadWhileAwakeMs,
                     (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS, (unsigned long)recoveryActions);
            failures += item;
        }
        if (!holderRight || !secondRight || framesEarly != 0 || sweepEarly != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: rows %d:0x%02X%s %d:0x%02X%s; %u frame(s) before the wake-up%s%s, %u of the sweep]",
                     layout.name, h, rowAddressOf(module, h), holderRight ? "" : "(wrong)", o, rowAddressOf(module, o),
                     secondRight ? "" : "(wrong)", framesEarly, framesEarly > 0 ? ", the first: " : "",
                     firstEarly.c_str(), sweepEarly);
            failures += item;
        }
    }
    std::string verdict = "a port that may hold the address of its wire's sensor was not dark until its window "
                          "as named:" +
                          failures;
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), verdict.c_str());
}

// (b) The write that would part the two refused - by the second device, on either wire, and on a fresh
//     module; or, at the address that is the second device's own port's, by the stored sensor asked to
//     leave it. Held over the pass and the ten minutes after it: one address write in all, refused and
//     counted; the second device's row is gone and the last structure describes no sensor on its port;
//     its port is taken down and not given its power again by anything - the pass, the passes after
//     it, the sweep; and the stored sensor is read all along - never unread with its port awake for
//     longer than a step of the discovery may hold the line, no port recovery - at the address it had.
//     How often it was read after the pass is printed.
void test_s18_a_device_that_will_not_be_parted_from_its_wires_sensor_leaves_the_wire_to_it(void)
{
    const OneAddressTwoDevices layouts[] = {
        {"stored on 0, a second device at 0x70 on 1 that takes no address", 0, 0x70, true, true, 1, false},
        {"stored on 2, a second device at 0x72 on 3 that takes no address", 2, 0x72, true, true, 3, false},
        {"a fresh module, devices at 0x70 on 0 and 1, the one on 1 takes no address", 0, 0x70, false, true, 1, false},
        {"stored on 0 at 0x71 and taking no address, a second device at 0x71 on 1", 0, 0x71, true, false, 1, true},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);
    const int TEN_MINUTES_S = 600;

    std::string failures;
    char item[640];
    for (size_t l = 0; l < count; l++)
    {
        const OneAddressTwoDevices& layout = layouts[l];
        ModuleUnderTest module;
        std::vector<SweepOfOneSensor> sensors;
        plugInOneAddressTwoDevices(module, layout, sensors);
        StructureWatch watch;
        CadenceRecorder cadence;
        SweepOfOneSensor fromTheFind;
        uint64_t recoveryActionsAtTheFind = 0;
        runTheRestartsPassWatchingFromTheFind(module, watch, cadence, sensors, layout.second, fromTheFind,
                                              recoveryActionsAtTheFind, TEN_MINUTES_S);

        const int h = layout.holder;
        const int o = layout.second;
        unsigned writes = 0;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            writes += (w.discovery && w.kind == StartupScanStep::WRITE_ADDRESS) ? 1 : 0;
        }
        const int takenDown = indexOfTheTakeDown(module, o, true);
        SimRs485Bus& secondsPort = portOf(module, o);
        const unsigned litAfterTheTakeDown =
            takenDown >= 0 ? powerOnsBetween(secondsPort, module.busWindows[takenDown].endedAtMs, (uint64_t)-1 / 1000)
                           : 0;
        const SweepOfOneSensor& holder = sensors[0];
        const uint64_t recoveryActions = module.acquisition.recoveryActionCount();
        const unsigned refused = (unsigned)module.startupScan.factoryAddressMoveRefusedCount();
        const bool holderRight = rowAddressOf(module, h) == (unsigned)layout.address &&
                                 portOf(module, h).device((uint8_t)layout.address) != nullptr;
        const bool described = theLastStructureDescribes(module.server, h);
        const bool secondDescribed = theLastStructureDescribes(module.server, o);

        printf("  s18: %s - port %d adopted at %lu ms, %u address write(s) in the pass and the ten minutes after it, "
               "%u refused; port %d's row %s, taken down at %lu ms (0: never) and given its power %u time(s) after; "
               "the holder on %d unread while awake %lu ms over the run, longest gap %lu ms, %u reading(s), at "
               "0x%02X, the last structure describing it %s and port %d %s; %lu port recovery action(s); pass over "
               "at %lu ms\n",
               layout.name, o, intoThePass(module, watch.adoptedAtMs[o]), writes, refused, o,
               rowAddressOf(module, o) != 0 ? "kept" : "gone",
               intoThePass(module, takenDown >= 0 ? module.busWindows[takenDown].startedAtMs : 0), litAfterTheTakeDown,
               h, (unsigned long)holder.worstUnreadWhileAwakeMs, (unsigned long)holder.worstGapMs, holder.readings,
               rowAddressOf(module, h), described ? "as a sensor" : "otherwise", o,
               secondDescribed ? "as a sensor" : "as no sensor", (unsigned long)recoveryActions,
               intoThePass(module, module.recordingStartupScan.finishedAtMs()));

        if (watch.adoptedAtMs[o] == 0 || (layout.holderStored && !answeredItsPing(module, h)))
        {
            snprintf(item, sizeof(item), " [%s: the scenario did not happen - port %d %s, the holder %s its ping]",
                     layout.name, o, watch.adoptedAtMs[o] > 0 ? "adopted" : "not adopted",
                     !layout.holderStored ? "had no row for" : (answeredItsPing(module, h) ? "answered" : "missed"));
            failures += item;
            continue;
        }
        if (writes != 1 || refused != 1)
        {
            snprintf(item, sizeof(item), " [%s: %u address write(s), %u refused]", layout.name, writes, refused);
            failures += item;
        }
        if (rowAddressOf(module, o) != 0 || secondDescribed || takenDown < 0 || litAfterTheTakeDown != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: port %d's row %s, the last structure describes it %s; the port taken down %s, given its "
                     "power %u time(s) after]",
                     layout.name, o, rowAddressOf(module, o) != 0 ? "kept" : "gone",
                     secondDescribed ? "as a sensor" : "as no sensor", takenDown >= 0 ? "once" : "never",
                     litAfterTheTakeDown);
            failures += item;
        }
        if (holder.worstUnreadWhileAwakeMs > ANY_STEP_OF_THE_DISCOVERY_MS || recoveryActions != 0 || !holderRight ||
            !described)
        {
            snprintf(item, sizeof(item),
                     " [%s: the holder unread while awake for %lu ms (limit %lu), %lu port recovery action(s), its row "
                     "at 0x%02X%s, %s by the last structure]",
                     layout.name, (unsigned long)holder.worstUnreadWhileAwakeMs,
                     (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS, (unsigned long)recoveryActions, rowAddressOf(module, h),
                     holderRight ? "" : "(wrong)", described ? "described" : "not described");
            failures += item;
        }
    }
    std::string verdict = "a device that will not be parted from the sensor of its wire did not leave the wire to "
                          "it as named:" +
                          failures;
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), verdict.c_str());
}

// (c) A stored sensor that answered its ping at the address of the paired port - a foreign address to
//     it - and a second device there on the paired port, which that port's window finds at its own
//     port's address. The address phase keeps the address with the second device and moves the stored
//     sensor; the move is made by the step right after the window's close - not inside the window, whose
//     to keep the stored sensor's port dark, and not at the end of the pass. Held over the pass and the
//     two minutes after it: one address write, the stored sensor's, with its port not lit while the
//     window is open; the stored sensor never unread with its port awake for longer than a step of the
//     discovery may hold the line, and no port recovery; every row at its own port's address with its
//     device answering there; no port asked before it has come up.
void test_s18_a_window_find_at_its_own_ports_address_moves_the_stored_sensor_holding_it_once_the_window_closes(void)
{
    const OneAddressTwoDevices layouts[] = {
        {"stored on 0 at 0x71, a second device at 0x71 on 1", 0, 0x71, true, true, 1, true},
        {"stored on 1 at 0x70, a second device at 0x70 on 0", 1, 0x70, true, true, 0, true},
        {"stored on 2 at 0x73, a second device at 0x73 on 3", 2, 0x73, true, true, 3, true},
        {"stored on 3 at 0x72, a second device at 0x72 on 2", 3, 0x72, true, true, 2, true},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);

    std::string failures;
    char item[640];
    for (size_t l = 0; l < count; l++)
    {
        const OneAddressTwoDevices& layout = layouts[l];
        ModuleUnderTest module;
        std::vector<SweepOfOneSensor> sensors;
        plugInOneAddressTwoDevices(module, layout, sensors);
        StructureWatch watch;
        CadenceRecorder cadence;
        SweepOfOneSensor fromTheFind;
        uint64_t recoveryActionsAtTheFind = 0;
        runTheRestartsPassWatchingFromTheFind(module, watch, cadence, sensors, layout.second, fromTheFind,
                                              recoveryActionsAtTheFind);

        const int h = layout.holder;
        const int o = layout.second;
        const int opened = indexOfTheStep(module, StartupScanStep::PROBE_PRESENCE, o);
        const int closed = indexOfTheStep(module, StartupScanStep::RELEASE_LINE, o);
        const int moved = indexOfTheStep(module, StartupScanStep::WRITE_ADDRESS, h);
        int nextAfterTheClose = -1;
        for (size_t i = (size_t)(closed + 1); closed >= 0 && i < module.busWindows.size() && nextAfterTheClose < 0; i++)
        {
            nextAfterTheClose = module.busWindows[i].discovery ? static_cast<int>(i) : -1;
        }
        unsigned writes = 0;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            writes += (w.discovery && w.kind == StartupScanStep::WRITE_ADDRESS) ? 1 : 0;
        }
        const unsigned holderLitInTheWindow =
            (opened >= 0 && closed >= 0) ? powerOnsBetween(portOf(module, h), module.busWindows[opened].startedAtMs,
                                                           module.busWindows[closed].startedAtMs)
                                         : 0;
        const SweepOfOneSensor& holder = sensors[0];
        const uint64_t recoveryActions = module.acquisition.recoveryActionCount();
        unsigned framesEarly = 0;
        unsigned sweepEarly = 0;
        std::string firstEarly;
        framesBeforeTheWakeUpOnAnyPort(module, framesEarly, sweepEarly, firstEarly);
        const bool holderRight = rowAddressOf(module, h) == (unsigned)(MODBUS_ADDRESS_MIN + h) &&
                                 portOf(module, h).device((uint8_t)(MODBUS_ADDRESS_MIN + h)) != nullptr;
        const bool secondRight = rowAddressOf(module, o) == (unsigned)(MODBUS_ADDRESS_MIN + o) &&
                                 portOf(module, o).device((uint8_t)(MODBUS_ADDRESS_MIN + o)) != nullptr;

        printf("  s18: %s - port %d adopted at %lu ms, its window open from %lu to %lu ms; the stored sensor on %d "
               "moved by the write %s (%u write(s), over at %lu ms), lit %u time(s) inside the window; unread while "
               "awake %lu ms over the run and %lu ms from the find on, longest gap %lu ms, %u reading(s); %lu port "
               "recovery action(s), %lu of them from the find on; rows %d:0x%02X %d:0x%02X; %u frame(s) and %u sweep "
               "read(s) before the wake-up; pass over at %lu ms\n",
               layout.name, o, intoThePass(module, watch.adoptedAtMs[o]),
               intoThePass(module, opened >= 0 ? module.busWindows[opened].startedAtMs : 0),
               intoThePass(module, closed >= 0 ? module.busWindows[closed].startedAtMs : 0), h,
               moved < 0 ? "never made"
                         : (moved == nextAfterTheClose ? "right after the window"
                                                       : (moved < closed ? "inside the window" : "later")),
               writes, intoThePass(module, moved >= 0 ? module.busWindows[moved].endedAtMs : 0), holderLitInTheWindow,
               (unsigned long)holder.worstUnreadWhileAwakeMs, (unsigned long)fromTheFind.worstUnreadWhileAwakeMs,
               (unsigned long)holder.worstGapMs, holder.readings, (unsigned long)recoveryActions,
               (unsigned long)(recoveryActions - recoveryActionsAtTheFind), h, rowAddressOf(module, h), o,
               rowAddressOf(module, o), framesEarly, sweepEarly,
               intoThePass(module, module.recordingStartupScan.finishedAtMs()));

        if (watch.adoptedAtMs[o] == 0 || !answeredItsPing(module, h) ||
            std::string(theStepThatAdopted(module, o, watch.adoptedAtMs[o])) !=
                BackgroundActs::ladderStepName(StartupScanStep::PROBE_RANGE))
        {
            snprintf(item, sizeof(item), " [%s: the scenario did not happen - port %d %s by %s, the holder %s its ping]",
                     layout.name, o, watch.adoptedAtMs[o] > 0 ? "adopted" : "not adopted",
                     theStepThatAdopted(module, o, watch.adoptedAtMs[o]),
                     answeredItsPing(module, h) ? "answered" : "missed");
            failures += item;
            continue;
        }
        if (writes != 1 || moved < 0 || moved != nextAfterTheClose || holderLitInTheWindow != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: %u write(s), the stored sensor's %s; its port lit %u time(s) inside the window]",
                     layout.name, writes,
                     moved < 0 ? "never made"
                               : (moved == nextAfterTheClose ? "right after the window"
                                                             : (moved < closed ? "inside the window" : "later")),
                     holderLitInTheWindow);
            failures += item;
        }
        if (holder.worstUnreadWhileAwakeMs > ANY_STEP_OF_THE_DISCOVERY_MS || recoveryActions != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: the stored sensor unread while awake for %lu ms over the run (limit %lu), %lu port "
                     "recovery action(s)]",
                     layout.name, (unsigned long)holder.worstUnreadWhileAwakeMs,
                     (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS, (unsigned long)recoveryActions);
            failures += item;
        }
        if (!holderRight || !secondRight || framesEarly != 0 || sweepEarly != 0)
        {
            snprintf(item, sizeof(item),
                     " [%s: rows %d:0x%02X%s %d:0x%02X%s; %u frame(s) before the wake-up%s%s, %u of the sweep]",
                     layout.name, h, rowAddressOf(module, h), holderRight ? "" : "(wrong)", o, rowAddressOf(module, o),
                     secondRight ? "" : "(wrong)", framesEarly, framesEarly > 0 ? ", the first: " : "",
                     firstEarly.c_str(), sweepEarly);
            failures += item;
        }
    }
    std::string verdict = "a window find at its own port's address did not move the stored sensor holding it once "
                          "the window closed as named:" +
                          failures;
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), verdict.c_str());
}

namespace {

/** @brief The index of the last address write the line refused; -1 if none. */
int indexOfTheLastRefusedWrite(const ModuleUnderTest& module)
{
    int found = -1;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::WRITE_ADDRESS && !w.answered)
        {
            found = static_cast<int>(i);
        }
    }
    return found;
}

/**
 * @brief The control loop's hand-over of the sweep's list, arriving late once a watched row has left it.
 *
 * SensorService hands the acquisition task the registry's ModBus rows on its 15 s tick, from the
 * control loop; a loop busy at that tick hands them over late, and the sweep goes on reading the list it
 * had. Once a list has carried the watched row, a later list without it is held until a given moment and
 * takes effect then - everything else goes straight through. This moves when a list arrives, nothing
 * about what the firmware does with it, and it says how late that was.
 */
class HandOverThatTakesEffectLate : public ISensorAcquisition {
public:
    HandOverThatTakesEffectLate(ISensorAcquisition& inner, VirtualClock& clock, const std::string& watchedUid)
        : _inner(inner), _clock(clock), _watchedUid(watchedUid)
    {
    }

    /** @brief The moment a held list without the watched row takes effect; until set, it is held. */
    void releaseAt(uint64_t atMs) { _releaseAtMs = atMs; }

    /** @brief When the first list without the watched row was handed over, and when it took effect; 0 if never. */
    uint64_t rowLeftTheRegistryListAtMs() const { return _heldSinceMs; }
    uint64_t rowGoneFromTheListAtMs() const { return _rowGoneAtMs; }

    void setSensors(const std::vector<SensorEntity>& sensors) override
    {
        if (carriesTheRow(sensors))
        {
            _seenTheRow = true;
            _holding = false;
            _inner.setSensors(sensors);
            return;
        }
        if (_seenTheRow && _rowGoneAtMs == 0 && (_releaseAtMs == 0 || _clock.nowMs() < _releaseAtMs))
        {
            if (!_holding && _heldSinceMs == 0)
            {
                _heldSinceMs = _clock.nowMs();
            }
            _held = sensors;
            _holding = true;
            return;
        }
        takeEffect(sensors);
    }

    bool drain(std::vector<AcquiredReading>& out) override
    {
        // The control loop drains every iteration, which is where a held list catches up.
        if (_holding && _releaseAtMs != 0 && _clock.nowMs() >= _releaseAtMs)
        {
            _holding = false;
            takeEffect(_held);
        }
        return _inner.drain(out);
    }

    bool ensureRunning() override { return _inner.ensureRunning(); }
    uint32_t submitBusCommand(const std::string& command, const std::string& options) override
    {
        return _inner.submitBusCommand(command, options);
    }
    uint32_t submitComplexBusCommand(const ComplexCommandWork& work) override { return _inner.submitComplexBusCommand(work); }
    uint32_t submitStartupScanStep(const StartupScanStep& step) override { return _inner.submitStartupScanStep(step); }
    bool drainBusCommandResults(std::vector<BusCommandCompletion>& out) override
    {
        return _inner.drainBusCommandResults(out);
    }

private:
    bool carriesTheRow(const std::vector<SensorEntity>& sensors) const
    {
        for (size_t i = 0; i < sensors.size(); i++)
        {
            if (sensors[i].uid == _watchedUid)
            {
                return true;
            }
        }
        return false;
    }

    void takeEffect(const std::vector<SensorEntity>& sensors)
    {
        if (_seenTheRow && _rowGoneAtMs == 0)
        {
            _rowGoneAtMs = _clock.nowMs();
        }
        _inner.setSensors(sensors);
    }

    ISensorAcquisition& _inner;
    VirtualClock& _clock;
    std::string _watchedUid;
    bool _seenTheRow = false;
    std::vector<SensorEntity> _held;
    bool _holding = false;
    uint64_t _heldSinceMs = 0;
    uint64_t _releaseAtMs = 0;
    uint64_t _rowGoneAtMs = 0;
};

/** @brief The first time this port's power came up at or after this moment; 0 if never. */
uint64_t firstPowerOnFrom(const SimRs485Bus& port, uint64_t fromMs)
{
    for (size_t i = 0; i < port.powerOnAtUs.size(); i++)
    {
        if (port.powerOnAtUs[i] >= fromMs * 1000ULL)
        {
            return port.powerOnAtUs[i] / 1000ULL;
        }
    }
    return 0;
}

/** @brief The index of the first step that took this one port down after the step at `afterIndex`; -1 if none. */
int indexOfTheTakeDownAfter(const ModuleUnderTest& module, int pin, int afterIndex)
{
    for (size_t i = (size_t)(afterIndex + 1); afterIndex >= 0 && i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::RELEASE_FREE_PORTS && w.pin == pin)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

// (d) The same refusal where neither device holds the address of its own port: the window finds the
//     second device and leaves the two to the address phase, which keeps the address with the row the
//     registry lists first and writes the other - and both refuse. The device the pass found is not
//     kept, as in (b): the step right after the refused write takes its port down, before the
//     preparation, and the sweep is kept off that port while its list may still carry the row - the
//     preparation switches an unused port off, and a read of the row through the old list would switch
//     it back on. The sweep comes to the row once right after the refused write, before the take-down,
//     and next a sweep interval later; on this stand the hand-over that drops the row comes first, so the
//     control loop is made late with it (HandOverThatTakesEffectLate) - late enough for the sweep's next
//     visit to find the row with its port taken down, which is checked, not assumed, and how late is
//     printed. Held from the take-down to the end of the pass and ten minutes after
//     it: the port is never lit again, the stored sensor is never unread with its port awake for longer
//     than a step of the discovery may hold the line, no port recovery acts, the stored sensor keeps its
//     address and its device answers there, the last structure describes it and not the device that was
//     let go. Not held, and printed: what the two cost each other before the address phase - nothing parts
//     them earlier.
void test_s18_a_device_the_address_phase_cannot_part_from_its_wires_sensor_leaves_the_wire_to_it(void)
{
    const OneAddressTwoDevices layouts[] = {
        {"stored on 0 at 0x73 and taking no address, a second device at 0x73 on 1 that takes none", 0, 0x73, true,
         false, 1, false},
        {"stored on 0 at 0x72 and taking no address, a second device at 0x72 on 1 that takes none", 0, 0x72, true,
         false, 1, false},
        {"stored on 2 at 0x71 and taking no address, a second device at 0x71 on 3 that takes none", 2, 0x71, true,
         false, 3, false},
        {"stored on 1 at 0x72 and taking no address, a second device at 0x72 on 0 that takes none", 1, 0x72, true,
         false, 0, false},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);
    const int TEN_MINUTES_S = 600;

    std::string failures;
    char item[640];
    for (size_t l = 0; l < count; l++)
    {
        const OneAddressTwoDevices& layout = layouts[l];
        const int h = layout.holder;
        const int o = layout.second;
        ModuleUnderTest module;
        std::vector<SweepOfOneSensor> sensors;
        plugInOneAddressTwoDevices(module, layout, sensors);
        HandOverThatTakesEffectLate handOver(module.acquisition, module.clock, uidOfPin(o));
        module.sensorService.setAcquisition(&handOver);
        StructureWatch watch;
        CadenceRecorder cadence;
        module.sendTheStructureTheWayTheBoardDoes();
        for (size_t i = 0; i < module.ports.size(); i++)
        {
            portOf(module, static_cast<int>(i)).recordPowerOffTimes(true);
        }
        module.boot();
        module.startArduinoLoopTask(cadence);

        SweepOfOneSensor fromTheTakeDown;
        uint64_t recoveryActionsAtTheTakeDown = 0;
        bool late = false;
        const auto oneSecond = [&]() {
            runWatching(module, watch, 1000, &sensors);
            const int refused = indexOfTheLastRefusedWrite(module);
            if (!late && refused >= 0)
            {
                // The sweep comes to the row once right after the refused write; the list without it
                // arrives once the sweep's own cadence has brought it to the row again.
                const uint64_t visitedAtMs = firstPowerOnFrom(portOf(module, o), module.busWindows[refused].endedAtMs);
                const bool noVisitComing = module.clock.nowMs() > module.busWindows[refused].endedAtMs +
                                                                      SensorAcquisitionService::SWEEP_INTERVAL_MS;
                if (visitedAtMs > 0 || noVisitComing)
                {
                    late = true;
                    handOver.releaseAt(visitedAtMs > 0 ? visitedAtMs + SensorAcquisitionService::SWEEP_INTERVAL_MS + 1000
                                                       : module.clock.nowMs());
                }
            }
            if (fromTheTakeDown.pin < 0 &&
                indexOfTheTakeDownAfter(module, o, indexOfTheLastRefusedWrite(module)) >= 0)
            {
                fromTheTakeDown.pin = h;
                fromTheTakeDown.uid = sensors[0].uid;
                fromTheTakeDown.lastReadMs = module.sensorValueModel.getLastReadTime(sensors[0].uid);
                fromTheTakeDown.lastSampleMs = module.clock.nowMs();
                fromTheTakeDown.readings = 1;
                recoveryActionsAtTheTakeDown = module.acquisition.recoveryActionCount();
            }
            if (fromTheTakeDown.pin >= 0)
            {
                sample(module, fromTheTakeDown);
            }
        };
        for (int s = 0; s < 1200 && module.recordingStartupScan.finishedAtMs() == 0; s++)
        {
            oneSecond();
        }
        TEST_ASSERT_TRUE_MESSAGE(module.recordingStartupScan.finishedAtMs() > 0,
                                 "the pass the restart armed did not end in twenty minutes");
        for (int s = 0; s < 10 + TEN_MINUTES_S; s++)
        {
            oneSecond();
        }
        cadence.closeAt(module.clock.nowMs());
        assertRunIsSound(module, cadence);

        const int refusedWrite = indexOfTheLastRefusedWrite(module);
        const int takenDown = indexOfTheTakeDownAfter(module, o, refusedWrite);
        const int prepared = indexOfTheStep(module, StartupScanStep::PREPARE_KNOWN, -1);
        // The sweep's visit to the row just after the refused write lights the dark port; its next visit
        // is a sweep interval later. The list without the row must arrive after that for the sweep to come
        // to the row once more with the port taken down.
        const uint64_t visitedAtMs =
            refusedWrite >= 0 ? firstPowerOnFrom(portOf(module, o), module.busWindows[refusedWrite].endedAtMs) : 0;
        const uint64_t rowGoneAtMs = handOver.rowGoneFromTheListAtMs();
        const uint64_t lateByMs = rowGoneAtMs > handOver.rowLeftTheRegistryListAtMs()
                                      ? rowGoneAtMs - handOver.rowLeftTheRegistryListAtMs()
                                      : 0;
        const bool cameBackToTheRow = visitedAtMs > 0 && rowGoneAtMs > visitedAtMs + SensorAcquisitionService::SWEEP_INTERVAL_MS;
        const unsigned litAfterTheTakeDown =
            takenDown >= 0 ? powerOnsBetween(portOf(module, o), module.busWindows[takenDown].endedAtMs,
                                             (uint64_t)-1 / 1000)
                           : 0;
        const uint64_t recoveryActions = module.acquisition.recoveryActionCount();
        const uint64_t recoveryFromTheTakeDown = recoveryActions - recoveryActionsAtTheTakeDown;
        const bool holderRight = rowAddressOf(module, h) == (unsigned)layout.address &&
                                 portOf(module, h).device((uint8_t)layout.address) != nullptr;
        const bool described = theLastStructureDescribes(module.server, h);
        const bool secondDescribed = theLastStructureDescribes(module.server, o);

        printf("  s18: %s - port %d adopted at %lu ms by %s; the refused write over at %lu ms, port %d taken down "
               "after it at %lu ms (0: never), the preparation at %lu ms; the sweep lit the port at %lu ms and its list "
               "lost the row at %lu ms, a hand-over %lu ms late; the port lit %u time(s) after the take-down; "
               "port %d's row %s; the stored sensor on %d unread while awake %lu ms over the run and %lu ms from the "
               "take-down on, longest gap %lu ms, %u reading(s), at 0x%02X, the last structure describing it %s and "
               "port %d %s; %lu port recovery action(s), %lu of them from the take-down on; pass over at %lu ms\n",
               layout.name, o, intoThePass(module, watch.adoptedAtMs[o]),
               theStepThatAdopted(module, o, watch.adoptedAtMs[o]),
               intoThePass(module, refusedWrite >= 0 ? module.busWindows[refusedWrite].endedAtMs : 0), o,
               intoThePass(module, takenDown >= 0 ? module.busWindows[takenDown].startedAtMs : 0),
               intoThePass(module, prepared >= 0 ? module.busWindows[prepared].startedAtMs : 0),
               intoThePass(module, visitedAtMs), intoThePass(module, rowGoneAtMs), (unsigned long)lateByMs,
               litAfterTheTakeDown, o,
               rowAddressOf(module, o) != 0 ? "kept" : "gone", h, (unsigned long)sensors[0].worstUnreadWhileAwakeMs,
               (unsigned long)fromTheTakeDown.worstUnreadWhileAwakeMs, (unsigned long)sensors[0].worstGapMs,
               sensors[0].readings, rowAddressOf(module, h), described ? "as a sensor" : "otherwise", o,
               secondDescribed ? "as a sensor" : "as no sensor", (unsigned long)recoveryActions,
               (unsigned long)recoveryFromTheTakeDown, intoThePass(module, module.recordingStartupScan.finishedAtMs()));

        if (watch.adoptedAtMs[o] == 0 || !answeredItsPing(module, h) || refusedWrite < 0 || !cameBackToTheRow ||
            std::string(theStepThatAdopted(module, o, watch.adoptedAtMs[o])) !=
                BackgroundActs::ladderStepName(StartupScanStep::PROBE_RANGE))
        {
            snprintf(item, sizeof(item),
                     " [%s: the scenario did not happen - port %d %s by %s, the holder %s its ping, %s refused write, "
                     "the sweep %s to the row with its port taken down]",
                     layout.name, o, watch.adoptedAtMs[o] > 0 ? "adopted" : "not adopted",
                     theStepThatAdopted(module, o, watch.adoptedAtMs[o]),
                     answeredItsPing(module, h) ? "answered" : "missed", refusedWrite >= 0 ? "a" : "no",
                     cameBackToTheRow ? "came back" : "did not come back");
            failures += item;
            continue;
        }
        if (takenDown < 0 || (prepared >= 0 && takenDown > prepared) || litAfterTheTakeDown != 0 ||
            rowAddressOf(module, o) != 0 || secondDescribed)
        {
            snprintf(item, sizeof(item),
                     " [%s: port %d taken down after the refused write %s, lit %u time(s) after; its row %s, the last "
                     "structure describes it %s]",
                     layout.name, o,
                     takenDown < 0 ? "never" : (prepared >= 0 && takenDown > prepared ? "after the preparation" : "before the preparation"),
                     litAfterTheTakeDown, rowAddressOf(module, o) != 0 ? "kept" : "gone",
                     secondDescribed ? "as a sensor" : "as no sensor");
            failures += item;
        }
        if (fromTheTakeDown.worstUnreadWhileAwakeMs > ANY_STEP_OF_THE_DISCOVERY_MS || recoveryFromTheTakeDown != 0 ||
            !holderRight || !described)
        {
            snprintf(item, sizeof(item),
                     " [%s: from the take-down on the stored sensor unread while awake for %lu ms (limit %lu), %lu "
                     "port recovery action(s); its row at 0x%02X%s, %s by the last structure]",
                     layout.name, (unsigned long)fromTheTakeDown.worstUnreadWhileAwakeMs,
                     (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS, (unsigned long)recoveryFromTheTakeDown,
                     rowAddressOf(module, h), holderRight ? "" : "(wrong)", described ? "described" : "not described");
            failures += item;
        }
    }
    std::string verdict = "a device the address phase could not part from the sensor of its wire did not leave the "
                          "wire to it as named:" +
                          failures;
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), verdict.c_str());
}

namespace {

/** @brief A stored sensor, a second device at its address on its pair, and a factory RY-WS12S on the other wire. */
struct CheapFirstLayout {
    const char* name;
    int holder;
    int second;
    int factory;
};

/** @brief The pin of the first cheap probe of the restart's pass; -1 if none. */
int pinOfTheFirstCheapProbe(const ModuleUnderTest& module)
{
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::PROBE_CANDIDATE)
        {
            return w.pin;
        }
    }
    return -1;
}

/** @brief The ports whose windows the restart's pass opened, in the order it opened them. */
std::string theWindowsInOrder(const ModuleUnderTest& module)
{
    std::string order;
    char item[8];
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == StartupScanStep::PROBE_PRESENCE &&
            w.startedAtMs <= module.recordingStartupScan.finishedAtMs())
        {
            snprintf(item, sizeof(item), "%s%d", order.empty() ? "" : ",", w.pin);
            order += item;
        }
    }
    return order;
}

} // namespace

// (e) The port sharing a stored sensor's wire is asked the cheap rung first. Taken down after its cheap
//     rung, it still answered the stored sensor's reads from its warm-up to that moment, and with the
//     ports walked in their own order that moment waits for every free port before it - a factory find
//     among them adds its confirming scan and the warm-up after it. So the cheap rung asks such a port
//     first and the windows keep the order of the ports. Held over the pass and the two minutes after
//     it: the first cheap probe is the second device's port's; the factory sensor is found by the cheap
//     rung, before the first window; the windows are opened in the order of the ports; the stored sensor
//     is never unread with its port awake for longer than a step of the discovery may hold the line, and
//     no port recovery acts; every row ends at its own port's address with its device answering there.
void test_s18_a_port_that_may_hold_its_wires_sensors_address_is_asked_its_cheap_rung_first(void)
{
    const CheapFirstLayout layouts[] = {
        {"stored on 2, a second device at 0x72 on 3, a factory sensor on 1", 2, 3, 1},
        {"stored on 3, a second device at 0x73 on 2, a factory sensor on 0", 3, 2, 0},
        {"stored on 0, a second device at 0x70 on 1, a factory sensor on 3", 0, 1, 3},
    };
    const size_t count = sizeof(layouts) / sizeof(layouts[0]);

    std::string failures;
    char item[640];
    for (size_t l = 0; l < count; l++)
    {
        const CheapFirstLayout& layout = layouts[l];
        const int h = layout.holder;
        const int o = layout.second;
        const int f = layout.factory;
        ModuleUnderTest module;
        eraseTheRegistry(module, theServersCatalog());
        portOf(module, h).addDevice(makeLoggedRyWs301((uint8_t)(MODBUS_ADDRESS_MIN + h)));
        module.sensorRepository.save(makeStoredRyWs301(h));
        portOf(module, o).addDevice(makeLoggedRyWs301((uint8_t)(MODBUS_ADDRESS_MIN + h)));
        portOf(module, f).addDevice(makeFactoryRyWs12s());
        std::vector<SweepOfOneSensor> sensors(1);
        sensors[0].pin = h;
        sensors[0].uid = uidOfPin(h);
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
        runWatching(module, watch, 120000, &sensors);

        const int firstAsked = pinOfTheFirstCheapProbe(module);
        const uint64_t firstWindowMs = firstWindowOpenedAtMs(module);
        const std::string windows = theWindowsInOrder(module);
        bool windowsInOrder = true;
        for (size_t i = 2; i < windows.size(); i += 2)
        {
            windowsInOrder = windowsInOrder && windows[i] > windows[i - 2];
        }
        const SweepOfOneSensor& holder = sensors[0];
        const uint64_t recoveryActions = module.acquisition.recoveryActionCount();
        const bool rowsRight = rowAddressOf(module, h) == (unsigned)(MODBUS_ADDRESS_MIN + h) &&
                               rowAddressOf(module, o) == (unsigned)(MODBUS_ADDRESS_MIN + o) &&
                               rowAddressOf(module, f) == (unsigned)(MODBUS_ADDRESS_MIN + f) &&
                               portOf(module, h).device((uint8_t)(MODBUS_ADDRESS_MIN + h)) != nullptr &&
                               portOf(module, o).device((uint8_t)(MODBUS_ADDRESS_MIN + o)) != nullptr &&
                               portOf(module, f).device((uint8_t)(MODBUS_ADDRESS_MIN + f)) != nullptr;

        printf("  s18: %s - the first cheap probe on port %d, port %d taken down at %lu ms, the factory sensor adopted at "
               "%lu ms, the first window at %lu ms, the windows on %s, port %d adopted at %lu ms; the stored sensor "
               "unread while awake %lu ms, longest gap %lu ms, %u reading(s); %lu port recovery action(s); rows "
               "%d:0x%02X %d:0x%02X %d:0x%02X; pass over at %lu ms\n",
               layout.name, firstAsked, o,
               intoThePass(module, indexOfTheTakeDown(module, o) >= 0
                                       ? module.busWindows[indexOfTheTakeDown(module, o)].startedAtMs
                                       : 0),
               intoThePass(module, watch.adoptedAtMs[f]), intoThePass(module, firstWindowMs), windows.c_str(), o,
               intoThePass(module, watch.adoptedAtMs[o]), (unsigned long)holder.worstUnreadWhileAwakeMs,
               (unsigned long)holder.worstGapMs, holder.readings, (unsigned long)recoveryActions, h, rowAddressOf(module, h),
               o, rowAddressOf(module, o), f, rowAddressOf(module, f),
               intoThePass(module, module.recordingStartupScan.finishedAtMs()));

        if (!answeredItsPing(module, h) || watch.adoptedAtMs[o] == 0 || watch.adoptedAtMs[f] == 0)
        {
            snprintf(item, sizeof(item), " [%s: the scenario did not happen - the holder %s its ping, port %d %s, port %d %s]",
                     layout.name, answeredItsPing(module, h) ? "answered" : "missed", o,
                     watch.adoptedAtMs[o] > 0 ? "adopted" : "not adopted", f,
                     watch.adoptedAtMs[f] > 0 ? "adopted" : "not adopted");
            failures += item;
            continue;
        }
        if (firstAsked != o || indexOfTheTakeDown(module, o) < 0 || watch.adoptedAtMs[f] >= firstWindowMs || !windowsInOrder)
        {
            snprintf(item, sizeof(item),
                     " [%s: the first cheap probe on port %d, port %d %s taken down, the factory sensor adopted %s the "
                     "first window, the windows on %s]",
                     layout.name, firstAsked, o, indexOfTheTakeDown(module, o) >= 0 ? "was" : "was not",
                     watch.adoptedAtMs[f] < firstWindowMs ? "before" : "after", windows.c_str());
            failures += item;
        }
        if (holder.worstUnreadWhileAwakeMs > ANY_STEP_OF_THE_DISCOVERY_MS || recoveryActions != 0 || !rowsRight)
        {
            snprintf(item, sizeof(item),
                     " [%s: the stored sensor unread while awake for %lu ms (limit %lu), %lu port recovery action(s); "
                     "rows %d:0x%02X %d:0x%02X %d:0x%02X]",
                     layout.name, (unsigned long)holder.worstUnreadWhileAwakeMs,
                     (unsigned long)ANY_STEP_OF_THE_DISCOVERY_MS, (unsigned long)recoveryActions, h,
                     rowAddressOf(module, h), o, rowAddressOf(module, o), f, rowAddressOf(module, f));
            failures += item;
        }
    }
    std::string verdict = "a port that may hold its wire's sensor's address was not asked its cheap rung first as "
                          "named:" +
                          failures;
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), verdict.c_str());
}

// --------------------------------------------------------------------------------------------
// The pass over the free ports that comes round for as long as the module runs asks what the restart's
// pass asks first: the module's own range, address by address, every scannable model at each. A device
// the module once gave an address, plugged into a free port later - moved from another port or another
// module - answers there and nowhere else, so a pass that asks the factory addresses alone never hears
// it. What the live wire cannot be asked is the address a stored sensor of the paired port holds: that
// sensor answers it, and the scan that confirms the answer takes its power.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief Frames put on this port, from frame number `fromFrame` on, addressed to this slave. */
size_t framesToSlave(const SimRs485Bus& port, uint8_t slave, size_t fromFrame)
{
    size_t count = 0;
    for (size_t f = fromFrame; f < port.sentFrames.size(); f++)
    {
        if (!port.sentFrames[f].empty() && port.sentFrames[f][0] == slave)
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief The wildcard frames - the ones a presence poll puts on a port - from frame number
 *        `fromFrame` on: slave 0xFF, FC 0x03 at register 0x0100 and FC 0x6E at register 0x0012.
 *
 * Not "everything addressed to 0xFF": a model whose factory address is 255 is asked there too.
 */
size_t wildcardQueryFrames(const SimRs485Bus& port, size_t fromFrame)
{
    size_t count = 0;
    for (size_t f = fromFrame; f < port.sentFrames.size(); f++)
    {
        const std::vector<uint8_t>& frame = port.sentFrames[f];
        if (frame.size() < 4 || frame[0] != 0xFF)
        {
            continue;
        }
        const unsigned reg = (unsigned)((frame[2] << 8) | frame[3]);
        if ((frame[1] == 0x03 && reg == 0x0100) || (frame[1] == 0x6E && reg == 0x0012))
        {
            count++;
        }
    }
    return count;
}

/** @brief Steps of one kind the discovery put on one port from this moment on. */
unsigned stepsOnThePortSince(const ModuleUnderTest& module, StartupScanStep::Kind kind, int pin,
                             uint64_t fromMs)
{
    unsigned count = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (w.discovery && w.kind == kind && w.pin == pin && w.startedAtMs >= fromMs)
        {
            count++;
        }
    }
    return count;
}

/** @brief Times a step of the discovery took this port's power, from this moment on. */
int powerOffsByTheDiscoverySince(const ModuleUnderTest& module, int pin, uint64_t fromMs)
{
    const SimRs485Bus& port = *static_cast<const SimRs485Bus*>(module.ports[static_cast<size_t>(pin)]);
    int count = 0;
    for (size_t o = 0; o < port.powerOffAtUs.size(); o++)
    {
        const uint64_t atMs = port.powerOffAtUs[o] / 1000;
        if (atMs < fromMs)
        {
            continue;
        }
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (w.discovery && atMs >= w.startedAtMs && atMs < w.endedAtMs)
            {
                count++;
                break;
            }
        }
    }
    return count;
}

/** @brief Scannable ModBus models of the server's catalog - the ones the cheap rung asks. */
unsigned scannableModelsOfTheServersCatalog()
{
    unsigned scannable = 0;
    const std::vector<SensorType> catalog = theServersCatalog();
    for (size_t i = 0; i < catalog.size(); i++)
    {
        if (catalog[i].canScan && catalog[i].isSupported && catalog[i].isModbusSensor())
        {
            scannable++;
        }
    }
    return scannable;
}

/** @brief Run until `passes` more passes over the free ports have begun and the last of them is over. */
void runAfewPassesAfterTheRestart(ModuleUnderTest& module, uint32_t passes)
{
    const uint32_t target = module.startupScan.freePortPassCount() + passes;
    const bool ran = runUntil(module, 900000, [&]() {
        return module.startupScan.freePortPassCount() >= target && !module.startupScan.aPassIsInFlight();
    });
    TEST_ASSERT_TRUE_MESSAGE(ran, "the passes after the restart did not come round in a quarter of an hour");
}

/** @brief What one pass over the free ports put on the line. */
struct PassMeasured {
    uint64_t startedAtMs = 0;
    uint64_t endedAtMs = 0;
    unsigned questions[MODULE_PIN_COUNT] = {0, 0, 0, 0};
    unsigned polls = 0;
    unsigned steps = 0;
    uint64_t lineMs = 0;
    size_t frames = 0;
    size_t broadcastFrames = 0;
    size_t writes = 0;
};

/**
 * @brief Run to the end of the first pass over the free ports after the restart's, and say what it did.
 *
 * The pass is the steps of the discovery between the moment it began - the pass counter moves - and
 * the moment it is no longer in flight.
 */
PassMeasured measureTheFirstPassAfterTheRestart(ModuleUnderTest& module)
{
    PassMeasured measured;
    const uint32_t before = module.startupScan.freePortPassCount();
    TEST_ASSERT_TRUE_MESSAGE(runUntil(module, 600000,
                                      [&]() { return module.startupScan.freePortPassCount() > before; }),
                             "no pass came round after the restart's");
    measured.startedAtMs = module.clock.nowMs();
    size_t framesBefore = 0;
    size_t broadcastBefore = 0;
    size_t writesBefore = 0;
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        framesBefore += portOf(module, static_cast<int>(i)).sentFrames.size();
        broadcastBefore += framesToSlave(portOf(module, static_cast<int>(i)), 0x00, 0);
        writesBefore += portOf(module, static_cast<int>(i)).writes.size();
    }
    TEST_ASSERT_TRUE_MESSAGE(runUntil(module, 600000, [&]() { return !module.startupScan.aPassIsInFlight(); }),
                             "the pass after the restart did not end");
    measured.endedAtMs = module.clock.nowMs();
    for (size_t i = 0; i < module.ports.size(); i++)
    {
        measured.frames += portOf(module, static_cast<int>(i)).sentFrames.size();
        measured.broadcastFrames += framesToSlave(portOf(module, static_cast<int>(i)), 0x00, 0);
        measured.writes += portOf(module, static_cast<int>(i)).writes.size();
    }
    measured.frames -= framesBefore;
    measured.broadcastFrames -= broadcastBefore;
    measured.writes -= writesBefore;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (!w.discovery || w.startedAtMs < measured.startedAtMs || w.endedAtMs > measured.endedAtMs)
        {
            continue;
        }
        measured.steps++;
        measured.lineMs += w.lengthMs();
        if (w.kind == StartupScanStep::PROBE_CANDIDATE && w.pin >= 0 && w.pin < MODULE_PIN_COUNT)
        {
            measured.questions[w.pin]++;
        }
        if (w.kind == StartupScanStep::PROBE_PRESENCE)
        {
            measured.polls++;
        }
    }
    return measured;
}

} // namespace

// (a) A device at 0x71 is plugged into free port 2 after the restart's pass is over, and no stored
//     sensor is on its wire. A pass after the restart asks port 2 the range, hears it there, and the
//     device is adopted at the address it answers at - no window, no identification, no write into the
//     port - and the server has a structure with it.
void test_s18_a_sensor_plugged_in_at_an_address_of_the_range_is_found_by_a_pass_after_the_restart(void)
{
    const PluggedIn layout = {"nothing plugged in at the restart", {-1, -1, -1, -1}, {-1, -1, -1, -1},
                              {-1, -1, -1, -1}};
    const int pin = 2;
    const uint8_t address = (uint8_t)(MODBUS_ADDRESS_MIN + 1);
    ModuleUnderTest module;
    plugIn(module, layout);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    TEST_ASSERT_FALSE_MESSAGE((bool)module.sensorRepository.get(uidOfPin(pin).c_str()),
                              "the premise: the port was not empty before the sensor was plugged in");
    const uint64_t pluggedAtMs = module.clock.nowMs();
    const uint32_t passesBefore = module.startupScan.freePortPassCount();
    const uint32_t identifyStepsBefore = module.startupScan.identifyStepCount();
    const size_t writesBefore = portOf(module, pin).writes.size();
    portOf(module, pin).addDevice(makeLoggedRyWs301(address));

    const bool found = runUntil(module, 600000, [&]() {
        return (bool)module.sensorRepository.get(uidOfPin(pin).c_str());
    });
    const uint64_t foundAtMs = module.clock.nowMs();
    // The structure goes out when the pass that found the sensor is over, not when it finds it.
    runUntil(module, 180000, [&]() { return firstStructureDescribing(module.server, pin) > pluggedAtMs; });

    int described = -1;
    const uint64_t shownAtMs = firstStructureDescribing(module.server, pin);
    const bool shown = !module.server.structures.empty() &&
                       describesTheSensorOf(module.server.structures.back().sensors, pin, &described);
    printf("  s18: a sensor plugged into port %d at 0x%02X after the restart - %s %lu ms later, by pass %u "
           "after the restart's; in a structure the server had %lu ms after it was plugged in; %u window(s), "
           "%u identification step(s), %u write(s) into the port; the row at 0x%02X\n",
           pin, address, found ? "found" : "never found", (unsigned long)(foundAtMs - pluggedAtMs),
           (unsigned)(module.startupScan.freePortPassCount() - passesBefore),
           (unsigned long)(shownAtMs > pluggedAtMs ? shownAtMs - pluggedAtMs : 0),
           stepsOnThePortSince(module, StartupScanStep::PROBE_PRESENCE, pin, pluggedAtMs),
           (unsigned)(module.startupScan.identifyStepCount() - identifyStepsBefore),
           (unsigned)(portOf(module, pin).writes.size() - writesBefore), rowAddressOf(module, pin));

    TEST_ASSERT_TRUE_MESSAGE(found, "a sensor at an address of the module's range, plugged into a free port "
                                    "after the restart, was never found by the passes that follow it");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(address, rowAddressOf(module, pin),
                                   "the sensor was not adopted at the address it answers at");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, stepsOnThePortSince(module, StartupScanStep::PROBE_PRESENCE, pin, pluggedAtMs),
                                   "the port the cheap rung named was given a poll all the same");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(identifyStepsBefore, module.startupScan.identifyStepCount(),
                                   "the identification procedure ran for a sensor the range had named");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(writesBefore, (unsigned)portOf(module, pin).writes.size(),
                                   "an address was written into a port whose device a read of the range had named");
    TEST_ASSERT_TRUE_MESSAGE(shownAtMs > pluggedAtMs,
                             "no structure with the sensor reached the server after it was plugged in");
    TEST_ASSERT_TRUE_MESSAGE(shown, "the last structure the server had does not describe the sensor");
    TEST_ASSERT_EQUAL_INT_MESSAGE(address, described,
                                  "the structure does not carry the address the sensor was adopted at");
}

// (b) The same, but the stored sensor of the port sharing the wire holds 0x71: it answers 0x71 on the
//     live wire for itself, and the scan that confirmed an answer would take its power. Free port 2 is
//     walked by every pass and asked the rest of the range - 0x70, 0x72, 0x73 - but never 0x71, and the
//     stored sensor on port 3 loses its power to no step of the discovery.
void test_s18_a_pass_after_the_restart_does_not_ask_the_address_the_stored_sensor_of_its_wire_holds(void)
{
    const int free = 2;
    const int holder = 3;
    const uint8_t held = (uint8_t)(MODBUS_ADDRESS_MIN + 1);
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    SensorEntity row = makeStoredRyWs301(holder);
    row.modbusDeviceAddress = held;
    portOf(module, holder).addDevice(makeLoggedRyWs301(held));
    module.sensorRepository.save(row);
    std::vector<SweepOfOneSensor> sensors(1);
    sensors[0].pin = holder;
    sensors[0].uid = uidOfPin(holder);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
    TEST_ASSERT_TRUE_MESSAGE(answeredItsPing(module, holder),
                             "the premise: the stored sensor answered the ping of the restart's pass");

    const uint64_t fromMs = module.clock.nowMs();
    const size_t framesBefore = portOf(module, free).sentFrames.size();
    const uint32_t passesBefore = module.startupScan.freePortPassCount();
    runAfewPassesAfterTheRestart(module, 3);
    const uint32_t passes = module.startupScan.freePortPassCount() - passesBefore;

    unsigned asked[4];
    for (int a = 0; a < 4; a++)
    {
        asked[a] = (unsigned)framesToSlave(portOf(module, free), (uint8_t)(MODBUS_ADDRESS_MIN + a), framesBefore);
    }
    const int heldOffs = powerOffsByTheDiscoverySince(module, holder, fromMs);
    printf("  s18: the stored sensor of port %d holds 0x%02X - in %u pass(es) after the restart's, port %d was "
           "asked 0x70 %u, 0x71 %u, 0x72 %u, 0x73 %u time(s); the stored sensor lost its power to %d step(s) "
           "of the discovery; %u adopted\n",
           holder, held, (unsigned)passes, free, asked[0], asked[1], asked[2], asked[3], heldOffs,
           (unsigned)module.startupScan.adoptedCount());

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, asked[1],
                                   "a pass after the restart asked a free port, on the live wire, the "
                                   "address the stored sensor of the port sharing its wire holds");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, heldOffs,
                                  "a step of a pass after the restart took the power of the stored sensor "
                                  "of the port sharing the free port's wire");
    TEST_ASSERT_TRUE_MESSAGE(asked[0] >= passes && asked[2] >= passes && asked[3] >= passes,
                             "a pass after the restart did not ask the free port the rest of the range - "
                             "only the held address is left out of it");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, module.startupScan.adoptedCount(),
                                   "something was adopted on a free port with nothing plugged into it");
}

// (c) What a pass after the restart costs a module with four empty free ports: per port and scannable
//     model, the four addresses of the range beside the model's own; frames on the line, the line time
//     and how long the pass lasts are printed.
void test_s18_what_a_pass_after_the_restart_asks_four_empty_free_ports(void)
{
    const PluggedIn layout = {"nothing plugged in", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};
    ModuleUnderTest module;
    plugIn(module, layout);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence);

    const PassMeasured pass = measureTheFirstPassAfterTheRestart(module);
    const unsigned perPort = scannableModelsOfTheServersCatalog() * (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1 + 1);
    printf("  s18: a pass after the restart over four empty free ports - %u step(s), cheap questions %u/%u/%u/%u "
           "(%u per port with the range), %u presence poll(s), %u frame(s) on the line, %u of them broadcast, "
           "%u register write(s); the pass took %lu ms, held the line %lu ms\n",
           pass.steps, pass.questions[0], pass.questions[1], pass.questions[2], pass.questions[3], perPort,
           pass.polls, (unsigned)pass.frames, (unsigned)pass.broadcastFrames, (unsigned)pass.writes,
           (unsigned long)(pass.endedAtMs - pass.startedAtMs), (unsigned long)pass.lineMs);

    for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
    {
        char message[160];
        snprintf(message, sizeof(message),
                 "port %d was asked %u cheap question(s) by a pass after the restart, not the range and the "
                 "factory addresses of every scannable model (%u)",
                 pin, pass.questions[pin], perPort);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(perPort, pass.questions[pin], message);
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.startupScan.adoptedCount(),
                                   "something was adopted on four empty ports");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)pass.broadcastFrames,
                                   "a pass after the restart put a broadcast frame on a free port");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)pass.writes,
                                   "a pass after the restart wrote a register into a free port");
}

// (d) Something that answers the line at an address no model has and the module does not give - 9 - is
//     plugged into free port 3 after the restart's pass, and a stored sensor works on the port sharing
//     the wire. A poll takes that sensor's power, and a pass after the restart does not pay that for a
//     note about a device nobody can name: port 3 is still asked the cheap questions - it is a port a
//     person can plug something into - and the stored sensor loses its power to no step of the
//     discovery.
void test_s18_a_pass_after_the_restart_does_not_poll_a_free_port_whose_wire_carries_a_stored_sensor(void)
{
    const PluggedIn layout = {"a stored sensor on 2", {-1, -1, -1, -1}, {-1, -1, -1, -1}, {2, -1, -1, -1}};
    const int pin = 3;
    ModuleUnderTest module;
    plugIn(module, layout);
    std::vector<SweepOfOneSensor> sensors(1);
    sensors[0].pin = 2;
    sensors[0].uid = uidOfPin(2);
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);

    const uint64_t pluggedAtMs = module.clock.nowMs();
    const size_t framesBefore = portOf(module, pin).sentFrames.size();
    const uint32_t passesBefore = module.startupScan.freePortPassCount();
    portOf(module, pin).addDevice(makeUnidentifiableDevice(9));
    runAfewPassesAfterTheRestart(module, 3);

    const unsigned polls = stepsOnThePortSince(module, StartupScanStep::PROBE_PRESENCE, pin, pluggedAtMs);
    const unsigned wildcardFrames = (unsigned)wildcardQueryFrames(portOf(module, pin), framesBefore);
    const unsigned asked = stepsOnThePortSince(module, StartupScanStep::PROBE_CANDIDATE, pin, pluggedAtMs);
    const int storedOffs = powerOffsByTheDiscoverySince(module, 2, pluggedAtMs);
    printf("  s18: something at address 9 plugged into port %d beside a stored sensor on port 2 - %u pass(es) "
           "since, %u cheap question(s) on the port, %u presence poll(s), %u wildcard frame(s); the stored "
           "sensor lost its power to %d step(s) of the discovery\n",
           pin, (unsigned)(module.startupScan.freePortPassCount() - passesBefore), asked, polls,
           wildcardFrames, storedOffs);

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() - passesBefore >= 3 && asked > 0,
                             "the passes after the restart did not ask the free port anything - there is "
                             "nothing to have held back");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, polls,
                                   "a pass after the restart polled a free port on the wire of a working sensor");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, wildcardFrames,
                                   "a wildcard frame went out on a free port on the wire of a working sensor");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, storedOffs,
                                  "a step of a pass after the restart took the power of the working sensor");
}

// --------------------------------------------------------------------------------------------
// A free port whose wire carries a stored sensor, on a pass after the restart. The cheap rung leaves
// out the addresses that sensor holds, so a device plugged into the free port at one of them is never
// asked: it answers the stored sensor's reads as well for as long as the free port is up, and two
// answers at once are no answer. The pass asks such a port first and takes it down as soon as its cheap
// rung is over, so that it is up for its own questions and no longer.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief What the stored sensor on port 1, at 0x71, went through in a quarter of an hour of passes. */
struct StoredSensorBesideAFreePort {
    uint32_t passes = 0;
    unsigned reads = 0;
    uint64_t worstGapMs = 0;
    unsigned updates = 0;
    unsigned updatesWithItOffline = 0;
    unsigned rowOnTheFreePort = 0;
    uint64_t freePortPoweredMs = 0;
};

/**
 * @brief Run the restart's pass out, give the module a minute, then - for `aSecondDevice` - plug a second
 *        device at the stored sensor's address into free port 0, which shares the stored sensor's wire,
 *        and watch fifteen minutes of passes. A reading is counted when the value model's time for the
 *        stored sensor moves, sampled once a second.
 */
StoredSensorBesideAFreePort watchAStoredSensorBesideAFreePort(bool aSecondDevice)
{
    const int stored = 1;
    const int beside = 0;
    const uint8_t address = (uint8_t)(MODBUS_ADDRESS_MIN + stored);
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    module.sensorRepository.save(makeStoredRyWs301(stored));
    portOf(module, stored).addDevice(makeLoggedRyWs301(address));
    module.sendTheStructureTheWayTheBoardDoes();
    module.boot();
    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    std::vector<uint64_t> readsAtMs;
    uint64_t lastReadMs = 0;
    const auto runSeconds = [&](unsigned seconds) {
        for (unsigned second = 0; second < seconds; second++)
        {
            module.scheduler.runForMs(1000);
            const uint64_t readMs = module.sensorValueModel.getLastReadTime(uidOfPin(stored));
            if (readMs != 0 && readMs != lastReadMs)
            {
                lastReadMs = readMs;
                readsAtMs.push_back(readMs);
            }
        }
    };
    for (int second = 0; second < 1200 && module.recordingStartupScan.finishedAtMs() == 0; second++)
    {
        runSeconds(1);
    }
    runSeconds(60);
    if (aSecondDevice)
    {
        portOf(module, beside).addDevice(makeLoggedRyWs301(address));
    }
    const uint64_t watchedFromMs = module.clock.nowMs();
    const uint32_t passesBefore = module.startupScan.freePortPassCount();
    const size_t readsBefore = readsAtMs.size();
    const uint64_t poweredBefore = portOf(module, beside).poweredMs();
    runSeconds(900);
    const uint64_t watchedUntilMs = module.clock.nowMs();

    StoredSensorBesideAFreePort seen;
    seen.passes = module.startupScan.freePortPassCount() - passesBefore;
    for (size_t i = readsBefore; i < readsAtMs.size(); i++)
    {
        seen.reads++;
        const uint64_t previousMs = (i == readsBefore) ? watchedFromMs : readsAtMs[i - 1];
        if (readsAtMs[i] - previousMs > seen.worstGapMs)
        {
            seen.worstGapMs = readsAtMs[i] - previousMs;
        }
    }
    for (size_t i = 0; i < module.server.updates.size(); i++)
    {
        const SimServer::Update& update = module.server.updates[i];
        if (update.atMs < watchedFromMs || update.atMs > watchedUntilMs)
        {
            continue;
        }
        seen.updates++;
        if (update.onlineCount == 0)
        {
            seen.updatesWithItOffline++;
        }
    }
    seen.rowOnTheFreePort = rowAddressOf(module, beside);
    seen.freePortPoweredMs = portOf(module, beside).poweredMs() - poweredBefore;
    cadence.closeAt(module.clock.nowMs());
    return seen;
}

} // namespace

// (e) A stored RY-WS301 at 0x71 on port 1 works; a minute after the restart's pass a second device at
//     0x71 is plugged into free port 0, which shares its wire. The passes after the restart never ask
//     port 0 the held address, so the second device is not found - and it answers the stored sensor's
//     reads whenever port 0 is up. Port 0 is up for its own questions and no longer: over fifteen
//     minutes at most 7 of the 60 updates the server gets carry the stored sensor as offline, it is read
//     at least 52 times, and no two of its readings are more than 30.3 s apart. Without the second
//     device the same quarter of an hour has it online in every update.
void test_s18_a_second_device_at_a_stored_sensors_address_leaves_the_sensor_read_by_the_passes_after_the_restart(void)
{
    const StoredSensorBesideAFreePort alone = watchAStoredSensorBesideAFreePort(false);
    const StoredSensorBesideAFreePort shared = watchAStoredSensorBesideAFreePort(true);

    printf("  s18: a stored sensor at 0x71 on port 1 for 900000 ms of passes after the restart - alone: %u pass(es), "
           "%u read(s), longest gap %lu ms, %u update(s) of which %u carry it offline; with a second device at "
           "0x71 in free port 0: %u pass(es), %u read(s), longest gap %lu ms, %u update(s) of which %u carry it "
           "offline, port 0 up %lu ms, its row at 0x%02X\n",
           (unsigned)alone.passes, alone.reads, (unsigned long)alone.worstGapMs, alone.updates,
           alone.updatesWithItOffline, (unsigned)shared.passes, shared.reads, (unsigned long)shared.worstGapMs,
           shared.updates, shared.updatesWithItOffline, (unsigned long)shared.freePortPoweredMs,
           shared.rowOnTheFreePort);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, alone.updatesWithItOffline,
                                   "the premise: with nothing in the free port the stored sensor was offline in an update");
    TEST_ASSERT_TRUE_MESSAGE(alone.reads >= 58 && alone.worstGapMs <= 20000,
                             "the premise: with nothing in the free port the stored sensor was not read every sweep");
    TEST_ASSERT_TRUE_MESSAGE(shared.passes >= 10,
                             "the premise: the passes after the restart did not come round the free port");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, shared.rowOnTheFreePort,
                                   "the premise: the second device at the held address was found");
    TEST_ASSERT_TRUE_MESSAGE(shared.updatesWithItOffline <= 7,
                             "a second device at the stored sensor's address in a free port of its wire made "
                             "more than 7 of 60 updates carry the stored sensor as offline");
    TEST_ASSERT_TRUE_MESSAGE(shared.reads >= 52,
                             "a second device at the stored sensor's address in a free port of its wire cost "
                             "the stored sensor more than 7 of its readings in a quarter of an hour");
    TEST_ASSERT_TRUE_MESSAGE(shared.worstGapMs <= 30300,
                             "a second device at the stored sensor's address in a free port of its wire left "
                             "the stored sensor unread for more than 30.3 s");
}

namespace {

/** @brief A stored RY-WS301 on `stored`, and the free port that shares its wire. */
struct AFreePortBesideAStoredSensor {
    int stored;
    int beside;
};

} // namespace

// (f) The free port that shares a stored sensor's wire is asked first by a pass after the restart,
//     whichever port of the wire the sensor is on and wherever the port stands among the free ones, and
//     is taken down as soon as its cheap rung is over - before any other free port is asked. It is asked
//     the range less the address the stored sensor holds, and the factory addresses; it is taken down
//     once and no other free port is taken down at all; no step of the discovery takes the stored
//     sensor's power.
void test_s18_a_pass_after_the_restart_asks_the_port_that_leaves_an_address_out_first_and_takes_it_down(void)
{
    const AFreePortBesideAStoredSensor layouts[] = {{3, 2}, {2, 3}};
    const unsigned models = scannableModelsOfTheServersCatalog();
    // The range less the held address, and every model's own address.
    const unsigned expectedQuestions = models * (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1 - 1 + 1);

    std::string failures;
    char item[512];
    for (size_t l = 0; l < sizeof(layouts) / sizeof(layouts[0]); l++)
    {
        const int stored = layouts[l].stored;
        const int beside = layouts[l].beside;
        const uint8_t held = (uint8_t)(MODBUS_ADDRESS_MIN + stored);
        ModuleUnderTest module;
        eraseTheRegistry(module, theServersCatalog());
        portOf(module, stored).addDevice(makeLoggedRyWs301(held));
        module.sensorRepository.save(makeStoredRyWs301(stored));
        std::vector<SweepOfOneSensor> sensors(1);
        sensors[0].pin = stored;
        sensors[0].uid = uidOfPin(stored);
        StructureWatch watch;
        CadenceRecorder cadence;
        runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
        const PassMeasured pass = measureTheFirstPassAfterTheRestart(module);

        int firstAsked = -1;
        int takenDownAt = -1;
        int lastQuestionToBeside = -1;
        int firstQuestionToAnother = -1;
        unsigned takeDowns = 0;
        unsigned takeDownsOfOthers = 0;
        unsigned askedTheHeldAddress = 0;
        for (size_t i = 0; i < module.busWindows.size(); i++)
        {
            const BusWindow& w = module.busWindows[i];
            if (!w.discovery || w.startedAtMs < pass.startedAtMs || w.endedAtMs > pass.endedAtMs)
            {
                continue;
            }
            if (w.kind == StartupScanStep::PROBE_CANDIDATE)
            {
                firstAsked = firstAsked < 0 ? w.pin : firstAsked;
                if (w.pin == beside)
                {
                    lastQuestionToBeside = static_cast<int>(i);
                    askedTheHeldAddress += (w.address == held) ? 1 : 0;
                }
                else if (firstQuestionToAnother < 0)
                {
                    firstQuestionToAnother = static_cast<int>(i);
                }
            }
            if (w.kind == StartupScanStep::RELEASE_FREE_PORTS && w.pin >= 0)
            {
                takeDowns += (w.pin == beside) ? 1 : 0;
                takeDownsOfOthers += (w.pin != beside) ? 1 : 0;
                takenDownAt = (w.pin == beside && takenDownAt < 0) ? static_cast<int>(i) : takenDownAt;
            }
        }
        const int storedOffs = powerOffsByTheDiscoverySince(module, stored, pass.startedAtMs);
        const int besideOffs = powerOffsByTheDiscoverySince(module, beside, pass.startedAtMs);
        uint64_t besideOffAtMs = 0;
        const SimRs485Bus& besidePort = portOf(module, beside);
        for (size_t o = 0; o < besidePort.powerOffAtUs.size() && besideOffAtMs == 0; o++)
        {
            besideOffAtMs = (besidePort.powerOffAtUs[o] / 1000 >= pass.startedAtMs) ? besidePort.powerOffAtUs[o] / 1000 : 0;
        }
        const bool wentDarkBeforeAnotherPortWasAsked =
            besideOffAtMs > 0 && firstQuestionToAnother >= 0 &&
            besideOffAtMs <= module.busWindows[static_cast<size_t>(firstQuestionToAnother)].startedAtMs;
        printf("  s18: stored sensor on port %d at 0x%02X, port %d free beside it - the pass asked port %d "
               "first, %u question(s) to it (%u at the held address), took it down %u time(s), after its last "
               "question %s and before the first question to another port %s, %u other port(s) taken down; its "
               "power went off %d time(s) in the pass, %s another port was asked, and the discovery took the "
               "stored sensor's power %d time(s); the pass took %lu ms in %u step(s) and held the line %lu ms\n",
               stored, held, beside, firstAsked, pass.questions[beside], askedTheHeldAddress, takeDowns,
               takenDownAt > lastQuestionToBeside ? "yes" : "no",
               takenDownAt >= 0 && takenDownAt < firstQuestionToAnother ? "yes" : "no", takeDownsOfOthers,
               besideOffs, wentDarkBeforeAnotherPortWasAsked ? "before" : "not before", storedOffs,
               (unsigned long)(pass.endedAtMs - pass.startedAtMs), pass.steps, (unsigned long)pass.lineMs);

        if (pass.questions[beside] == 0 || firstQuestionToAnother < 0)
        {
            snprintf(item, sizeof(item), " [stored on %d: the pass did not ask the free ports at all]", stored);
            failures += item;
            continue;
        }
        if (firstAsked != beside)
        {
            snprintf(item, sizeof(item), " [stored on %d: the pass asked port %d first, not port %d]", stored,
                     firstAsked, beside);
            failures += item;
        }
        if (pass.questions[beside] != expectedQuestions || askedTheHeldAddress != 0)
        {
            snprintf(item, sizeof(item),
                     " [stored on %d: port %d was asked %u question(s), %u of them at the held address; %u expected]",
                     stored, beside, pass.questions[beside], askedTheHeldAddress, expectedQuestions);
            failures += item;
        }
        if (takeDowns != 1 || takenDownAt < lastQuestionToBeside || takenDownAt > firstQuestionToAnother ||
            takeDownsOfOthers != 0 || besideOffs != 1 || !wentDarkBeforeAnotherPortWasAsked)
        {
            snprintf(item, sizeof(item),
                     " [stored on %d: port %d taken down %u time(s), %s its last question and %s the first question "
                     "to another port, %u other port(s) taken down, its power taken by %d step(s), %s another port "
                     "was asked]",
                     stored, beside, takeDowns, takenDownAt > lastQuestionToBeside ? "after" : "not after",
                     takenDownAt >= 0 && takenDownAt < firstQuestionToAnother ? "before" : "not before",
                     takeDownsOfOthers, besideOffs, wentDarkBeforeAnotherPortWasAsked ? "dark before" : "not dark before");
            failures += item;
        }
        if (storedOffs != 0)
        {
            snprintf(item, sizeof(item), " [stored on %d: a step of the discovery took the stored sensor's power %d time(s)]",
                     stored, storedOffs);
            failures += item;
        }
    }
    std::string verdict = "a pass after the restart did not ask the port that leaves an address out first and take "
                          "it down as named:" +
                          failures;
    TEST_ASSERT_TRUE_MESSAGE(failures.empty(), verdict.c_str());
}


// (g) The same with exactly one port asked after the port that leaves an address out: stored sensors on ports
//     1 and 3, free ports 0 and 2 - each shares its wire with a stored sensor. The port asked first is taken
//     down by a step of its own before the other one is asked, although that is the only port left to ask;
//     the other one is dark by the end of the pass; no step of the discovery takes a stored sensor's power.
void test_s18_a_port_that_leaves_an_address_out_is_taken_down_when_one_port_is_asked_after_it(void)
{
    ModuleUnderTest module;
    eraseTheRegistry(module, theServersCatalog());
    const int stored[2] = {1, 3};
    std::vector<SweepOfOneSensor> sensors(2);
    for (size_t i = 0; i < 2; i++)
    {
        portOf(module, stored[i]).addDevice(makeLoggedRyWs301((uint8_t)(MODBUS_ADDRESS_MIN + stored[i])));
        module.sensorRepository.save(makeStoredRyWs301(stored[i]));
        sensors[i].pin = stored[i];
        sensors[i].uid = uidOfPin(stored[i]);
    }
    StructureWatch watch;
    CadenceRecorder cadence;
    runTheRestartsPassAndItsStructures(module, watch, cadence, &sensors);
    const PassMeasured pass = measureTheFirstPassAfterTheRestart(module);

    int first = -1;
    int second = -1;
    int firstQuestionToSecond = -1;
    unsigned takeDownsOfFirst = 0;
    for (size_t i = 0; i < module.busWindows.size(); i++)
    {
        const BusWindow& w = module.busWindows[i];
        if (!w.discovery || w.startedAtMs < pass.startedAtMs || w.endedAtMs > pass.endedAtMs)
        {
            continue;
        }
        if (w.kind == StartupScanStep::PROBE_CANDIDATE)
        {
            first = first < 0 ? w.pin : first;
            if (w.pin != first && second < 0)
            {
                second = w.pin;
                firstQuestionToSecond = static_cast<int>(i);
            }
        }
        if (w.kind == StartupScanStep::RELEASE_FREE_PORTS && w.pin >= 0 && w.pin == first &&
            (firstQuestionToSecond < 0))
        {
            takeDownsOfFirst++;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(first >= 0 && second >= 0 && (first == 0 || first == 2) && (second == 0 || second == 2),
                             "the premise: the pass did not ask both free ports");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, takeDownsOfFirst,
                                   "the free port that leaves an address out was not taken down by a step of its own "
                                   "before the last free port was asked");

    uint64_t firstOffAtMs = 0;
    const SimRs485Bus& firstPort = portOf(module, first);
    for (size_t o = 0; o < firstPort.powerOffAtUs.size() && firstOffAtMs == 0; o++)
    {
        firstOffAtMs = (firstPort.powerOffAtUs[o] / 1000 >= pass.startedAtMs) ? firstPort.powerOffAtUs[o] / 1000 : 0;
    }
    TEST_ASSERT_TRUE_MESSAGE(firstOffAtMs > 0 && firstQuestionToSecond >= 0 &&
                                 firstOffAtMs <= module.busWindows[static_cast<size_t>(firstQuestionToSecond)].startedAtMs,
                             "the free port that leaves an address out was still up when the last free port was asked");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, powerOffsByTheDiscoverySince(module, second, pass.startedAtMs),
                                  "the last free port was not taken down once by the end of the pass");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, powerOffsByTheDiscoverySince(module, 1, pass.startedAtMs) +
                                         powerOffsByTheDiscoverySince(module, 3, pass.startedAtMs),
                                  "a step of the discovery took a stored sensor's power");
}


extern "C" int run_s18_boot_without_scan_tests(void)
{
    RUN_TEST(test_s18_the_known_sensors_are_reported_while_the_discovery_is_still_running);
    RUN_TEST(test_s18_the_loop_stays_bounded_while_the_discovery_walks_the_free_ports);
    RUN_TEST(test_s18_a_command_and_the_discovery_take_turns_on_the_line);
    RUN_TEST(test_s18_a_sensor_found_during_a_sweep_joins_the_registry_and_gets_polled);
    RUN_TEST(test_s18_a_no_scan_boot_puts_nothing_of_the_discovery_on_the_line);
    RUN_TEST(test_s18_the_free_ports_cost_their_windows_and_no_more);
    RUN_TEST(test_s18_a_free_port_on_the_wire_of_a_working_sensor_is_honestly_empty);
    RUN_TEST(test_s18_the_free_ports_are_dark_between_two_passes);
    RUN_TEST(test_s18_a_sensor_plugged_in_after_the_boot_is_found_without_a_restart);
    RUN_TEST(test_s18_a_sensor_that_takes_time_to_come_up_is_found_too);
    RUN_TEST(test_s18_a_port_that_matches_no_model_gets_one_identification_pass);
    RUN_TEST(test_s18_a_sensor_put_where_the_module_gave_up_is_found_without_a_command);
    RUN_TEST(test_s18_a_sensor_slower_than_a_pass_is_found_by_the_command);
    RUN_TEST(test_s18_a_scan_command_the_module_refuses_asks_the_line_for_nothing);
    RUN_TEST(test_s18_the_worst_case_costs_no_more_than_the_procedure_it_rations);
    RUN_TEST(test_s18_a_free_port_pays_once_for_an_answer_that_came_from_its_wire);
    RUN_TEST(test_s18_the_sensor_a_free_port_keeps_hearing_is_moved_off_the_factory_address);
    RUN_TEST(test_s18_a_hand_asked_scan_finds_a_twin_the_note_holds_off);
    RUN_TEST(test_s18_a_row_that_kept_the_factory_address_is_moved_off_it_with_nothing_free);
    RUN_TEST(test_s18_a_sensor_added_from_the_app_lands_on_an_address_of_the_module);
    RUN_TEST(test_s18_a_row_whose_device_was_not_there_at_the_restart_is_moved_when_it_arrives);
    RUN_TEST(test_s18_a_pass_that_adopts_a_sensor_leaves_the_refusing_device_alone);
    RUN_TEST(test_s18_a_sensor_the_module_once_addressed_is_found_where_it_answers);
    RUN_TEST(test_s18_two_sensors_at_one_address_of_one_wire_are_both_found_and_parted);
    RUN_TEST(test_s18_a_device_on_an_address_nobody_knows_takes_the_one_its_model_writes);
    RUN_TEST(test_s18_a_device_that_takes_only_the_blind_write_is_found_too);
    RUN_TEST(test_s18_what_the_window_costs_a_port_nothing_is_plugged_into);
    RUN_TEST(test_s18_a_pass_after_the_restart_asks_the_range_but_neither_writes_nor_opens_a_window);
    RUN_TEST(test_s18_the_sensors_the_module_reads_come_through_the_restarts_windows);
    RUN_TEST(test_s18_a_command_sent_during_a_window_waits_for_it_to_close);
    RUN_TEST(test_s18_a_fresh_module_begins_its_line_before_the_first_frame_and_finds_its_sensor);
    RUN_TEST(test_s18_a_stored_sensor_answers_the_first_read_of_the_first_sweep);
    RUN_TEST(test_s18_a_stored_sensor_that_needs_its_warm_up_is_read_before_the_discovery_starts);
    RUN_TEST(test_s18_a_raw_read_before_any_scan_reaches_the_device);
    RUN_TEST(test_s18_a_no_scan_boot_and_a_board_test_boot_begin_the_line_too);
    RUN_TEST(test_s18_a_factory_sensor_on_the_last_port_is_found_and_shown_before_the_windows);
    RUN_TEST(test_s18_two_sensors_found_cheaply_go_out_one_structure_at_a_time);
    RUN_TEST(test_s18_a_port_the_cheap_rung_named_gets_no_window_and_no_more_darkness);
    RUN_TEST(test_s18_cheap_finds_in_a_row_take_the_stored_sensors_power_no_more_and_show_no_sensor_as_empty);
    RUN_TEST(test_s18_a_structure_sent_mid_pass_says_nothing_of_the_ports_the_pass_has_not_reached);
    RUN_TEST(test_s18_twins_at_their_factory_address_on_one_wire_are_both_found);
    RUN_TEST(test_s18_a_factory_sensor_two_ports_after_a_cheap_find_is_found_by_the_cheap_rung);
    RUN_TEST(test_s18_a_window_after_a_cheap_find_asks_a_port_with_its_power_up);
    RUN_TEST(test_s18_the_restarts_pass_asks_no_port_before_it_has_come_up);
    RUN_TEST(test_s18_when_the_sensor_a_restart_finds_reaches_the_server);
    RUN_TEST(test_s18_a_sensor_silent_with_its_port_up_is_still_power_cycled);
    RUN_TEST(test_s18_a_silence_that_began_before_the_dark_is_not_finished_by_it);
    RUN_TEST(test_s18_a_scan_command_leaves_the_stored_sensors_nothing_to_recover_from);
    RUN_TEST(test_s18_a_raw_read_into_a_port_a_command_left_dark_waits_for_it_to_come_up);
    RUN_TEST(test_s18_a_ping_that_gives_a_silent_sensors_port_back_does_not_finish_its_silence);
    RUN_TEST(test_s18_the_first_telemetry_of_a_boot_carries_no_record_the_module_has_not_read);
    RUN_TEST(test_s18_a_sensor_the_discovery_adopts_is_not_reported_before_its_first_reading);
    RUN_TEST(test_s18_a_sensor_at_an_address_of_the_range_is_found_by_the_cheap_rung);
    RUN_TEST(test_s18_a_sensor_of_the_range_and_a_factory_sensor_on_one_wire_are_both_found_by_the_cheap_rung);
    RUN_TEST(test_s18_an_address_the_sensor_of_the_paired_port_holds_is_not_asked_on_the_live_wire);
    RUN_TEST(test_s18_the_second_of_two_twins_on_a_wire_is_found_by_the_cheap_rung_asked_again);
    RUN_TEST(test_s18_a_sensor_moved_to_the_port_sharing_its_wire_is_found_and_its_old_row_kept);
    RUN_TEST(test_s18_a_sensor_moved_beside_a_stored_sensor_is_parted_from_its_old_row_at_no_extra_cost);
    RUN_TEST(test_s18_a_sensor_of_the_range_on_the_paired_port_costs_one_confirming_scan);
    RUN_TEST(test_s18_what_the_range_on_the_cheap_rung_costs_a_module_with_nothing_plugged_in);
    RUN_TEST(test_s18_an_echo_of_the_range_beside_a_stored_sensor_on_the_other_wire_costs_it_nothing);
    RUN_TEST(test_s18_a_port_asked_again_beside_a_find_at_a_factory_address_pays_one_echo);
    RUN_TEST(test_s18_a_stored_sensor_silent_to_its_ping_by_chance_costs_one_confirming_scan);
    RUN_TEST(test_s18_the_restarts_pass_reads_every_stored_sensor_within_the_rules_freshness_window);
    RUN_TEST(test_s18_a_window_find_at_its_wires_sensors_address_is_parted_from_it_inside_the_window);
    RUN_TEST(test_s18_a_window_find_that_refuses_the_parting_write_is_written_to_once);
    RUN_TEST(test_s18_a_confirming_scan_takes_the_power_of_its_wire_and_of_no_other);
    RUN_TEST(test_s18_a_scan_command_takes_the_power_of_its_wire_and_of_no_other);
    RUN_TEST(test_s18_a_port_that_may_hold_its_wires_sensors_address_is_dark_until_its_window);
    RUN_TEST(test_s18_a_device_that_will_not_be_parted_from_its_wires_sensor_leaves_the_wire_to_it);
    RUN_TEST(test_s18_a_window_find_at_its_own_ports_address_moves_the_stored_sensor_holding_it_once_the_window_closes);
    RUN_TEST(test_s18_a_device_the_address_phase_cannot_part_from_its_wires_sensor_leaves_the_wire_to_it);
    RUN_TEST(test_s18_a_port_that_may_hold_its_wires_sensors_address_is_asked_its_cheap_rung_first);
    RUN_TEST(test_s18_a_sensor_plugged_in_at_an_address_of_the_range_is_found_by_a_pass_after_the_restart);
    RUN_TEST(test_s18_a_pass_after_the_restart_does_not_ask_the_address_the_stored_sensor_of_its_wire_holds);
    RUN_TEST(test_s18_what_a_pass_after_the_restart_asks_four_empty_free_ports);
    RUN_TEST(test_s18_a_pass_after_the_restart_does_not_poll_a_free_port_whose_wire_carries_a_stored_sensor);
    RUN_TEST(test_s18_a_second_device_at_a_stored_sensors_address_leaves_the_sensor_read_by_the_passes_after_the_restart);
    RUN_TEST(test_s18_a_pass_after_the_restart_asks_the_port_that_leaves_an_address_out_first_and_takes_it_down);
    RUN_TEST(test_s18_a_port_that_leaves_an_address_out_is_taken_down_when_one_port_is_asked_after_it);
    return 0;
}
