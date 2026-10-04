// A ScanSensors command on a module without a sensor type catalog fetches one, stores it, and scans
// from it. Storing it is the same stream of file operations the catalog synchronization hands to
// the storage task - one chunk per sensor type into a temporary file, then the renames that
// publish it - and it has to go the same way: not on the control loop, and never while another
// write of the same file is under way.
//
// What is under test is production code: the real LoopServiceConfigureDone drives the real
// CommandService, Outbox and NetworkCommandRunner with their tasks, the real
// ModuleCommandExecutionUseCase and SensorAcquisitionService over the real SensorReader and
// ModbusSensorReader on a simulated RS-485 line, the real SyncSensorTypesUseCase, the real
// SensorTypeCatalogWriter with its task, and the real SensorTypeRepository and SpiffsSensorTypeDao
// with the product's own replacement sequence under it. Replaced are the outside world - the
// server, the line, the flash device - and time. Every scenario starts from a fresh module: nothing
// on flash, no sensor in the registry - the only state in which a scan fetches a catalog at all.
//
// (a) The command stores the catalog it fetched: no file operation of that write on the control
//     loop, all of them on the storage task, and the loop keeps its period meanwhile. The scan is
//     set waiting for the write once, and held until it lands.
// (b) The command's fetch comes back while the synchronization's own write of the catalog is open.
//     The flash stand-in records any second writer of the file while a write is open; there must
//     be none, and the file left behind must be the whole catalog.
// (c) What the command reports and saves does not depend on who stored the catalog: the sensor it
//     found, moved to the address of its port, under the uid of the pin, in the structure - and the
//     catalog on flash byte for byte what the DAO stores for the server's catalog.
// (d) A write that does not land - refused by the file system, or with no task that could make it -
//     ends the command in an Error that says so, and leaves nothing waiting.
// (e) The synchronous composition, which has no control loop to collect a write, still stores the
//     catalog and finishes.
// (f) The synchronization refuses a fetched catalog while its own write is in flight, without
//     touching the file and without recording a failure.
// (g) The mirror of (b): the synchronization's own answer comes back while the command's write is
//     in flight - as it opens, and as it publishes - and no task but the storage task reads or
//     touches the catalog's files meanwhile.
// (h) While the synchronization's own write is in flight, the catalog is answered from what the
//     module holds and not read from storage.
//
// ### Reproducing the red
//
// (a) and (b) are red with the command storing the catalog itself, which is what it does with no
// synchronization to hand it to: remove the setSyncSensorTypesUseCase() line from ModuleUnderTest's
// constructor. (a) then counts every file operation of the write on the control loop and a loop
// step as long as the write; (b) records the control loop opening the catalog for writing while
// the storage task is writing it.

#include <unity.h>

#include "constants.h"

#include "Application/Services/CommandService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Mock/Application/MockClearModuleDataService.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
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

// The claim of (a): with the catalog being stored, the worst control-loop step is one loop period.
const uint32_t MAX_ACCEPTED_STEP_MS = CONTROL_PERIOD_MS;

// What one write operation costs on the simulated flash - the figure the synchronization's own
// test charges. Nothing in SPIFFS promises a number; this one makes a catalog write two orders of
// magnitude longer than a loop period, which is all the claims here need.
const uint32_t FLASH_OP_MS = 5;

// The catalog a live account has.
const size_t CATALOG_TYPES = 14;

// How long a scenario is given: the list fetch, the catalog fetch, the write, the scan, the report.
const uint32_t SCENARIO_MS = 120000;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "scan-module";
const char* const CATALOG_FILE = "/data/sensorTypes.json";
const char* const CATALOG_TEMP_FILE = "/data/sensorTypes.json.tmp";
const char* const OUTBOX_TASK_NAME = "outbox-net";

// The address register of the model the scan finds, and the command it is written with.
const uint16_t ADDRESS_STORE_REGISTER = 0x0100;
const uint8_t WRITE_SINGLE_REGISTER = 6;

// "pin":4 in the command's options is port 0 (ModuleCommandExecutionUseCase::convertPinFromJson()).
const char* const SCAN_OPTIONS = "{\"pin\":4}";
const int SCANNED_PORT = 0;
const char* const SCAN_COMMAND_UID = "scan-1";

// --- the outside world ------------------------------------------------------------------------

/**
 * @brief The flash device under the DAO: an in-memory file system that takes time and watches who
 *        writes the catalog.
 *
 * What it substitutes is hardware: the streaming write, the temporary file and the renames that
 * publish it are the product's own code above it. Every write operation costs FLASH_OP_MS and is
 * charged to the task that made it.
 *
 * A write of the catalog is open from openForRewrite() to the end of its commit() or abort(). A
 * second openForRewrite() of the catalog while one is open, or a rename or removal of one of the
 * catalog's files by another task meanwhile, is a second writer of the file, and is recorded. It
 * also does here what it does on flash: both writers stream into the one `<file>.tmp`, and the
 * renames of one take the file from under the other.
 */
class SimCatalogFlash : public MockSpiffsAdapter {
public:
    SimCatalogFlash(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock) {}

    /** Charging starts when a test says so, so that staging the initial state is free. */
    void startCharging() { _opCostMs = FLASH_OP_MS; }

    uint64_t writeOpsTotal() const { return _writeOps; }

    uint64_t writeOpsBy(const char* taskName) const
    {
        std::map<std::string, uint64_t>::const_iterator it = _writeOpsByTask.find(taskName);
        return (it == _writeOpsByTask.end()) ? 0 : it->second;
    }

    /** How many writes of the catalog were opened, and by which task. */
    uint32_t catalogWritesOpened() const { return _catalogWritesOpened; }
    uint32_t catalogWritesOpenedBy(const char* taskName) const
    {
        std::map<std::string, uint32_t>::const_iterator it = _catalogWritesOpenedByTask.find(taskName);
        return (it == _catalogWritesOpenedByTask.end()) ? 0 : it->second;
    }

    /** Whether a write of the catalog is open right now, whoever opened it. */
    bool catalogWriteOpen() const { return !_openCatalogWriters.empty(); }

    /** Whether a write of the catalog opened by this task is open right now. */
    bool catalogWriteOpenBy(const char* taskName) const
    {
        for (size_t i = 0; i < _openCatalogWriters.size(); i++)
        {
            if (_openCatalogWriters[i] == taskName)
            {
                return true;
            }
        }
        return false;
    }

    /** How many times the catalog took its own name, and when it last did (0: never). */
    uint32_t catalogPublishes() const { return _catalogPublishes; }
    uint64_t catalogPublishedAtMs() const { return _catalogPublishedAtMs; }

    /** Second writers of the catalog seen while a write of it was open, and the first one. */
    uint32_t secondWriterCount() const { return _secondWriters; }
    const std::string& firstSecondWriter() const { return _firstSecondWriter; }

    /** Reads of the catalog's files; and those made by another task while a write was open. */
    uint32_t catalogReads() const { return _catalogReads; }
    uint32_t foreignReadsDuringWrite() const { return _foreignReadsDuringWrite; }

    std::string readSPIFFSstr(const char* filePath) override
    {
        if (isCatalogFile(filePath))
        {
            _catalogReads++;
            if (!_openCatalogWriters.empty() && !catalogWriteOpenBy(_scheduler.currentTaskName()))
            {
                _foreignReadsDuringWrite++;
            }
        }
        return MockSpiffsAdapter::readSPIFFSstr(filePath);
    }

    std::unique_ptr<ISpiffsFileWriter> openForRewrite(const char* filePath) override
    {
        const bool catalog = isCatalogFile(filePath);
        if (catalog)
        {
            if (!_openCatalogWriters.empty())
            {
                noteSecondWriter(std::string(_scheduler.currentTaskName()) +
                                 " opened the catalog for writing while " + _openCatalogWriters[0] +
                                 " was writing it");
            }
            _openCatalogWriters.push_back(_scheduler.currentTaskName());
            _catalogWritesOpened++;
            _catalogWritesOpenedByTask[_scheduler.currentTaskName()]++;
        }

        chargeWriteOp();
        std::unique_ptr<ISpiffsFileWriter> inner = MockSpiffsAdapter::openForRewrite(filePath);
        if (!inner)
        {
            if (catalog)
            {
                closeCatalogWriter(_scheduler.currentTaskName());
            }
            return inner;
        }
        return std::unique_ptr<ISpiffsFileWriter>(
            new TimedWriter(*this, std::move(inner), catalog, _scheduler.currentTaskName()));
    }

    bool renameFile(const char* fromPath, const char* toPath) override
    {
        noteForeignTouch("renamed", fromPath);
        chargeWriteOp();
        const bool renamed = MockSpiffsAdapter::renameFile(fromPath, toPath);
        if (renamed && toPath != nullptr && std::string(toPath) == CATALOG_FILE)
        {
            _catalogPublishes++;
            _catalogPublishedAtMs = _clock.nowMs();
        }
        return renamed;
    }

    bool deleteFile(const char* filePath) override
    {
        noteForeignTouch("deleted", filePath);
        chargeWriteOp();
        return MockSpiffsAdapter::deleteFile(filePath);
    }

    void removeFile(const char* filePath) override
    {
        noteForeignTouch("removed", filePath);
        chargeWriteOp();
        MockSpiffsAdapter::removeFile(filePath);
    }

private:
    /** Delegates the write, charges the time and the task for it, and closes the open window. */
    class TimedWriter : public ISpiffsFileWriter {
    public:
        TimedWriter(SimCatalogFlash& flash, std::unique_ptr<ISpiffsFileWriter> inner, bool catalog,
                    const std::string& taskName)
            : _flash(flash), _inner(std::move(inner)), _catalog(catalog), _taskName(taskName) {}

        ~TimedWriter() override { close(); }

        bool write(const char* chunk) override
        {
            _flash.chargeWriteOp();
            return _inner->write(chunk);
        }

        bool commit() override
        {
            _flash.chargeWriteOp();
            const bool committed = _inner->commit();
            close();
            return committed;
        }

        void abort() override
        {
            _flash.chargeWriteOp();
            _inner->abort();
            close();
        }

        size_t bytesWritten() const override { return _inner->bytesWritten(); }

    private:
        void close()
        {
            if (_catalog && !_closed)
            {
                _closed = true;
                _flash.closeCatalogWriter(_taskName);
            }
        }

        SimCatalogFlash& _flash;
        std::unique_ptr<ISpiffsFileWriter> _inner;
        bool _catalog;
        std::string _taskName;
        bool _closed = false;
    };

    static bool isCatalogFile(const char* path)
    {
        if (path == nullptr)
        {
            return false;
        }
        const std::string p(path);
        return p == CATALOG_FILE || p == std::string(CATALOG_FILE) + ".tmp" ||
               p == std::string(CATALOG_FILE) + ".bak";
    }

    void closeCatalogWriter(const std::string& taskName)
    {
        for (size_t i = 0; i < _openCatalogWriters.size(); i++)
        {
            if (_openCatalogWriters[i] == taskName)
            {
                _openCatalogWriters.erase(_openCatalogWriters.begin() + i);
                return;
            }
        }
    }

    /** A rename or removal of the catalog's files by a task that is not writing it, mid-write. */
    void noteForeignTouch(const char* what, const char* path)
    {
        if (!isCatalogFile(path) || _openCatalogWriters.empty() ||
            catalogWriteOpenBy(_scheduler.currentTaskName()))
        {
            return;
        }
        noteSecondWriter(std::string(_scheduler.currentTaskName()) + " " + what + " " + path +
                         " while " + _openCatalogWriters[0] + " was writing the catalog");
    }

    void noteSecondWriter(const std::string& what)
    {
        if (_secondWriters == 0)
        {
            _firstSecondWriter = what;
        }
        _secondWriters++;
    }

    void chargeWriteOp()
    {
        if (_opCostMs == 0)
        {
            return;
        }
        _writeOps++;
        _writeOpsByTask[_scheduler.currentTaskName()]++;
        _scheduler.delayMs(_opCostMs);
    }

    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
    uint32_t _opCostMs = 0;
    uint64_t _writeOps = 0;
    std::map<std::string, uint64_t> _writeOpsByTask;
    std::vector<std::string> _openCatalogWriters;
    uint32_t _catalogWritesOpened = 0;
    std::map<std::string, uint32_t> _catalogWritesOpenedByTask;
    uint32_t _catalogPublishes = 0;
    uint64_t _catalogPublishedAtMs = 0;
    uint32_t _secondWriters = 0;
    std::string _firstSecondWriter;
    uint32_t _catalogReads = 0;
    uint32_t _foreignReadsDuringWrite = 0;
};

/**
 * @brief The far end of every network call. Answers instantly, except where a test holds the
 *        catalog fetch a command makes.
 */
class SimServer : public MockHybridAPIService {
public:
    SimServer(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock) {}

    std::vector<ModuleCommandDto> pendingModuleCommands;
    std::vector<SensorType> catalog;

    /**
     * The catalog fetch a command makes - on the command network task - returns only once this
     * answers true (or a minute has passed). Unset: it answers at once.
     */
    std::function<bool()> answerScanFetchWhen;
    /** Run when a command's catalog fetch arrives, before it is answered. */
    std::function<void()> onScanFetch;
    bool scanFetchAnsweredOnCondition = false;
    uint64_t scanFetchAnsweredAtMs = 0;

    /** The same for the synchronization's own fetch, on the outbox's network task. */
    std::function<bool()> answerSyncFetchWhen;
    bool syncFetchAnsweredOnCondition = false;
    uint64_t syncFetchAnsweredAtMs = 0;

    std::map<std::string, int> catalogFetchesByTask;

    std::vector<std::vector<ModuleCommandUpdateDto>> moduleCommandReports;
    std::vector<uint64_t> moduleCommandReportAtMs;

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

    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        getSensorTypesListCallCount++;
        catalogFetchesByTask[_scheduler.currentTaskName()]++;

        if (std::string(_scheduler.currentTaskName()) == NetworkCommandRunner::TASK_NAME)
        {
            if (onScanFetch)
            {
                onScanFetch();
            }
            const uint64_t giveUpAtMs = _clock.nowMs() + 60000;
            while (answerScanFetchWhen && !answerScanFetchWhen() && _clock.nowMs() < giveUpAtMs)
            {
                _scheduler.delayMs(1);
            }
            scanFetchAnsweredOnCondition = answerScanFetchWhen && answerScanFetchWhen();
            scanFetchAnsweredAtMs = _clock.nowMs();
        }
        else if (std::string(_scheduler.currentTaskName()) == OUTBOX_TASK_NAME)
        {
            const uint64_t giveUpAtMs = _clock.nowMs() + 60000;
            while (answerSyncFetchWhen && !answerSyncFetchWhen() && _clock.nowMs() < giveUpAtMs)
            {
                _scheduler.delayMs(1);
            }
            syncFetchAnsweredOnCondition = answerSyncFetchWhen && answerSyncFetchWhen();
            syncFetchAnsweredAtMs = _clock.nowMs();
        }

        if (httpFailed != nullptr)
        {
            *httpFailed = false;
        }
        return catalog;
    }

    int catalogFetchesBy(const char* taskName) const
    {
        std::map<std::string, int>::const_iterator it = catalogFetchesByTask.find(taskName);
        return (it == catalogFetchesByTask.end()) ? 0 : it->second;
    }

    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        moduleCommandReports.push_back(updates);
        moduleCommandReportAtMs.push_back(_clock.nowMs());
        return true;
    }

private:
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
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
                _handler->handleMessage("GrowDirectorMqttTopic/scan-module",
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

class IdleSensorService : public ISensorService {
public:
    bool init() override { return true; }
    void loop() override {}
    bool readAllSensors() override { return true; }
    bool readSensor(SensorEntity) override { return true; }
};

class IdleRuleService : public IRuleService {
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

/**
 * @brief A Modbus model of the catalog: one channel, scannable, with an address the module can
 *        write. Every model of the catalog has the same register layout, so a device that answers
 *        is taken for the first of them the scan tries.
 */
SensorType makeModbusSensorType(size_t index)
{
    SensorType type;
    type.uid = "type-uid-" + std::to_string(index);
    type.name = "SCAN-MODEL-" + std::to_string(index);
    type.displayName = type.name + " display name for the module structure";
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = true;
    type.isSupported = true;
    type.modbusDeviceAddress = 1;
    type.baudRate = 9600;
    type.modbusPingAddress = 0x0000;
    type.registerToStoreModbusDeviceAddress = ADDRESS_STORE_REGISTER;
    type.writeRegistersCommand = WRITE_SINGLE_REGISTER;
    type.createdAt = "2025-09-22T07:04:26.803128+00:00";
    type.updatedAt = "2026-02-11T09:01:35.44352+00:00";

    SubSensorType subType;
    subType.name = "temperature";
    subType.displayName = "Temperature";
    subType.unit = "C";
    type.sensorTypes.push_back(subType);

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

std::vector<SensorType> makeCatalog(size_t count)
{
    std::vector<SensorType> catalog;
    for (size_t i = 0; i < count; i++)
    {
        catalog.push_back(makeModbusSensorType(i));
    }
    return catalog;
}

/**
 * @brief A factory-fresh device of those models on the scanned port: at the models' address, and
 *        movable to another by a write of its address register.
 */
SimRs485Bus::Device makeFactoryFreshDevice()
{
    SimRs485Bus::Device device;
    device.address = 1;
    // 250 x 0.1 = 25.0, inside the can-connect range: the scan's plausibility check passes.
    device.registerValues.assign(1, 250);
    device.addressIsWritable = true;
    device.addressStoreRegister = ADDRESS_STORE_REGISTER;
    return device;
}

std::string moduleCommandSignal()
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"ScanSensors\",\"options\":{}}",
             MODULE_UID);
    return std::string(buffer);
}

ModuleCommandDto scanCommand()
{
    ModuleCommandDto dto;
    dto.uid = SCAN_COMMAND_UID;
    dto.command = "ScanSensors";
    dto.createdAt = "2026-08-20T10:00:00Z";
    dto.options = SCAN_OPTIONS;
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
    MockNTPTimeAdapter timeAdapter;
    MockWiFiManager wifiManager;
    MockClearModuleDataService clearService;
    ArduinoJsonConverter jsonConverter;
    ModuleStructure moduleStructure;

    SimCatalogFlash flash;
    SpiffsSensorTypeDao sensorTypeDao;
    SensorTypeRepository sensorTypeRepository;
    SensorTypeCatalogWriter catalogWriter;

    ModbusSensorReader modbusSensorReader;
    RawBusAccessAdapter rawBusAccess;
    SensorReader sensorReader;
    SensorAcquisitionService acquisition;

    SimServer server;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModbusAddressAllocator modbusAddressAllocator;
    ModuleCommandExecutionUseCase moduleCommandUseCase;

    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    NetworkCommandRunner networkRunner;
    SyncSensorTypesUseCase syncSensorTypes;

    CommandService commandService;
    CommandTopicHandler topicHandler;
    ScriptedMqttManager mqttManager;

    IdleSensorService sensorService;
    IdleRuleService ruleService;
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
          flash(scheduler, clock),
          sensorTypeDao(&flash, &jsonConverter, &debugSystem, &logger),
          sensorTypeRepository(&sensorTypeDao, &debugSystem, &logger),
          catalogWriter(&sensorTypeDao, &scheduler, &logger, &debugSystem),
          modbusSensorReader(ports, arduino, &logger, &debugSystem),
          rawBusAccess(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          server(scheduler, clock),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          modbusAddressAllocator(&sensorRepository),
          moduleCommandUseCase(&logger, &debugSystem, &timeAdapter, nullptr, &arduino,
                               &sensorRepository, &sensorTypeRepository, &sensorReader,
                               &moduleConfig, &settings, &server, &clearService,
                               &moduleStructure, nullptr, &wifiManager, nullptr,
                               &rawBusAccess, nullptr, nullptr, nullptr,
                               &modbusAddressAllocator),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          networkRunner(&scheduler, &logger, &debugSystem),
          syncSensorTypes(&server, &sensorTypeRepository, &arduino, &timeAdapter, &jsonConverter,
                          &logger, &debugSystem, &sensorRepository, &moduleConfig),
          commandService(nullptr, &moduleCommandUseCase, nullptr, &moduleStructure,
                         &logger, &debugSystem, &timeAdapter, &jsonConverter),
          topicHandler(&logger, &debugSystem, MODULE_UID, &commandService, &jsonConverter)
    {
        debugSystem.featureEnabled = false;

        timeAdapter.setEpochTime(1755690000);
        timeAdapter.setCommandTimeValid(true);

        settings.setUser("scan@example.com");
        settings.setAccountUid("scan-account");
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setSettings(&settings);

        // ESP32-S3 structures carry one node; without it addSensor() has nowhere to put a sensor.
        moduleStructure.nodes.push_back(Node());

        server.catalog = makeCatalog(CATALOG_TYPES);
        server.pendingModuleCommands.push_back(scanCommand());

        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        rawBusAccess.setBusLock(&busLock);
        // The line as a booted module has it: the boot begins the UARTs before the first frame.
        sensorReader.init();
        // When the scan reached the line, for the command's time line.
        port0.recordSendTimes(true);

        moduleCommandUseCase.setSensorStructureUseCase(&sensorStructure);
        acquisition.setRawCommandExecutor(&moduleCommandUseCase);
        acquisition.ensureRunning();
        networkRunner.setExecutor(&moduleCommandUseCase);
        commandService.setOutbox(&outbox);
        commandService.setBusCommandExecutor(&acquisition);
        commandService.setNetworkCommandRunner(&networkRunner);
        mqttManager.setHandler(&topicHandler);

        syncSensorTypes.setOutbox(&outbox);
        syncSensorTypes.setCatalogWriter(&catalogWriter);
        // Remove this line to reproduce the red of (a) and (b): the command then stores the
        // catalog it fetched itself, on the task that finishes it - the control loop.
        moduleCommandUseCase.setSyncSensorTypesUseCase(&syncSensorTypes);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setCommandService(&commandService);
        loopService.setSensorService(&sensorService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
        loopService.setSyncSensorTypesUseCase(&syncSensorTypes);
        loopService.setSensorTypeCatalogWriter(&catalogWriter);
    }

    /** One task, mirroring main.cpp: the loop service, the watchdog feed, one period of sleep. */
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

    /** What a module that has just booted would read out of storage. */
    std::vector<SensorType> storedCatalog()
    {
        SpiffsSensorTypeDao daoAfterBoot(&flash, &jsonConverter, &debugSystem, &logger);
        std::vector<SensorType> stored;
        daoAfterBoot.getAll(stored);
        return stored;
    }

    /** The bytes the DAO stores for this catalog, written on a flash of its own. */
    std::string referenceFileFor(const std::vector<SensorType>& catalog)
    {
        MockSpiffsAdapter referenceFlash;
        SpiffsSensorTypeDao referenceDao(&referenceFlash, &jsonConverter, &debugSystem, &logger);
        TEST_ASSERT_TRUE_MESSAGE(referenceDao.saveAll(catalog), "the reference catalog could not be written");
        return referenceFlash.getFileContent(CATALOG_FILE);
    }

    /** The command's outcome as the server got it: the report after its InProgress. */
    const ModuleCommandUpdateDto* outcome() const
    {
        for (size_t i = 0; i < server.moduleCommandReports.size(); i++)
        {
            const std::vector<ModuleCommandUpdateDto>& batch = server.moduleCommandReports[i];
            if (!batch.empty() && batch[0].uid == SCAN_COMMAND_UID && batch[0].status != "InProgress")
            {
                return &batch[0];
            }
        }
        return nullptr;
    }

    uint64_t outcomeAtMs() const
    {
        for (size_t i = 0; i < server.moduleCommandReports.size(); i++)
        {
            const std::vector<ModuleCommandUpdateDto>& batch = server.moduleCommandReports[i];
            if (!batch.empty() && batch[0].uid == SCAN_COMMAND_UID && batch[0].status != "InProgress")
            {
                return server.moduleCommandReportAtMs[i];
            }
        }
        return 0;
    }
};

/** @brief The soundness gate every scenario passes before its verdict counts. */
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.server.catalogFetchesBy(NetworkCommandRunner::TASK_NAME),
                                  "the command did not fetch the catalog for its scan - the "
                                  "scenario did not happen");
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

/** @brief The command's outcome: Done, with the sensor it found on the scanned port. */
void assertTheScanFoundTheSensor(ModuleUnderTest& module)
{
    const ModuleCommandUpdateDto* outcome = module.outcome();
    TEST_ASSERT_NOT_NULL_MESSAGE(outcome, "the command's outcome never reached the server");
    char message[200];
    snprintf(message, sizeof(message), "the scan ended '%s': %s", outcome->status.c_str(),
             outcome->errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", outcome->status.c_str(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)outcome->result.sensors.size(),
                                     "the outcome does not carry the sensor on the scanned port");
}

/**
 * @brief One write, one wait: the scan's stage is set waiting once and held until the write has
 *        landed - not set waiting again, with a copy of the stage and a line of the log, on every
 *        loop iteration the write lasts.
 */
void assertTheScanWaitedOnce(ModuleUnderTest& module)
{
    char message[200];
    snprintf(message, sizeof(message),
             "the scan's stage was set waiting %u time(s) for one catalog write",
             (unsigned)module.commandService.catalogWaitParkCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.commandService.catalogWaitParkCount(), message);
}

/** @brief The command's outcome: an Error with this reason, and nothing of it left waiting. */
void assertTheCommandEndedInError(ModuleUnderTest& module, const char* reason)
{
    const ModuleCommandUpdateDto* outcome = module.outcome();
    TEST_ASSERT_NOT_NULL_MESSAGE(outcome, "the command never ended - its outcome never reached the "
                                          "server, and the backend expires it as not completed in time");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Error", outcome->status.c_str(),
                                     "a catalog that was not stored did not end the command in Error");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(reason, outcome->errorMessage.c_str(),
                                     "the command's Error does not say why");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.commandService.pendingBatchCount(),
                                     "the command's list is still waiting to be reported");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.commandService.catalogWaitCount(),
                                     "a stage of the command is still waiting for the catalog");
    TEST_ASSERT_TRUE_MESSAGE(module.port0.sentFrames.empty(),
                             "the scan went to the line without a catalog to scan from");
}

/**
 * @brief Once no write is in flight, the repository asks storage again: a write that ended without
 *        a catalog leaves the module where it was, not answered from an empty hand for good.
 */
void assertStorageIsAskedAgain(ModuleUnderTest& module)
{
    const uint32_t readsBefore = module.flash.catalogReads();
    const int held = module.sensorTypeRepository.count();
    TEST_ASSERT_EQUAL_INT(0, held);
    TEST_ASSERT_TRUE_MESSAGE(module.flash.catalogReads() > readsBefore,
                             "the repository did not ask storage for the catalog after the write "
                             "was over - it still answers as though one were in flight");
}

/** The command's time line, from the signal: what the backend's 300 s are spent on. */
void printCommandTime(ModuleUnderTest& module, uint64_t signalAtMs, const char* what)
{
    const ModuleCommandUpdateDto* outcome = module.outcome();
    const uint64_t firstFrameAtMs =
        module.port0.sentAtUs.empty() ? 0 : module.port0.sentAtUs[0] / 1000;
    printf("  %s: catalog fetched at +%lu ms, on flash at +%lu ms, first scan frame at +%lu ms, "
           "outcome '%s'%s%s reported at +%lu ms; catalog writes opened %lu (control loop %lu, "
           "storage task %lu)\n",
           what,
           (unsigned long)(module.server.scanFetchAnsweredAtMs - signalAtMs),
           (unsigned long)(module.flash.catalogPublishedAtMs() ? module.flash.catalogPublishedAtMs() - signalAtMs : 0),
           (unsigned long)(firstFrameAtMs ? firstFrameAtMs - signalAtMs : 0),
           outcome ? outcome->status.c_str() : "(none)",
           (outcome && !outcome->errorMessage.empty()) ? " - " : "",
           outcome ? outcome->errorMessage.c_str() : "",
           (unsigned long)(module.outcomeAtMs() ? module.outcomeAtMs() - signalAtMs : 0),
           (unsigned long)module.flash.catalogWritesOpened(),
           (unsigned long)module.flash.catalogWritesOpenedBy(LOOP_TASK_NAME),
           (unsigned long)module.flash.catalogWritesOpenedBy(SensorTypeCatalogWriter::TASK_NAME));
}

} // namespace

// --------------------------------------------------------------------------------------------
// (a) A fresh module is told to scan a port. It has no catalog, fetches the account's catalog for
//     the scan, stores it, and scans. The write of the catalog runs on the storage task; the loop
//     makes none of its file operations and keeps its period.
// --------------------------------------------------------------------------------------------

static void test_a_scan_stores_the_catalog_it_fetched_off_the_control_loop(void)
{
    ModuleUnderTest module;
    module.port0.addDevice(makeFactoryFreshDevice());
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    const uint64_t signalAtMs = module.clock.nowMs();
    module.mqttManager.deliver(moduleCommandSignal());
    module.scheduler.runForMs(SCENARIO_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.flash.writeOpsTotal() >= CATALOG_TYPES,
                             "the catalog was never written - nothing slow was exercised");
    printCommandTime(module, signalAtMs, "a scan that fetched and stored the catalog");

    char message[224];
    snprintf(message, sizeof(message),
             "%lu of the %lu file operations of the catalog write ran on the control loop",
             (unsigned long)module.flash.writeOpsBy(LOOP_TASK_NAME),
             (unsigned long)module.flash.writeOpsTotal());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.flash.writeOpsBy(LOOP_TASK_NAME), message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(module.flash.writeOpsTotal(),
                                     module.flash.writeOpsBy(SensorTypeCatalogWriter::TASK_NAME),
                                     "the catalog write did not run on the storage task");
    assertLoopStayedBounded(cadence, "a scan stored the 14-type catalog it fetched on a flash device "
                                     "charging 5 ms per operation");

    // What follows the write still follows it: the scan, from the catalog that was stored.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(CATALOG_TYPES, (unsigned)module.storedCatalog().size(),
                                   "the catalog the scan fetched is not on flash");
    assertTheScanFoundTheSensor(module);
    assertTheScanWaitedOnce(module);
}

// --------------------------------------------------------------------------------------------
// (b) The same fresh module, and the synchronization stores the catalog at the same time - its own
//     fetch, as a change of local IP asks for it. The command's fetch comes back while that write
//     is open. The file has one writer at a time, and what is left on it is the whole catalog.
// --------------------------------------------------------------------------------------------

static void test_a_scan_does_not_write_the_catalog_while_the_synchronization_writes_it(void)
{
    ModuleUnderTest module;
    module.port0.addDevice(makeFactoryFreshDevice());
    module.flash.startCharging();
    module.server.answerScanFetchWhen = [&module]() {
        return module.flash.catalogWriteOpenBy(SensorTypeCatalogWriter::TASK_NAME);
    };

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    const uint64_t signalAtMs = module.clock.nowMs();
    module.mqttManager.deliver(moduleCommandSignal());
    module.scheduler.runForMs(1000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.server.catalogFetchesBy(NetworkCommandRunner::TASK_NAME),
                                  "the command is not fetching its catalog - the scenario did not happen");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.flash.writeOpsTotal(),
                                     "the catalog was written before the synchronization asked for it");

    module.syncSensorTypes.forceSync();
    module.scheduler.runForMs(SCENARIO_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.server.catalogFetchesBy(OUTBOX_TASK_NAME),
                                  "the synchronization did not fetch the catalog");
    TEST_ASSERT_TRUE_MESSAGE(module.server.scanFetchAnsweredOnCondition,
                             "the command's fetch did not come back while the synchronization's "
                             "write was open - the scenario did not happen");
    printCommandTime(module, signalAtMs, "a scan whose fetch came back during the sync's write");

    char message[256];
    snprintf(message, sizeof(message), "%lu second writer(s) of the catalog file; the first: %s",
             (unsigned long)module.flash.secondWriterCount(),
             module.flash.firstSecondWriter().c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.flash.secondWriterCount(), message);
    // Nor read meanwhile: a module without a catalog has no cache, and a read of it goes to the
    // file - which, between two renames of the replacement, the recovery path would take over.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.flash.foreignReadsDuringWrite(),
                                     "the catalog's files were read by another task while the "
                                     "storage task was writing them");

    // What is on flash is the whole catalog, as the DAO stores it.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(module.referenceFileFor(module.server.catalog).c_str(),
                                     module.flash.getFileContent(CATALOG_FILE).c_str(),
                                     "the catalog on flash is not the server's catalog as the DAO "
                                     "stores it");

    // The choice made here: the command waited for the write in flight and scanned from what it
    // stored, rather than queueing a second write of the same server catalog behind it.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.flash.catalogWritesOpened(),
                                     "the catalog was written more than once");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the synchronization's write was not reported stored");
    assertTheScanFoundTheSensor(module);
    assertTheScanWaitedOnce(module);
    assertLoopStayedBounded(cadence, "a scan waited for the synchronization's catalog write");
}

// --------------------------------------------------------------------------------------------
// (c) What the command does with the catalog once it is stored: the scan finds the factory-fresh
//     device on the first model, moves it to the address of its port, saves it under the uid of
//     the pin with the model's identity, puts it in the structure and reports it. Every one of
//     these comes out of the catalog the scan ran from, and none may depend on who stored it.
// --------------------------------------------------------------------------------------------

static void test_a_scan_after_a_catalog_fetch_saves_and_reports_what_it_found(void)
{
    ModuleUnderTest module;
    module.port0.addDevice(makeFactoryFreshDevice());
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandSignal());
    module.scheduler.runForMs(SCENARIO_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    // The report the backend gets.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.moduleCommandReports.size(),
                                     "the server did not see both the InProgress and the outcome");
    TEST_ASSERT_EQUAL_STRING("InProgress", module.server.moduleCommandReports[0][0].status.c_str());
    assertTheScanFoundTheSensor(module);
    const SensorResultDto& reported = module.outcome()->result.sensors[0];
    const std::string expectedUid = std::string(MODULE_UID) + ".00";
    TEST_ASSERT_EQUAL_STRING(expectedUid.c_str(), reported.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("SCAN-MODEL-0", reported.type.c_str());
    TEST_ASSERT_EQUAL_INT(SCANNED_PORT, reported.pin);
    TEST_ASSERT_EQUAL_STRING("type-uid-0", reported.modelUid.c_str());

    // The row the scan adopted.
    const Utils::Optional<SensorEntity> saved = module.sensorRepository.get(expectedUid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(saved.has_value(), "the sensor the scan found was not saved");
    const SensorEntity& sensor = saved.value();
    TEST_ASSERT_EQUAL_STRING("type-uid-0", sensor.sensorTypeUid.c_str());
    TEST_ASSERT_EQUAL_STRING("SCAN-MODEL-0", sensor.model.c_str());
    TEST_ASSERT_EQUAL_STRING("ModBus", sensor.protocol.c_str());
    TEST_ASSERT_EQUAL_INT(SCANNED_PORT, sensor.pin);
    TEST_ASSERT_EQUAL_INT(9600, sensor.baudRate);
    TEST_ASSERT_EQUAL_INT((int)SensorStatusEnum::SENSOR_STATUS_NEW, (int)sensor.status);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)sensor.modbusRegisterConfigs.size());
    TEST_ASSERT_EQUAL_UINT(ADDRESS_STORE_REGISTER, sensor.registerToStoreModbusDeviceAddress);

    // The address: off the models' own, onto the one of its port - on the line and in the row.
    char message[160];
    snprintf(message, sizeof(message), "the sensor was saved on address 0x%02X",
             (unsigned)sensor.modbusDeviceAddress);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MODBUS_ADDRESS_MIN + SCANNED_PORT, sensor.modbusDeviceAddress, message);
    TEST_ASSERT_NOT_NULL_MESSAGE(module.port0.device(MODBUS_ADDRESS_MIN + SCANNED_PORT),
                                 "the device on the line was not moved to the address of its port");
    TEST_ASSERT_NULL_MESSAGE(module.port0.device(1), "the device still answers at the factory address");

    // The structure, and the structure report that follows a sensor change.
    TEST_ASSERT_NOT_NULL_MESSAGE(module.moduleStructure.findSensorByUid(expectedUid + ".01"),
                                 "the found sensor did not reach the module structure");
    TEST_ASSERT_TRUE_MESSAGE(module.server.sendToServerModuleStructureCallCount >= 1,
                             "the updated structure never reached the server");

    // And the catalog the scan ran from is the server's, on flash and in the module.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(module.referenceFileFor(module.server.catalog).c_str(),
                                     module.flash.getFileContent(CATALOG_FILE).c_str(),
                                     "the catalog on flash is not the server's catalog as the DAO "
                                     "stores it");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)CATALOG_TYPES, module.sensorTypeRepository.count(),
                                  "the module does not hold the catalog it scanned from");
}

// --------------------------------------------------------------------------------------------
// (d) A write that does not land. The file system refuses it, or no task can be created to make
//     it; either way the module still has no catalog, and the command ends - in an Error that says
//     the catalog could not be stored - instead of waiting for a catalog that is not coming.
// --------------------------------------------------------------------------------------------

static void test_a_scan_whose_catalog_the_file_system_refuses_ends_in_an_error(void)
{
    ModuleUnderTest module;
    module.port0.addDevice(makeFactoryFreshDevice());
    module.flash.startCharging();
    // Nothing can be opened for writing, which is how a full or broken file system answers.
    module.flash.setShouldFail(true);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandSignal());
    module.scheduler.runForMs(SCENARIO_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_TRUE_MESSAGE(module.flash.catalogWritesOpened() >= 1,
                             "nothing tried to write the catalog - the refusal was never met");

    assertTheCommandEndedInError(module, "Sensor types could not be stored");
    TEST_ASSERT_FALSE_MESSAGE(module.flash.fileExists(CATALOG_FILE),
                              "a refused write left a catalog file behind");
    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                              "a catalog that was not stored was reported synchronized");
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.isTaskAlive(SensorTypeCatalogWriter::TASK_NAME),
                              "the storage task outlived the refused write");
    assertStorageIsAskedAgain(module);
    assertLoopStayedBounded(cadence, "the file system refused the catalog a scan fetched");
}

static void test_a_scan_whose_catalog_no_task_can_store_ends_in_an_error(void)
{
    ModuleUnderTest module;
    module.port0.addDevice(makeFactoryFreshDevice());
    module.flash.startCharging();
    // Armed while the command's fetch is out: every task the module had to create for the command
    // exists by then, and the next one asked for is the storage task.
    module.server.onScanFetch = [&module]() { module.scheduler.failNextCreateTasks(1); };

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.mqttManager.deliver(moduleCommandSignal());
    module.scheduler.runForMs(SCENARIO_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.scheduler.refusedTaskCount(),
                                     "no task creation was refused - the scenario did not happen");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.catalogWriter.taskStartFailureCount(),
                                     "the refused task was not the storage task");

    assertTheCommandEndedInError(module, "Sensor types could not be stored");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.flash.writeOpsTotal(),
                                     "the flash was written although no task could write it");
    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.isWritePending(),
                              "the synchronization thinks a write is in flight that no task makes");
    assertStorageIsAskedAgain(module);
    assertLoopStayedBounded(cadence, "no task could be created to store the catalog a scan fetched");
}

// --------------------------------------------------------------------------------------------
// (e) executeCommands() composes a command on its caller's stack and returns its outcome; nothing
//     collects a write handed to the storage task until it returns. A scan composed that way, on a
//     module whose synchronization is wired, stores its catalog on the calling task and finishes.
// --------------------------------------------------------------------------------------------

static void test_a_scan_composed_inline_stores_the_catalog_itself_and_finishes(void)
{
    ModuleUnderTest module;
    module.port0.addDevice(makeFactoryFreshDevice());
    module.flash.startCharging();

    const char* const callerTaskName = "inline-caller";
    std::vector<ModuleCommandExecutionResult> results;
    bool returned = false;
    module.scheduler.createTask(callerTaskName, 512 * 1024, 1, [&module, &results, &returned]() {
        results = module.moduleCommandUseCase.executeCommands({scanCommand()});
        returned = true;
    });
    module.scheduler.runForMs(SCENARIO_MS);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(returned, "the inline composition of a scan never returned");
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)results.size());
    char message[160];
    snprintf(message, sizeof(message), "the inline scan ended '%s': %s", results[0].status.c_str(),
             results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", results[0].status.c_str(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)results[0].result.sensors.size(),
                                     "the inline scan did not find the sensor");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.flash.catalogWritesOpenedBy(callerTaskName),
                                     "the inline composition did not store the catalog on its own task");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.catalogWriter.taskStartCount(),
                                     "the inline composition handed the catalog to the storage task");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(CATALOG_TYPES, (unsigned)module.storedCatalog().size(),
                                   "the catalog the inline scan fetched is not on flash");
}

// --------------------------------------------------------------------------------------------
// (f) The synchronization's side of the same rule. A fetched catalog handed to it while its own
//     write is in flight is refused at once: not merged against a catalog about to be replaced -
//     which on a module without one means reading the very file the storage task is writing - and
//     not recorded as a failed synchronization that the write in flight is about to contradict.
// --------------------------------------------------------------------------------------------

static void test_the_sync_refuses_a_fetched_catalog_while_its_own_write_is_in_flight(void)
{
    ModuleUnderTest module;
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.syncSensorTypes.forceSync();
    for (int ms = 0; ms < 60000 && !module.flash.catalogWriteOpenBy(SensorTypeCatalogWriter::TASK_NAME); ms++)
    {
        module.scheduler.runForMs(1);
    }
    TEST_ASSERT_TRUE_MESSAGE(module.flash.catalogWriteOpenBy(SensorTypeCatalogWriter::TASK_NAME),
                             "the synchronization's write never opened - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.isWritePending(),
                             "the synchronization does not know its write is in flight");

    const uint32_t readsBefore = module.flash.catalogReads();
    const ISyncSensorTypesUseCase::StoreResult stored =
        module.syncSensorTypes.storeFetchedCatalog(makeCatalog(CATALOG_TYPES));
    const uint32_t readsOfTheHandOver = module.flash.catalogReads() - readsBefore;

    TEST_ASSERT_TRUE_MESSAGE(stored == ISyncSensorTypesUseCase::StoreResult::NotStored,
                             "a catalog handed over during a write in flight was taken");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, readsOfTheHandOver,
                                     "the refused hand-over read the catalog's files while the "
                                     "storage task was writing them");
    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.isRearmPending(),
                              "the refused hand-over armed a fetch the write in flight makes pointless");

    // And the write in flight ends as it would have.
    module.scheduler.runForMs(60000);
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the synchronization's write was not reported stored");
    TEST_ASSERT_EQUAL_UINT32(1, module.flash.catalogWritesOpened());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(module.referenceFileFor(module.server.catalog).c_str(),
                                     module.flash.getFileContent(CATALOG_FILE).c_str(),
                                     "the catalog on flash is not the server's catalog as the DAO "
                                     "stores it");
}

// --------------------------------------------------------------------------------------------
// (g) The mirror of (b): the command's write is the one in flight, and the synchronization's own
//     fetch - a change of local IP asks for one - comes back meanwhile. A module without a catalog
//     has no cache to answer from, so a read of the repository then would go to the file the
//     storage task is writing; and in the window where that write publishes its temporary file, a
//     read that finds no catalog takes the temporary file for an interrupted replacement and
//     renames it. Two windows: the write just opened, and its temporary file complete with the
//     renames still to come. In neither may another task read or touch the catalog's files.
// --------------------------------------------------------------------------------------------

namespace {

enum class WriteWindow
{
    Opened,           // the command's write has just opened the catalog for writing
    TemporaryComplete // its temporary file holds the whole catalog; the renames are to come
};

void answerTheSyncInsideTheScansWrite(ModuleUnderTest& module, WriteWindow window, const char* what)
{
    module.port0.addDevice(makeFactoryFreshDevice());
    module.flash.startCharging();
    // The command's fetch comes back once the synchronization's own fetch is out ...
    module.server.answerScanFetchWhen = [&module]() {
        return module.server.catalogFetchesBy(OUTBOX_TASK_NAME) >= 1;
    };
    // ... and the synchronization's comes back inside the write the command's catalog went to.
    module.server.answerSyncFetchWhen = [&module, window]() {
        if (!module.flash.catalogWriteOpen())
        {
            return false;
        }
        if (window == WriteWindow::Opened)
        {
            return true;
        }
        const std::string temporary = module.flash.getFileContent(CATALOG_TEMP_FILE);
        return !temporary.empty() && temporary[temporary.size() - 1] == ']';
    };

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    const uint64_t signalAtMs = module.clock.nowMs();
    module.mqttManager.deliver(moduleCommandSignal());
    for (int ms = 0; ms < 60000 && module.server.catalogFetchesBy(NetworkCommandRunner::TASK_NAME) == 0; ms++)
    {
        module.scheduler.runForMs(1);
    }
    module.syncSensorTypes.forceSync();
    module.scheduler.runForMs(SCENARIO_MS);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.server.catalogFetchesBy(OUTBOX_TASK_NAME),
                                  "the synchronization did not fetch the catalog");
    TEST_ASSERT_TRUE_MESSAGE(module.server.scanFetchAnsweredOnCondition,
                             "the command's fetch came back before the synchronization asked - the "
                             "scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.server.syncFetchAnsweredOnCondition,
                             "the synchronization's answer did not come back inside the command's "
                             "write - the scenario did not happen");
    printf("  %s: the synchronization's answer came back at +%lu ms; %lu read(s) of the catalog's "
           "files by another task during the write, %lu second writer(s)%s%s\n",
           what, (unsigned long)(module.server.syncFetchAnsweredAtMs - signalAtMs),
           (unsigned long)module.flash.foreignReadsDuringWrite(),
           (unsigned long)module.flash.secondWriterCount(),
           module.flash.secondWriterCount() ? " - the first: " : "",
           module.flash.firstSecondWriter().c_str());

    char message[256];
    snprintf(message, sizeof(message),
             "the catalog's files were read %lu time(s) by another task while the command's write "
             "was in flight",
             (unsigned long)module.flash.foreignReadsDuringWrite());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.flash.foreignReadsDuringWrite(), message);
    snprintf(message, sizeof(message), "%lu second writer(s) of the catalog file; the first: %s",
             (unsigned long)module.flash.secondWriterCount(),
             module.flash.firstSecondWriter().c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.flash.secondWriterCount(), message);

    // And the write in flight ends as it would have: the server's catalog on flash, once; the
    // synchronization reported successful with nothing left to re-arm; the scan done.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(module.referenceFileFor(module.server.catalog).c_str(),
                                     module.flash.getFileContent(CATALOG_FILE).c_str(),
                                     "the catalog on flash is not the server's catalog as the DAO "
                                     "stores it");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.flash.catalogWritesOpened(),
                                     "the catalog was written more than once");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the stored catalog was not reported as a successful synchronization");
    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.isRearmPending(),
                              "a fetch is still armed although the server's catalog is stored");
    assertTheScanFoundTheSensor(module);
}

} // namespace

static void test_a_sync_answer_as_the_scans_catalog_write_opens_touches_no_file(void)
{
    ModuleUnderTest module;
    answerTheSyncInsideTheScansWrite(module, WriteWindow::Opened,
                                     "the sync's answer as the scan's write opened");
}

static void test_a_sync_answer_as_the_scans_catalog_write_publishes_touches_no_file(void)
{
    ModuleUnderTest module;
    answerTheSyncInsideTheScansWrite(module, WriteWindow::TemporaryComplete,
                                     "the sync's answer as the scan's write published");
}

// --------------------------------------------------------------------------------------------
// (h) The same rule for the synchronization's own write. A module without a catalog synchronizes,
//     and while the storage task writes, the catalog is asked for the ways the loop asks for it -
//     a count, a snapshot, one type. They are answered with what the module holds, which is
//     nothing yet, and storage is not read; once the write has landed the module holds the
//     server's catalog without reading the file back either.
// --------------------------------------------------------------------------------------------

static void test_the_catalog_is_not_read_from_storage_while_the_sync_writes_it(void)
{
    ModuleUnderTest module;
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.syncSensorTypes.forceSync();
    for (int ms = 0; ms < 60000 && !module.flash.catalogWriteOpenBy(SensorTypeCatalogWriter::TASK_NAME); ms++)
    {
        module.scheduler.runForMs(1);
    }
    TEST_ASSERT_TRUE_MESSAGE(module.flash.catalogWriteOpenBy(SensorTypeCatalogWriter::TASK_NAME),
                             "the synchronization's write never opened - the scenario did not happen");

    const uint32_t readsBefore = module.flash.catalogReads();
    const int counted = module.sensorTypeRepository.count();
    const size_t snapshotted = module.sensorTypeRepository.catalogSnapshot()->size();
    const bool found = module.sensorTypeRepository.get("type-uid-0").has_value();
    const uint32_t readsDuringWrite = module.flash.catalogReads() - readsBefore;

    char message[200];
    snprintf(message, sizeof(message),
             "three questions about the catalog read its files %lu time(s) while the storage task "
             "was writing them",
             (unsigned long)readsDuringWrite);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, readsDuringWrite, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, counted, "a catalog was counted before it was stored");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)snapshotted, "a catalog was handed out before it was stored");
    TEST_ASSERT_FALSE_MESSAGE(found, "a type was found before the catalog was stored");

    module.scheduler.runForMs(60000);
    cadence.closeAt(module.clock.nowMs());
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the synchronization's write was not reported stored");
    const uint32_t readsAfterWrite = module.flash.catalogReads();
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)CATALOG_TYPES, module.sensorTypeRepository.count(),
                                  "the module does not hold the catalog that was stored");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(readsAfterWrite, module.flash.catalogReads(),
                                     "the stored catalog was read back from the file");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.flash.foreignReadsDuringWrite(),
                                     "the catalog's files were read by another task during the write");
}

extern "C" void run_scan_command_catalog_write_tests(void)
{
    RUN_TEST(test_a_scan_stores_the_catalog_it_fetched_off_the_control_loop);
    RUN_TEST(test_a_scan_does_not_write_the_catalog_while_the_synchronization_writes_it);
    RUN_TEST(test_a_scan_after_a_catalog_fetch_saves_and_reports_what_it_found);
    RUN_TEST(test_a_scan_whose_catalog_the_file_system_refuses_ends_in_an_error);
    RUN_TEST(test_a_scan_whose_catalog_no_task_can_store_ends_in_an_error);
    RUN_TEST(test_a_scan_composed_inline_stores_the_catalog_itself_and_finishes);
    RUN_TEST(test_the_sync_refuses_a_fetched_catalog_while_its_own_write_is_in_flight);
    RUN_TEST(test_a_sync_answer_as_the_scans_catalog_write_opens_touches_no_file);
    RUN_TEST(test_a_sync_answer_as_the_scans_catalog_write_publishes_touches_no_file);
    RUN_TEST(test_the_catalog_is_not_read_from_storage_while_the_sync_writes_it);
}
