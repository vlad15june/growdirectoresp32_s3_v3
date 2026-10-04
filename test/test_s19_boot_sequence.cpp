// Scenario S19 - after setup the module comes up in an order of events: the link, the structure that
// reports the module, and only then what the boot sends after it.
//
// The shape under test: once setup is over, the first thing the module puts on the network is its
// structure - the module's report of itself to the server - and the rest of the boot follows it, each
// part on the event it actually depends on, rather than on a timer from millis 0 that happens to
// fire before or after the structure depending on how long the Wi-Fi took.
//
// What the order guards against: with the first telemetry on a 15 s timer from millis 0, it leaves in
// the iteration before the structure and carries no reading (the first read of the stored sensors is
// asked for in that same iteration); on a module whose Wi-Fi takes longer than seven seconds it is the
// first request of the boot, and the structure waits behind it - and behind the catalog fetch the
// rotation of the outbox picks next.
//
// What is under test is production code: the real ModuleSetupService builds the structure, the real
// LoopServiceConfigureDone drives the real OnChangeLocalIpUseCase, SyncSensorTypesUseCase,
// SensorService over the real SensorAcquisitionService and SensorReader on a simulated RS-485 line,
// the real CommandService, DeviceService, RuleService and DiagnosticService, all submitting to the
// real Outbox with its network task; the real StartupSensorScanUseCase and the real
// RepublishModuleStructureUseCase; the real SensorRepository over an in-memory DAO, and the real
// SensorTypeRepository and SensorTypeCatalogWriter over an in-memory catalog store. Replaced are the
// outside world - the radio, the server, the broker, the flash - and time.
//
// The radio's stand-in answers as the radio does: no address while there is no link (the board's
// WifiAdapter::getLocalIP() answers 0.0.0.0), and the outbox is wired to it - without that the
// outbox would take a request on a module with no link at all.
//
// ### Two orders of tasks
//
// On the board the network task runs on the other core: a request the loop hands over is taken
// while the loop is still finishing its iteration. The host scheduler is cooperative and, left as it
// is, starts a new task only when the loop yields - which shows one of the two orders the board can
// produce. Every test of an order here is run in both (HostTaskScheduler::setNewTasksStartAtOnce()).
//
// ### Reproducing the red
//
// The red of these tests is the boot without its sequence: take out of ModuleUnderTest every line
// that names the boot sequencer or the boot gates (the include, the member, its initializer and the
// wiring), and the module boots without it - telemetry and the reads on their timers from 0.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/Boot/BootSequencer.h"
#include "Application/Services/ClearModuleDataService.h"
#include "Application/Services/CommandService.h"
#include "Application/Services/Devices/DeviceService.h"
#include "Application/Services/DiagnosticService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/ModuleSetupService.h"
#include "Application/Services/Net/LinkSupervisor.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Rules/RuleService.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCase.h"
#include "Core/Domain/UseCases/Diagnostics/PreventiveRestartUseCase.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/CheckModuleDeletionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/FirmwareUpdateRecoveryUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeLocalIpUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnConnectivityRestoredUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Configuration/PendingFirmwareUpdateStore.h"
#include "Infrastructure/Controllers/HTTP/FirmwareRemoteUpdate.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/Sensors/RemoteSensors/RemoteSensorClient.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Controllers/MQTT/MqttManager.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"
#include "Infrastructure/Repositories/SensorRepository.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Core/Domain/MockFirmwareImageWriter.h"
#include "Mock/Core/Domain/MockOtaPartitionControl.h"
#include "Mock/Core/Domain/MockOtaUpdateLog.h"
#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorDao.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorTypeDao.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimLinkProbe.h"
#include "Sim/SimArduino.h"
#include "Sim/SimOtaHttpClient.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <set>
#include <string>
#include <vector>

using hostsim::HostBusLock;
using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimOtaHttpClient;
using hostsim::SimRs485Bus;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;
const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "s19-module";
const char* const MODULE_IP = "192.168.1.100";
const int MODULE_PIN_COUNT = 4;

/** What the server takes to answer anything, spent on the task that asked. */
const uint32_t SERVER_ANSWER_MS = 200;

/** The server's is_deleted query, which no outbox kind names: the deletion check's own call. */
const OutboxKind IS_DELETED_QUERY = static_cast<OutboxKind>(30);

// --- the outside world ----------------------------------------------------------------------------

/** @brief One line the module wrote, and when. */
struct TapeLine {
    uint64_t atMs = 0;
    std::string text;
};

/**
 * @brief The serial port: every line of the features the debug system has on, with its moment.
 *
 * Kept in memory and nowhere else - a boot of ten virtual minutes writes thousands of lines, and
 * the stand reads them back, it does not need them on disk.
 */
class TapeLogger : public ILogger {
public:
    explicit TapeLogger(VirtualClock& clock) : _clock(clock) {}

    std::vector<TapeLine> lines;
    static const size_t MAX_LINES = 200000;

    void print(const char* message) override { keep(message); }
    void println(const char* message) override { keep(message); }
    void printf(const char* format, ...) override { keep(format); }
    void print(const char* message, MqttLoggerMode) override { keep(message); }
    void println(const char* message, MqttLoggerMode) override { keep(message); }
    void printf(const char* format, MqttLoggerMode, ...) override { keep(format); }
    void print(const char*, const char* message) override { keep(message); }
    void println(const char*, const char* message) override { keep(message); }
    void printf(const char*, const char* format, ...) override { keep(format); }
    void print(const char*, const char* message, MqttLoggerMode) override { keep(message); }
    void println(const char*, const char* message, MqttLoggerMode) override { keep(message); }
    void printf(const char*, const char* format, MqttLoggerMode, ...) override { keep(format); }
    void print(const char*, MqttLoggerMode, const char* message) override { keep(message); }
    void println(const char*, MqttLoggerMode, const char* message) override { keep(message); }
    void printf(const char*, MqttLoggerMode, const char* format, ...) override { keep(format); }
    void log(LogLevel, const char* message) override { keep(message); }
    void log(LogLevel, const char*, const char* message) override { keep(message); }
    void logJson(const char*, const char*, const char*) override {}
    void logJson(const char*, const char*, const char*, const char*) override {}
    void setModuleUid(const char*) override {}

    /** @brief Lines that begin with this text, in the order they were written. */
    std::vector<TapeLine> linesStartingWith(const char* prefix) const
    {
        std::vector<TapeLine> found;
        const size_t length = strlen(prefix);
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i].text.compare(0, length, prefix) == 0)
            {
                found.push_back(lines[i]);
            }
        }
        return found;
    }

    /** @brief Index of the first line containing this text, or -1. */
    int indexOf(const char* text) const
    {
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i].text.find(text) != std::string::npos)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    size_t countOf(const char* text) const
    {
        size_t count = 0;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i].text.find(text) != std::string::npos)
            {
                count++;
            }
        }
        return count;
    }

    void printLinesStartingWith(const char* prefix) const
    {
        const std::vector<TapeLine> found = linesStartingWith(prefix);
        for (size_t i = 0; i < found.size(); i++)
        {
            ::printf("  s19 tape [%7lu ms] %s\n", (unsigned long)found[i].atMs, found[i].text.c_str());
        }
    }

private:
    void keep(const char* message)
    {
        if (lines.size() >= MAX_LINES)
        {
            return;
        }
        TapeLine line;
        line.atMs = _clock.nowMs();
        line.text = message != nullptr ? message : "";
        lines.push_back(line);
    }

    VirtualClock& _clock;
};

/** @brief A debug system with only the named features on - the board has most of them on. */
class TapeDebugSystem : public IDebugSystem {
public:
    std::set<std::string> enabled;

    IDebugFeature* registerFeature(const char*, const char* = nullptr, bool = false,
                                   MqttLoggerMode = MqttLoggerMode::MqttAndSerial) override
    {
        return nullptr;
    }
    IDebugFeature* getFeature(const char*) override { return nullptr; }
    void setDebugFeatureState(const char*, bool) override {}
    void setFeatureLogChannel(const char*, MqttLoggerMode) override {}
    bool isFeatureEnabled(const char* featureName) override
    {
        return featureName != nullptr && enabled.count(featureName) > 0;
    }
};

/**
 * @brief The radio as the board's adapter answers for it.
 *
 * WifiAdapter::getLocalIP() answers 0.0.0.0 while the station is not connected, and the restart of
 * the radio associates only if there is a network to associate with - here, the link the test has
 * raised. Neither is what MockWifiAdapter does on its own.
 *
 * The boot's association - the first restart of the radio, made on the network task - takes the time
 * the stand gives it, spent on the task that asked, as WifiAdapter::reconnect() spends it there; every
 * later restart is instant.
 */
class SimLink : public MockWifiAdapter {
public:
    bool linkAvailable = true;
    uint32_t bootAssociationMs = 0;
    HostTaskScheduler* scheduler = nullptr;

    const char* getLocalIP() override
    {
        return connected ? localIP : "0.0.0.0";
    }

    bool reconnect() override
    {
        reconnectCalled = true;
        reconnectCallCount++;
        if (reconnectCallCount == 1 && bootAssociationMs > 0 && scheduler != nullptr)
        {
            scheduler->delayMs(bootAssociationMs);
        }
        connected = linkAvailable;
        return connected;
    }

    void raise() { linkAvailable = true; connected = true; }
    void drop() { linkAvailable = false; connected = false; }
};

/** @brief The far end of every call the module makes: what it was asked, when, and how long it took. */
class SimServer : public MockHybridAPIService {
public:
    struct Call {
        OutboxKind kind = OutboxKind::SENSOR_UPDATE;
        uint64_t startedAtMs = 0;
        uint64_t endedAtMs = 0;
        bool answered = true;
        /** The task that made the call; empty for setup, which runs before any task. */
        std::string task;
        /** MODULE_STRUCTURE: the module node's address and records. */
        std::string ip;
        std::vector<Sensor> structureSensors;
        /** SENSOR_UPDATE: what the update carried. */
        std::vector<SensorDto> sensors;
        /** DIAGNOSTIC: the uptime the snapshot was taken at, and the report as the server reads it. */
        unsigned long diagnosticUptimeMs = 0;
        std::string diagnosticJson;
        /** SENSOR_UPDATE: the uptime the update was built at. */
        unsigned long updateMillis = 0;
        /** MODULE_COMMAND_RESULT: the outcomes the batch carried. */
        std::vector<ModuleCommandUpdateDto> commandUpdates;
        /** COMMAND_RESULT: the command and the status reported for it. */
        std::string resultCommand;
        std::string resultStatus;
    };

    SimServer(VirtualClock& clock, HostTaskScheduler& scheduler) : _clock(clock), _scheduler(scheduler)
    {
        for (int i = 0; i < KIND_SLOTS; i++)
        {
            answerMs[i] = SERVER_ANSWER_MS;
            refuse[i] = false;
        }
    }

    static const int KIND_SLOTS = 32;
    std::vector<Call> calls;
    uint32_t answerMs[KIND_SLOTS];
    bool refuse[KIND_SLOTS];

    std::vector<SensorType> catalog;
    std::vector<ModuleCommandDto> pendingModuleCommands;
    /**
     * Answers the list as the backend does for statuses=New: a command stays on it, and is handed
     * out again by every fetch, until a status for it has been reported. Off: the list is handed
     * out once and emptied.
     */
    bool keepUntilReported = false;
    /** The radio: a call made while it has no link is not answered, whoever makes it. */
    IWifiAdapter* link = nullptr;
    /** Whether the server holds the module as deleted: what its is_deleted query answers. */
    bool moduleDeleted = false;
    /** The answer times of the next is_deleted queries, in order; SERVER_ANSWER_MS once used up. */
    std::deque<uint32_t> deletionAnswerMs;

    void setAnswerMs(OutboxKind kind, uint32_t ms) { answerMs[(int)kind] = ms; }

    /** @brief Calls the outbox's network task made, in order: the module's network acts. */
    std::vector<Call> networkActs() const
    {
        std::vector<Call> acts;
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i].task == "outbox-net")
            {
                acts.push_back(calls[i]);
            }
        }
        return acts;
    }

    /** @brief The first network act of this kind, or nullptr. */
    const Call* firstAct(OutboxKind kind) const
    {
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i].task == "outbox-net" && calls[i].kind == kind)
            {
                return &calls[i];
            }
        }
        return nullptr;
    }

    /** @brief The first network act of this kind that started at or after this moment, or nullptr. */
    const Call* firstActFrom(OutboxKind kind, uint64_t fromMs) const
    {
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i].task == "outbox-net" && calls[i].kind == kind && calls[i].startedAtMs >= fromMs)
            {
                return &calls[i];
            }
        }
        return nullptr;
    }

    /** @brief Network acts of this kind that started inside [fromMs, untilMs). */
    size_t actCountBetween(OutboxKind kind, uint64_t fromMs, uint64_t untilMs) const
    {
        size_t count = 0;
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i].task == "outbox-net" && calls[i].kind == kind && calls[i].startedAtMs >= fromMs &&
                calls[i].startedAtMs < untilMs)
            {
                count++;
            }
        }
        return count;
    }

    size_t actCount(OutboxKind kind) const
    {
        size_t count = 0;
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i].task == "outbox-net" && calls[i].kind == kind)
            {
                count++;
            }
        }
        return count;
    }

    void printActs(size_t limit = 40) const
    {
        const std::vector<Call> acts = networkActs();
        for (size_t i = 0; i < acts.size() && i < limit; i++)
        {
            printf("  s19 wire %2u: %-22s %7lu -> %7lu ms%s\n", (unsigned)i, kindName(acts[i].kind),
                   (unsigned long)acts[i].startedAtMs, (unsigned long)acts[i].endedAtMs,
                   acts[i].answered ? "" : " (refused)");
        }
    }

    static const char* kindName(OutboxKind kind)
    {
        switch (kind)
        {
        case OutboxKind::SENSOR_UPDATE: return "SENSOR_UPDATE";
        case OutboxKind::DEVICE_UPDATE: return "DEVICE_UPDATE";
        case OutboxKind::DIAGNOSTIC: return "DIAGNOSTIC";
        case OutboxKind::RULE_LIST: return "RULE_LIST";
        case OutboxKind::RULE_STATE_BULK: return "RULE_STATE_BULK";
        case OutboxKind::RULE_STATE_BY_UID: return "RULE_STATE_BY_UID";
        case OutboxKind::RULE_SENSOR_OFFLINE: return "RULE_SENSOR_OFFLINE";
        case OutboxKind::DEVICE_COMMAND_FETCH: return "DEVICE_COMMAND_FETCH";
        case OutboxKind::MODULE_COMMAND_FETCH: return "MODULE_COMMAND_FETCH";
        case OutboxKind::COMMAND_RESULT: return "COMMAND_RESULT";
        case OutboxKind::DEVICE_COMMAND_RESULT: return "DEVICE_COMMAND_RESULT";
        case OutboxKind::MODULE_COMMAND_RESULT: return "MODULE_COMMAND_RESULT";
        case OutboxKind::RAW_IO_RESULT: return "RAW_IO_RESULT";
        case OutboxKind::MODULE_STRUCTURE: return "MODULE_STRUCTURE";
        case OutboxKind::SENSOR_TYPES_FETCH: return "SENSOR_TYPES_FETCH";
        default: return kind == IS_DELETED_QUERY ? "IS_DELETED" : "OTHER";
        }
    }

    // --- the calls ----------------------------------------------------------------------------

    bool sendToServerModuleStructure(const ModuleStructure& structure,
                                     ApiPriority = ApiPriority::UNDEFINED) override
    {
        Call& call = begin(OutboxKind::MODULE_STRUCTURE);
        if (!structure.nodes.empty())
        {
            call.ip = structure.nodes[0].ip;
            call.structureSensors = structure.nodes[0].sensors;
        }
        return end(call);
    }

    bool sendModuleUpdateData(const UpdateDto& update, ApiPriority = ApiPriority::UNDEFINED) override
    {
        Call& call = begin(update.devices.empty() ? OutboxKind::SENSOR_UPDATE : OutboxKind::DEVICE_UPDATE);
        call.sensors = update.sensors;
        call.updateMillis = update.millis;
        return end(call);
    }

    bool sendDiagnosticData(const DiagnosticDto& diagnostic, ApiPriority = ApiPriority::UNDEFINED) override
    {
        Call& call = begin(OutboxKind::DIAGNOSTIC);
        call.diagnosticUptimeMs = diagnostic.uptimeMs;
        call.diagnosticJson = _json.diagnosticToJson(diagnostic);
        return end(call);
    }

    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        Call& call = begin(OutboxKind::MODULE_COMMAND_RESULT);
        call.commandUpdates = updates;
        return end(call);
    }

    /** @brief How many answered outcome batches told the server this status for this command. */
    size_t reportedCount(const char* uid, const char* status) const
    {
        size_t count = 0;
        for (size_t i = 0; i < calls.size(); i++)
        {
            for (size_t j = 0; calls[i].answered && j < calls[i].commandUpdates.size(); j++)
            {
                const ModuleCommandUpdateDto& update = calls[i].commandUpdates[j];
                count += (update.uid == uid && update.status == status) ? 1 : 0;
            }
        }
        return count;
    }

    /** @brief How many answered outcome batches carried a status, whichever, for this command. */
    size_t statusesReportedFor(const char* uid) const
    {
        size_t count = 0;
        for (size_t i = 0; i < calls.size(); i++)
        {
            for (size_t j = 0; calls[i].answered && j < calls[i].commandUpdates.size(); j++)
            {
                count += (calls[i].commandUpdates[j].uid == uid) ? 1 : 0;
            }
        }
        return count;
    }

    bool sendCommandResult(const CommandResultDto& result, ApiPriority = ApiPriority::UNDEFINED) override
    {
        Call& call = begin(OutboxKind::COMMAND_RESULT);
        call.resultCommand = result.command;
        call.resultStatus = result.status;
        return end(call);
    }

    ModuleDeletionQueryResult queryModuleDeletion() override
    {
        if (!deletionAnswerMs.empty())
        {
            setAnswerMs(IS_DELETED_QUERY, deletionAnswerMs.front());
            deletionAnswerMs.pop_front();
        }
        const bool answered = end(begin(IS_DELETED_QUERY));
        setAnswerMs(IS_DELETED_QUERY, SERVER_ANSWER_MS);
        ModuleDeletionQueryResult result;
        result.received200 = answered;
        result.queryFailed = !answered;
        result.isDeletedFlag = answered ? (moduleDeleted ? 1 : 0) : -1;
        result.finalHttpCode = answered ? 200 : -1;
        return result;
    }

    /** @brief Calls of this kind made by this task, whoever's they are. */
    std::vector<Call> callsOf(OutboxKind kind, const char* task) const
    {
        std::vector<Call> found;
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i].kind == kind && calls[i].task == task)
            {
                found.push_back(calls[i]);
            }
        }
        return found;
    }

    /** @brief How many answered results of this command carried this status. */
    size_t commandResultCount(const char* command, const char* status) const
    {
        size_t count = 0;
        for (size_t i = 0; i < calls.size(); i++)
        {
            count += (calls[i].answered && calls[i].resultCommand == command && calls[i].resultStatus == status) ? 1 : 0;
        }
        return count;
    }

    bool sendRuleStateCommand(const char*, ApiPriority = ApiPriority::UNDEFINED) override
    {
        return end(begin(OutboxKind::RULE_STATE_BULK));
    }

    std::vector<Rule> getRuleList(bool* httpFailed = nullptr, bool* readWhole = nullptr) override
    {
        const bool answered = end(begin(OutboxKind::RULE_LIST));
        if (httpFailed != nullptr) *httpFailed = !answered;
        if (readWhole != nullptr) *readWhole = answered;
        return std::vector<Rule>();
    }

    std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) override
    {
        // The statuses already reported are read before the fetch is on the wire: what the server
        // hands out is what it held when the request reached it.
        std::vector<ModuleCommandDto> stillNew;
        for (size_t i = 0; keepUntilReported && i < pendingModuleCommands.size(); i++)
        {
            if (statusesReportedFor(pendingModuleCommands[i].uid.c_str()) == 0)
            {
                stillNew.push_back(pendingModuleCommands[i]);
            }
        }
        const bool answered = end(begin(OutboxKind::MODULE_COMMAND_FETCH));
        if (httpFailed != nullptr) *httpFailed = !answered;
        std::vector<ModuleCommandDto> answer;
        if (answered)
        {
            if (keepUntilReported)
            {
                answer = stillNew;
            }
            else
            {
                answer.swap(pendingModuleCommands);
            }
        }
        return answer;
    }

    std::vector<DeviceCommandDto> getDeviceCommand(bool* httpFailed = nullptr) override
    {
        const bool answered = end(begin(OutboxKind::DEVICE_COMMAND_FETCH));
        if (httpFailed != nullptr) *httpFailed = !answered;
        return std::vector<DeviceCommandDto>();
    }

    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        const bool answered = end(begin(OutboxKind::SENSOR_TYPES_FETCH));
        if (httpFailed != nullptr) *httpFailed = !answered;
        return answered ? catalog : std::vector<SensorType>();
    }

private:
    Call& begin(OutboxKind kind)
    {
        Call call;
        call.kind = kind;
        call.startedAtMs = _clock.nowMs();
        call.task = _scheduler.currentTaskName();
        calls.push_back(call);
        _open = calls.size() - 1;
        _linkAtBegin = (link == nullptr) || link->isConnected();
        return calls.back();
    }

    /** Spends the answer time on the calling task; returns whether the server answered. */
    bool end(Call& opened)
    {
        const size_t index = _open;
        const OutboxKind kind = opened.kind;
        const uint32_t ms = answerMs[(int)kind];
        if (ms > 0)
        {
            // The vector may grow while this task waits - another task's call - so the record is
            // found again by index afterwards.
            _scheduler.delayMs(ms);
        }
        Call& call = calls[index];
        call.endedAtMs = _clock.nowMs();
        call.answered = !refuse[(int)kind] && _linkAtBegin;
        return call.answered;
    }

    VirtualClock& _clock;
    HostTaskScheduler& _scheduler;
    size_t _open = 0;
    bool _linkAtBegin = true;
    ArduinoJsonConverter _json;
};

/**
 * @brief The broker as the module sees it: the boot's CONNECT, made by the client's state machine on
 *        the network task once the link is up, and the inbound half - a queue drained on the task that
 *        calls loop(), delivered only to a connected session.
 *
 * After the boot's CONNACK the connection is the test's to drive, through `connected`.
 */
class ScriptedMqttManager : public IMqttManager, public IMqttLinkMachine {
public:
    bool connected = false;
    /** Topics subscribed to, and when - kept, as MqttManager keeps them, whether connected or not. */
    std::vector<std::string> subscribed;
    std::vector<uint64_t> subscribedAtMs;
    VirtualClock* clock = nullptr;
    IWifiAdapter* link = nullptr;
    /** From the CONNECT to the CONNACK. */
    uint32_t connackMs = SERVER_ANSWER_MS;
    /** When the boot's CONNACK came; 0 while it has not. */
    uint64_t connackAtMs = 0;

    /** MqttManager::connect(): configures the client; with no link that is all it does. */
    bool connect() override
    {
        configured = true;
        return false;
    }

    bool serviceLinkOnce() override
    {
        if (!configured || connackAtMs > 0 || clock == nullptr)
        {
            return false;
        }
        if (connected)
        {
            connackAtMs = clock->nowMs();
            return false;
        }
        if (link == nullptr || !link->isConnected())
        {
            _connectAtMs = 0;
            return false;
        }
        if (_connectAtMs == 0)
        {
            _connectAtMs = clock->nowMs();
        }
        if (clock->nowMs() - _connectAtMs < connackMs)
        {
            return true;
        }
        connected = true;
        connackAtMs = clock->nowMs();
        return false;
    }
    void disconnect() override {}
    bool isConnected() override { return connected; }
    bool subscribe(const char* topic) override
    {
        subscribed.push_back(topic != nullptr ? topic : "");
        subscribedAtMs.push_back(clock != nullptr ? clock->nowMs() : 0);
        return true;
    }
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
        while (connected && !_inbound.empty())
        {
            const std::string payload = _inbound.front();
            _inbound.pop_front();
            if (_handler != nullptr)
            {
                _handler->handleMessage("GrowDirectorMqttTopic/s19-module",
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
    bool configured = false;
    uint64_t _connectAtMs = 0;
};

/**
 * @brief The completion queue between the network task and the loop, which can lose one completion.
 *
 * What the board's queue does when it is full - it refuses the push, and the outbox counts the outcome
 * as lost. The queue is sized so that this does not happen; the stand makes it happen once, to see
 * what a wait for a completion that never comes does.
 */
class LosingCompletionQueue : public HostMessageQueue<OutboxCompletion> {
public:
    explicit LosingCompletionQueue(size_t capacity) : HostMessageQueue<OutboxCompletion>(capacity) {}

    void loseTheNextOf(OutboxKind kind)
    {
        _losing = true;
        _kind = kind;
    }

    bool push(const OutboxCompletion& item) override
    {
        if (_losing && item.kind == _kind)
        {
            _losing = false;
            lost++;
            return false;
        }
        return HostMessageQueue<OutboxCompletion>::push(item);
    }

    uint32_t lost = 0;

private:
    bool _losing = false;
    OutboxKind _kind = OutboxKind::SENSOR_UPDATE;
};

/**
 * @brief The boot sequence's ceilings as globals.h derives them: the same owners' constants, and the
 *        same two facts of the HTTP stack and the reader that no constant names - the handshake
 *        counted at WiFiClientSecure's default 120 s, as globals.h counts it.
 */
BootSequencer::Ceilings boardCeilings()
{
    BootSequencer::CeilingInputs inputs;
    inputs.restAttemptMs = 15000 + 5000 + 120000 + 30000;
    inputs.attemptsPerJob = Outbox::DEFAULT_MAX_ATTEMPTS;
    inputs.firstBackoffMs = Outbox::DEFAULT_BACKOFF_BASE_MS;
    inputs.backoffFactor = Outbox::BACKOFF_FACTOR;
    inputs.mqttActMs = MqttManager::CLOSE_GRACE_MS + MqttManager::CONNECT_TIMEOUT_MS;
    inputs.jobsAheadOfTheCatalog = 6;
    inputs.portWakeUpMs = PortPower::WAKE_UP_MS;
    inputs.sweepIntervalMs = SensorAcquisitionService::SWEEP_INTERVAL_MS;
    inputs.channelReadMs = 3 * 300 + 2 * 100;
    return BootSequencer::ceilingsFrom(inputs);
}

/** @brief One decision about the link or a restart, and when the module made it. */
struct Decision {
    uint64_t atMs = 0;
    std::string what;
    /** The control loop's iterations so far. */
    uint64_t iteration = 0;
};

/**
 * @brief The wall clock as SNTP leaves it: it runs with the module's uptime, from a moment the stand
 *        chooses. Asked to synchronize again, it says so.
 */
class SimWallClock : public MockNTPTimeAdapter {
public:
    explicit SimWallClock(VirtualClock& clock) : _clock(clock) {}

    /** @brief Start the wall clock at this local hour and minute, of a day of September 2025. */
    void startAtLocal(int hour, int minute)
    {
        struct tm local;
        memset(&local, 0, sizeof(local));
        local.tm_year = 2025 - 1900;
        local.tm_mon = 8;
        local.tm_mday = 5;
        local.tm_hour = hour;
        local.tm_min = minute;
        local.tm_isdst = -1;
        _epochAtStart = static_cast<unsigned long>(mktime(&local));
        _uptimeAtStartMs = _clock.nowMs();
    }

    unsigned long getEpochTime() override
    {
        return _epochAtStart + static_cast<unsigned long>((_clock.nowMs() - _uptimeAtStartMs) / 1000);
    }

    bool getLocalTime(struct tm* out) override
    {
        if (out == nullptr)
        {
            return false;
        }
        const time_t now = static_cast<time_t>(getEpochTime());
        localtime_r(&now, out);
        return true;
    }

    void forceSyncTime() override
    {
        MockNTPTimeAdapter::forceSyncTime();
        if (decisions != nullptr)
        {
            Decision decision;
            decision.atMs = _clock.nowMs();
            decision.what = "SNTP_FORCED";
            decisions->push_back(decision);
        }
    }

    std::vector<Decision>* decisions = nullptr;

private:
    VirtualClock& _clock;
    unsigned long _epochAtStart = 0;
    uint64_t _uptimeAtStartMs = 0;
};

/** @brief The rule sync as the connectivity edge sees it: the same sync, every forced ask recorded. */
class RecordingRuleSync : public ISyncRulesUseCase {
public:
    RecordingRuleSync(ISyncRulesUseCase& inner, VirtualClock& clock) : _inner(inner), _clock(clock) {}

    void onOutboxCompletion(const OutboxCompletion& completion) override { _inner.onOutboxCompletion(completion); }
    bool init() override { return _inner.init(); }
    void loop() override { _inner.loop(); }
    bool forceSync() override
    {
        if (decisions != nullptr)
        {
            Decision decision;
            decision.atMs = _clock.nowMs();
            decision.what = "RULES_FORCED_ON_RESTORE";
            decisions->push_back(decision);
        }
        return _inner.forceSync();
    }
    bool getLastSyncStatus() const override { return _inner.getLastSyncStatus(); }
    time_t getLastSyncTime() const override { return _inner.getLastSyncTime(); }

    std::vector<Decision>* decisions = nullptr;

private:
    ISyncRulesUseCase& _inner;
    VirtualClock& _clock;
};

/** @brief The factory DeviceService::init() asks for the real state machine of the relays. */
class RealDeviceStateFactory : public IProcessDeviceStateUseCaseFactory {
public:
    RealDeviceStateFactory(ILogger* logger, IDebugSystem* debugSystem, IModuleConfig* moduleConfig,
                           IArduinoAdapter* arduino, IDeviceModel* deviceModel)
        : _logger(logger), _debugSystem(debugSystem), _moduleConfig(moduleConfig), _arduino(arduino),
          _deviceModel(deviceModel)
    {
    }

    IProcessDeviceStateUseCase* create(int) override
    {
        return new ProcessDeviceStateUseCase(_logger, _debugSystem, _moduleConfig, _arduino, _deviceModel);
    }

private:
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IModuleConfig* _moduleConfig;
    IArduinoAdapter* _arduino;
    IDeviceModel* _deviceModel;
};

// --- what is on the line, and what the module knows -----------------------------------------------

/** @brief The registers of the two-channel model the stand's sensors are: temperature, humidity. */
std::vector<ModbusRegisterConfig> twoChannelRegisters()
{
    std::vector<ModbusRegisterConfig> configs;
    for (int channel = 1; channel <= 2; channel++)
    {
        ModbusRegisterConfig config;
        config.functionCode = 0x03;
        config.startRegister = static_cast<uint16_t>(channel - 1);
        config.numRegisters = 1;
        config.multiplier = 0.1f;
        config.channel = channel;
        config.channelName = channel == 1 ? "temperature" : "humidity";
        config.sensorType = config.channelName;
        config.canConnectMinValue = 0.0f;
        config.canConnectMaxValue = 100.0f;
        configs.push_back(config);
    }
    return configs;
}

SensorType makeCatalogType()
{
    SensorType type;
    type.uid = "type-th";
    type.name = "S19-TH";
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = true;
    type.isSupported = true;
    type.baudRate = 9600;
    type.modbusDeviceAddress = 1;
    type.modbusPingAddress = 0x0000;
    type.updatedAt = "2026-09-01T00:00:00Z";
    type.modbusRegisterConfig = twoChannelRegisters();
    return type;
}

/**
 * @brief A device of the stand's model as the board sees one: it needs PortPower::WAKE_UP_MS of power
 *        before it answers, and a read of a register answers that register.
 */
SimRs485Bus::Device makeDevice(uint8_t address)
{
    SimRs485Bus::Device device;
    device.address = address;
    device.registerValues.push_back(250);
    device.registerValues.push_back(600);
    device.registersAreAddressed = true;
    device.wakeUpMs = PortPower::WAKE_UP_MS;
    return device;
}

/** @brief A sensor the module stored before the restart, on a port of its own range's address. */
SensorEntity makeStoredSensor(int pin)
{
    SensorEntity sensor;
    char uid[40];
    snprintf(uid, sizeof(uid), "%s.%02d", MODULE_UID, pin);
    sensor.uid = uid;
    sensor.protocol = "ModBus";
    sensor.model = "S19-TH";
    sensor.sensorType = "temperature";
    sensor.sensorTypeUid = "type-th";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.status = SENSOR_STATUS_ADDED;
    sensor.pin = pin;
    sensor.modbusDeviceAddress = MODBUS_ADDRESS_MIN + pin;
    sensor.baudRate = 9600;
    sensor.modbusRegisterConfigs = twoChannelRegisters();
    return sensor;
}

/** @brief How the boot under test goes. */
struct Boot {
    /** How long the boot's association takes, on the network task from the loop's first iteration -
     *  as long as setup took in device logs, where all of it was the Wi-Fi's. */
    uint32_t associationMs = 12000;
    bool linkAtBoot = true;
    /** Stored sensors, by port; a device answering at its row's address is put on each of them
     *  unless the port is listed as silent too. */
    std::vector<int> storedOn;
    std::vector<int> silentStoredOn;
    /** The stored sensors' second channel has a type the catalog's model no longer gives it. */
    bool storedBehindTheCatalog = false;
    bool catalogOnFlash = true;
    bool tasksStartAtOnce = false;
    /** A relay module with two relays, instead of a sensor module. */
    bool deviceModule = false;
    /** How long the server takes over a structure; SERVER_ANSWER_MS when 0. */
    uint32_t structureAnswerMs = 0;
    /** The button was held at the last power-on: the discovery is skipped on this boot. */
    bool noScanSensors = false;
    /** The server has taken this module's structure before; false for one it has never seen. */
    bool registered = true;
};

/** @brief A relay as DeviceService reports it; value-initialized, as `new DeviceDigitalOut()` is. */
void setUpRelay(DeviceDigitalOut& relay, const char* uid)
{
    relay.deviceUid = uid;
    relay.deviceType = DEVICE_TYPE_DIGITAL;
    relay.state = DEVICE_STATE_OFF;
    relay.isError = false;
    relay.isAuto = false;
    relay.isManual = false;
    relay.workByRule = false;
    relay.workManual = false;
    relay.needToSwitchBack = false;
    relay.needReportingRuleUid = false;
    relay.commandId = 0;
    relay.ruleUid = nullptr;
    relay.switchedAt = nullptr;
    relay.ruleType = RuleType::SENSOR;
    relay.ruleVersion = 0;
    relay.workflowUid = nullptr;
    relay.workflowVersion = 0;
    relay.sensorUid = nullptr;
    relay.ruleSensorStartAt = nullptr;
    relay.ruleSensorStopAt = nullptr;
    relay.ruleStopReason = nullptr;
    relay.ruleScheduleStartAt = nullptr;
    relay.isRuleTurnOnDevice = false;
}

/** @brief The message a broker hands a module's session when it connects, as in device logs. */
std::string moduleCommandSignalJson(const char* command)
{
    char buffer[384];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"%s\",\"options\":{}}",
             MODULE_UID, command);
    return std::string(buffer);
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

    TapeLogger logger;
    TapeDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    SimLink wifiAdapter;
    SimLinkProbe linkProbe;
    ModuleStatusBoard statusBoard;
    MockWiFiManager wifiManager;
    SimWallClock timeAdapter;
    MockRestAPIService restApi;
    MockSpiffsSensorDao sensorDao;
    SensorRepository sensorRepository;
    MockSpiffsSensorTypeDao sensorTypeDao;
    SensorTypeRepository sensorTypeRepository;
    SensorTypeCatalogWriter catalogWriter;
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;
    ArduinoJsonConverter jsonConverter;
    MockRuleRepository ruleRepository;
    MockDeviceModel deviceModel;
    DeviceDigitalOut relay1;
    DeviceDigitalOut relay2;

    ModbusSensorReader modbusSensorReader;
    RawBusAccessAdapter rawBusAccess;
    SensorReader sensorReader;

    SimServer server;

    // The firmware update's half of the module, wired as globals.h wires it: the real download
    // session over a simulated socket and flash slot, and the file an update is kept in.
    SimOtaHttpClient otaHttp;
    MockFirmwareImageWriter imageWriter;
    MockOtaUpdateLog otaLog;
    FirmwareRemoteUpdate firmwareUpdater;
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter pendingUpdateConverter;
    PendingFirmwareUpdateStore pendingUpdateStore;
    MockOtaPartitionControl otaPartitionControl;
    FirmwareUpdateRecoveryUseCase updateRecovery;

    // The factory reset and the deletion check, as globals.h wires them for the command path.
    MockSpiffsRuleDao ruleDao;
    ClearModuleDataService clearModuleData;
    CheckModuleDeletionUseCase deletionCheck;

    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModbusAddressAllocator modbusAddressAllocator;
    ModuleCommandExecutionUseCase moduleCommandUseCase;
    StartupSensorScanUseCase startupScan;

    SensorAcquisitionService acquisition;
    LosingCompletionQueue completions;
    Outbox outbox;
    NetworkCommandRunner networkRunner;
    RepublishModuleStructureUseCase republish;

    CommandService commandService;
    CommandTopicHandler topicHandler;
    ScriptedMqttManager mqttManager;

    SyncRulesUseCase syncRules;
    SyncSensorTypesUseCase syncSensorTypes;
    OnChangeLocalIpUseCase onChangeLocalIp;
    LinkSupervisor linkSupervisor;
    DiagnosticService diagnosticService;
    SensorService sensorService;
    RealDeviceStateFactory deviceStateFactory;
    DeviceService deviceService;
    RuleService ruleService;
    RemoteSensorReceiver remoteSensors;
    RecordingRuleSync rulesOnRestore;
    OnConnectivityRestoredUseCase connectivityRestored;
    PreventiveRestartUseCase preventiveRestart;
    BootSequencer bootSequencer;
    LoopServiceConfigureDone loopService;

    MockButtonUseCases buttonUseCases;
    ModuleSetupService setupService;

    /** What the module decided about its link and its restarts, and when - see watchTheDecisions(). */
    std::vector<Decision> decisions;
    int reconnectsSeen = 0;
    uint32_t catalogAsksSeen = 0;
    int downloadsSeen = 0;

    /** When the discovery's first step reached the line, as the loop saw it; 0 while none has. */
    uint64_t firstScanStepAtMs = 0;
    /** When the catalog sync first handed a fetch to the outbox, as the loop saw it; 0 while it has not. */
    uint64_t firstCatalogAskAtMs = 0;
    uint64_t loopIterations = 0;

    explicit ModuleUnderTest(const Boot& boot)
        : scheduler(clock),
          arduino(clock, &scheduler),
          busLock(scheduler, clock),
          port0(clock, "sim-port-0"),
          port1(clock, "sim-port-1"),
          port2(clock, "sim-port-2"),
          port3(clock, "sim-port-3"),
          ports{&port0, &port1, &port2, &port3},
          logger(clock),
          linkProbe(wifiAdapter, scheduler),
          timeAdapter(clock),
          moduleConfig(&logger, &debugSystem, &settings),
          sensorRepository(&sensorDao),
          sensorTypeRepository(&sensorTypeDao, &debugSystem, &logger),
          catalogWriter(&sensorTypeDao, &scheduler, &logger, &debugSystem),
          relay1(),
          relay2(),
          modbusSensorReader(ports, arduino, &logger, &debugSystem),
          rawBusAccess(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          server(clock, scheduler),
          otaHttp(clock),
          firmwareUpdater(&otaHttp, &imageWriter, &arduino, &wifiAdapter, &logger, &debugSystem, &otaLog),
          pendingUpdateStore(&spiffs, &pendingUpdateConverter),
          updateRecovery(&linkSupervisor, &moduleConfig, &settings, &pendingUpdateStore, &logger, &debugSystem,
                         &otaLog, &otaPartitionControl),
          clearModuleData(&ruleDao, &sensorDao, &sensorTypeDao, &logger, &debugSystem, &settings, &spiffs),
          deletionCheck(&server, &logger, &debugSystem),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          modbusAddressAllocator(&sensorRepository),
          moduleCommandUseCase(&logger, &debugSystem, &timeAdapter, &firmwareUpdater, &arduino, &sensorRepository,
                               &sensorTypeRepository, &sensorReader, &moduleConfig, &settings, &server,
                               &clearModuleData, &moduleStructure, nullptr, nullptr, nullptr, &rawBusAccess,
                               &deletionCheck, nullptr, &updateRecovery, &modbusAddressAllocator),
          startupScan(&logger, &debugSystem, &moduleConfig, &sensorRepository, &sensorTypeRepository,
                      &sensorReader, &moduleStructure, &sensorStructure, &modbusAddressAllocator,
                      &sensorValueModel, &virtualChannels, &arduino),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          completions(Outbox::COMPLETION_QUEUE_DEPTH),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          networkRunner(&scheduler, &logger, &debugSystem),
          republish(&moduleStructure, &sensorRepository, &server, &sensorStructure, &logger, &debugSystem,
                    &arduino),
          commandService(nullptr, &moduleCommandUseCase, nullptr, &moduleStructure, &logger, &debugSystem,
                         &timeAdapter, &jsonConverter, nullptr, &settings),
          topicHandler(&logger, &debugSystem, MODULE_UID, &commandService, &jsonConverter),
          syncRules(&server, &ruleRepository, &arduino, &timeAdapter, &jsonConverter, &logger, &debugSystem),
          syncSensorTypes(&server, &sensorTypeRepository, &arduino, &timeAdapter, &jsonConverter, &logger,
                          &debugSystem, &sensorRepository, &moduleConfig),
          onChangeLocalIp(wifiAdapter, wifiManager, moduleStructure, moduleConfig, syncRules, syncSensorTypes,
                          logger, debugSystem),
          linkSupervisor(&restApi, &moduleConfig, logger, debugSystem, &wifiAdapter, &timeAdapter, &arduino,
                         &mqttManager),
          diagnosticService(&restApi, &moduleConfig, logger, debugSystem, &jsonConverter, &wifiAdapter,
                            &timeAdapter, &arduino, &mqttManager, &server),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader, &sensorValueModel,
                        &timeAdapter, &arduino, &server, &logger, &debugSystem),
          deviceStateFactory(&logger, &debugSystem, &moduleConfig, &arduino, &deviceModel),
          deviceService(&moduleConfig, &moduleStructure, &deviceModel, &timeAdapter, &arduino,
                        &deviceStateFactory, nullptr, &logger, &debugSystem),
          ruleService(nullptr, &syncRules, &deviceModel, &logger, &debugSystem),
          remoteSensors(nullptr, nullptr, &mqttManager, nullptr, &sensorValueModel, &ruleRepository,
                        &sensorRepository, &jsonConverter, &arduino, &logger, &debugSystem),
          rulesOnRestore(syncRules, clock),
          connectivityRestored(&linkSupervisor, &rulesOnRestore, &moduleConfig, &arduino, &logger, &debugSystem),
          preventiveRestart(&moduleConfig, &deviceModel, nullptr, nullptr, &timeAdapter, &logger, &debugSystem),
          bootSequencer(&arduino, &logger, &debugSystem),
          setupService(&logger, &debugSystem, &moduleConfig, nullptr, nullptr, nullptr, &buttonUseCases,
                       &wifiAdapter, &wifiManager, nullptr, nullptr, &mqttManager, &moduleStructure,
                       &deviceService, &ruleService, &sensorRepository, &sensorTypeRepository, &sensorReader,
                       nullptr, nullptr, nullptr, nullptr, nullptr, &arduino, &syncSensorTypes,
                       &remoteSensors, &connectivityRestored)
    {
        debugSystem.enabled.insert(DebugFeatures::SETUP_DEBUG);
        timeAdapter.decisions = &decisions;
        rulesOnRestore.decisions = &decisions;
        debugSystem.enabled.insert(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG);
        mqttManager.clock = &clock;
        scheduler.setNewTasksStartAtOnce(boot.tasksStartAtOnce);

        wirePortsAsTheBoardDoes();

        timeAdapter.startAtLocal(12, 0);
        timeAdapter.setCommandTimeValid(true);

        settings.setUser("s19@example.com");
        settings.setAccountUid("s19-account");
        settings.setNoScanSensors(boot.noScanSensors);
        settings.setWasInitialSyncCompleted(boot.registered ? 1 : 0);
        settings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
        moduleConfig.setSettings(&settings);
        moduleConfig.setWifiAdapter(&wifiAdapter);
        moduleConfig.setModuleUid(MODULE_UID);
        if (boot.deviceModule)
        {
            moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
            moduleConfig.setModuleSubType(MODULE_SUBTYPE_RELAY);
            moduleConfig.setDeviceCount(2);
            setUpRelay(relay1, "s19-relay-1");
            setUpRelay(relay2, "s19-relay-2");
            deviceModel.addDevice(&relay1);
            deviceModel.addDevice(&relay2);
        }
        else
        {
            moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
            moduleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
            moduleConfig.setDeviceCount(0);
        }
        moduleConfig.setSensorPinCount(MODULE_PIN_COUNT);
        moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
        wifiAdapter.setMACAddress(MODULE_UID);
        wifiAdapter.setLocalIP(MODULE_IP);
        wifiAdapter.connected = false;
        wifiAdapter.linkAvailable = boot.linkAtBoot;

        // The boot's association, on the network task: what device logs show it to be.
        wifiAdapter.bootAssociationMs = boot.associationMs;
        wifiAdapter.scheduler = &scheduler;
        mqttManager.link = &wifiAdapter;

        if (boot.structureAnswerMs > 0)
        {
            server.setAnswerMs(OutboxKind::MODULE_STRUCTURE, boot.structureAnswerMs);
        }
        server.catalog.push_back(makeCatalogType());
        if (boot.catalogOnFlash)
        {
            sensorTypeDao.addToStorage(makeCatalogType());
        }

        for (size_t i = 0; i < boot.storedOn.size(); i++)
        {
            const int pin = boot.storedOn[i];
            SensorEntity stored = makeStoredSensor(pin);
            if (boot.storedBehindTheCatalog)
            {
                stored.modbusRegisterConfigs[1].sensorType = "humidity_dig";
            }
            sensorRepository.save(stored);
            bool silent = false;
            for (size_t j = 0; j < boot.silentStoredOn.size(); j++)
            {
                silent = silent || boot.silentStoredOn[j] == pin;
            }
            if (!silent)
            {
                port(pin).addDevice(makeDevice(static_cast<uint8_t>(MODBUS_ADDRESS_MIN + pin)));
            }
        }

        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        rawBusAccess.setBusLock(&busLock);

        // As initDependencies() does it.
        setupService.setSensorStructureUseCase(&sensorStructure);
        setupService.setStartupSensorScan(&startupScan);
        moduleCommandUseCase.setSensorStructureUseCase(&sensorStructure);
        moduleCommandUseCase.setSyncSensorTypesUseCase(&syncSensorTypes);

        syncSensorTypes.setVirtualSensorChannelsUseCase(&virtualChannels);
        syncSensorTypes.setRepublishModuleStructureUseCase(&republish);
        syncSensorTypes.setSensorValueModel(&sensorValueModel);
        syncSensorTypes.setNetworkStatusProvider(&linkSupervisor);

        outbox.setWifiAdapter(&wifiAdapter);
        outbox.setMqttLink(&mqttManager);
        outbox.setLinkProbe(&linkProbe, boardLinkProbePlan());
        outbox.setStatusBoard(&statusBoard);
        linkSupervisor.setStatusBoard(&statusBoard);
        republish.setOutbox(&outbox);
        diagnosticService.setOutbox(&outbox);
        linkSupervisor.setOutbox(&outbox);
        diagnosticService.setLinkSupervisor(&linkSupervisor);
        sensorService.setOutbox(&outbox);
        deviceService.setOutbox(&outbox);
        syncRules.setOutbox(&outbox);
        commandService.setOutbox(&outbox);
        syncSensorTypes.setOutbox(&outbox);
        syncSensorTypes.setCatalogWriter(&catalogWriter);
        onChangeLocalIp.setOutbox(&outbox);
        onChangeLocalIp.setClock(&arduino);

        sensorService.setAcquisition(&acquisition);
        acquisition.setRawCommandExecutor(&moduleCommandUseCase);
        acquisition.setStartupSensorScan(&startupScan);
        commandService.setBusCommandExecutor(&acquisition);
        commandService.setStartupSensorScan(&startupScan);
        startupScan.setRepublishModuleStructureUseCase(&republish);
        startupScan.setRawBusAccess(&rawBusAccess);
        startupScan.setPortLines(modbusSensorReader.portLines());
        republish.setStoredSensorDescriber(&startupScan);
        networkRunner.setExecutor(&moduleCommandUseCase);
        commandService.setNetworkCommandRunner(&networkRunner);
        commandService.setFirmwareUpdateRecovery(&updateRecovery);
        mqttManager.setHandler(&topicHandler);
        server.link = &wifiAdapter;

        bootSequencer.setWifiAdapter(&wifiAdapter);
        bootSequencer.setOnChangeLocalIpUseCase(&onChangeLocalIp);
        bootSequencer.setSensorService(&sensorService);
        bootSequencer.setSyncSensorTypesUseCase(&syncSensorTypes);
        bootSequencer.setStartupSensorScan(&startupScan);
        sensorService.setBootGates(&bootSequencer);
        ruleService.setBootGates(&bootSequencer);
        deviceService.setBootGates(&bootSequencer);
        startupScan.setBootGates(&bootSequencer);
        republish.setBootGates(&bootSequencer);
        bootSequencer.setMqttManager(&mqttManager);
        bootSequencer.setRemoteSensorReceiver(&remoteSensors);
        bootSequencer.setSyncRulesUseCase(&syncRules);
        bootSequencer.setNetworkListeners(&setupService);
        bootSequencer.setBootAssociation(&linkSupervisor);
        bootSequencer.setCommandService(&commandService);
        bootSequencer.setClock(&timeAdapter);
        bootSequencer.setCeilings(boardCeilings());

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setCommandService(&commandService);
        loopService.setSensorService(&sensorService);
        loopService.setDeviceService(&deviceService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
        loopService.setOnChangeLocalIpUseCase(&onChangeLocalIp);
        loopService.setSyncSensorTypesUseCase(&syncSensorTypes);
        loopService.setSensorTypeCatalogWriter(&catalogWriter);
        loopService.setRepublishModuleStructureUseCase(&republish);
        loopService.setDiagnosticService(&diagnosticService);
        loopService.setLinkSupervisor(&linkSupervisor);
        loopService.setOnConnectivityRestoredUseCase(&connectivityRestored);
        loopService.setPreventiveRestartUseCase(&preventiveRestart);
        loopService.setBootSequencer(&bootSequencer);
    }

    /** @brief Two UARTs, four power pins, all of them down - globals.h's ports. */
    void wirePortsAsTheBoardDoes()
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

    /** @brief setup() as main.cpp runs it: the module setup, and nothing of the sensor service's own. */
    void bootAndStartTheLoop()
    {
        setupService.setupModule();
        startTheLoop();
    }

    SimRs485Bus& port(int pin) { return *static_cast<SimRs485Bus*>(ports[static_cast<size_t>(pin)]); }

    /** @brief The radio finds the network: the link has its address, and the broker its socket. */
    void raiseTheLink()
    {
        wifiAdapter.raise();
        mqttManager.connected = true;
    }

    /** @brief The access point goes: no association, no address, no broker. */
    void dropTheLink()
    {
        wifiAdapter.drop();
        mqttManager.connected = false;
    }

    /** @brief Moment of the first tape line that contains this text; 0 when there is none. */
    uint64_t tapeAtMs(const char* text) const
    {
        const int at = logger.indexOf(text);
        return at < 0 ? 0 : logger.lines[static_cast<size_t>(at)].atMs;
    }

    /** @brief Run in steps of a second until a tape line contains this text, or the time is up. */
    bool runUntilTheTapeSays(const char* text, uint64_t untilMs)
    {
        while (clock.nowMs() < untilMs)
        {
            if (logger.indexOf(text) >= 0)
            {
                return true;
            }
            scheduler.runForMs(1000);
        }
        return logger.indexOf(text) >= 0;
    }

    /** @brief One task, mirroring main.cpp: loop() runs the loop service and sleeps a period. */
    void startTheLoop()
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this]() {
            while (true)
            {
                loopService.loop();
                loopIterations++;
                if (firstScanStepAtMs == 0 && acquisition.startupScanStepCount() > 0)
                {
                    firstScanStepAtMs = clock.nowMs();
                }
                if (firstCatalogAskAtMs == 0 && syncSensorTypes.requestCount() > 0)
                {
                    firstCatalogAskAtMs = clock.nowMs();
                }
                watchTheDecisions();
                scheduler.watchdogFeed();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }

    void runUntilMs(uint64_t atMs) { scheduler.runUntilMs(atMs); }

    /** @brief The same, ten minutes at a time: the scheduler's step budget is per call. */
    void runLongUntilMs(uint64_t atMs)
    {
        while (clock.nowMs() < atMs && !scheduler.stepBudgetExhausted())
        {
            const uint64_t next = clock.nowMs() + 600000;
            scheduler.runUntilMs(next < atMs ? next : atMs);
        }
    }

    void noteDecision(const char* what)
    {
        Decision decision;
        decision.atMs = clock.nowMs();
        decision.what = what;
        decision.iteration = loopIterations;
        decisions.push_back(decision);
    }

    /**
     * @brief After every iteration of the loop: what the module asked of the radio, whether it asked
     *        for a restart, and whether the catalog sync asked the server again. A restart asked for
     *        is noted and taken back, so that the next one is seen too - nothing on the stand acts on
     *        it.
     */
    void watchTheDecisions()
    {
        if (wifiAdapter.reconnectCallCount != reconnectsSeen)
        {
            // The boot's association is the same job as a restart of the radio; the tape says which.
            const bool bootAssociation =
                reconnectsSeen == 0 && logger.indexOf("WIFI_RECONNECT: boot association handed") >= 0;
            reconnectsSeen = wifiAdapter.reconnectCallCount;
            noteDecision(bootAssociation ? "BOOT_ASSOCIATION" : "RADIO_RESTARTED");
        }
        if (moduleConfig.getRequestedModuleMode() == ModuleModeEnum::CONFIGURED_NEED_RESTART)
        {
            noteDecision("NEED_RESTART");
            moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
        }
        if (syncSensorTypes.requestCount() != catalogAsksSeen)
        {
            catalogAsksSeen = syncSensorTypes.requestCount();
            noteDecision("CATALOG_ASKED");
        }
        if (imageWriter.beginCalls != downloadsSeen)
        {
            downloadsSeen = imageWriter.beginCalls;
            noteDecision("DOWNLOAD_BEGAN");
        }
    }

    /** @brief The first time the loop saw this decision, or nullptr. */
    const Decision* firstDecision(const char* what) const
    {
        for (size_t i = 0; i < decisions.size(); i++)
        {
            if (decisions[i].what == what)
            {
                return &decisions[i];
            }
        }
        return nullptr;
    }

    void printDecisions() const
    {
        for (size_t i = 0; i < decisions.size(); i++)
        {
            printf("  s19 decision %2u: %8lu ms %s\n", (unsigned)i, (unsigned long)decisions[i].atMs,
                   decisions[i].what.c_str());
        }
    }
};

void assertTheRunIsSound(ModuleUnderTest& module)
{
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_FALSE_MESSAGE(module.busLock.deadlockDetected(),
                              "a task waited for the bus lock in a way that could never be served");
    TEST_ASSERT_TRUE_MESSAGE(module.loopIterations > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");
}

const char* orderName(bool atOnce) { return atOnce ? "new task at once" : "new task when the loop yields"; }

/** @brief How many records of the update belong to this sensor, and how many say online. */
void recordsOf(const std::vector<SensorDto>& sensors, const std::string& sensorUid, size_t& records,
               size_t& online)
{
    records = 0;
    online = 0;
    for (size_t i = 0; i < sensors.size(); i++)
    {
        if (sensors[i].uid.compare(0, sensorUid.size(), sensorUid) == 0)
        {
            records++;
            if (sensors[i].state == "online")
            {
                online++;
            }
        }
    }
}

} // namespace

// --------------------------------------------------------------------------------------------------
// 1. The structure is the first thing the module puts on the network, however long the association took.
// --------------------------------------------------------------------------------------------------

void test_s19_the_structure_is_the_first_network_act_of_the_boot(void)
{
    const uint32_t associations[] = {12000, 15400};
    for (size_t s = 0; s < sizeof(associations) / sizeof(associations[0]); s++)
    {
        for (int order = 0; order < 2; order++)
        {
            Boot boot;
            boot.associationMs = associations[s];
            boot.storedOn.push_back(0);
            boot.tasksStartAtOnce = (order == 1);
            ModuleUnderTest module(boot);

            module.bootAndStartTheLoop();
            module.runUntilMs(60000);
            assertTheRunIsSound(module);

            printf("  s19: association %lu ms, %s:\n", (unsigned long)associations[s], orderName(boot.tasksStartAtOnce));
            module.server.printActs(6);

            const std::vector<SimServer::Call> acts = module.server.networkActs();
            TEST_ASSERT_TRUE_MESSAGE(!acts.empty(), "the module put nothing on the network");

            char message[200];
            snprintf(message, sizeof(message),
                     "association of %lu ms, %s: the first network act of the boot was %s, not the structure",
                     (unsigned long)associations[s], orderName(boot.tasksStartAtOnce),
                     SimServer::kindName(acts[0].kind));
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)OutboxKind::MODULE_STRUCTURE, (int)acts[0].kind, message);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(MODULE_IP, acts[0].ip.c_str(),
                                             "the first structure did not carry the link's address");
        }
    }
}

// Setup does nothing on the network: it takes no longer for an association of twelve seconds, or of a
// minute that never comes, than for one that is instant - and the association is the first thing the
// loop asks for, on its first iteration, on the network task.
void test_s19_setup_waits_for_no_network(void)
{
    const uint32_t associations[] = {0, 12000, 60000};
    const size_t runs = sizeof(associations) / sizeof(associations[0]);
    uint64_t setupTook[runs];
    std::string firstAsk[runs];
    uint64_t firstAskAtMs[runs];
    for (size_t s = 0; s < runs; s++)
    {
        Boot boot;
        boot.associationMs = associations[s];
        boot.linkAtBoot = (associations[s] != 60000);
        boot.storedOn.push_back(0);
        ModuleUnderTest module(boot);

        const uint64_t setupFrom = module.clock.nowMs();
        module.setupService.setupModule();
        setupTook[s] = module.clock.nowMs() - setupFrom;
        module.startTheLoop();
        module.runUntilMs(module.clock.nowMs() + 5000);
        firstAsk[s] = module.decisions.empty() ? std::string("nothing") : module.decisions[0].what;
        firstAskAtMs[s] = module.decisions.empty() ? 0 : module.decisions[0].atMs - setupTook[s];
        printf("  s19: association of %lu ms%s: setup took %lu ms, the loop's first ask was %s %lu ms after it\n",
               (unsigned long)associations[s], boot.linkAtBoot ? "" : " (none comes)", (unsigned long)setupTook[s],
               firstAsk[s].c_str(), (unsigned long)firstAskAtMs[s]);
    }

    for (size_t s = 0; s < runs; s++)
    {
        char message[200];
        snprintf(message, sizeof(message),
                 "setup took %lu ms with an association of %lu ms and %lu ms with an instant one - it waited for "
                 "the network",
                 (unsigned long)setupTook[s], (unsigned long)associations[s], (unsigned long)setupTook[0]);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(setupTook[0], setupTook[s], message);
        TEST_ASSERT_TRUE_MESSAGE(setupTook[s] < 1000, message);
    }
    for (size_t s = 0; s < runs; s++)
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("BOOT_ASSOCIATION", firstAsk[s].c_str(),
                                         "the boot's association was not the loop's first ask of the network task");
        TEST_ASSERT_TRUE_MESSAGE(firstAskAtMs[s] < 2 * CONTROL_PERIOD_MS,
                                 "the boot's association was not asked for on the loop's first iteration");
    }
}

// The broker is connected by the client's state machine on the network task once the boot's association
// is over - not in setup, which has no link - and its CONNACK is in before the structure goes, the first
// request of the boot.
void test_s19_the_broker_connects_after_the_association_and_before_the_structure(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.storedOn.push_back(0);
        boot.tasksStartAtOnce = (order == 1);
        ModuleUnderTest module(boot);

        module.bootAndStartTheLoop();
        module.runUntilMs(60000);
        assertTheRunIsSound(module);

        const uint64_t associatedAt = module.tapeAtMs("WIFI_RECONNECT: boot association result=1");
        const uint64_t connackAt = module.mqttManager.connackAtMs;
        const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
        printf("  s19: %s: associated at %lu ms, CONNACK at %lu ms, structure at %lu ms\n",
               orderName(boot.tasksStartAtOnce), (unsigned long)associatedAt, (unsigned long)connackAt,
               structure != nullptr ? (unsigned long)structure->startedAtMs : 0UL);
        TEST_ASSERT_TRUE_MESSAGE(associatedAt >= boot.associationMs, "the boot's association never came back");
        TEST_ASSERT_TRUE_MESSAGE(connackAt > 0, "the broker was never connected");
        TEST_ASSERT_TRUE_MESSAGE(connackAt >= boot.associationMs, "the broker answered before there was a link");
        TEST_ASSERT_NOT_NULL_MESSAGE(structure, "no structure reached the server");
        TEST_ASSERT_TRUE_MESSAGE(connackAt <= structure->startedAtMs,
                                 "the structure went out before the broker's CONNACK");
    }
}

// The UDP listeners start on the boot's first link with an address, once - not in setup, which has no
// link, and not again on a return of the link.
void test_s19_the_udp_listeners_start_once_on_the_first_link(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(60000);
    module.dropTheLink();
    module.runUntilMs(100000);
    module.raiseTheLink();
    module.runUntilMs(160000);
    assertTheRunIsSound(module);

    const uint64_t linkAt = module.tapeAtMs("BOOT_SEQ: LINK -> NET_STRUCTURE");
    const uint64_t startedAt = module.tapeAtMs("BOOT_SEQ: UDP listeners started on the first link");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.logger.countOf("BOOT_SEQ: UDP listeners started"),
                                     "the UDP listeners were not started exactly once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.logger.countOf("BOOT_SEQ: LINK -> NET_STRUCTURE"),
                                     "the stand did not bring the link back");
    TEST_ASSERT_TRUE_MESSAGE(linkAt > 0 && startedAt == linkAt,
                             "the UDP listeners did not start with the first link");
}

// --------------------------------------------------------------------------------------------------
// 2. The first telemetry goes out after the structure has settled and after every stored sensor has
//    answered or failed once - with what they said in it.
// --------------------------------------------------------------------------------------------------

void test_s19_the_first_telemetry_follows_the_structure_and_every_first_reading(void)
{
    const uint32_t associations[] = {3000, 12000, 15400};
    for (size_t s = 0; s < sizeof(associations) / sizeof(associations[0]); s++)
    {
        for (int order = 0; order < 2; order++)
        {
            Boot boot;
            boot.associationMs = associations[s];
            boot.tasksStartAtOnce = (order == 1);
            // One sensor that answers and one whose port has nothing on it: "every" is the claim,
            // and a sensor that does not answer has its first outcome only after its read budget.
            boot.storedOn.push_back(0);
            boot.storedOn.push_back(2);
            boot.silentStoredOn.push_back(2);
            ModuleUnderTest module(boot);

            module.bootAndStartTheLoop();
            module.runUntilMs(90000);
            assertTheRunIsSound(module);

            printf("  s19: association %lu ms, %s:\n", (unsigned long)associations[s], orderName(boot.tasksStartAtOnce));
            module.server.printActs(6);

            const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
            const SimServer::Call* telemetry = module.server.firstAct(OutboxKind::SENSOR_UPDATE);
            TEST_ASSERT_NOT_NULL_MESSAGE(structure, "no structure reached the server");
            TEST_ASSERT_NOT_NULL_MESSAGE(telemetry, "no telemetry reached the server");

            char message[240];
            snprintf(message, sizeof(message),
                     "association of %lu ms, %s: the first telemetry left at %lu ms, before the structure had "
                     "settled at %lu ms",
                     (unsigned long)associations[s], orderName(boot.tasksStartAtOnce),
                     (unsigned long)telemetry->startedAtMs, (unsigned long)structure->endedAtMs);
            TEST_ASSERT_TRUE_MESSAGE(telemetry->startedAtMs >= structure->endedAtMs, message);

            size_t records = 0;
            size_t online = 0;
            recordsOf(telemetry->sensors, makeStoredSensor(0).uid, records, online);
            snprintf(message, sizeof(message),
                     "association of %lu ms, %s: the first telemetry carried %u record(s) of the answering "
                     "sensor, %u online - it went out before the sensor's first reading",
                     (unsigned long)associations[s], orderName(boot.tasksStartAtOnce), (unsigned)records,
                     (unsigned)online);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)online, message);

            recordsOf(telemetry->sensors, makeStoredSensor(2).uid, records, online);
            snprintf(message, sizeof(message),
                     "association of %lu ms, %s: the first telemetry carried %u record(s) of the silent "
                     "sensor - it went out before that sensor's first outcome",
                     (unsigned long)associations[s], orderName(boot.tasksStartAtOnce), (unsigned)records);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)records, message);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)online,
                                             "a sensor with nothing on its port was reported online");
        }
    }
}

// --------------------------------------------------------------------------------------------------
// 3. The tape says what the boot did, in the order it did it.
// --------------------------------------------------------------------------------------------------

void test_s19_the_boot_sequence_is_on_the_tape_in_order(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(60000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ");

    const char* expected[] = {
        "BOOT_SEQ: LOCAL -> LINK (loop started)",
        "BOOT_SEQ: LINK -> NET_STRUCTURE (link up, 192.168.1.100)",
        "BOOT_SEQ: NET_STRUCTURE -> NET_CATALOG (structure settled)",
        "BOOT_SEQ: gate RULE_LIST open (structure settled)",
        "BOOT_SEQ: gate DEVICE_UPDATE open (structure settled)",
        "BOOT_SEQ: NET_CATALOG -> STEADY (catalog settled)",
    };
    int last = -1;
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        const int at = module.logger.indexOf(expected[i]);
        char message[200];
        snprintf(message, sizeof(message), "the tape has no line \"%s\" after the line before it", expected[i]);
        TEST_ASSERT_TRUE_MESSAGE(at > last, message);
        snprintf(message, sizeof(message), "the tape has \"%s\" more than once", expected[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.logger.countOf(expected[i]), message);
        last = at;
    }

    const int readings = module.logger.indexOf("BOOT_SEQ: first readings settled (1 sensor(s)");
    const int telemetry = module.logger.indexOf("BOOT_SEQ: gate TELEMETRY open");
    TEST_ASSERT_TRUE_MESSAGE(readings >= 0, "the tape does not say when the first readings settled");
    TEST_ASSERT_TRUE_MESSAGE(telemetry > readings,
                             "the telemetry gate opened before the first readings had settled");
    TEST_ASSERT_TRUE_MESSAGE(telemetry > module.logger.indexOf("BOOT_SEQ: NET_STRUCTURE -> NET_CATALOG"),
                             "the telemetry gate opened before the structure had settled");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.logger.countOf("BOOT_SEQ: gate TELEMETRY open"),
                                     "the telemetry gate opened more than once");
}

// --------------------------------------------------------------------------------------------------
// 4. The catalog is asked for once the structure has settled, not beside it.
// --------------------------------------------------------------------------------------------------

void test_s19_the_catalog_is_asked_for_once_the_structure_has_settled(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.storedOn.push_back(0);
        boot.tasksStartAtOnce = (order == 1);
        // A structure that takes its time, so that "beside it" and "after it" are seconds apart.
        boot.structureAnswerMs = 2000;
        ModuleUnderTest module(boot);

        module.bootAndStartTheLoop();
        module.runUntilMs(60000);
        assertTheRunIsSound(module);

        printf("  s19: %s:\n", orderName(boot.tasksStartAtOnce));
        module.server.printActs(6);

        const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
        const SimServer::Call* catalog = module.server.firstAct(OutboxKind::SENSOR_TYPES_FETCH);
        TEST_ASSERT_NOT_NULL_MESSAGE(structure, "no structure reached the server");
        TEST_ASSERT_NOT_NULL_MESSAGE(catalog, "the catalog was never fetched");
        TEST_ASSERT_TRUE_MESSAGE(module.firstCatalogAskAtMs > 0, "the catalog sync never asked the outbox");

        char message[200];
        snprintf(message, sizeof(message),
                 "%s: the catalog was asked for at %lu ms, while the structure was still on its way "
                 "(it settled at %lu ms)",
                 orderName(boot.tasksStartAtOnce), (unsigned long)module.firstCatalogAskAtMs,
                 (unsigned long)structure->endedAtMs);
        TEST_ASSERT_TRUE_MESSAGE(module.firstCatalogAskAtMs >= structure->endedAtMs, message);
        TEST_ASSERT_TRUE_MESSAGE(catalog->startedAtMs >= structure->endedAtMs,
                                 "the catalog went out on the network before the structure had settled");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.actCount(OutboxKind::SENSOR_TYPES_FETCH),
                                         "the boot fetched the catalog more than once from a server that answered");
    }
}

// --------------------------------------------------------------------------------------------------
// 5. A module with relays: the structure is still its first network act, in either order of tasks,
//    and it asks for its rules once, after the structure.
// --------------------------------------------------------------------------------------------------

void test_s19_a_module_with_relays_puts_its_structure_first(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.deviceModule = true;
        boot.tasksStartAtOnce = (order == 1);
        ModuleUnderTest module(boot);

        module.bootAndStartTheLoop();
        module.runUntilMs(60000);
        assertTheRunIsSound(module);

        printf("  s19: relays, %s:\n", orderName(boot.tasksStartAtOnce));
        module.server.printActs(6);

        const std::vector<SimServer::Call> acts = module.server.networkActs();
        TEST_ASSERT_TRUE_MESSAGE(!acts.empty(), "the module put nothing on the network");
        char message[200];
        snprintf(message, sizeof(message),
                 "a module with relays, %s: the first network act of the boot was %s, not the structure",
                 orderName(boot.tasksStartAtOnce), SimServer::kindName(acts[0].kind));
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)OutboxKind::MODULE_STRUCTURE, (int)acts[0].kind, message);

        // The first picture of the relays is still sent - after the structure, not instead of it.
        const SimServer::Call* devices = module.server.firstAct(OutboxKind::DEVICE_UPDATE);
        TEST_ASSERT_NOT_NULL_MESSAGE(devices, "the relays were never reported");
        TEST_ASSERT_TRUE_MESSAGE(devices->startedAtMs >= acts[0].endedAtMs,
                                 "the relays were reported before the structure had settled");
    }
}

void test_s19_a_module_with_relays_asks_for_its_rules_once_after_the_structure(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.deviceModule = true;
        boot.tasksStartAtOnce = (order == 1);
        ModuleUnderTest module(boot);

        module.bootAndStartTheLoop();
        module.runUntilMs(60000);
        assertTheRunIsSound(module);

        const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
        const SimServer::Call* rules = module.server.firstAct(OutboxKind::RULE_LIST);
        TEST_ASSERT_NOT_NULL_MESSAGE(structure, "no structure reached the server");
        TEST_ASSERT_NOT_NULL_MESSAGE(rules, "the rules were never fetched");

        char message[200];
        snprintf(message, sizeof(message),
                 "a module with relays, %s: the boot fetched the rule list %u times from a server that "
                 "answered the first",
                 orderName(boot.tasksStartAtOnce), (unsigned)module.server.actCount(OutboxKind::RULE_LIST));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.actCount(OutboxKind::RULE_LIST), message);
        TEST_ASSERT_TRUE_MESSAGE(rules->startedAtMs >= structure->endedAtMs,
                                 "the rules were fetched before the structure had settled");
    }
}

// --------------------------------------------------------------------------------------------------
// 6. Guard: the module command a broker hands the session at connect is fetched after the structure.
//    Green without the boot sequence too - the loop drains the broker after the structure is handed
//    over, and one network task serves one request at a time; the sequence must not change that.
// --------------------------------------------------------------------------------------------------

void test_s19_the_module_command_the_broker_hands_over_at_connect_follows_the_structure(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.storedOn.push_back(0);
        boot.tasksStartAtOnce = (order == 1);
        ModuleUnderTest module(boot);

        module.setupService.setupModule();
        module.mqttManager.deliver(moduleCommandSignalJson("SetSensorModel"));
        module.startTheLoop();
        module.runUntilMs(60000);
        assertTheRunIsSound(module);

        const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
        const SimServer::Call* commands = module.server.firstAct(OutboxKind::MODULE_COMMAND_FETCH);
        TEST_ASSERT_NOT_NULL_MESSAGE(structure, "no structure reached the server");
        TEST_ASSERT_NOT_NULL_MESSAGE(commands, "the module command was never fetched");
        TEST_ASSERT_TRUE_MESSAGE(commands->startedAtMs >= structure->endedAtMs,
                                 "the module command was fetched before the structure had settled");
    }
}

namespace {

/** @brief An UpdateFirmware command for the stand's image, which carries no checksum. */
ModuleCommandDto updateFirmwareCommand(const char* uid, const char* createdAt)
{
    ModuleCommandDto command;
    command.uid = uid;
    command.command = "UpdateFirmware";
    command.options = "{\"url\":\"http://fw.test/s19.bin\",\"version\":\"9.9.9\"}";
    command.createdAt = createdAt;
    return command;
}

/** The stand's image and the pace it arrives at: 32 s of download. */
const int UPDATE_IMAGE_BYTES = 262144;
const uint32_t UPDATE_BYTES_PER_SECOND = 8192;

/**
 * @brief The download began after the boot's structure had settled, ran on while the control loop
 *        kept its pace, and its outcome reached the server once.
 */
void assertTheUpdateFollowedTheStructureWhileTheLoopWorked(ModuleUnderTest& module, const char* uid)
{
    const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
    const Decision* began = module.firstDecision("DOWNLOAD_BEGAN");
    const Decision* done = module.firstDecision("NEED_RESTART");
    TEST_ASSERT_NOT_NULL_MESSAGE(structure, "no structure reached the server");
    TEST_ASSERT_NOT_NULL_MESSAGE(began, "the update was never downloaded");
    TEST_ASSERT_NOT_NULL_MESSAGE(done, "the download never finished with the restart that boots the image");
    printf("  s19: structure settled at %lu ms, download %lu -> %lu ms over %lu loop iterations\n",
           (unsigned long)structure->endedAtMs, (unsigned long)began->atMs, (unsigned long)done->atMs,
           (unsigned long)(done->iteration - began->iteration));

    TEST_ASSERT_TRUE_MESSAGE(began->atMs >= structure->endedAtMs,
                             "the update was downloaded before the boot's structure had settled");
    TEST_ASSERT_TRUE_MESSAGE(done->atMs - began->atMs >= 30000, "the premise: the download takes 32 s");
    TEST_ASSERT_TRUE_MESSAGE(done->iteration - began->iteration >= (done->atMs - began->atMs) / CONTROL_PERIOD_MS / 2,
                             "the control loop did not keep its pace while the update downloaded");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted, "the downloaded image was not installed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.reportedCount(uid, "Done"),
                                     "the server did not see the update's outcome once");
}

} // namespace

// An update the server held for the module while it was off is on the list the boot asks for once its
// structure has settled - setup asks the server for nothing - and it downloads on the command network
// task while the loop works on.
void test_s19_an_update_on_the_boots_list_downloads_after_the_structure_while_the_loop_works(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.storedOn.push_back(0);
        boot.tasksStartAtOnce = (order == 1);
        ModuleUnderTest module(boot);
        module.server.pendingModuleCommands.push_back(updateFirmwareCommand("s19-fw", "2025-09-05T09:59:00Z"));
        module.otaHttp.queueHealthyDownload(UPDATE_IMAGE_BYTES, UPDATE_BYTES_PER_SECOND);

        module.bootAndStartTheLoop();
        module.runUntilMs(120000);
        assertTheRunIsSound(module);
        printf("  s19: %s:\n", orderName(boot.tasksStartAtOnce));
        module.server.printActs(8);

        const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
        const SimServer::Call* fetch = module.server.firstAct(OutboxKind::MODULE_COMMAND_FETCH);
        TEST_ASSERT_NOT_NULL_MESSAGE(fetch, "the boot never asked the server for its module commands");
        TEST_ASSERT_TRUE_MESSAGE(structure != nullptr && fetch->startedAtMs >= structure->endedAtMs,
                                 "the boot asked for its module commands before its structure had settled");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.actCount(OutboxKind::MODULE_COMMAND_FETCH),
                                         "the boot asked for its module commands more than once");
        assertTheUpdateFollowedTheStructureWhileTheLoopWorked(module, "s19-fw");
    }
}

// The update the previous boot kept for this one waits in its file for the link: a boot that never gets
// one loses nothing. Once the link has come and the structure has settled, it downloads.
void test_s19_a_kept_update_waits_in_its_file_for_the_link(void)
{
    Boot boot;
    boot.associationMs = 60000;
    boot.linkAtBoot = false;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.mqttManager.connected = false;
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(updateFirmwareCommand("s19-fw-kept", "")),
                             "the premise: the update is in its file");
    module.otaHttp.queueHealthyDownload(UPDATE_IMAGE_BYTES, UPDATE_BYTES_PER_SECOND);

    module.bootAndStartTheLoop();
    module.runUntilMs(100000);

    ModuleCommandDto stillKept;
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.load(stillKept),
                             "a boot with no link took the kept update out of its file");
    TEST_ASSERT_EQUAL_STRING("s19-fw-kept", stillKept.uid.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls, "a download was attempted with no link");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.server.reportedCount("s19-fw-kept", "Error"),
                                     "the kept update was spent as an error on a boot with no link");

    module.raiseTheLink();
    module.runUntilMs(200000);
    assertTheRunIsSound(module);
    module.server.printActs(8);

    assertTheUpdateFollowedTheStructureWhileTheLoopWorked(module, "s19-fw-kept");
    ModuleCommandDto leftOver;
    TEST_ASSERT_FALSE_MESSAGE(module.pendingUpdateStore.load(leftOver),
                              "the update stayed in its file after it was carried out");
}

// The restart that kept the update came before its InProgress reached the server, so the server still holds
// the command as new and hands it out on the boot's list. The module carries out the update from its file
// and the same command, offered again by the list, is not carried out a second time: one download, one
// outcome, and not a word more to the server about it.
void test_s19_a_kept_update_the_server_still_lists_as_new_is_carried_out_once(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.storedOn.push_back(0);
        boot.tasksStartAtOnce = (order == 1);
        ModuleUnderTest module(boot);
        module.server.keepUntilReported = true;
        module.debugSystem.enabled.insert(DebugFeatures::MODULE_COMMAND_DEBUG);
        TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(updateFirmwareCommand("s19-fw-kept", "")),
                                 "the premise: the update is in its file");
        module.server.pendingModuleCommands.push_back(updateFirmwareCommand("s19-fw-kept", "2025-09-05T09:59:00Z"));
        // Two downloads are on offer, so that a module that takes the command twice really can run it twice.
        module.otaHttp.queueHealthyDownload(UPDATE_IMAGE_BYTES, UPDATE_BYTES_PER_SECOND);
        module.otaHttp.queueHealthyDownload(UPDATE_IMAGE_BYTES, UPDATE_BYTES_PER_SECOND);

        module.bootAndStartTheLoop();
        module.runUntilMs(150000);
        assertTheRunIsSound(module);
        printf("  s19: %s:\n", orderName(boot.tasksStartAtOnce));
        module.server.printActs(8);

        TEST_ASSERT_NOT_NULL_MESSAGE(module.server.firstAct(OutboxKind::MODULE_COMMAND_FETCH),
                                     "the premise: the boot asked the server for its module commands");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                      "the update the module carried out from its file was downloaded again "
                                      "when the server's list offered it");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.reportedCount("s19-fw-kept", "Done"),
                                         "the server did not see the update's outcome once");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.statusesReportedFor("s19-fw-kept"),
                                         "the server was told more than the one outcome of the update");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.logger.countOf("firmware update uid=s19-fw-kept was started on this boot already"),
                                         "the tape does not say once that the offered update was not carried out again");
    }
}

namespace {

/** @brief The DeleteGd a broker hands the session, as in device logs. */
std::string deleteGdSignalJson()
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"DeleteGdCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"commandUid\":\"s19-del\",\"createdAt\":\"2025-09-05T09:59:00Z\"}",
             MODULE_UID);
    return std::string(buffer);
}

/** @brief Did any outcome batch the server was sent name this command? */
bool anOutcomeWasReportedFor(const ModuleUnderTest& module, const char* uid)
{
    for (size_t i = 0; i < module.server.calls.size(); i++)
    {
        for (size_t j = 0; j < module.server.calls[i].commandUpdates.size(); j++)
        {
            if (module.server.calls[i].commandUpdates[j].uid == uid)
            {
                return true;
            }
        }
    }
    return false;
}

const char* const BOOT_CHECK_DELETED = "the boot's deletion check - verdict DELETED";
const char* const DELETE_GD_DONE = "Delete command execution completed - status: Done";
const char* const RESET_DONE = "Module data clearing completed";

} // namespace

// A module the server deleted while its DeleteGd was lost is reset by the boot's own check: one is_deleted
// query on the command network task once the boot has reached STEADY - setup has no link to ask on - the
// factory reset and the restart, and nothing reported, since no command asked for it.
void test_s19_a_module_deleted_on_the_server_is_reset_after_the_first_steady(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.server.moduleDeleted = true;
    module.debugSystem.enabled.insert(DebugFeatures::MODULE_COMMAND_DEBUG);

    module.bootAndStartTheLoop();
    module.runUntilMs(60000);
    assertTheRunIsSound(module);

    const uint64_t steadyAt = module.tapeAtMs("BOOT_SEQ: NET_CATALOG -> STEADY");
    const std::vector<SimServer::Call> queries =
        module.server.callsOf(IS_DELETED_QUERY, NetworkCommandRunner::TASK_NAME);
    const Decision* restart = module.firstDecision("NEED_RESTART");
    printf("  s19: STEADY at %lu ms, %u is_deleted quer%s, first at %lu ms, restart asked at %lu ms\n",
           (unsigned long)steadyAt, (unsigned)queries.size(), queries.size() == 1 ? "y" : "ies",
           queries.empty() ? 0UL : (unsigned long)queries[0].startedAtMs,
           restart != nullptr ? (unsigned long)restart->atMs : 0UL);

    TEST_ASSERT_TRUE_MESSAGE(steadyAt > 0, "the boot never reached STEADY");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.callsOf(IS_DELETED_QUERY, "").size() +
                                            (uint32_t)queries.size(),
                                     "the boot did not ask whether the module was deleted exactly once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)queries.size(),
                                     "the deletion check was not made on the command network task");
    TEST_ASSERT_TRUE_MESSAGE(queries[0].startedAtMs >= steadyAt,
                             "the deletion check went before the boot's first STEADY");
    TEST_ASSERT_NOT_NULL_MESSAGE(restart, "a module the server deleted was not restarted");
    TEST_ASSERT_TRUE_MESSAGE(restart->atMs >= queries[0].endedAtMs, "the restart came before the verdict");
    TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::USER_CAN_CONFIGURE, module.settings.getIfConfigured(),
                              "a module the server deleted was not reset to be configured again");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, module.settings.getWasInitialSyncCompleted(),
                                    "the reset left the module registered");
    TEST_ASSERT_FALSE_MESSAGE(anOutcomeWasReportedFor(module, "boot-deletion-check"),
                              "the boot's own check was reported to the server as a command");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.server.actCount(OutboxKind::COMMAND_RESULT),
                                     "the boot's own check was reported to the server as a DeleteGd");
}

// The ordinary case: the server has not deleted the module. One query, and nothing else - no reset, no
// report, and no error on the tape: NOT_DELETED is the answer the check expects.
void test_s19_a_module_the_server_has_not_deleted_goes_on_without_a_word(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.debugSystem.enabled.insert(DebugFeatures::MODULE_COMMAND_DEBUG);

    module.bootAndStartTheLoop();
    module.runUntilMs(60000);
    assertTheRunIsSound(module);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.callsOf(IS_DELETED_QUERY, NetworkCommandRunner::TASK_NAME).size(),
                                     "the boot did not ask whether the module was deleted");
    TEST_ASSERT_NULL_MESSAGE(module.firstDecision("NEED_RESTART"), "a module the server has not deleted was restarted");
    TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::CONFIGURED_DONE, module.settings.getIfConfigured(),
                              "a module the server has not deleted was reset");
    TEST_ASSERT_TRUE_MESSAGE(module.logger.indexOf("the boot's deletion check - verdict NOT_DELETED, nothing to do") >= 0,
                             "the tape does not say what the check found");
    TEST_ASSERT_TRUE_MESSAGE(module.logger.indexOf("module is not marked as deleted") < 0,
                             "the expected answer was written to the tape as an error");
    TEST_ASSERT_FALSE_MESSAGE(anOutcomeWasReportedFor(module, "boot-deletion-check"),
                              "the boot's own check was reported to the server as a command");
}

// A module the server had not registered when the boot began is not asked about: it is not one the server
// can have deleted, and its first structure - which registers it - settles before the first STEADY.
void test_s19_a_module_not_registered_when_the_boot_began_is_not_asked_about_its_deletion(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    boot.registered = false;
    ModuleUnderTest module(boot);
    module.server.moduleDeleted = true;

    module.bootAndStartTheLoop();
    module.runUntilMs(60000);
    assertTheRunIsSound(module);

    TEST_ASSERT_TRUE_MESSAGE(module.tapeAtMs("BOOT_SEQ: NET_CATALOG -> STEADY") > 0, "the boot never reached STEADY");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.settings.getWasInitialSyncCompleted(),
                                    "the premise: the boot's structure registered the module");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.server.callsOf(IS_DELETED_QUERY, NetworkCommandRunner::TASK_NAME).size(),
                                     "a module that was not registered when the boot began was asked about its deletion");
    TEST_ASSERT_NULL_MESSAGE(module.firstDecision("NEED_RESTART"), "a module registered on this boot was reset");
}

namespace {

/**
 * @brief A registered module the server has deleted, with a DeleteGd that arrives at this moment - 0 for
 *        the session the broker hands over at the CONNACK, before the first STEADY; otherwise once the
 *        boot is in STEADY - and the is_deleted queries answering in these times, in the order the
 *        command network task makes them.
 */
void runADeletionRace(ModuleUnderTest& module, bool deleteGdBeforeSteady, uint32_t firstAnswerMs, uint32_t secondAnswerMs)
{
    module.server.moduleDeleted = true;
    module.server.deletionAnswerMs.push_back(firstAnswerMs);
    module.server.deletionAnswerMs.push_back(secondAnswerMs);
    module.debugSystem.enabled.insert(DebugFeatures::MODULE_COMMAND_DEBUG);
    module.debugSystem.enabled.insert(DebugFeatures::SPIFFS_DEBUG);
    if (deleteGdBeforeSteady)
    {
        module.mqttManager.deliver(deleteGdSignalJson());
    }
    module.bootAndStartTheLoop();
    if (!deleteGdBeforeSteady)
    {
        TEST_ASSERT_TRUE_MESSAGE(module.runUntilTheTapeSays("BOOT_SEQ: NET_CATALOG -> STEADY", 60000),
                                 "the boot never reached STEADY");
        module.mqttManager.deliver(deleteGdSignalJson());
    }
    module.runUntilMs(90000);
    assertTheRunIsSound(module);
}

/** @brief Both verdicts were applied, the DeleteGd was answered done once, and the module is reset. */
void assertTheRaceEndsInOneReset(ModuleUnderTest& module, const char* order)
{
    char message[200];
    snprintf(message, sizeof(message), "%s: the two checks did not both reach the server", order);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.callsOf(IS_DELETED_QUERY, NetworkCommandRunner::TASK_NAME).size(),
                                     message);
    snprintf(message, sizeof(message), "%s: the DeleteGd was not answered done exactly once", order);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.commandResultCount("DeleteModule", "done"), message);
    snprintf(message, sizeof(message), "%s: the DeleteGd was answered with an error", order);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.server.commandResultCount("DeleteModule", "error"), message);
    snprintf(message, sizeof(message), "%s: the module was not reset", order);
    TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::USER_CAN_CONFIGURE, module.settings.getIfConfigured(), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(module.firstDecision("NEED_RESTART"), "the module was not restarted");
    // Each DELETED is a reset of its own; the second writes the same empty sets again.
    snprintf(message, sizeof(message), "%s: the reset ran other than once per DELETED verdict", order);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.logger.countOf(RESET_DONE), message);
    TEST_ASSERT_FALSE_MESSAGE(anOutcomeWasReportedFor(module, "boot-deletion-check"),
                              "the boot's own check was reported to the server as a command");
}

} // namespace

// The DeleteGd first, then the boot's check: the command network task serves them one after the other, and
// the loop applies each verdict on its own iteration.
void test_s19_a_delete_gd_before_the_boots_deletion_check_ends_in_one_reset(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    runADeletionRace(module, true, 5000, 5000);
    const uint64_t deleteGdAt = module.tapeAtMs(DELETE_GD_DONE);
    const uint64_t bootCheckAt = module.tapeAtMs(BOOT_CHECK_DELETED);
    printf("  s19: DeleteGd applied at %lu ms, the boot's check at %lu ms\n", (unsigned long)deleteGdAt,
           (unsigned long)bootCheckAt);
    TEST_ASSERT_TRUE_MESSAGE(deleteGdAt > 0 && bootCheckAt > deleteGdAt, "the premise: the DeleteGd's verdict came first");
    assertTheRaceEndsInOneReset(module, "DeleteGd first");
}

void test_s19_a_delete_gd_after_the_boots_deletion_check_ends_in_one_reset(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    runADeletionRace(module, false, 5000, 5000);
    const uint64_t deleteGdAt = module.tapeAtMs(DELETE_GD_DONE);
    const uint64_t bootCheckAt = module.tapeAtMs(BOOT_CHECK_DELETED);
    printf("  s19: the boot's check applied at %lu ms, DeleteGd at %lu ms\n", (unsigned long)bootCheckAt,
           (unsigned long)deleteGdAt);
    TEST_ASSERT_TRUE_MESSAGE(bootCheckAt > 0 && deleteGdAt > bootCheckAt, "the premise: the boot's verdict came first");
    assertTheRaceEndsInOneReset(module, "the boot's check first");
}

void test_s19_a_delete_gd_and_the_boots_deletion_check_in_one_iteration_end_in_one_reset(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    // The DeleteGd's query answers at once: both verdicts are back before the loop's next drain.
    runADeletionRace(module, false, 5000, 0);
    const uint64_t deleteGdAt = module.tapeAtMs(DELETE_GD_DONE);
    const uint64_t bootCheckAt = module.tapeAtMs(BOOT_CHECK_DELETED);
    printf("  s19: the boot's check applied at %lu ms, DeleteGd at %lu ms\n", (unsigned long)bootCheckAt,
           (unsigned long)deleteGdAt);
    TEST_ASSERT_TRUE_MESSAGE(bootCheckAt > 0 && deleteGdAt == bootCheckAt,
                             "the premise: both verdicts were applied in the same iteration");
    assertTheRaceEndsInOneReset(module, "one iteration");
}

// The first telemetry carries the time SNTP set on this boot: the server drops an update stamped minutes
// off its own clock. SNTP answering at 30 s holds the telemetry until then - not later.
void test_s19_the_first_telemetry_waits_for_the_clock_sntp_sets(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.timeAdapter.sntpSetThisBoot = false;

    module.bootAndStartTheLoop();
    module.runUntilMs(30000);
    TEST_ASSERT_TRUE_MESSAGE(module.server.firstAct(OutboxKind::MODULE_STRUCTURE) != nullptr,
                             "the premise: the structure had settled before SNTP answered");
    TEST_ASSERT_NULL_MESSAGE(module.server.firstAct(OutboxKind::SENSOR_UPDATE),
                             "the first telemetry went before SNTP had set the clock");
    module.timeAdapter.sntpSetAt = 30000;
    module.timeAdapter.sntpSetThisBoot = true;
    module.runUntilMs(60000);
    assertTheRunIsSound(module);

    const SimServer::Call* telemetry = module.server.firstAct(OutboxKind::SENSOR_UPDATE);
    TEST_ASSERT_NOT_NULL_MESSAGE(telemetry, "no telemetry once SNTP had set the clock");
    printf("  s19: SNTP at 30000 ms, first telemetry at %lu ms\n", (unsigned long)telemetry->startedAtMs);
    TEST_ASSERT_TRUE_MESSAGE(telemetry->startedAtMs >= 30000 && telemetry->startedAtMs <= 31000,
                             "the first telemetry did not go as SNTP set the clock");
    TEST_ASSERT_TRUE_MESSAGE(module.logger.indexOf("BOOT_SEQ: clock set by SNTP at 30000 ms (seeded from EEPROM: no)") >= 0,
                             "the tape does not say when SNTP set the clock");
}

// No SNTP on this boot: the first telemetry waits ten minutes from the start and then goes with the clock
// the module has - here the one seeded from storage.
void test_s19_without_sntp_the_first_telemetry_goes_with_the_modules_clock_after_ten_minutes(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.timeAdapter.sntpSetThisBoot = false;
    module.timeAdapter.seededFromStorage = true;

    module.bootAndStartTheLoop();
    module.runLongUntilMs(660000);
    assertTheRunIsSound(module);

    const SimServer::Call* telemetry = module.server.firstAct(OutboxKind::SENSOR_UPDATE);
    TEST_ASSERT_NOT_NULL_MESSAGE(telemetry, "with no SNTP the module never sent its telemetry");
    printf("  s19: no SNTP, first telemetry at %lu ms\n", (unsigned long)telemetry->startedAtMs);
    TEST_ASSERT_TRUE_MESSAGE(telemetry->startedAtMs >= BootSequencer::SNTP_WAIT_CEILING_MS,
                             "the first telemetry went before SNTP had set the clock or its wait was over");
    TEST_ASSERT_TRUE_MESSAGE(telemetry->startedAtMs <= BootSequencer::SNTP_WAIT_CEILING_MS + 1000,
                             "the first telemetry waited past the ten minutes");
    TEST_ASSERT_TRUE_MESSAGE(module.logger.indexOf("BOOT_SEQ: no SNTP in 600000 ms - the telemetry goes with the module's clock (seeded from EEPROM: yes)") >= 0,
                             "the tape does not say the telemetry went without SNTP");
}

// --------------------------------------------------------------------------------------------------
// 7. The discovery starts when the boot is over - its window of a minute is only the bound.
// --------------------------------------------------------------------------------------------------

void test_s19_the_discovery_starts_once_the_boot_is_over(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.storedOn.push_back(0);
        boot.tasksStartAtOnce = (order == 1);
        ModuleUnderTest module(boot);

        module.bootAndStartTheLoop();
        module.runUntilMs(120000);
        assertTheRunIsSound(module);

        const uint64_t steadyAt = module.tapeAtMs("BOOT_SEQ: NET_CATALOG -> STEADY");
        const uint64_t readingsAt = module.tapeAtMs("BOOT_SEQ: first readings settled");
        const uint64_t armedAt = module.tapeAtMs("STARTUP_SCAN: armed");
        printf("  s19: %s: armed at %lu ms, first readings at %lu ms, STEADY at %lu ms, first step at "
               "%lu ms\n",
               orderName(boot.tasksStartAtOnce), (unsigned long)armedAt, (unsigned long)readingsAt,
               (unsigned long)steadyAt, (unsigned long)module.firstScanStepAtMs);

        TEST_ASSERT_TRUE_MESSAGE(steadyAt > 0 && readingsAt > 0, "the boot never got to STEADY");
        TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs > 0, "the discovery never took a step");
        char message[240];
        snprintf(message, sizeof(message),
                 "%s: the discovery's first step was at %lu ms, the boot was over at %lu ms - it "
                 "waited out its window instead (armed at %lu ms)",
                 orderName(boot.tasksStartAtOnce), (unsigned long)module.firstScanStepAtMs,
                 (unsigned long)steadyAt, (unsigned long)armedAt);
        TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs >= steadyAt && module.firstScanStepAtMs >= readingsAt,
                                 message);
        TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs <= steadyAt + 1000, message);
    }
}

// Guard (the two of S18 that pin the first read, repeated with the sequence wired): the stored sensor
// is read as soon as its port can answer - its warm-up from the power setup gave it, however long the
// association takes - and the discovery starts after that read, never before it.
void test_s19_the_stored_sensor_is_read_before_the_discovery_starts(void)
{
    const uint32_t associations[] = {3000, 12000};
    for (size_t s = 0; s < sizeof(associations) / sizeof(associations[0]); s++)
    {
        Boot boot;
        boot.associationMs = associations[s];
        boot.storedOn.push_back(0);
        ModuleUnderTest module(boot);

        module.bootAndStartTheLoop();
        module.runUntilMs(120000);
        assertTheRunIsSound(module);

        const SimServer::Call* telemetry = module.server.firstAct(OutboxKind::SENSOR_UPDATE);
        const uint64_t readingsAt = module.tapeAtMs("BOOT_SEQ: first readings settled");
        printf("  s19: association %lu ms: first readings settled at %lu ms, first discovery step at %lu ms\n",
               (unsigned long)associations[s], (unsigned long)readingsAt, (unsigned long)module.firstScanStepAtMs);

        TEST_ASSERT_NOT_NULL_MESSAGE(telemetry, "no telemetry reached the server");
        size_t records = 0;
        size_t online = 0;
        recordsOf(telemetry->sensors, makeStoredSensor(0).uid, records, online);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)online,
                                         "the stored sensor's first read did not take both channels");
        TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs > 0, "the discovery never took a step");
        TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs >= readingsAt,
                                 "the discovery took its first step before the stored sensor had been read");
        TEST_ASSERT_TRUE_MESSAGE(readingsAt >= PortPower::WAKE_UP_MS && readingsAt <= PortPower::WAKE_UP_MS + 1000,
                                 "the stored sensor was not read as soon as its port could answer");
    }
}

// --------------------------------------------------------------------------------------------------
// 8. With no link at the boot the discovery runs on the catalog the module holds, and the network
//    phases wait for the link - which, when it comes, carries the structure first.
// --------------------------------------------------------------------------------------------------

void test_s19_without_a_link_the_discovery_runs_on_the_catalog_on_flash(void)
{
    Boot boot;
    boot.associationMs = 60000; // what the radio waits for an association that does not come
    boot.linkAtBoot = false;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.mqttManager.connected = false;

    module.bootAndStartTheLoop();
    module.runUntilMs(100000);

    const uint64_t readingsAt = module.tapeAtMs("BOOT_SEQ: first readings settled");
    printf("  s19: no link: first readings at %lu ms, first discovery step at %lu ms\n",
           (unsigned long)readingsAt, (unsigned long)module.firstScanStepAtMs);
    TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs > 0,
                             "with no link and a catalog on flash the discovery had not started 40 s "
                             "after the boot");
    TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs >= readingsAt,
                             "the discovery started before the stored sensor's first read");
    TEST_ASSERT_TRUE_MESSAGE(module.server.networkActs().empty(),
                             "something reached the server with no link");

    module.raiseTheLink();
    module.runUntilMs(160000);
    assertTheRunIsSound(module);
    module.server.printActs(4);

    const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
    const SimServer::Call* telemetry = module.server.firstAct(OutboxKind::SENSOR_UPDATE);
    const SimServer::Call* catalog = module.server.firstAct(OutboxKind::SENSOR_TYPES_FETCH);
    TEST_ASSERT_NOT_NULL_MESSAGE(structure, "no structure reached the server once the link came");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(MODULE_IP, structure->ip.c_str(),
                                     "the structure did not carry the link's address");
    TEST_ASSERT_TRUE_MESSAGE(telemetry != nullptr && telemetry->startedAtMs >= structure->endedAtMs,
                             "the telemetry went out before the structure once the link came");
    TEST_ASSERT_TRUE_MESSAGE(catalog != nullptr && catalog->startedAtMs >= structure->endedAtMs,
                             "the catalog went out before the structure once the link came");
}

// --------------------------------------------------------------------------------------------------
// 9. A boot that had no catalog arms the discovery when the catalog arrives - and a fresh module does
//    not wait out a minute either.
// --------------------------------------------------------------------------------------------------

void test_s19_a_catalog_that_arrives_after_the_boot_arms_the_discovery(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    boot.catalogOnFlash = false;
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(120000);
    assertTheRunIsSound(module);
    module.server.printActs(5);

    const SimServer::Call* catalog = module.server.firstAct(OutboxKind::SENSOR_TYPES_FETCH);
    TEST_ASSERT_NOT_NULL_MESSAGE(catalog, "the catalog was never fetched after the boot");
    TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs > 0,
                             "the catalog arrived and the discovery was never armed");
    TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs >= catalog->endedAtMs,
                             "the discovery stepped before it had a catalog");
    TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs <= catalog->endedAtMs + 2000,
                             "the discovery was armed by the catalog and then waited out a window");
    // The preparation takes steps too: what the catalog armed is the discovery itself.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.startupScan.armCount(),
                                     "the catalog did not arm the discovery of a module with no flag");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.startupScan.preparationArmCount(),
                                     "a module with no flag was armed for the preparation only");
}

void test_s19_a_fresh_module_starts_its_discovery_once_the_boot_is_over(void)
{
    Boot boot; // nothing stored, the catalog on flash
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(120000);
    assertTheRunIsSound(module);

    const std::vector<SimServer::Call> acts = module.server.networkActs();
    const uint64_t steadyAt = module.tapeAtMs("BOOT_SEQ: NET_CATALOG -> STEADY");
    printf("  s19: fresh module: STEADY at %lu ms, first discovery step at %lu ms\n",
           (unsigned long)steadyAt, (unsigned long)module.firstScanStepAtMs);
    TEST_ASSERT_TRUE_MESSAGE(!acts.empty(), "the module put nothing on the network");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)OutboxKind::MODULE_STRUCTURE, (int)acts[0].kind,
                                  "a fresh module's first network act was not its structure");
    TEST_ASSERT_TRUE_MESSAGE(steadyAt > 0 && module.firstScanStepAtMs >= steadyAt &&
                                 module.firstScanStepAtMs <= steadyAt + 1000,
                             "a fresh module's discovery did not start when its boot was over");
}

namespace {

/** @brief An erased module - nothing stored, no catalog, and setup asks the server for none - booted. */
void bootAnErasedModule(ModuleUnderTest& module)
{
    module.bootAndStartTheLoop();
    module.runUntilMs(120000);
    assertTheRunIsSound(module);
    module.server.printActs(4);

    const std::vector<SimServer::Call> acts = module.server.networkActs();
    TEST_ASSERT_TRUE_MESSAGE(!acts.empty(), "the module put nothing on the network");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)OutboxKind::MODULE_STRUCTURE, (int)acts[0].kind,
                                  "an erased module's first network act was not its structure");
    TEST_ASSERT_FALSE_MESSAGE(module.settings.getNoScanSensors(),
                              "the boot did not consume the noScanSensors flag");
}

Boot anErasedModule(bool noScanSensors)
{
    Boot boot; // nothing stored
    boot.catalogOnFlash = false;
    boot.noScanSensors = noScanSensors;
    return boot;
}

} // namespace

void test_s19_an_erased_module_arms_its_discovery_when_the_catalog_arrives(void)
{
    ModuleUnderTest module(anErasedModule(false));
    bootAnErasedModule(module);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.startupScan.armCount(),
                                     "the catalog arrived and the discovery was not armed");
    TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs > 0, "the armed discovery never stepped");
}

namespace {

/**
 * @brief A new module that skips the discovery on this boot, with a sensor on port 2: the boot had
 *        no catalog and reported placeholders, the catalog arrives, and the run goes on until the
 *        restart's pass is over. The structure that went out last after the catalog describes the
 *        sensor the pass found and no placeholder.
 */
void assertTheNewModuleReportsTheSensorItFound(ModuleUnderTest& module)
{
    const SimServer::Call* first = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
    TEST_ASSERT_TRUE_MESSAGE(first != nullptr && !first->structureSensors.empty(),
                             "the premise: the boot reported a structure with records");
    for (size_t i = 0; i < first->structureSensors.size(); i++)
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("none", first->structureSensors[i].type.c_str(),
                                         "the premise: the boot's structure reports placeholders");
    }
    const SimServer::Call* catalog = module.server.firstAct(OutboxKind::SENSOR_TYPES_FETCH);
    TEST_ASSERT_NOT_NULL_MESSAGE(catalog, "the premise: the catalog was fetched after the boot");
    // Copied now: the server's record of calls grows while the module runs on, and moves.
    const uint64_t catalogEndedAtMs = catalog->endedAtMs;

    TEST_ASSERT_TRUE_MESSAGE(module.runUntilTheTapeSays("STARTUP_SCAN: finished", 1500000),
                             "the catalog arrived and no pass ran to its end");
    module.scheduler.runForMs(60000);
    assertTheRunIsSound(module);

    const std::vector<SimServer::Call> acts = module.server.networkActs();
    std::string described;
    for (size_t i = 0; i < acts.size(); i++)
    {
        if (acts[i].kind != OutboxKind::MODULE_STRUCTURE || acts[i].startedAtMs < catalogEndedAtMs)
        {
            continue;
        }
        std::vector<std::string> records;
        for (size_t r = 0; r < acts[i].structureSensors.size(); r++)
        {
            records.push_back(acts[i].structureSensors[r].uid + ":" + acts[i].structureSensors[r].type);
        }
        std::sort(records.begin(), records.end());
        described.clear();
        for (size_t r = 0; r < records.size(); r++)
        {
            described += (r == 0 ? "" : " ") + records[r];
        }
    }
    printf("  s19: a new module that skipped the discovery, its last structure: %s\n", described.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("s19-module.02.01:temperature s19-module.02.02:humidity", described.c_str(),
                                     "after the catalog the server holds placeholders, not the sensor on port 2");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.startupScan.preparationArmCount(),
                                     "a module holding no sensor was armed for a preparation of nothing");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.startupScan.armCount(),
                                     "the catalog arrived and the discovery was not armed");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.adoptedCount() >= 1, "the discovery found nothing on port 2");
}

} // namespace

// A new module - nothing stored, no catalog on flash, a sensor plugged into port 2 - booted with
// noScanSensors already set, as the link supervisor sets it before an offline restart (a command, a
// firmware recovery and a factory reset set it too). Its boot had no catalog to discover anything
// with and reported placeholders. The catalog arrives: a module holding no sensor has nothing to
// prepare, so the discovery is armed, finds the sensor, and the placeholders are replaced by it.
void test_s19_a_new_module_restarted_to_skip_the_discovery_finds_its_sensor_when_the_catalog_arrives(void)
{
    ModuleUnderTest module(anErasedModule(true));
    module.port(2).addDevice(makeDevice(1));
    bootAnErasedModule(module);

    assertTheNewModuleReportsTheSensorItFound(module);
}

// The same new module with the button held at power-on: setup sets the flag itself
// (ModuleSetupService::handleModuleMode()). The same placeholders, the same catalog, the same find.
void test_s19_a_new_module_powered_on_with_the_button_held_finds_its_sensor_when_the_catalog_arrives(void)
{
    ModuleUnderTest module(anErasedModule(false));
    module.port(2).addDevice(makeDevice(1));
    module.buttonUseCases.buttonPressedResult = true;
    bootAnErasedModule(module);
    TEST_ASSERT_TRUE_MESSAGE(module.logger.indexOf("SETUP: Button pressed at startup, noScanSensors flag set") >= 0,
                             "the premise: the button set the flag at power-on");

    assertTheNewModuleReportsTheSensorItFound(module);
}

// A module with a sensor stored on port 0 and no catalog on flash, told by its last power-on to skip
// the discovery. Its boot has no catalog to describe the sensor by and reports placeholders for the
// ports. The catalog arrives later and arms the preparation only, which finds nothing out - and one
// more structure goes out all the same, describing the sensor the registry holds.
void test_s19_a_late_catalog_sends_the_structure_a_boot_of_placeholders_owes(void)
{
    Boot boot = anErasedModule(true);
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    bootAnErasedModule(module);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.startupScan.preparationArmCount(),
                                     "the premise: the catalog armed the preparation only");
    const SimServer::Call* first = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
    TEST_ASSERT_TRUE_MESSAGE(first != nullptr && !first->structureSensors.empty(),
                             "the premise: the boot reported a structure with records");
    for (size_t i = 0; i < first->structureSensors.size(); i++)
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("none", first->structureSensors[i].type.c_str(),
                                         "the premise: the boot's structure reports placeholders");
    }
    const SimServer::Call* catalog = module.server.firstAct(OutboxKind::SENSOR_TYPES_FETCH);
    TEST_ASSERT_NOT_NULL_MESSAGE(catalog, "the premise: the catalog was fetched after the boot");

    const SimServer::Call* after = module.server.firstActFrom(OutboxKind::MODULE_STRUCTURE, catalog->endedAtMs);
    TEST_ASSERT_NOT_NULL_MESSAGE(after, "the catalog arrived and the server was left with the boot's placeholders");
    std::vector<std::string> records;
    for (size_t i = 0; i < after->structureSensors.size(); i++)
    {
        records.push_back(after->structureSensors[i].uid + ":" + after->structureSensors[i].type);
    }
    std::sort(records.begin(), records.end());
    std::string described;
    for (size_t i = 0; i < records.size(); i++)
    {
        described += (i == 0 ? "" : " ") + records[i];
    }
    printf("  s19: the structure after the late catalog: %s\n", described.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("s19-module.00.01:temperature s19-module.00.02:humidity", described.c_str(),
                                     "the structure after the late catalog does not describe the stored sensor");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, module.server.actCountBetween(OutboxKind::MODULE_STRUCTURE, catalog->endedAtMs,
                                                                     UINT64_MAX),
                                   "the late catalog was answered with more than one structure");
}

// The same boot, with the stored sensor behind the catalog: its second channel has a type the
// model no longer gives it. The catalog synchronization stores the sensor with the model's channels
// and sends the structure itself - and that structure, built from the registry, is the one the
// placeholders were owed: the preparation the catalog arms does not send another.
void test_s19_a_late_catalog_that_changes_the_stored_sensor_sends_one_structure(void)
{
    Boot boot = anErasedModule(true);
    boot.storedOn.push_back(0);
    boot.storedBehindTheCatalog = true;
    ModuleUnderTest module(boot);
    bootAnErasedModule(module);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.startupScan.preparationArmCount(),
                                     "the premise: the catalog armed the preparation only");
    const SimServer::Call* catalog = module.server.firstAct(OutboxKind::SENSOR_TYPES_FETCH);
    TEST_ASSERT_NOT_NULL_MESSAGE(catalog, "the premise: the catalog was fetched after the boot");
    const SimServer::Call* after = module.server.firstActFrom(OutboxKind::MODULE_STRUCTURE, catalog->endedAtMs);
    TEST_ASSERT_NOT_NULL_MESSAGE(after, "the catalog arrived and the server was left with the boot's placeholders");
    bool describesTheModelsType = false;
    for (size_t i = 0; i < after->structureSensors.size(); i++)
    {
        describesTheModelsType = describesTheModelsType ||
                                 (after->structureSensors[i].uid == "s19-module.00.02" &&
                                  after->structureSensors[i].type == "humidity");
    }
    TEST_ASSERT_TRUE_MESSAGE(describesTheModelsType,
                             "the premise: the structure describes the sensor by the catalog's channels");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, module.server.actCountBetween(OutboxKind::MODULE_STRUCTURE, catalog->endedAtMs,
                                                                     UINT64_MAX),
                                   "the late catalog was answered with more than one structure");
}

namespace {

/** @brief The structure that went out last: its records, "uid:type", sorted and joined. */
std::string theLastStructure(const ModuleUnderTest& module)
{
    const std::vector<SimServer::Call> acts = module.server.networkActs();
    std::string described;
    for (size_t i = 0; i < acts.size(); i++)
    {
        if (acts[i].kind != OutboxKind::MODULE_STRUCTURE)
        {
            continue;
        }
        std::vector<std::string> records;
        for (size_t r = 0; r < acts[i].structureSensors.size(); r++)
        {
            records.push_back(acts[i].structureSensors[r].uid + ":" + acts[i].structureSensors[r].type);
        }
        std::sort(records.begin(), records.end());
        described.clear();
        for (size_t r = 0; r < records.size(); r++)
        {
            described += (r == 0 ? "" : " ") + records[r];
        }
    }
    return described;
}

/** @brief The run goes on until the discovery's pass is over, and a minute for what it sends. */
void runUntilThePassIsOver(ModuleUnderTest& module)
{
    module.bootAndStartTheLoop();
    TEST_ASSERT_TRUE_MESSAGE(module.runUntilTheTapeSays("STARTUP_SCAN: finished", 1500000),
                             "no pass of the discovery ran to its end");
    module.scheduler.runForMs(60000);
    assertTheRunIsSound(module);
}

} // namespace

// A module with the catalog on flash and no sensor stored, booted with noScanSensors set - configured on
// the web page without a reset, restarted by the link's supervision, or after a firmware update: it has no
// sensor to prepare, so the discovery is armed, finds the sensor on port 2, and the server learns of it.
void test_s19_a_module_with_a_catalog_and_no_sensor_finds_its_sensor_on_a_boot_that_skips_the_discovery(void)
{
    Boot boot; // the catalog on flash, nothing stored
    boot.noScanSensors = true;
    boot.registered = false;
    ModuleUnderTest module(boot);
    module.port(2).addDevice(makeDevice(1));

    runUntilThePassIsOver(module);

    const std::string described = theLastStructure(module);
    printf("  s19: catalog on flash, no sensor, the flag set - the last structure: %s\n", described.c_str());
    TEST_ASSERT_FALSE_MESSAGE(module.settings.getNoScanSensors(), "the boot did not consume the noScanSensors flag");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.startupScan.armCount(),
                                     "a module that holds no sensor was not armed for the discovery");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.startupScan.preparationArmCount(),
                                     "a module that holds no sensor was armed for a preparation of nothing");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("s19-module.02.01:temperature s19-module.02.02:humidity", described.c_str(),
                                     "the server was not told of the sensor on port 2");
}

// The whole road of a module that is reset and configured again: the factory reset as the module makes
// it (ClearModuleDataService::clearAllData()), the next boot's half of it before setup (main.cpp, the
// needClearSpiffs marker: clearStoredFiles()), the web page's /set_module_to_configured, which sets
// CONFIGURED_DONE and noScanSensors (ModuleConfigurationWebServer), and the first boot after it,
// with a sensor on port 2: found, and in the structure.
void test_s19_a_module_reset_and_configured_on_the_web_finds_its_sensor_on_the_first_boot(void)
{
    Boot boot; // a module that has run: registered, the catalog on flash
    ModuleUnderTest module(boot);
    module.port(2).addDevice(makeDevice(1));

    TEST_ASSERT_TRUE_MESSAGE(module.clearModuleData.clearAllData(), "the factory reset failed");
    TEST_ASSERT_EQUAL_MESSAGE(NEED_CLEAR_SPIFFS_MARKER, module.settings.getNeedClearSpiffs(),
                              "the premise: the reset asks the next boot to remove the files");
    module.clearModuleData.clearStoredFiles();
    module.settings.setNeedClearSpiffs(0);
    TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::USER_CAN_CONFIGURE, module.settings.getIfConfigured(),
                              "the premise: the reset left the module to be configured");
    module.settings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
    module.settings.setNoScanSensors(1);

    runUntilThePassIsOver(module);

    const SimServer::Call* first = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
    const std::string described = theLastStructure(module);
    printf("  s19: reset, configured on the web - the last structure: %s\n", described.c_str());
    TEST_ASSERT_TRUE_MESSAGE(first != nullptr && !first->structureSensors.empty() &&
                                 first->structureSensors[0].type == "none",
                             "the premise: the reset removed the catalog and the boot reported placeholders");
    TEST_ASSERT_FALSE_MESSAGE(module.settings.getNoScanSensors(), "the boot did not consume the noScanSensors flag");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.startupScan.armCount(),
                                     "the reset and configured module was not armed for the discovery");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("s19-module.02.01:temperature s19-module.02.02:humidity", described.c_str(),
                                     "the server was not told of the sensor on port 2");
}

// Guard: a server that holds every attempt for 170 s keeps the network phases busy for minutes; the
// discovery does not wait for them past its window - it starts at arm + BOOT_SETTLE_MS, and not later.
void test_s19_on_a_silent_server_the_discovery_starts_at_the_end_of_its_window(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    boot.structureAnswerMs = 170000;
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(150000);
    assertTheRunIsSound(module);

    const uint64_t armedAt = module.tapeAtMs("STARTUP_SCAN: armed");
    printf("  s19: silent server: armed at %lu ms, first discovery step at %lu ms\n",
           (unsigned long)armedAt, (unsigned long)module.firstScanStepAtMs);
    TEST_ASSERT_TRUE_MESSAGE(armedAt > 0, "the boot did not arm the discovery");
    TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs >= armedAt + StartupSensorScanUseCase::BOOT_SETTLE_MS,
                             "on a silent server the discovery started inside its window");
    TEST_ASSERT_TRUE_MESSAGE(module.firstScanStepAtMs <= armedAt + StartupSensorScanUseCase::BOOT_SETTLE_MS + 1000,
                             "on a silent server the discovery waited past its window");
}

// --------------------------------------------------------------------------------------------------
// 10. What the discovery finds before the module has reported itself is reported after it - with the
//     link's address, not the 0.0.0.0 of a boot without one.
// --------------------------------------------------------------------------------------------------

namespace {

/**
 * @brief The structures on the wire once the link came: the boot's first - every one of them with the
 *        link's address - and then the one the discovery owed, with what it found.
 *
 * The boot's structure is built from the module's node, which the discovery writes its finds into as it
 * makes them, so the boot's may already carry the find; what the discovery owes is its own structure,
 * built from the registry when it goes - and that one must not go before the boot's has settled.
 */
void assertTheFindIsReportedAfterTheBootsStructure(ModuleUnderTest& module, int pinOfTheFind,
                                                   uint64_t linkAtMs)
{
    module.logger.printLinesStartingWith("SETUP: STRUCTURE_REPUBLISH");
    std::vector<SimServer::Call> structures;
    const std::vector<SimServer::Call> acts = module.server.networkActs();
    for (size_t i = 0; i < acts.size(); i++)
    {
        if (acts[i].kind == OutboxKind::MODULE_STRUCTURE)
        {
            printf("  s19: structure at %lu ms, ip %s, %u record(s)\n", (unsigned long)acts[i].startedAtMs,
                   acts[i].ip.c_str(), (unsigned)acts[i].structureSensors.size());
            if (acts[i].startedAtMs <= linkAtMs + 10000)
            {
                structures.push_back(acts[i]);
            }
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(!structures.empty(), "no structure reached the server");
    for (size_t i = 0; i < structures.size(); i++)
    {
        char message[160];
        snprintf(message, sizeof(message), "structure %u on the wire carried the address %s", (unsigned)i,
                 structures[i].ip.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(MODULE_IP, structures[i].ip.c_str(), message);
    }

    char uid[40];
    snprintf(uid, sizeof(uid), "%s.%02d", MODULE_UID, pinOfTheFind);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)structures.size(),
                                     "in the ten seconds after the link came, the boot's structure and "
                                     "the one the discovery owed should have gone out, and a different "
                                     "number did");
    TEST_ASSERT_TRUE_MESSAGE(structures[1].startedAtMs >= structures[0].endedAtMs,
                             "the discovery's structure went out beside the boot's");
    bool foundInSecond = false;
    for (size_t i = 0; i < structures[1].structureSensors.size(); i++)
    {
        foundInSecond = foundInSecond ||
                        structures[1].structureSensors[i].uid.compare(0, strlen(uid), uid) == 0;
    }
    TEST_ASSERT_TRUE_MESSAGE(foundInSecond, "the discovery's structure did not carry what it found");
}

} // namespace

void test_s19_without_a_link_the_restarts_find_is_reported_after_the_boots_structure(void)
{
    Boot boot;
    boot.associationMs = 60000;
    boot.linkAtBoot = false;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.mqttManager.connected = false;
    // A sensor somebody plugged into port 2 while the module was off, still at its factory address.
    module.port(2).addDevice(makeDevice(1));

    module.bootAndStartTheLoop();
    TEST_ASSERT_TRUE_MESSAGE(module.runUntilTheTapeSays("STARTUP_SCAN: finished", 1500000),
                             "the restart's pass did not end with no link");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.adoptedCount() >= 1, "the restart's pass found nothing");
    TEST_ASSERT_TRUE_MESSAGE(module.server.networkActs().empty(), "something reached the server with no link");

    const uint64_t linkAtMs = module.clock.nowMs();
    module.raiseTheLink();
    module.scheduler.runForMs(60000);
    assertTheRunIsSound(module);
    assertTheFindIsReportedAfterTheBootsStructure(module, 2, linkAtMs);
}

void test_s19_without_a_link_a_later_passs_find_is_reported_after_the_boots_structure(void)
{
    Boot boot;
    boot.associationMs = 60000;
    boot.linkAtBoot = false;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.mqttManager.connected = false;

    module.bootAndStartTheLoop();
    TEST_ASSERT_TRUE_MESSAGE(module.runUntilTheTapeSays("STARTUP_SCAN: finished", 1500000),
                             "the restart's pass did not end with no link");

    // Plugged in after the restart's pass: the periodic pass over the free ports finds it.
    const uint32_t adoptedBefore = module.startupScan.adoptedCount();
    module.port(2).addDevice(makeDevice(1));
    for (int second = 0; second < 600 && module.startupScan.adoptedCount() == adoptedBefore; second++)
    {
        module.scheduler.runForMs(1000);
    }
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.adoptedCount() > adoptedBefore,
                             "the periodic pass did not find the sensor plugged in after the restart");
    // And until that pass is over: its structure is the one it sends as it ends.
    const size_t passesEnded = module.logger.countOf("STARTUP_SCAN: finished");
    for (int second = 0; second < 600 && module.logger.countOf("STARTUP_SCAN: finished") == passesEnded; second++)
    {
        module.scheduler.runForMs(1000);
    }
    TEST_ASSERT_TRUE_MESSAGE(module.logger.countOf("STARTUP_SCAN: finished") > passesEnded,
                             "the pass that found the sensor did not end");
    TEST_ASSERT_TRUE_MESSAGE(module.server.networkActs().empty(), "something reached the server with no link");

    const uint64_t linkAtMs = module.clock.nowMs();
    module.raiseTheLink();
    module.scheduler.runForMs(60000);
    assertTheRunIsSound(module);
    assertTheFindIsReportedAfterTheBootsStructure(module, 2, linkAtMs);
}

// --------------------------------------------------------------------------------------------------
// 11. The diagnostic: its tick runs from millis 0, its first send waits for the boot's first STEADY.
// --------------------------------------------------------------------------------------------------

namespace {

/** @brief The uptimes the diagnostic snapshots the server received were taken at, in order. */
std::vector<unsigned long> diagnosticSnapshots(const ModuleUnderTest& module)
{
    std::vector<unsigned long> uptimes;
    const std::vector<SimServer::Call> acts = module.server.networkActs();
    for (size_t i = 0; i < acts.size(); i++)
    {
        if (acts[i].kind == OutboxKind::DIAGNOSTIC)
        {
            uptimes.push_back(acts[i].diagnosticUptimeMs);
        }
    }
    return uptimes;
}

} // namespace

void test_s19_the_first_diagnostic_goes_at_the_first_steady_of_a_slow_boot(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    boot.structureAnswerMs = 40000; // the structure is still on its way at the 30 s tick
    ModuleUnderTest module(boot);
    module.debugSystem.enabled.insert(DebugFeatures::DIAGNOSTIC_DEBUG);

    module.bootAndStartTheLoop();
    module.runUntilMs(120000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ");

    const uint64_t steadyAt = module.tapeAtMs("BOOT_SEQ: NET_CATALOG -> STEADY");
    const std::vector<unsigned long> snapshots = diagnosticSnapshots(module);
    TEST_ASSERT_TRUE_MESSAGE(steadyAt > 0, "the boot never got to STEADY");
    TEST_ASSERT_TRUE_MESSAGE(!snapshots.empty(), "no diagnostic reached the server");

    char message[200];
    snprintf(message, sizeof(message),
             "the first diagnostic the server got was taken at %lu ms, during the boot's network phases "
             "(STEADY at %lu ms)",
             snapshots[0], (unsigned long)steadyAt);
    TEST_ASSERT_TRUE_MESSAGE(snapshots[0] >= steadyAt, message);
    TEST_ASSERT_TRUE_MESSAGE(snapshots[0] <= steadyAt + 1000, "the owed diagnostic waited past the first STEADY");

    // The tick itself ran at 30 s: only its send waited.
    const uint64_t firstTickAt = module.tapeAtMs("DIAG: Performed diagnostics");
    TEST_ASSERT_TRUE_MESSAGE(firstTickAt >= 30000 && firstTickAt < 30000 + 2 * CONTROL_PERIOD_MS,
                             "the diagnostic's tick did not run at 30 s");
}

// The first STEADY of a slow boot on the very iteration of a diagnostic tick: the tick's own send is
// the one the boot owed. One snapshot, one send, and the tape names that send.
void test_s19_a_first_steady_on_a_diagnostic_tick_sends_the_owed_diagnostic_once(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    // With an association of 12 s, and the boot's module command fetch served before the catalog, this
    // answer puts the first STEADY on the iteration of the 60 s tick.
    boot.structureAnswerMs = 47050;
    ModuleUnderTest module(boot);
    module.debugSystem.enabled.insert(DebugFeatures::DIAGNOSTIC_DEBUG);

    module.bootAndStartTheLoop();
    module.runUntilMs(59000);
    const uint32_t collapsedBefore = module.outbox.collapsedCount();
    module.runUntilMs(62000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ");

    const uint64_t steadyAt = module.tapeAtMs("BOOT_SEQ: NET_CATALOG -> STEADY");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(60000, steadyAt, "the stand did not put the first STEADY on the 60 s tick");

    size_t snapshotsAtSteady = 0;
    for (size_t i = 0; i < module.logger.lines.size(); i++)
    {
        const TapeLine& line = module.logger.lines[i];
        if (line.atMs >= steadyAt && line.text.find("DIAG: Performed diagnostics") != std::string::npos)
        {
            snapshotsAtSteady++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)snapshotsAtSteady,
                                     "the first STEADY on a tick took the diagnostic twice");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(collapsedBefore, module.outbox.collapsedCount(),
                                     "a second diagnostic replaced the first in the outbox");

    // And the server got it: one diagnostic, the snapshot taken at STEADY.
    const std::vector<unsigned long> delivered = diagnosticSnapshots(module);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)delivered.size(),
                                     "the server did not get exactly one diagnostic by the end of the STEADY tick");
    TEST_ASSERT_TRUE_MESSAGE(delivered[0] >= steadyAt && delivered[0] < steadyAt + CONTROL_PERIOD_MS,
                             "the diagnostic the server got was not the snapshot taken at STEADY");

    char owed[96];
    snprintf(owed, sizeof(owed), "sent on STEADY at %lu ms", (unsigned long)steadyAt);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.logger.countOf("BOOT_SEQ: diagnostic owed since"),
                                     "the tape did not say once that the owed diagnostic went");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.logger.countOf(owed),
                                     "the tape put the owed diagnostic's send at another moment than STEADY");
}

// Guard: on an ordinary boot the network phases are over long before the first tick, which goes - and
// sends - at 30 s.
void test_s19_on_an_ordinary_boot_the_first_diagnostic_goes_at_30_s(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(70000);
    assertTheRunIsSound(module);

    const std::vector<unsigned long> snapshots = diagnosticSnapshots(module);
    TEST_ASSERT_TRUE_MESSAGE(!snapshots.empty(), "no diagnostic reached the server");
    TEST_ASSERT_TRUE_MESSAGE(snapshots[0] >= 30000 && snapshots[0] < 30000 + 2 * CONTROL_PERIOD_MS,
                             "on an ordinary boot the first diagnostic was not taken at 30 s");
    TEST_ASSERT_TRUE_MESSAGE(snapshots.size() >= 2 && snapshots[1] >= 60000 && snapshots[1] < 60000 + 4 * CONTROL_PERIOD_MS,
                             "the second diagnostic was not 30 s after the first");
}

// With no link at the boot: setup waits for none, the boot's association is asked for on the loop's
// first iteration, and nothing about the link is decided while it is on its way. Once it is back
// without a link, the next supervision tick sees the link gone and restarts the radio, and the restarts
// after that keep the algorithm's pauses.
void test_s19_without_a_link_the_boot_asks_for_the_association_on_its_first_iteration(void)
{
    Boot boot;
    boot.associationMs = 60000; // what the radio waits for an association that does not come
    boot.linkAtBoot = false;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(215000);
    assertTheRunIsSound(module);
    module.printDecisions();
    module.logger.printLinesStartingWith("WIFI_RECONNECT");
    module.logger.printLinesStartingWith("LINK_CHECK");

    TEST_ASSERT_TRUE_MESSAGE(module.decisions.size() >= 3, "the module asked the radio for too little");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("BOOT_ASSOCIATION", module.decisions[0].what.c_str(),
                                     "the first thing asked of the radio was not the boot's association");
    TEST_ASSERT_TRUE_MESSAGE(module.decisions[0].atMs < 2 * CONTROL_PERIOD_MS + 100,
                             "the boot's association was not asked for on the loop's first iteration");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.logger.countOf("WIFI_RECONNECT: boot association handed"),
                                     "the boot's association was asked for more than once");

    const uint64_t endedAt = module.tapeAtMs("WIFI_RECONNECT: boot association result=0");
    TEST_ASSERT_TRUE_MESSAGE(endedAt >= 60000, "the boot's association did not come back without a link");
    const uint64_t lostAt = module.tapeAtMs("LINK_CHECK: OK -> LINK_DOWN");
    TEST_ASSERT_TRUE_MESSAGE(lostAt > endedAt, "the link was decided lost while the boot's association was on its way");

    // The first tick after it, and from there the algorithm's own pauses.
    const uint64_t firstTickAfter = ((endedAt / LinkSupervisor::TICK_INTERVAL_MS) + 1) * LinkSupervisor::TICK_INTERVAL_MS;
    TEST_ASSERT_EQUAL_STRING("RADIO_RESTARTED", module.decisions[1].what.c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.decisions[1].atMs >= firstTickAfter &&
                                 module.decisions[1].atMs < firstTickAfter + 2 * CONTROL_PERIOD_MS + 100,
                             "the radio was not restarted on the first tick after the association came back");
    TEST_ASSERT_EQUAL_STRING("RADIO_RESTARTED", module.decisions[2].what.c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.decisions[2].atMs >= module.decisions[1].atMs + 60000 &&
                                 module.decisions[2].atMs < module.decisions[1].atMs + 60000 +
                                                                LinkSupervisor::TICK_INTERVAL_MS + 100,
                             "the second restart did not follow the first after its pause of a minute");
}

void test_s19_without_a_link_nothing_goes_before_the_structure_once_the_link_comes(void)
{
    Boot boot;
    boot.associationMs = 60000;
    boot.linkAtBoot = false;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.mqttManager.connected = false;

    module.bootAndStartTheLoop();
    module.runUntilMs(100000);
    module.raiseTheLink();
    module.runUntilMs(160000);
    assertTheRunIsSound(module);
    module.server.printActs(4);

    const std::vector<SimServer::Call> acts = module.server.networkActs();
    TEST_ASSERT_TRUE_MESSAGE(!acts.empty(), "nothing reached the server once the link came");
    char message[160];
    snprintf(message, sizeof(message), "the first network act once the link came was %s, not the structure",
             SimServer::kindName(acts[0].kind));
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)OutboxKind::MODULE_STRUCTURE, (int)acts[0].kind, message);
}

// With no link at the boot the boot's association is the module's only request. When the heap refuses
// the network task at that submission, the supervision's next tick creates it, and the association
// runs then - nothing else would submit before the link came back by itself.
void test_s19_without_a_link_a_refused_network_task_is_created_on_the_next_supervision_tick(void)
{
    Boot boot;
    boot.associationMs = 60000;
    boot.linkAtBoot = false;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.scheduler.failNextCreateTasksNamed("outbox-net", 1);

    module.bootAndStartTheLoop();
    module.runUntilMs(29000);
    TEST_ASSERT_TRUE_MESSAGE(module.logger.indexOf("WIFI_RECONNECT: boot association handed") >= 0,
                             "with no link the loop did not ask for the boot's association");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.taskStartFailureCount(),
                                     "the stand did not refuse the network task at the first submission");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.wifiAdapter.reconnectCallCount,
                                     "the radio was started with no network task to start it");

    module.runUntilMs(31000);
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.taskStarted(), "the network task was not created on the next tick");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.wifiAdapter.reconnectCallCount,
                                     "the association waiting for the network task was not run");
    TEST_ASSERT_TRUE_MESSAGE(!module.decisions.empty() && module.decisions[0].what == "BOOT_ASSOCIATION" &&
                                 module.decisions[0].atMs >= LinkSupervisor::TICK_INTERVAL_MS &&
                                 module.decisions[0].atMs < LinkSupervisor::TICK_INTERVAL_MS + 2 * CONTROL_PERIOD_MS + 100,
                             "the boot's association was not run on the supervision's next tick");
    module.runUntilMs(100000);
    assertTheRunIsSound(module);
}

// The boot's association, still waiting when the link came back by itself: the network task finds the
// station associated and leaves the radio alone - the structure, not a minute without a link, is what
// the return brings.
void test_s19_a_boot_association_waiting_for_a_link_that_is_back_is_not_run(void)
{
    Boot boot;
    boot.associationMs = 60000;
    boot.linkAtBoot = false;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.scheduler.failNextCreateTasksNamed("outbox-net", 1);

    module.bootAndStartTheLoop();
    module.runUntilMs(10000);
    TEST_ASSERT_TRUE_MESSAGE(module.logger.indexOf("WIFI_RECONNECT: boot association handed") >= 0,
                             "with no link the loop did not ask for the boot's association");
    TEST_ASSERT_FALSE_MESSAGE(module.outbox.taskStarted(), "the stand did not leave the association waiting");

    // The link comes back by itself before the supervision's next tick; the structure that reports the
    // address creates the network task, which takes the waiting association first.
    module.raiseTheLink();
    module.runUntilMs(25000);
    assertTheRunIsSound(module);

    TEST_ASSERT_TRUE_MESSAGE(module.outbox.taskStarted(), "the structure did not create the network task");
    TEST_ASSERT_NOT_NULL_MESSAGE(module.server.firstAct(OutboxKind::MODULE_STRUCTURE),
                                 "the structure did not reach the server once the link was back");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.wifiAdapter.reconnectCallCount,
                                     "the radio was started on a link that had come back");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.logger.countOf("WIFI_RECONNECT: boot association result=1"),
                                     "the supervision did not learn that the station is associated");
}

// --------------------------------------------------------------------------------------------------
// 12. The subscriptions to the sensors other modules publish are reconciled on the first CONNACK -
//     not skipped because setup ended before a timer did.
// --------------------------------------------------------------------------------------------------

void test_s19_the_remote_sensor_subscriptions_are_reconciled_on_the_first_connack(void)
{
    Boot boot;
    boot.associationMs = 5000;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    // A rule that reads a sensor of another module.
    Rule rule;
    rule.uid = "rule-remote";
    rule.deviceUid = "device-1";
    rule.sensorUid = "84:F7:03:FE:88:2C.00.01";
    rule.ruleType = RuleType::SENSOR;
    rule.mode = RuleMode::ON;
    module.ruleRepository.rules.push_back(rule);

    module.bootAndStartTheLoop();
    module.runUntilMs(20000);
    assertTheRunIsSound(module);

    bool subscribed = false;
    uint64_t subscribedAt = 0;
    for (size_t i = 0; i < module.mqttManager.subscribed.size(); i++)
    {
        if (module.mqttManager.subscribed[i] == "hardware/update/84:F7:03:FE:88:2C")
        {
            subscribed = true;
            subscribedAt = module.mqttManager.subscribedAtMs[i];
        }
    }
    const uint64_t connackAt = module.mqttManager.connackAtMs;
    printf("  s19: CONNACK at %lu ms; remote sensor topic subscribed: %s at %lu ms\n", (unsigned long)connackAt,
           subscribed ? "yes" : "no", (unsigned long)subscribedAt);
    TEST_ASSERT_TRUE_MESSAGE(connackAt >= 5000, "the broker answered before the boot's association was over");
    TEST_ASSERT_TRUE_MESSAGE(subscribed, "the module never subscribed to the module its rule reads");
    TEST_ASSERT_TRUE_MESSAGE(subscribedAt >= connackAt && subscribedAt < connackAt + 2 * CONTROL_PERIOD_MS,
                             "the subscriptions were not reconciled on the first CONNACK");
}

// --------------------------------------------------------------------------------------------------
// 13. A return of the link goes through the same order: the structure first when the address changed,
//     and otherwise the acts of a module whose gates stay open through the outage.
// --------------------------------------------------------------------------------------------------

void test_s19_after_a_flap_to_a_new_address_the_telemetry_follows_the_new_structure(void)
{
    for (int order = 0; order < 2; order++)
    {
        Boot boot;
        boot.storedOn.push_back(0);
        boot.tasksStartAtOnce = (order == 1);
        ModuleUnderTest module(boot);

        module.bootAndStartTheLoop();
        module.runUntilMs(40000);
        module.dropTheLink();
        module.runUntilMs(58000);
        module.wifiAdapter.setLocalIP("192.168.1.101");
        const uint64_t returnAt = module.clock.nowMs();
        module.raiseTheLink();
        module.runUntilMs(100000);
        assertTheRunIsSound(module);
        module.server.printActs(16);

        const SimServer::Call* structure = module.server.firstActFrom(OutboxKind::MODULE_STRUCTURE, returnAt);
        const SimServer::Call* telemetry = module.server.firstActFrom(OutboxKind::SENSOR_UPDATE, returnAt);
        TEST_ASSERT_NOT_NULL_MESSAGE(structure, "the new address was never reported");
        TEST_ASSERT_EQUAL_STRING("192.168.1.101", structure->ip.c_str());
        TEST_ASSERT_NOT_NULL_MESSAGE(telemetry, "no telemetry after the link came back");

        char message[240];
        snprintf(message, sizeof(message),
                 "%s: the first telemetry after the return was built at %lu ms and left at %lu ms; the "
                 "structure of the new address settled at %lu ms",
                 orderName(boot.tasksStartAtOnce), telemetry->updateMillis, (unsigned long)telemetry->startedAtMs,
                 (unsigned long)structure->endedAtMs);
        TEST_ASSERT_TRUE_MESSAGE(telemetry->startedAtMs >= structure->endedAtMs, message);
        TEST_ASSERT_TRUE_MESSAGE(telemetry->updateMillis >= structure->endedAtMs, message);
    }
}

namespace {

/** @brief What one module sends over a window that holds two flaps of its link, by kind. */
struct FlapActs {
    size_t diagnostic = 0;
    size_t sensorUpdate = 0;
    size_t ruleList = 0;
    size_t deviceUpdate = 0;
    size_t structure = 0;
    size_t catalog = 0;
};

const uint64_t FLAPS_FROM_MS = 40000;
// What a module whose gates stay open whatever its link does sends in this run - see the test below.
const size_t REFERENCE_SENSORS_DIAGNOSTIC = 5;
const size_t REFERENCE_SENSORS_SENSOR_UPDATE = 9;
const size_t REFERENCE_SENSORS_RULE_LIST = 0;
const size_t REFERENCE_SENSORS_DEVICE_UPDATE = 0;
const size_t REFERENCE_SENSORS_STRUCTURE = 1;
const size_t REFERENCE_SENSORS_CATALOG = 0;
const size_t REFERENCE_RELAYS_DIAGNOSTIC = 5;
const size_t REFERENCE_RELAYS_SENSOR_UPDATE = 0;
const size_t REFERENCE_RELAYS_RULE_LIST = 1;
const size_t REFERENCE_RELAYS_DEVICE_UPDATE = 5;
const size_t REFERENCE_RELAYS_STRUCTURE = 0;
const size_t REFERENCE_RELAYS_CATALOG = 0;
const uint64_t FLAPS_UNTIL_MS = 220000;

/**
 * @brief Boot, then a minute without the access point - long enough for the module's own offline
 *        decision - and later a two-second gap, the length of a move between access points; the
 *        address stays the same throughout.
 */
FlapActs runTwoFlapsWithTheSameAddress(bool deviceModule)
{
    Boot boot;
    boot.deviceModule = deviceModule;
    if (!deviceModule)
    {
        boot.storedOn.push_back(0);
    }
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(FLAPS_FROM_MS);
    module.dropTheLink();
    module.runUntilMs(100000);
    module.raiseTheLink();
    module.runUntilMs(130000);
    module.dropTheLink();
    module.runUntilMs(132000);
    module.raiseTheLink();
    module.runUntilMs(FLAPS_UNTIL_MS + 1000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ: STEADY -> LINK");

    // The deletion check belongs to the boot's first STEADY; the returns to STEADY after the flaps do not ask again.
    const std::vector<SimServer::Call> deletionQueries =
        module.server.callsOf(IS_DELETED_QUERY, NetworkCommandRunner::TASK_NAME);
    char deletionMessage[160];
    snprintf(deletionMessage, sizeof(deletionMessage), "%s: the module's deletion was asked about other than once on the boot",
             deviceModule ? "a relay module" : "a sensor module");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)deletionQueries.size(), deletionMessage);

    FlapActs acts;
    acts.diagnostic = module.server.actCountBetween(OutboxKind::DIAGNOSTIC, FLAPS_FROM_MS, FLAPS_UNTIL_MS);
    acts.sensorUpdate = module.server.actCountBetween(OutboxKind::SENSOR_UPDATE, FLAPS_FROM_MS, FLAPS_UNTIL_MS);
    acts.ruleList = module.server.actCountBetween(OutboxKind::RULE_LIST, FLAPS_FROM_MS, FLAPS_UNTIL_MS);
    acts.deviceUpdate = module.server.actCountBetween(OutboxKind::DEVICE_UPDATE, FLAPS_FROM_MS, FLAPS_UNTIL_MS);
    acts.structure = module.server.actCountBetween(OutboxKind::MODULE_STRUCTURE, FLAPS_FROM_MS, FLAPS_UNTIL_MS);
    acts.catalog = module.server.actCountBetween(OutboxKind::SENSOR_TYPES_FETCH, FLAPS_FROM_MS, FLAPS_UNTIL_MS);
    printf("  s19: %s, two flaps, acts in [%lu, %lu) ms: DIAGNOSTIC %u, SENSOR_UPDATE %u, RULE_LIST %u, "
           "DEVICE_UPDATE %u, MODULE_STRUCTURE %u, SENSOR_TYPES_FETCH %u\n",
           deviceModule ? "relays" : "sensors", (unsigned long)FLAPS_FROM_MS, (unsigned long)FLAPS_UNTIL_MS,
           (unsigned)acts.diagnostic, (unsigned)acts.sensorUpdate, (unsigned)acts.ruleList,
           (unsigned)acts.deviceUpdate, (unsigned)acts.structure, (unsigned)acts.catalog);
    return acts;
}

void assertTheFlapActsAre(const FlapActs& seen, const FlapActs& reference, const char* module)
{
    char message[160];
    snprintf(message, sizeof(message), "%s: the diagnostics of the two flaps differ from the reference", module);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)reference.diagnostic, (uint32_t)seen.diagnostic, message);
    snprintf(message, sizeof(message), "%s: the sensor updates of the two flaps differ from the reference", module);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)reference.sensorUpdate, (uint32_t)seen.sensorUpdate, message);
    snprintf(message, sizeof(message), "%s: the rule lists of the two flaps differ from the reference", module);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)reference.ruleList, (uint32_t)seen.ruleList, message);
    snprintf(message, sizeof(message), "%s: the device updates of the two flaps differ from the reference", module);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)reference.deviceUpdate, (uint32_t)seen.deviceUpdate, message);
    snprintf(message, sizeof(message), "%s: the structures of the two flaps differ from the reference", module);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)reference.structure, (uint32_t)seen.structure, message);
    snprintf(message, sizeof(message), "%s: the catalog fetches of the two flaps differ from the reference", module);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)reference.catalog, (uint32_t)seen.catalog, message);
}

} // namespace

// Two flaps with the same address send, kind by kind, what a module whose gates in STEADY stay open
// whatever its link does sends in the same run: the reference above. Closing the gates on a lost link
// and holding what they hold changes the order of a return, not what it sends.
void test_s19_flaps_without_a_new_address_send_what_they_sent_before(void)
{
    FlapActs sensorsReference;
    sensorsReference.diagnostic = REFERENCE_SENSORS_DIAGNOSTIC;
    sensorsReference.sensorUpdate = REFERENCE_SENSORS_SENSOR_UPDATE;
    sensorsReference.ruleList = REFERENCE_SENSORS_RULE_LIST;
    sensorsReference.deviceUpdate = REFERENCE_SENSORS_DEVICE_UPDATE;
    sensorsReference.structure = REFERENCE_SENSORS_STRUCTURE;
    sensorsReference.catalog = REFERENCE_SENSORS_CATALOG;
    assertTheFlapActsAre(runTwoFlapsWithTheSameAddress(false), sensorsReference, "a sensor module");

    FlapActs relaysReference;
    relaysReference.diagnostic = REFERENCE_RELAYS_DIAGNOSTIC;
    relaysReference.sensorUpdate = REFERENCE_RELAYS_SENSOR_UPDATE;
    relaysReference.ruleList = REFERENCE_RELAYS_RULE_LIST;
    relaysReference.deviceUpdate = REFERENCE_RELAYS_DEVICE_UPDATE;
    relaysReference.structure = REFERENCE_RELAYS_STRUCTURE;
    relaysReference.catalog = REFERENCE_RELAYS_CATALOG;
    assertTheFlapActsAre(runTwoFlapsWithTheSameAddress(true), relaysReference, "a relay module");
}

// --------------------------------------------------------------------------------------------------
// 14. A wait that does not end is given up on - after a ceiling derived from what it waits for - and
//     giving up does what the event would have done.
// --------------------------------------------------------------------------------------------------

void test_s19_a_structure_whose_completion_is_lost_is_given_up_on(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.completions.loseTheNextOf(OutboxKind::MODULE_STRUCTURE);

    module.bootAndStartTheLoop();
    module.runUntilMs(780000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ");
    module.server.printActs(6);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.completions.lost, "the stand did not lose the structure's completion");
    const uint64_t gaveUpAt = module.tapeAtMs("BOOT_SEQ: NET_STRUCTURE gave up after 702500 ms");
    TEST_ASSERT_TRUE_MESSAGE(gaveUpAt > 0, "the boot waited for the lost completion past its ceiling");

    const SimServer::Call* catalog = module.server.firstAct(OutboxKind::SENSOR_TYPES_FETCH);
    const SimServer::Call* telemetry = module.server.firstAct(OutboxKind::SENSOR_UPDATE);
    TEST_ASSERT_TRUE_MESSAGE(catalog != nullptr && catalog->startedAtMs >= gaveUpAt,
                             "giving up on the structure did not ask for the catalog");
    TEST_ASSERT_TRUE_MESSAGE(telemetry != nullptr && telemetry->startedAtMs >= gaveUpAt,
                             "giving up on the structure did not let the telemetry go");
    TEST_ASSERT_TRUE_MESSAGE(module.tapeAtMs("BOOT_SEQ: NET_CATALOG -> STEADY") >= gaveUpAt,
                             "the boot did not get to STEADY after giving up on the structure");
}

// Guard: a server that holds one attempt for the longest the ceilings count an attempt at - 170 s, the
// handshake at WiFiClientSecure's default - is not given up on: the completion comes, inside the ceiling.
void test_s19_a_structure_that_takes_its_longest_attempt_is_not_given_up_on(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    boot.structureAnswerMs = 170000;
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(240000);
    assertTheRunIsSound(module);

    const SimServer::Call* structure = module.server.firstAct(OutboxKind::MODULE_STRUCTURE);
    const SimServer::Call* catalog = module.server.firstAct(OutboxKind::SENSOR_TYPES_FETCH);
    TEST_ASSERT_NOT_NULL(structure);
    TEST_ASSERT_TRUE_MESSAGE(catalog != nullptr && catalog->startedAtMs >= structure->endedAtMs,
                             "the catalog did not follow the slow structure");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.countOf("gave up"),
                                     "a structure inside its ceiling was given up on");
}

// A server that holds the structure past the ceiling - a body that comes a byte at a time has no
// bound - is given up on while the job still runs. The order on the wire stays the boot's: the jobs
// the give-up let go wait behind the structure, and the structure's own completion, when it comes,
// costs one more catalog fetch at most.
void test_s19_a_structure_held_past_its_ceiling_keeps_its_place_on_the_wire(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    boot.structureAnswerMs = 800000;
    ModuleUnderTest module(boot);

    module.bootAndStartTheLoop();
    module.runUntilMs(1000000);
    assertTheRunIsSound(module);
    module.server.printActs(8);

    const uint64_t gaveUpAt = module.tapeAtMs("BOOT_SEQ: NET_STRUCTURE gave up after 702500 ms");
    TEST_ASSERT_TRUE_MESSAGE(gaveUpAt > 0, "the boot waited past its ceiling for a structure held by the server");

    const std::vector<SimServer::Call> acts = module.server.networkActs();
    TEST_ASSERT_TRUE_MESSAGE(!acts.empty(), "the module put nothing on the network");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)OutboxKind::MODULE_STRUCTURE, (int)acts[0].kind,
                                  "the structure was not the first network act");
    for (size_t i = 1; i < acts.size(); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(acts[i].startedAtMs >= acts[0].endedAtMs,
                                 "something went on the wire beside the structure the boot gave up on");
    }
    TEST_ASSERT_TRUE_MESSAGE(module.server.actCount(OutboxKind::SENSOR_TYPES_FETCH) >= 1 &&
                                 module.server.actCount(OutboxKind::SENSOR_TYPES_FETCH) <= 2,
                             "the give-up and the late completion together cost more than one extra "
                             "catalog fetch");
}

void test_s19_a_catalog_whose_completion_is_lost_is_given_up_on(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.completions.loseTheNextOf(OutboxKind::SENSOR_TYPES_FETCH);

    module.bootAndStartTheLoop();
    module.runUntilMs(4980000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ");

    const uint64_t gaveUpAt = module.tapeAtMs("BOOT_SEQ: NET_CATALOG gave up after 4917500 ms (catalog fetch still in flight)");
    TEST_ASSERT_TRUE_MESSAGE(gaveUpAt > 0, "the boot waited for the lost catalog completion past its ceiling");
    TEST_ASSERT_TRUE_MESSAGE(module.tapeAtMs("BOOT_SEQ: NET_CATALOG -> STEADY (gave up)") >= gaveUpAt,
                             "giving up on the catalog did not take the boot to STEADY");
    // What STEADY opens: the diagnostic's send.
    bool sentAfter = false;
    const std::vector<SimServer::Call> acts = module.server.networkActs();
    for (size_t i = 0; i < acts.size(); i++)
    {
        sentAfter = sentAfter || (acts[i].kind == OutboxKind::DIAGNOSTIC && acts[i].startedAtMs >= gaveUpAt);
    }
    TEST_ASSERT_TRUE_MESSAGE(sentAfter, "no diagnostic was sent once the boot gave up on the catalog");
}

void test_s19_first_readings_that_never_come_are_given_up_on(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    // The heap cannot spare the acquisition task's stack, at any of the polling ticks.
    module.scheduler.failNextCreateTasksNamed(SensorAcquisitionService::TASK_NAME, 1000);

    module.bootAndStartTheLoop();
    module.runUntilMs(60000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ");

    // One stored sensor of two channels: 25 000 ms and 1 100 ms a channel, from the loop's start.
    const uint64_t loopAt = module.tapeAtMs("BOOT_SEQ: LOCAL -> LINK");
    const uint64_t gaveUpAt = module.tapeAtMs("BOOT_SEQ: first readings gave up after 27200 ms (1 of 1 sensor(s) without an outcome)");
    TEST_ASSERT_TRUE_MESSAGE(gaveUpAt > 0, "the boot waited for first readings that never came past its ceiling");
    TEST_ASSERT_TRUE_MESSAGE(gaveUpAt >= loopAt + 27200 && gaveUpAt < loopAt + 27200 + 2 * CONTROL_PERIOD_MS,
                             "the first readings were given up on at the wrong moment");
    const SimServer::Call* telemetry = module.server.firstAct(OutboxKind::SENSOR_UPDATE);
    TEST_ASSERT_TRUE_MESSAGE(telemetry != nullptr && telemetry->startedAtMs >= gaveUpAt,
                             "giving up on the first readings did not let the telemetry go");
}

// A stored sensor deleted before it had its first outcome will never have one: the next read of the
// stored sensors stops waiting for it, and the first readings settle on the sensors that are left -
// not on the ceiling.
void test_s19_a_sensor_deleted_before_its_first_outcome_is_not_waited_for(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    boot.storedOn.push_back(2);
    ModuleUnderTest module(boot);
    // The first read's hand-over finds no acquisition task; the next one, 15 s later, does.
    module.scheduler.failNextCreateTasksNamed(SensorAcquisitionService::TASK_NAME, 1);

    module.bootAndStartTheLoop();
    TEST_ASSERT_TRUE_MESSAGE(module.runUntilTheTapeSays("BOOT_SEQ: LOCAL -> LINK", 30000), "the loop never started");
    const uint64_t loopAt = module.tapeAtMs("BOOT_SEQ: LOCAL -> LINK");
    module.runUntilMs(loopAt + 1000);
    const SensorEntity deleted = makeStoredSensor(2);
    TEST_ASSERT_TRUE_MESSAGE(module.sensorRepository.remove(deleted.uid.c_str()), "the stand could not delete the sensor");
    module.runUntilMs(loopAt + 45000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ: first readings");

    // Two stored sensors of two channels each: the ceiling is 25 000 ms and 4 x 1 100 ms from the loop's start.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.countOf("BOOT_SEQ: first readings gave up"),
                                     "the boot waited out its ceiling for a sensor that had been deleted");
    const uint64_t settledAt = module.tapeAtMs("BOOT_SEQ: first readings settled");
    TEST_ASSERT_TRUE_MESSAGE(settledAt > 0 && settledAt < loopAt + 29400,
                             "the first readings did not settle on the sensor that was left");
}

// The same with the module's only stored sensor: with no sensor left there is nothing to wait for.
void test_s19_the_only_sensor_deleted_before_its_first_outcome_is_not_waited_for(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.scheduler.failNextCreateTasksNamed(SensorAcquisitionService::TASK_NAME, 1);

    module.bootAndStartTheLoop();
    TEST_ASSERT_TRUE_MESSAGE(module.runUntilTheTapeSays("BOOT_SEQ: LOCAL -> LINK", 30000), "the loop never started");
    const uint64_t loopAt = module.tapeAtMs("BOOT_SEQ: LOCAL -> LINK");
    module.runUntilMs(loopAt + 1000);
    const SensorEntity deleted = makeStoredSensor(0);
    TEST_ASSERT_TRUE_MESSAGE(module.sensorRepository.remove(deleted.uid.c_str()), "the stand could not delete the sensor");
    module.runUntilMs(loopAt + 45000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ: first readings");

    // One stored sensor of two channels: the ceiling is 25 000 ms and 2 x 1 100 ms from the loop's start.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.countOf("BOOT_SEQ: first readings gave up"),
                                     "the boot waited out its ceiling for the only sensor, deleted");
    const uint64_t settledAt = module.tapeAtMs("BOOT_SEQ: first readings settled");
    TEST_ASSERT_TRUE_MESSAGE(settledAt > 0 && settledAt <= loopAt + 1000 + 2 * CONTROL_PERIOD_MS,
                             "the first readings did not settle once no sensor was left");
}

// A stored sensor given another model while the first read of the old one is on the line: the answer
// the old model gave is dropped (it is not this sensor's any more), and a dropped reading is not the
// sensor's first outcome - the boot waits for the sensor read as the model it has now.
void test_s19_a_reading_dropped_for_its_old_model_is_not_the_first_outcome(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.debugSystem.enabled.insert(DebugFeatures::SENSOR_DATA_DEBUG);

    module.bootAndStartTheLoop();
    while (module.tapeAtMs("BOOT_SEQ: LOCAL -> LINK") == 0 && module.clock.nowMs() < 30000)
    {
        module.runUntilMs(module.clock.nowMs() + CONTROL_PERIOD_MS);
    }
    const uint64_t loopAt = module.tapeAtMs("BOOT_SEQ: LOCAL -> LINK");
    TEST_ASSERT_TRUE_MESSAGE(loopAt > 0, "the loop never started");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.countOf("BOOT_SEQ: first readings"),
                                     "the stand gave the sensor its new model after its first outcome");

    // As SetSensorModel does it: the row keeps its uid and its port and takes another model.
    SensorEntity renamed = makeStoredSensor(0);
    renamed.model = "S19-TH-B";
    TEST_ASSERT_TRUE_MESSAGE(module.sensorRepository.createOrUpdateSensor(renamed), "the stand could not change the model");

    // The first poll was handed over on the first iteration; the port answers once it has warmed.
    module.runUntilMs(loopAt + PortPower::WAKE_UP_MS + 1000);
    char dropped[96];
    snprintf(dropped, sizeof(dropped), "dropped a reading of %s polled as S19-TH", renamed.uid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.logger.countOf(dropped) > 0, "the stand did not make the old model's reading arrive");
    TEST_ASSERT_FALSE_MESSAGE(module.sensorService.firstReadings().settled,
                              "the reading dropped for its old model settled the first readings");

    module.runUntilMs(loopAt + 45000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ: first readings");

    // The next hand-over, a read interval on, gives the acquisition task the new model: its outcome
    // is the one that settles the first readings - before their ceiling.
    const uint64_t settledAt = module.tapeAtMs("BOOT_SEQ: first readings settled");
    TEST_ASSERT_TRUE_MESSAGE(settledAt >= loopAt + 15000,
                             "the first readings settled before the sensor was read as its new model");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.countOf("BOOT_SEQ: first readings gave up"),
                                     "the sensor read as its new model did not settle the first readings");
}

// The same with the registers: the catalog synchronization stores the sensor again with other
// registers while the first read by the old ones is on the line. That answer was read by registers
// the sensor no longer has and is dropped; a dropped reading is not the sensor's first outcome either.
void test_s19_a_reading_dropped_for_its_old_registers_is_not_the_first_outcome(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.debugSystem.enabled.insert(DebugFeatures::SENSOR_DATA_DEBUG);

    module.bootAndStartTheLoop();
    while (module.tapeAtMs("BOOT_SEQ: LOCAL -> LINK") == 0 && module.clock.nowMs() < 30000)
    {
        module.runUntilMs(module.clock.nowMs() + CONTROL_PERIOD_MS);
    }
    const uint64_t loopAt = module.tapeAtMs("BOOT_SEQ: LOCAL -> LINK");
    TEST_ASSERT_TRUE_MESSAGE(loopAt > 0, "the loop never started");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.countOf("BOOT_SEQ: first readings"),
                                     "the stand gave the sensor its new registers after its first outcome");

    // As the catalog synchronization does it: the row keeps its uid, its port and its model and is
    // stored again with other registers.
    SensorEntity rescaled = makeStoredSensor(0);
    rescaled.modbusRegisterConfigs[0].multiplier *= 10.0f;
    TEST_ASSERT_TRUE_MESSAGE(module.sensorRepository.save(rescaled), "the stand could not change the registers");

    // The first poll was handed over on the first iteration; the port answers once it has warmed.
    module.runUntilMs(loopAt + PortPower::WAKE_UP_MS + 1000);
    char dropped[112];
    snprintf(dropped, sizeof(dropped), "dropped a reading of %s polled by the registers it had before",
             rescaled.uid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.logger.countOf(dropped) > 0, "the stand did not make the old registers' reading arrive");
    TEST_ASSERT_FALSE_MESSAGE(module.sensorService.firstReadings().settled,
                              "the reading dropped for its old registers settled the first readings");

    module.runUntilMs(loopAt + 45000);
    assertTheRunIsSound(module);
    module.logger.printLinesStartingWith("BOOT_SEQ: first readings");

    // The next hand-over, a read interval on, gives the acquisition task the new registers: their
    // outcome is the one that settles the first readings - before their ceiling.
    const uint64_t settledAt = module.tapeAtMs("BOOT_SEQ: first readings settled");
    TEST_ASSERT_TRUE_MESSAGE(settledAt > 0,
                             "the first readings never settled: no poll by the new registers before the ceiling");
    TEST_ASSERT_TRUE_MESSAGE(settledAt >= loopAt + 15000,
                             "the first readings settled before the sensor was read by its new registers");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.countOf("BOOT_SEQ: first readings gave up"),
                                     "the sensor read by its new registers did not settle the first readings");
}

// --------------------------------------------------------------------------------------------------
// 15. The decisions about the link and the restarts are made on the supervision's own tick, every
//     30 s from millis 0, whatever the boot does: each run below is compared, decision by decision,
//     with the reference moments of that tick.
// --------------------------------------------------------------------------------------------------

namespace {

enum class LinkScenario { WIFI_25_MIN, MQTT_15_MIN, HTTP_15_MIN, OFFLINE_1_H, OFFLINE_4_H, NIGHT };

const char* scenarioName(LinkScenario scenario)
{
    switch (scenario)
    {
    case LinkScenario::WIFI_25_MIN: return "access point gone for 25 min (sensors)";
    case LinkScenario::MQTT_15_MIN: return "broker gone for 15 min (relays)";
    case LinkScenario::HTTP_15_MIN: return "server and broker gone for 15 min (relays)";
    case LinkScenario::OFFLINE_1_H: return "access point gone for good (sensors)";
    case LinkScenario::OFFLINE_4_H: return "access point gone for good (relays)";
    case LinkScenario::NIGHT: return "the night window (relays)";
    }
    return "?";
}

std::vector<Decision> runTheLinkScenario(LinkScenario scenario)
{
    Boot boot;
    boot.deviceModule = (scenario == LinkScenario::MQTT_15_MIN || scenario == LinkScenario::HTTP_15_MIN ||
                         scenario == LinkScenario::OFFLINE_4_H || scenario == LinkScenario::NIGHT);
    if (!boot.deviceModule)
    {
        boot.storedOn.push_back(0);
    }
    ModuleUnderTest module(boot);
    if (scenario == LinkScenario::WIFI_25_MIN)
    {
        // A catalog fetch that failed at boot is what the connectivity edge re-arms.
        module.server.refuse[(int)OutboxKind::SENSOR_TYPES_FETCH] = true;
    }
    if (scenario == LinkScenario::NIGHT)
    {
        module.timeAdapter.startAtLocal(1, 58);
    }

    module.setupService.setupModule();
    module.startTheLoop();
    module.runUntilMs(100000);
    module.server.refuse[(int)OutboxKind::SENSOR_TYPES_FETCH] = false;

    switch (scenario)
    {
    case LinkScenario::WIFI_25_MIN:
        module.dropTheLink();
        module.runLongUntilMs(1600000);
        module.raiseTheLink();
        module.runUntilMs(1700000);
        break;
    case LinkScenario::MQTT_15_MIN:
        module.mqttManager.connected = false;
        module.runUntilMs(1000000);
        module.mqttManager.connected = true;
        module.runUntilMs(1100000);
        break;
    case LinkScenario::HTTP_15_MIN:
        module.mqttManager.connected = false;
        module.restApi.countOffline = 10;
        module.runUntilMs(1000000);
        module.mqttManager.connected = true;
        module.restApi.countOffline = 0;
        module.runUntilMs(1100000);
        break;
    case LinkScenario::OFFLINE_1_H:
        module.dropTheLink();
        module.runLongUntilMs(100000 + 3600000 + 120000);
        break;
    case LinkScenario::OFFLINE_4_H:
        module.dropTheLink();
        module.runLongUntilMs(100000 + 4 * 3600000 + 120000);
        break;
    case LinkScenario::NIGHT:
        module.runUntilMs(400000);
        break;
    }
    assertTheRunIsSound(module);

    printf("  s19: %s:\n", scenarioName(scenario));
    module.printDecisions();
    return module.decisions;
}

/** @brief One decision of the reference: its moment and what it was. */
struct Expected {
    uint64_t atMs;
    const char* what;
};

void assertTheDecisionsAre(LinkScenario scenario, const Expected* reference, size_t count)
{
    const std::vector<Decision> seen = runTheLinkScenario(scenario);
    char message[240];
    snprintf(message, sizeof(message), "%s: %u decision(s) where the reference has %u", scenarioName(scenario),
             (unsigned)seen.size(), (unsigned)count);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)count, (uint32_t)seen.size(), message);
    for (size_t i = 0; i < count; i++)
    {
        snprintf(message, sizeof(message), "%s: decision %u was %s at %lu ms, the reference has %s at %lu ms",
                 scenarioName(scenario), (unsigned)i, seen[i].what.c_str(), (unsigned long)seen[i].atMs,
                 reference[i].what, (unsigned long)reference[i].atMs);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(reference[i].what, seen[i].what.c_str(), message);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(reference[i].atMs, seen[i].atMs, message);
    }
}

// The reference: every decision of these runs, the night restart among them, on the 30 s tick from
// millis 0 - the tick the supervision runs on, which no boot event moves. The first is the boot's
// association, asked for on the loop's first iteration - setup takes the 100 ms of its stored port's
// power on the sensor module here and nothing on the relay module - and on the wire on the network
// task's next poll; the catalog follows the boot's structure, which waits one connect act of the
// broker (200 ms) behind the association. A lost association is
// answered on the tick that sees it (the restart is on the wire 50 ms later, the network task's poll)
// and then after 1, 2, 5 and every 10 minutes, each pause counted from the end of the restart before
// - instant here - and taken on the tick after it: 90 s, 150 s, 330 s, then 630 s apart. A broker or
// a server that fails behind a router that answers restarts nothing, and no outage restarts the module.
const Expected REFERENCE_WIFI_25_MIN[] = {
    {110, "BOOT_ASSOCIATION"},
    {12500, "CATALOG_ASKED"},
    {120050, "RADIO_RESTARTED"},
    {210050, "RADIO_RESTARTED"},
    {360050, "RADIO_RESTARTED"},
    {690050, "RADIO_RESTARTED"},
    {1320050, "RADIO_RESTARTED"},
    {1620000, "SNTP_FORCED"},
    {1620010, "CATALOG_ASKED"}};
const Expected REFERENCE_HTTP_15_MIN[] = {
    {10, "BOOT_ASSOCIATION"},
    {1020000, "SNTP_FORCED"},
    {1020010, "RULES_FORCED_ON_RESTORE"}};
const Expected REFERENCE_OFFLINE_1_H[] = {
    {110, "BOOT_ASSOCIATION"},
    {12500, "CATALOG_ASKED"},
    {120050, "RADIO_RESTARTED"},
    {210050, "RADIO_RESTARTED"},
    {360050, "RADIO_RESTARTED"},
    {690050, "RADIO_RESTARTED"},
    {1320050, "RADIO_RESTARTED"},
    {1950050, "RADIO_RESTARTED"},
    {2580050, "RADIO_RESTARTED"},
    {3210050, "RADIO_RESTARTED"}};
const Expected REFERENCE_OFFLINE_4_H[] = {
    {10, "BOOT_ASSOCIATION"},
    {120050, "RADIO_RESTARTED"},
    {210050, "RADIO_RESTARTED"},
    {360050, "RADIO_RESTARTED"},
    {690050, "RADIO_RESTARTED"},
    {1320050, "RADIO_RESTARTED"},
    {1950050, "RADIO_RESTARTED"},
    {2580050, "RADIO_RESTARTED"},
    {3210050, "RADIO_RESTARTED"},
    {3840050, "RADIO_RESTARTED"},
    {4470050, "RADIO_RESTARTED"},
    {5100050, "RADIO_RESTARTED"},
    {5730050, "RADIO_RESTARTED"},
    {6360050, "RADIO_RESTARTED"},
    {6990050, "RADIO_RESTARTED"},
    {7620050, "RADIO_RESTARTED"},
    {8250050, "RADIO_RESTARTED"},
    {8880050, "RADIO_RESTARTED"},
    {9510050, "RADIO_RESTARTED"},
    {10140050, "RADIO_RESTARTED"},
    {10770050, "RADIO_RESTARTED"},
    {11400050, "RADIO_RESTARTED"},
    {12030050, "RADIO_RESTARTED"},
    {12660050, "RADIO_RESTARTED"},
    {13290050, "RADIO_RESTARTED"},
    {13920050, "RADIO_RESTARTED"},
    {14550050, "RADIO_RESTARTED"}};
const Expected REFERENCE_NIGHT[] = {
    {10, "BOOT_ASSOCIATION"},
    {120000, "NEED_RESTART"}};

} // namespace

void test_s19_the_link_decisions_keep_their_moments(void)
{
    assertTheDecisionsAre(LinkScenario::WIFI_25_MIN, REFERENCE_WIFI_25_MIN,
                          sizeof(REFERENCE_WIFI_25_MIN) / sizeof(REFERENCE_WIFI_25_MIN[0]));
    const Expected bootOnly[] = {{10, "BOOT_ASSOCIATION"}};
    assertTheDecisionsAre(LinkScenario::MQTT_15_MIN, bootOnly, sizeof(bootOnly) / sizeof(bootOnly[0]));
    assertTheDecisionsAre(LinkScenario::HTTP_15_MIN, REFERENCE_HTTP_15_MIN,
                          sizeof(REFERENCE_HTTP_15_MIN) / sizeof(REFERENCE_HTTP_15_MIN[0]));
    assertTheDecisionsAre(LinkScenario::OFFLINE_1_H, REFERENCE_OFFLINE_1_H,
                          sizeof(REFERENCE_OFFLINE_1_H) / sizeof(REFERENCE_OFFLINE_1_H[0]));
    assertTheDecisionsAre(LinkScenario::OFFLINE_4_H, REFERENCE_OFFLINE_4_H,
                          sizeof(REFERENCE_OFFLINE_4_H) / sizeof(REFERENCE_OFFLINE_4_H[0]));
    assertTheDecisionsAre(LinkScenario::NIGHT, REFERENCE_NIGHT, sizeof(REFERENCE_NIGHT) / sizeof(REFERENCE_NIGHT[0]));
}

// The decisions need no diagnostic: with none wired, the link going and staying gone is answered on
// the same tick - the radio restarted - and an hour of it does not restart the module.
void test_s19_the_link_is_supervised_without_a_diagnostic(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    ModuleUnderTest module(boot);
    module.loopService.setDiagnosticService(nullptr);

    module.setupService.setupModule();
    module.startTheLoop();
    module.runUntilMs(100000);
    module.dropTheLink();
    module.runLongUntilMs(100000 + 3600000 + 120000);
    assertTheRunIsSound(module);
    module.printDecisions();

    uint64_t restartedAt = 0;
    uint64_t needRestartAt = 0;
    for (size_t i = 0; i < module.decisions.size(); i++)
    {
        if (restartedAt == 0 && module.decisions[i].what == "RADIO_RESTARTED")
        {
            restartedAt = module.decisions[i].atMs;
        }
        if (needRestartAt == 0 && module.decisions[i].what == "NEED_RESTART")
        {
            needRestartAt = module.decisions[i].atMs;
        }
    }
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(120050, restartedAt,
                                     "with no diagnostic wired the lost link was not answered on its tick");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, needRestartAt, "an hour offline restarted the module");
}

// The decisions keep the tick they have always had - every thirty seconds from millis 0 - also on a
// boot whose first diagnostic went late, at the first STEADY, and started its own interval there.
void test_s19_the_link_decisions_keep_their_tick_when_the_first_diagnostic_goes_late(void)
{
    Boot boot;
    boot.storedOn.push_back(0);
    boot.structureAnswerMs = 40000; // STEADY at about 52.5 s: the 30 s diagnostic is owed until then
    ModuleUnderTest module(boot);

    module.setupService.setupModule();
    module.startTheLoop();
    module.runUntilMs(100000);
    module.dropTheLink();
    module.runUntilMs(130000);
    assertTheRunIsSound(module);
    module.printDecisions();
    TEST_ASSERT_TRUE_MESSAGE(module.tapeAtMs("BOOT_SEQ: diagnostic owed since") > 0,
                             "the stand did not make the first diagnostic go late");

    uint64_t restartedAt = 0;
    for (size_t i = 0; i < module.decisions.size(); i++)
    {
        if (restartedAt == 0 && module.decisions[i].what == "RADIO_RESTARTED")
        {
            restartedAt = module.decisions[i].atMs;
        }
    }
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(120050, restartedAt,
                                     "the lost link was answered off the tick of thirty seconds from millis 0");
}

// --------------------------------------------------------------------------------------------------
// 16. When the services fail, the module finds out which part of the path failed before it restarts
//     anything: the radio only for a router out of reach, the module only for a fault of its own that
//     held - never for time offline - and the outage is reported once the module is back.
//
//     The world past the radio is SimLinkProbe: a router that answers ARP or not, an internet that
//     echoes or not. The broker and the server fail as in section 15 - the broker's socket gone, the
//     REST client counting failed requests.
// --------------------------------------------------------------------------------------------------

namespace {

const uint64_t FOUR_HOURS_MS = 4ull * 3600ull * 1000ull;

/** @brief A sensor module with a stored sensor, booted and running for 100 s with everything up. */
struct TroubleRun {
    Boot boot;
    ModuleUnderTest* module;

    TroubleRun()
    {
        boot.storedOn.push_back(0);
        module = new ModuleUnderTest(boot);
        module->setupService.setupModule();
        module->startTheLoop();
        module->runUntilMs(100000);
    }
    ~TroubleRun() { delete module; }

    /** @brief The broker's socket gone and the REST client past its count of failed requests. */
    void theServicesFail()
    {
        module->mqttManager.connected = false;
        module->restApi.countOffline = 10;
    }

    void theServicesComeBack()
    {
        module->mqttManager.connected = true;
        module->restApi.countOffline = 0;
    }

    std::vector<uint64_t> momentsOf(const char* what, uint64_t fromMs) const
    {
        std::vector<uint64_t> moments;
        for (size_t i = 0; i < module->decisions.size(); i++)
        {
            if (module->decisions[i].what == what && module->decisions[i].atMs >= fromMs)
            {
                moments.push_back(module->decisions[i].atMs);
            }
        }
        return moments;
    }

    /** @brief The first diagnostic the server received at or after this moment, as JSON; empty if none. */
    std::string firstDiagnosticFrom(uint64_t fromMs) const
    {
        const SimServer::Call* call = module->server.firstActFrom(OutboxKind::DIAGNOSTIC, fromMs);
        return call != nullptr ? call->diagnosticJson : std::string();
    }
};

/** @brief The pauses between restarts the algorithm asks for, from the end of one to the next. */
const uint64_t RESTART_PAUSES_MS[] = {60000, 120000, 300000, 600000, 600000, 600000};

/**
 * @brief Each restart follows the one before it after its pause, and not later than the supervision
 *        tick after the pause plus `extraMs` - the probe, where one is taken before the restart.
 */
void assertTheRestartsFollowThePauses(const std::vector<uint64_t>& restarts, uint64_t extraMs)
{
    for (size_t i = 0; i + 1 < restarts.size() && i < sizeof(RESTART_PAUSES_MS) / sizeof(RESTART_PAUSES_MS[0]); i++)
    {
        const uint64_t gap = restarts[i + 1] - restarts[i];
        char message[160];
        snprintf(message, sizeof(message), "restart %u came %lu ms after restart %u; its pause is %lu ms",
                 (unsigned)(i + 2), (unsigned long)gap, (unsigned)(i + 1), (unsigned long)RESTART_PAUSES_MS[i]);
        TEST_ASSERT_TRUE_MESSAGE(gap >= RESTART_PAUSES_MS[i], message);
        TEST_ASSERT_TRUE_MESSAGE(gap < RESTART_PAUSES_MS[i] + LinkSupervisor::TICK_INTERVAL_MS + extraMs, message);
    }
}

/** @brief The last outage the report carries: its cause, start and length; false when it carries none. */
bool lastOutageOf(const std::string& diagnosticJson, std::string& cause, uint64_t& startMs, uint64_t& durationMs)
{
    DynamicJsonDocument doc(4096);
    if (diagnosticJson.empty() || deserializeJson(doc, diagnosticJson) || !doc.containsKey("lastOutage"))
    {
        return false;
    }
    cause = doc["lastOutage"]["cause"].as<std::string>();
    startMs = doc["lastOutage"]["startUptimeMs"].as<uint64_t>();
    durationMs = doc["lastOutage"]["durationMs"].as<uint64_t>();
    return true;
}

} // namespace

/**
 * The broker and the server go, the router and the internet stay: whatever is wrong is past both, and
 * neither a radio restart nor a module restart repairs it. Four hours of it restart nothing; the
 * gateway is asked, answers, and is asked again every five minutes.
 */
void test_s19_a_server_down_behind_a_working_router_restarts_neither_the_radio_nor_the_module(void)
{
    TroubleRun run;
    run.theServicesFail();
    run.module->runLongUntilMs(100000 + FOUR_HOURS_MS + 120000);
    assertTheRunIsSound(*run.module);
    run.module->printDecisions();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)run.momentsOf("RADIO_RESTARTED", 100000).size(),
                                     "the radio was restarted for a server past a working router");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)run.momentsOf("NEED_RESTART", 100000).size(),
                                     "the module was restarted for a server past a working router");
    TEST_ASSERT_TRUE_MESSAGE(run.module->tapeAtMs("LINK_CHECK: probe gateway=answered public=answered") > 0,
                             "the module never looked at its link");
    printf("  s19: the gateway was asked %d time(s)\n", run.module->linkProbe.gatewayAsks);
    // Once a minute after the failure, then every five minutes for four hours: 48 more.
    TEST_ASSERT_TRUE_MESSAGE(run.module->linkProbe.gatewayAsks >= 48, "the gateway was not asked every five minutes");
    TEST_ASSERT_TRUE_MESSAGE(run.module->linkProbe.gatewayAsks <= 50, "the gateway was asked more often than every five minutes");
}

/**
 * The station stays associated but its router does not answer: two silent checks thirty seconds
 * apart, and the radio is restarted - then again after 1, 2, 5 and 10 minutes, each time after
 * the gateway was asked once more and stayed silent. A restart asked for with the station associated
 * is one that takes the association down: it is run.
 */
void test_s19_a_router_out_of_reach_is_answered_with_radio_restarts_after_a_growing_pause(void)
{
    TroubleRun run;
    run.module->linkProbe.routerAnswers = false;
    run.theServicesFail();
    run.module->runLongUntilMs(100000 + 40ull * 60ull * 1000ull);
    assertTheRunIsSound(*run.module);
    run.module->printDecisions();

    const std::vector<uint64_t> restarts = run.momentsOf("RADIO_RESTARTED", 100000);
    TEST_ASSERT_TRUE_MESSAGE(restarts.size() >= 5, "the radio was not restarted on the growing pause");
    // The failure is seen on the tick at 120 s; a minute later the gateway is asked, thirty seconds
    // after that once more - each check 9 s of silence and echoes - and only then is the radio
    // restarted.
    TEST_ASSERT_TRUE_MESSAGE(restarts[0] >= 120000 + 60000 + 30000 + 9000,
                             "the radio was restarted before the gateway was found silent twice");
    TEST_ASSERT_TRUE_MESSAGE(restarts[0] < 120000 + 60000 + 30000 + 9000 + 1000,
                             "the second silent check did not restart the radio"); 
    assertTheRestartsFollowThePauses(restarts, 10000);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)restarts.size() + 1, (uint32_t)run.module->linkProbe.gatewayAsks,
                                     "a restart was made without the gateway asked first");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)run.momentsOf("NEED_RESTART", 100000).size());
}

/**
 * The router answers, the internet does not: the radio is left alone for the whole hour - it cannot
 * repair what is past the router - and the module is not restarted. Back, the module reports the
 * outage as the internet's, from the tick that saw it to the tick that saw it end.
 */
void test_s19_an_internet_down_behind_a_working_router_restarts_nothing_and_is_reported(void)
{
    TroubleRun run;
    run.module->linkProbe.internetAnswers = false;
    run.theServicesFail();
    run.module->runLongUntilMs(100000 + 3600000 + 120000);
    assertTheRunIsSound(*run.module);
    run.module->printDecisions();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)run.momentsOf("RADIO_RESTARTED", 100000).size(),
                                     "the radio was restarted for an internet past a working router");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)run.momentsOf("NEED_RESTART", 100000).size(),
                                     "the module was restarted for an internet past a working router");
    TEST_ASSERT_TRUE(run.module->tapeAtMs("LINK_CHECK: probe gateway=answered public=silent") > 0);
    printf("  s19: the gateway was asked %d time(s)\n", run.module->linkProbe.gatewayAsks);

    const uint64_t backAt = run.module->clock.nowMs();
    run.theServicesComeBack();
    run.module->runUntilMs(backAt + 120000);
    std::string cause;
    uint64_t startMs = 0;
    uint64_t durationMs = 0;
    TEST_ASSERT_TRUE_MESSAGE(lastOutageOf(run.firstDiagnosticFrom(backAt + LinkSupervisor::TICK_INTERVAL_MS), cause,
                                          startMs, durationMs),
                             "the first diagnostic after the outage did not carry it");
    TEST_ASSERT_EQUAL_STRING("internet", cause.c_str());
    TEST_ASSERT_EQUAL_UINT64(120000, startMs);
    TEST_ASSERT_TRUE_MESSAGE(durationMs >= backAt - 120000 && durationMs < backAt - 120000 + LinkSupervisor::TICK_INTERVAL_MS,
                             "the outage did not last from the tick that saw it to the tick that saw it end");
}

/**
 * The server goes and every handshake fails for memory: a fault of the module itself, which only a
 * restart of the module clears. Held for 720.5 s with the link in trouble, it restarts the module -
 * as a technical boot that skips the discovery - on the first tick after the hold; the radio is not
 * restarted for it. Without the fault the same outage restarts nothing (the first test above).
 */
void test_s19_tls_short_of_memory_that_holds_restarts_the_module(void)
{
    TroubleRun run;
    run.theServicesFail();
    for (uint64_t t = 100000; t <= 100000 + 30ull * 60ull * 1000ull; t += 20000)
    {
        run.module->runUntilMs(t);
        run.module->statusBoard.noteTlsConnect((uint32_t)t, ModuleStatusBoard::TlsConnect::OUT_OF_MEMORY);
    }
    assertTheRunIsSound(*run.module);
    run.module->printDecisions();

    // The fault and the failure are both seen on the tick at 120 s; the hold of 720.5 s ends at
    // 840.5 s, and the next tick is at 870 s.
    const std::vector<uint64_t> restarts = run.momentsOf("NEED_RESTART", 100000);
    TEST_ASSERT_TRUE_MESSAGE(restarts.size() >= 1, "a fault of the module that held did not restart it");
    TEST_ASSERT_EQUAL_UINT64(870000, restarts[0]);
    TEST_ASSERT_EQUAL_UINT8(1, run.module->settings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_TECHNICAL, run.module->settings.getBootType());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)run.momentsOf("RADIO_RESTARTED", 100000).size(),
                                     "the radio was restarted for a fault of the module");
}

/**
 * The server goes and every connect finds the module's own network stack refusing it a socket, three
 * in a row and more: a fault of the module. Held for 720.5 s with the link in trouble, it restarts the
 * module on the first tick after the hold, as a technical boot that skips the discovery; the radio is
 * not restarted for it.
 */
void test_s19_sockets_refused_that_hold_restart_the_module(void)
{
    TroubleRun run;
    run.theServicesFail();
    for (uint64_t t = 100000; t <= 100000 + 30ull * 60ull * 1000ull; t += 20000)
    {
        run.module->runUntilMs(t);
        for (int i = 0; i < 3; i++)
        {
            run.module->statusBoard.noteSocketOpen((uint32_t)t, ModuleStatusBoard::SocketOpen::REFUSED);
        }
    }
    assertTheRunIsSound(*run.module);
    run.module->printDecisions();

    // The refusals and the failure are both seen on the tick at 120 s; the hold of 720.5 s ends at
    // 840.5 s, and the next tick is at 870 s.
    const std::vector<uint64_t> restarts = run.momentsOf("NEED_RESTART", 100000);
    TEST_ASSERT_TRUE_MESSAGE(restarts.size() >= 1, "sockets refused for the whole hold did not restart the module");
    TEST_ASSERT_EQUAL_UINT64(870000, restarts[0]);
    TEST_ASSERT_EQUAL_UINT8(1, run.module->settings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_TECHNICAL, run.module->settings.getBootType());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)run.momentsOf("RADIO_RESTARTED", 100000).size(),
                                     "the radio was restarted for a fault of the module");
}

namespace {

/** @brief The server down, or back: every call the module makes to it refused, and the broker gone. */
void theServerIsDown(ModuleUnderTest& module, bool down)
{
    for (int i = 0; i < SimServer::KIND_SLOTS; i++)
    {
        module.server.refuse[i] = down;
    }
    module.mqttManager.connected = !down;
    module.restApi.countOffline = down ? 10 : 0;
}

} // namespace

/**
 * A module the server has never seen boots into a server that is down - every call refused, the broker
 * gone - for half an hour, and then the server is back. The structure that registers it goes
 * undelivered and is offered again after 1, 2 and 5 minutes and then every 10, and the module is never
 * restarted for it. With the server back the next offer is delivered, the module is registered, and its
 * first synchronization goes through: the catalog for a sensor module, the rule list for a relay
 * module - each asks only for its own.
 */
static void runAnUnregisteredModuleThroughAServerDown(bool deviceModule)
{
    Boot boot;
    boot.deviceModule = deviceModule;
    if (!deviceModule)
    {
        boot.storedOn.push_back(0);
    }
    boot.registered = false;
    ModuleUnderTest module(boot);
    theServerIsDown(module, true);
    module.setupService.setupModule();
    module.startTheLoop();

    const uint64_t backAt = 30ull * 60ull * 1000ull;
    module.runLongUntilMs(backAt);
    const uint32_t offersWhileDown = module.onChangeLocalIp.submitCount();
    theServerIsDown(module, false);
    module.runLongUntilMs(backAt + 15ull * 60ull * 1000ull);
    assertTheRunIsSound(module);
    printf("  s19: an unregistered %s module:\n", deviceModule ? "relay" : "sensor");
    module.printDecisions();

    uint32_t restarts = 0;
    for (size_t i = 0; i < module.decisions.size(); i++)
    {
        restarts += module.decisions[i].what == "NEED_RESTART" ? 1u : 0u;
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, restarts, "the module was restarted for a structure the server did not take");

    // At boot, then after 1, 2, 5 and 10 minutes - each pause counted from the end of an offer of one
    // job's length - is five offers by 30 minutes, and a sixth at its edge. Offered with no pause, one
    // job after another, it would be over a hundred.
    printf("  s19: the structure was offered %u time(s) while the server was down\n", (unsigned)offersWhileDown);
    TEST_ASSERT_TRUE_MESSAGE(offersWhileDown >= 4, "the structure was not offered again while the server was down");
    TEST_ASSERT_TRUE_MESSAGE(offersWhileDown <= 7, "the structure was offered again before its pause had run");

    const SimServer::Call* delivered = nullptr;
    for (size_t i = 0; i < module.server.calls.size() && delivered == nullptr; i++)
    {
        const SimServer::Call& call = module.server.calls[i];
        if (call.task == "outbox-net" && call.kind == OutboxKind::MODULE_STRUCTURE && call.answered)
        {
            delivered = &call;
        }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(delivered, "the structure was never delivered once the server was back");
    TEST_ASSERT_TRUE(delivered->startedAtMs >= backAt);
    TEST_ASSERT_TRUE_MESSAGE(delivered->startedAtMs < backAt + 600000 + 120000,
                             "the structure was not offered within its pause of the server's return");
    printf("  s19: the structure was delivered at %lu ms\n", (unsigned long)delivered->endedAtMs);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.settings.getWasInitialSyncCompleted(),
                                    "the delivered structure did not register the module");

    const OutboxKind firstSync = deviceModule ? OutboxKind::RULE_LIST : OutboxKind::SENSOR_TYPES_FETCH;
    const SimServer::Call* sync = module.server.firstActFrom(firstSync, delivered->endedAtMs);
    TEST_ASSERT_NOT_NULL_MESSAGE(sync, deviceModule ? "the rule list was not asked for once the module was registered"
                                                    : "the catalog was not asked for once the module was registered");
    TEST_ASSERT_TRUE(sync->answered);
    printf("  s19: the first synchronization went at %lu ms\n", (unsigned long)sync->startedAtMs);
}

void test_s19_an_unregistered_sensor_module_waits_out_a_server_down_without_a_restart_and_registers_once_it_is_back(void)
{
    runAnUnregisteredModuleThroughAServerDown(false);
}

void test_s19_an_unregistered_relay_module_waits_out_a_server_down_without_a_restart_and_registers_once_it_is_back(void)
{
    runAnUnregisteredModuleThroughAServerDown(true);
}

/**
 * A new sensor module, as it comes out of the box: nothing stored, no catalog on flash, never
 * registered, no button held at power-on. It boots into a server that is down for half an hour - setup
 * asks the server for nothing, so the structure it builds holds placeholders - and then the server
 * works. No restart in the whole run; the structure is delivered and the module registered, the
 * catalog arrives, the discovery it arms runs, and no structure the server takes once the catalog is
 * there carries a placeholder - the last one it takes least of all.
 */
void test_s19_a_new_module_waits_out_a_server_down_and_registers_with_its_catalog(void)
{
    Boot boot;
    boot.catalogOnFlash = false;
    boot.registered = false;
    ModuleUnderTest module(boot);
    theServerIsDown(module, true);
    module.setupService.setupModule();
    module.startTheLoop();

    const uint64_t backAt = 30ull * 60ull * 1000ull;
    module.runLongUntilMs(backAt);
    theServerIsDown(module, false);
    module.runLongUntilMs(backAt + 25ull * 60ull * 1000ull);
    assertTheRunIsSound(module);
    module.printDecisions();

    uint32_t restarts = 0;
    for (size_t i = 0; i < module.decisions.size(); i++)
    {
        restarts += module.decisions[i].what == "NEED_RESTART" ? 1u : 0u;
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, restarts, "a new module was restarted for a server that was down");

    const SimServer::Call* catalog = nullptr;
    uint32_t structuresTaken = 0;
    uint32_t placeholdersTaken = 0;
    uint32_t placeholdersInTheLast = 0;
    for (size_t i = 0; i < module.server.calls.size(); i++)
    {
        const SimServer::Call& call = module.server.calls[i];
        if (call.task != "outbox-net" || !call.answered)
        {
            continue;
        }
        if (call.kind == OutboxKind::SENSOR_TYPES_FETCH && catalog == nullptr)
        {
            catalog = &call;
        }
        if (call.kind == OutboxKind::MODULE_STRUCTURE)
        {
            structuresTaken++;
            uint32_t placeholders = 0;
            for (size_t k = 0; k < call.structureSensors.size(); k++)
            {
                placeholders += call.structureSensors[k].type == "none" ? 1u : 0u;
            }
            if (catalog != nullptr && call.startedAtMs >= catalog->endedAtMs)
            {
                placeholdersTaken += placeholders;
            }
            placeholdersInTheLast = placeholders;
            printf("  s19: structure taken %lu -> %lu ms, %u record(s), %u placeholder(s)\n",
                   (unsigned long)call.startedAtMs, (unsigned long)call.endedAtMs,
                   (unsigned)call.structureSensors.size(), (unsigned)placeholders);
        }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(catalog, "the catalog never arrived once the server was back");
    printf("  s19: the catalog arrived at %lu ms\n", (unsigned long)catalog->endedAtMs);
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.armCount() >= 1, "the catalog that arrived armed no discovery");
    TEST_ASSERT_TRUE_MESSAGE(structuresTaken >= 1, "no structure was delivered once the server was back");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, module.settings.getWasInitialSyncCompleted(),
                                    "the new module was not registered once the server was back");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, placeholdersTaken,
                                     "the server was given placeholder records after the catalog had arrived");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, placeholdersInTheLast, "the last structure the server took held placeholders");
}

/**
 * The access point goes for good: the radio is restarted at once and then after 1, 2, 5 and every 10
 * minutes - and the module is never restarted for it, in four hours. When the access point is back
 * the module reports the outage as the router's.
 */
void test_s19_an_access_point_gone_is_answered_with_radio_restarts_on_a_growing_pause_and_no_module_restart(void)
{
    TroubleRun run;
    run.module->dropTheLink();
    run.module->runLongUntilMs(100000 + FOUR_HOURS_MS + 120000);
    assertTheRunIsSound(*run.module);
    run.module->printDecisions();

    const std::vector<uint64_t> restarts = run.momentsOf("RADIO_RESTARTED", 100000);
    TEST_ASSERT_TRUE(restarts.size() >= 6);
    TEST_ASSERT_TRUE_MESSAGE(restarts[0] >= 120000 && restarts[0] < 121000,
                             "the lost link was not answered on the tick that saw it");
    assertTheRestartsFollowThePauses(restarts, 1000);
    // Four hours at one restart per 10.5 min at most, after the first few: no more than 26.
    TEST_ASSERT_TRUE(restarts.size() <= 26);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)run.momentsOf("NEED_RESTART", 100000).size(),
                                     "the module was restarted for being offline");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, run.module->linkProbe.gatewayAsks, "a probe was asked of a station with no association");

    const uint64_t backAt = run.module->clock.nowMs();
    run.module->raiseTheLink();
    run.module->runUntilMs(backAt + 120000);
    std::string cause;
    uint64_t startMs = 0;
    uint64_t durationMs = 0;
    TEST_ASSERT_TRUE_MESSAGE(lastOutageOf(run.firstDiagnosticFrom(backAt + LinkSupervisor::TICK_INTERVAL_MS), cause,
                                          startMs, durationMs),
                             "the first diagnostic after the outage did not carry it");
    TEST_ASSERT_EQUAL_STRING("router", cause.c_str());
    TEST_ASSERT_EQUAL_UINT64(120000, startMs);
    TEST_ASSERT_TRUE(durationMs >= backAt - 120000 && durationMs < backAt - 120000 + LinkSupervisor::TICK_INTERVAL_MS);
}

/**
 * Five minutes without the access point, and back: the first diagnostic after the module is back
 * carries the outage - the router's, from the tick that saw it go to the tick that saw it come back.
 */
void test_s19_the_first_diagnostic_after_an_outage_carries_its_cause_start_and_length(void)
{
    TroubleRun run;
    run.module->dropTheLink();
    run.module->runUntilMs(400000);
    run.module->raiseTheLink();
    run.module->runUntilMs(520000);
    assertTheRunIsSound(*run.module);

    std::string cause;
    uint64_t startMs = 0;
    uint64_t durationMs = 0;
    TEST_ASSERT_TRUE_MESSAGE(lastOutageOf(run.firstDiagnosticFrom(420000), cause, startMs, durationMs),
                             "the first diagnostic after the outage did not carry it");
    TEST_ASSERT_EQUAL_STRING("router", cause.c_str());
    TEST_ASSERT_EQUAL_UINT64(120000, startMs);
    TEST_ASSERT_EQUAL_UINT64(300000, durationMs);
}

/** A report from a module that has had no outage since its boot says nothing about one. */
void test_s19_a_module_without_an_outage_reports_none(void)
{
    TroubleRun run;
    run.module->runUntilMs(200000);
    std::string cause;
    uint64_t startMs = 0;
    uint64_t durationMs = 0;
    const std::string json = run.firstDiagnosticFrom(100000);
    TEST_ASSERT_FALSE_MESSAGE(json.empty(), "no diagnostic was sent");
    TEST_ASSERT_FALSE(lastOutageOf(json, cause, startMs, durationMs));
}

extern "C" void run_s19_boot_sequence_tests(void)
{
    printf("\n----- Running S19 Boot Sequence Tests -----\n");

    RUN_TEST(test_s19_the_structure_is_the_first_network_act_of_the_boot);
    RUN_TEST(test_s19_setup_waits_for_no_network);
    RUN_TEST(test_s19_the_broker_connects_after_the_association_and_before_the_structure);
    RUN_TEST(test_s19_the_udp_listeners_start_once_on_the_first_link);
    RUN_TEST(test_s19_the_first_telemetry_follows_the_structure_and_every_first_reading);
    RUN_TEST(test_s19_the_boot_sequence_is_on_the_tape_in_order);
    RUN_TEST(test_s19_the_catalog_is_asked_for_once_the_structure_has_settled);
    RUN_TEST(test_s19_a_module_with_relays_puts_its_structure_first);
    RUN_TEST(test_s19_a_module_with_relays_asks_for_its_rules_once_after_the_structure);
    RUN_TEST(test_s19_the_module_command_the_broker_hands_over_at_connect_follows_the_structure);
    RUN_TEST(test_s19_an_update_on_the_boots_list_downloads_after_the_structure_while_the_loop_works);
    RUN_TEST(test_s19_a_kept_update_waits_in_its_file_for_the_link);
    RUN_TEST(test_s19_a_kept_update_the_server_still_lists_as_new_is_carried_out_once);
    RUN_TEST(test_s19_a_module_deleted_on_the_server_is_reset_after_the_first_steady);
    RUN_TEST(test_s19_a_module_the_server_has_not_deleted_goes_on_without_a_word);
    RUN_TEST(test_s19_a_module_not_registered_when_the_boot_began_is_not_asked_about_its_deletion);
    RUN_TEST(test_s19_a_delete_gd_before_the_boots_deletion_check_ends_in_one_reset);
    RUN_TEST(test_s19_a_delete_gd_after_the_boots_deletion_check_ends_in_one_reset);
    RUN_TEST(test_s19_a_delete_gd_and_the_boots_deletion_check_in_one_iteration_end_in_one_reset);
    RUN_TEST(test_s19_the_first_telemetry_waits_for_the_clock_sntp_sets);
    RUN_TEST(test_s19_without_sntp_the_first_telemetry_goes_with_the_modules_clock_after_ten_minutes);
    RUN_TEST(test_s19_the_discovery_starts_once_the_boot_is_over);
    RUN_TEST(test_s19_the_stored_sensor_is_read_before_the_discovery_starts);
    RUN_TEST(test_s19_without_a_link_the_discovery_runs_on_the_catalog_on_flash);
    RUN_TEST(test_s19_a_catalog_that_arrives_after_the_boot_arms_the_discovery);
    RUN_TEST(test_s19_a_fresh_module_starts_its_discovery_once_the_boot_is_over);
    RUN_TEST(test_s19_an_erased_module_arms_its_discovery_when_the_catalog_arrives);
    RUN_TEST(test_s19_a_new_module_restarted_to_skip_the_discovery_finds_its_sensor_when_the_catalog_arrives);
    RUN_TEST(test_s19_a_new_module_powered_on_with_the_button_held_finds_its_sensor_when_the_catalog_arrives);
    RUN_TEST(test_s19_a_late_catalog_sends_the_structure_a_boot_of_placeholders_owes);
    RUN_TEST(test_s19_a_late_catalog_that_changes_the_stored_sensor_sends_one_structure);
    RUN_TEST(test_s19_a_module_with_a_catalog_and_no_sensor_finds_its_sensor_on_a_boot_that_skips_the_discovery);
    RUN_TEST(test_s19_a_module_reset_and_configured_on_the_web_finds_its_sensor_on_the_first_boot);
    RUN_TEST(test_s19_on_a_silent_server_the_discovery_starts_at_the_end_of_its_window);
    RUN_TEST(test_s19_without_a_link_the_restarts_find_is_reported_after_the_boots_structure);
    RUN_TEST(test_s19_without_a_link_a_later_passs_find_is_reported_after_the_boots_structure);
    RUN_TEST(test_s19_the_first_diagnostic_goes_at_the_first_steady_of_a_slow_boot);
    RUN_TEST(test_s19_a_first_steady_on_a_diagnostic_tick_sends_the_owed_diagnostic_once);
    RUN_TEST(test_s19_on_an_ordinary_boot_the_first_diagnostic_goes_at_30_s);
    RUN_TEST(test_s19_without_a_link_the_boot_asks_for_the_association_on_its_first_iteration);
    RUN_TEST(test_s19_without_a_link_nothing_goes_before_the_structure_once_the_link_comes);
    RUN_TEST(test_s19_without_a_link_a_refused_network_task_is_created_on_the_next_supervision_tick);
    RUN_TEST(test_s19_a_boot_association_waiting_for_a_link_that_is_back_is_not_run);
    RUN_TEST(test_s19_the_remote_sensor_subscriptions_are_reconciled_on_the_first_connack);
    RUN_TEST(test_s19_after_a_flap_to_a_new_address_the_telemetry_follows_the_new_structure);
    RUN_TEST(test_s19_flaps_without_a_new_address_send_what_they_sent_before);
    RUN_TEST(test_s19_a_structure_whose_completion_is_lost_is_given_up_on);
    RUN_TEST(test_s19_a_structure_that_takes_its_longest_attempt_is_not_given_up_on);
    RUN_TEST(test_s19_a_structure_held_past_its_ceiling_keeps_its_place_on_the_wire);
    RUN_TEST(test_s19_a_catalog_whose_completion_is_lost_is_given_up_on);
    RUN_TEST(test_s19_first_readings_that_never_come_are_given_up_on);
    RUN_TEST(test_s19_a_sensor_deleted_before_its_first_outcome_is_not_waited_for);
    RUN_TEST(test_s19_the_only_sensor_deleted_before_its_first_outcome_is_not_waited_for);
    RUN_TEST(test_s19_a_reading_dropped_for_its_old_model_is_not_the_first_outcome);
    RUN_TEST(test_s19_a_reading_dropped_for_its_old_registers_is_not_the_first_outcome);
    RUN_TEST(test_s19_the_link_decisions_keep_their_moments);
    RUN_TEST(test_s19_the_link_is_supervised_without_a_diagnostic);
    RUN_TEST(test_s19_the_link_decisions_keep_their_tick_when_the_first_diagnostic_goes_late);
    RUN_TEST(test_s19_a_server_down_behind_a_working_router_restarts_neither_the_radio_nor_the_module);
    RUN_TEST(test_s19_a_router_out_of_reach_is_answered_with_radio_restarts_after_a_growing_pause);
    RUN_TEST(test_s19_an_internet_down_behind_a_working_router_restarts_nothing_and_is_reported);
    RUN_TEST(test_s19_tls_short_of_memory_that_holds_restarts_the_module);
    RUN_TEST(test_s19_sockets_refused_that_hold_restart_the_module);
    RUN_TEST(test_s19_an_unregistered_sensor_module_waits_out_a_server_down_without_a_restart_and_registers_once_it_is_back);
    RUN_TEST(test_s19_an_unregistered_relay_module_waits_out_a_server_down_without_a_restart_and_registers_once_it_is_back);
    RUN_TEST(test_s19_a_new_module_waits_out_a_server_down_and_registers_with_its_catalog);
    RUN_TEST(test_s19_an_access_point_gone_is_answered_with_radio_restarts_on_a_growing_pause_and_no_module_restart);
    RUN_TEST(test_s19_the_first_diagnostic_after_an_outage_carries_its_cause_start_and_length);
    RUN_TEST(test_s19_a_module_without_an_outage_reports_none);

    printf("\n----- S19 Boot Sequence Tests Complete -----\n");
}
