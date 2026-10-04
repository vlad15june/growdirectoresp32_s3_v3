#pragma once

#include "Application/Interfaces/ICommandService.h"
#include "Application/Interfaces/INetworkCommandRunner.h"
#include "Application/Interfaces/IOutbox.h"
#include "Application/Interfaces/ISensorAcquisition.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/CommandReportDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/RawIoResultDto.h"
#include "Application/DTOs/RuleStateUpdateDto.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IFirmwareUpdateRecoveryUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorCommandUseCase.h"
// Only for ApiPriority, which the reports carry. This class must not hold an IHybridAPIService:
// everything it sends to the server goes through the outbox, never through a direct call.
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Application/Services/Rules/RuleValidator.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <memory>

/**
 * @brief Turns an incoming command into work, without waiting for anything.
 *
 * Command handling is three steps, each of them short, and no two of them on the same call stack -
 * that is what keeps the control loop's iteration bound independent of how slow the server or the
 * RS-485 line is:
 *
 *  1. `process()` accepts the signal. For device and complex module commands it submits a fetch to
 *     the outbox and returns; for a raw bus command it hands the transaction to the acquisition
 *     task and returns; for the rest it does the work, which is local.
 *  2. `onOutboxCompletion()` receives the fetched list on the control loop and routes it. The
 *     *fast* execution stays on the loop deliberately: the loop is the only owner of device state
 *     and of the switch-back timers, so moving that would move the race rather than remove it. A
 *     GPIO write and a state update are microseconds. The *slow* parts of a complex command are
 *     not: a scan against a silent pin is seconds on the bus and an OTA download is minutes on
 *     the network, so those go to the acquisition task and the command network task, with
 *     validation before and the state writes after still on the loop.
 *  3. `loop()` collects what those tasks finished and reports every outcome - as outbox *events*,
 *     which are never dropped in silence. An executed command whose report is lost leaves the
 *     backend showing a command that never ran. A fetched list is reported as one batch in list
 *     order once its last command finishes: one server answer, one batch of outcomes.
 */
class CommandService : public ICommandService
{
public:
    /**
     * @brief Signals waiting for a fetch to come back.
     *
     * Kept because the MQTT payload is the fallback: when the server does not answer, the command
     * is executed out of the message that announced it. Bounded so that a burst of signals during
     * an outage cannot grow with the length of the outage.
     */
    static const size_t MAX_PENDING_SIGNALS = 8;

    /**
     * @brief Reports the outbox had no room for, kept for the next iteration.
     *
     * The event ring is eight deep and a single command list can produce more reports than that.
     * "Never dropped in silence" is only true if the producer holds what was refused, which is
     * what this queue is; the counter below is what makes the case where even this overflows
     * checkable rather than assumed.
     */
    static const size_t MAX_PENDING_REPORTS = 24;

    /**
     * @brief Command lists (batches) in flight at once.
     *
     * Eight, like MAX_PENDING_SIGNALS and the outbox event ring: one list per fetch, and eight
     * lists piled up behind one slow command already means the app is resending faster than the
     * module executes. A list refused here is dropped whole *before* anything is reported - no
     * InProgress goes out, so on the backend its commands simply stay `new` and the next signal
     * fetches them again once the queue has drained. The refusal is counted, never silent.
     */
    static const size_t MAX_PENDING_BATCHES = 8;

    /**
     * @brief UpdateFirmware commands remembered as started on this boot.
     *
     * The server keeps a command as new until a status for it arrives, and a broker hands a
     * notification out again whenever the session subscribes - so the command of an update that is
     * already running comes back, and is not carried out a second time. A download ends in a restart
     * or in an outcome the server then holds, so a few entries are plenty; the oldest goes first.
     */
    static const size_t MAX_STARTED_UPDATES = 4;

    /**
     * @brief Loop iterations between two looks at a firmware update held back by the probation.
     *
     * Asking costs a read of the bootloader's slot state, which on the device is a flash read, and
     * the probation window is minutes long - so it is asked at a coarse pace rather than on every
     * pass. Nothing is late because of it: the update is waiting for a confirmation that is
     * minutes away, not milliseconds. When nothing is held back the question costs a comparison.
     */
    static const uint32_t DEFERRED_UPDATE_CHECK_EVERY_ITERATIONS = 100;

private:
    struct PendingReport
    {
        OutboxKind kind = OutboxKind::COMMAND_RESULT;
        std::string uid;
        std::string json;
        std::unique_ptr<CommandReportDto> payload;
    };

    /**
     * @brief One complex command of a fetched list, while its slow part runs elsewhere.
     *
     * A slot is `done` either immediately (validation failed, the work was refused, the command
     * is local) or when the acquisition or network task hands its outcome back. The tickets say
     * which completion belongs here.
     */
    struct PendingSlot
    {
        bool done = false;
        /** The boot's own deletion check: no command asked for it, so nothing is reported. */
        bool bootDeletionCheck = false;
        /** An update this boot has started, offered again: it is not carried out and nothing is reported. */
        bool silent = false;
        std::string commandName;
        uint32_t busTicket = 0;
        uint32_t netTicket = 0;
        /** A ScanSensors stage waiting for a catalog on its way to flash; empty otherwise. */
        std::unique_ptr<ComplexCommandWork> catalogWait;
        ModuleCommandExecutionResult result{"", ""};
    };

    /**
     * @brief One fetched command list whose commands are still executing.
     *
     * The backend expects one batch of outcomes per list, in list order - that is what the
     * synchronous path sent. Commands now finish at different times on different tasks, so the
     * batch is held here and reported only when every slot is done, and batches are reported in
     * the order the lists arrived. A batch whose slots are all done therefore waits for every
     * earlier batch - which is why the queue needs the explicit cap below: the work queues bound
     * the number of *unfinished* slots, not the number of finished batches parked behind one
     * slow command.
     */
    struct PendingBatch
    {
        std::vector<PendingSlot> slots;
        /** DeleteGdCommandNotification: report a single CommandResultDto, not a batch. */
        bool direct = false;
        /** The boot's own deletion check: report nothing. */
        bool unreported = false;
    };

    IDeviceCommandExecutionUseCase *deviceCommandUseCase;
    IModuleCommandExecutionUseCase *moduleCommandUseCase;
    ISensorCommandUseCase *sensorCommandUseCase;
    ModuleStructure *moduleStructure;
    ILogger *logger;
    IDebugSystem *debugSystem;
    INTPTimeAdapter *timeAdapter;
    IJsonConverter *jsonConverter;
    IRemoteSensorReceiver *_remoteSensorReceiver = nullptr;
    IRuleRepository *ruleRepository;
    ISettings *settings;
    RuleValidator _ruleValidator;
    static const unsigned long commandMaxAgeSeconds = 300;

    IOutbox *outbox = nullptr;
    ISensorAcquisition *busCommands = nullptr;
    IModuleConfig *moduleConfig = nullptr;
    IStartupSensorScan *startupSensorScan = nullptr;
    INetworkCommandRunner *networkCommands = nullptr;
    IFirmwareUpdateRecoveryUseCase *firmwareUpdateRecovery = nullptr;
    uint32_t _loopIterations = 0;
    /**
     * Whether the server had registered the module when the boot began, read on the first loop():
     * before any structure of this boot can have been delivered, which registers a new module.
     */
    bool _registrationAtBootRead = false;
    bool _registeredAtBoot = false;

    std::vector<CommandDto> _deviceSignals;
    std::deque<PendingReport> _pendingReports;
    std::deque<PendingBatch> _batches;
    /** The UpdateFirmware commands whose download this boot has started, oldest first. */
    std::vector<std::string> _startedUpdates;
    /**
     * The drain buffers, members rather than locals: each swaps with its producer's result
     * vector, so keeping them - reserved to the same ceiling - lets the two buffers of each
     * pair circulate between the sides with no allocation in steady state. A local would carry
     * the producer's capacity away and die with it every drain.
     */
    std::vector<NetworkCommandCompletion> _networkCompletions;
    std::vector<BusCommandCompletion> _busCompletions;

    /** Slots whose stage waits for the sensor type catalog to reach flash. */
    size_t _catalogWaits = 0;
    /** How many times a stage was set waiting - one per wait, however long the write takes. */
    uint32_t _catalogWaitParks = 0;

    uint32_t _droppedSignalCount = 0;
    uint32_t _droppedReportCount = 0;
    uint32_t _droppedBatchCount = 0;
    uint32_t _refusedBusCommandCount = 0;
    uint32_t _deviceFetchCount = 0;
    uint32_t _moduleFetchCount = 0;

    std::string accountUid()
    {
        if (!settings)
        {
            return "";
        }
        char buffer[256];
        buffer[0] = '\0';
        settings->getAccountUid(buffer);
        return std::string(buffer);
    }

public:
    CommandService(
        IDeviceCommandExecutionUseCase *deviceCommandUseCase,
        IModuleCommandExecutionUseCase *moduleCommandUseCase,
        ISensorCommandUseCase *sensorCommandUseCase,
        ModuleStructure *moduleStructure,
        ILogger *logger,
        IDebugSystem *debugSystem,
        INTPTimeAdapter *timeAdapter = nullptr,
        IJsonConverter *jsonConverter = nullptr,
        IRuleRepository *ruleRepository = nullptr,
        ISettings *settings = nullptr)
        : deviceCommandUseCase(deviceCommandUseCase),
          moduleCommandUseCase(moduleCommandUseCase),
          sensorCommandUseCase(sensorCommandUseCase),
          moduleStructure(moduleStructure),
          logger(logger),
          debugSystem(debugSystem),
          timeAdapter(timeAdapter),
          jsonConverter(jsonConverter),
          ruleRepository(ruleRepository),
          settings(settings),
          _ruleValidator(logger, debugSystem)
    {
        // Bounded containers allocate their ceiling once. A vector that grows by doubling
        // reallocates on every step up, briefly holds both buffers and leaves the old block
        // behind - on this device that is heap fragmentation paid for nothing, since the limit
        // is known here. The deque-backed queues (_pendingReports, _batches) have no reserve().
        _deviceSignals.reserve(MAX_PENDING_SIGNALS);
        _startedUpdates.reserve(MAX_STARTED_UPDATES);
        _networkCompletions.reserve(INetworkCommandRunner::RESULT_BUFFER_RESERVE);
        _busCompletions.reserve(ISensorAcquisition::RESULT_BUFFER_RESERVE);
    }

    void setRemoteSensorReceiver(IRemoteSensorReceiver *remoteSensorReceiver)
    {
        _remoteSensorReceiver = remoteSensorReceiver;
    }

    /** @brief The queue every request and every report of this service goes through. */
    void setOutbox(IOutbox *outboxToUse) { outbox = outboxToUse; }

    /** @brief The task that owns the RS-485 line and executes the raw bus commands. */
    void setBusCommandExecutor(ISensorAcquisition *acquisition) { busCommands = acquisition; }

    /** @brief The module's type: whether it has a sensor line for bus work to go to. */
    void setModuleConfig(IModuleConfig *config) { moduleConfig = config; }

    /**
     * @brief The startup discovery, carried to and from the line by this service.
     *
     * Not because discovery is a command - it is not - but because the loop-side half of a piece
     * of bus work has to be applied by whoever drains the acquisition task's results, and that is
     * this service. What a step is and what its answer means stay in the use case; this only
     * hands the step over and hands the answer back.
     */
    void setStartupSensorScan(IStartupSensorScan *scan) { startupSensorScan = scan; }

    /** @brief The task that waits on the network for module commands. */
    void setNetworkCommandRunner(INetworkCommandRunner *runner) { networkCommands = runner; }

    /**
     * @brief The use case holding a firmware update that could not be started at boot.
     *
     * Without it this service behaves as before: nothing is held back and nothing is resumed.
     */
    void setFirmwareUpdateRecovery(IFirmwareUpdateRecoveryUseCase *recovery)
    {
        firmwareUpdateRecovery = recovery;
    }

    bool process(const CommandDto &command) override
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Processing command type: %s", command.type.c_str());

        bool accepted = false;

        if (command.isDeviceCommand())
        {
            accepted = beginDeviceCommand(command);
        }
        else if (command.isRuleCommand())
        {
            accepted = processRuleCommand(command);
        }
        else if (command.isModuleCommand())
        {
            accepted = processModuleCommand(command);
        }
        else if (command.isSensorTypeCommand())
        {
            accepted = processSensorTypeCommand(command);
        }
        else if (command.isDeleteModuleCommand())
        {
            accepted = processDeleteModuleCommand(command);
        }
        else
        {
            logError("CommandService: Unknown command type: %s", command.type.c_str());
        }

        flushReports();
        return accepted;
    }

    void onOutboxCompletion(const OutboxCompletion &completion) override
    {
        switch (completion.kind)
        {
        case OutboxKind::DEVICE_COMMAND_FETCH:
            onDeviceCommandFetched(completion);
            break;
        case OutboxKind::MODULE_COMMAND_FETCH:
            onModuleCommandFetched(completion);
            break;
        default:
            // Every other kind belongs to another producer. The reports this service submits are
            // events and need nothing done when they come back - they are either delivered or
            // counted undelivered by the outbox.
            break;
        }

        flushReports();
    }

    void loop() override
    {
        if (!_registrationAtBootRead)
        {
            _registrationAtBootRead = true;
            _registeredAtBoot = (settings != nullptr) && (settings->getWasInitialSyncCompleted() != 0);
        }

        drainBusCommandResults();
        drainNetworkCommandResults();
        resumeCatalogWaits();
        resumeDeferredFirmwareUpdate();
        finalizeBatches();
        flushReports();
        pumpStartupSensorScan();
        reviewSensorLiveness();
    }

    void requestModuleCommandFetch() override
    {
        if (!outbox)
        {
            logError("CommandService: Outbox not available for the boot's module command fetch");
            return;
        }

        const uint32_t ticket = outbox->submitModuleCommandFetch();
        if (ticket == IOutbox::kRejected)
        {
            logError("CommandService: the boot's module command fetch refused by the outbox");
            return;
        }

        _moduleFetchCount++;
        log(DebugFeatures::MODULE_COMMAND_DEBUG, "CommandService: the boot asked for its module commands");
    }

    /**
     * Through the same pipeline a command from the server takes: the download runs on the network
     * task while the loop works on, and its outcome reaches the server as a batch.
     */
    void resumeKeptUpdate() override
    {
        if (firmwareUpdateRecovery == nullptr)
        {
            return;
        }

        // Room is checked before the update is taken: taking it empties the file it waits in.
        if (_batches.size() >= MAX_PENDING_BATCHES)
        {
            logError("CommandService: batch queue full (%u), the kept firmware update stays in its file",
                     (unsigned)MAX_PENDING_BATCHES);
            return;
        }

        ModuleCommandDto kept;
        if (!firmwareUpdateRecovery->takeKeptUpdate(kept))
        {
            return;
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: resuming the firmware update kept for this boot, uid=%s", kept.uid.c_str());
        std::vector<ModuleCommandDto> resumed = {kept};
        runModuleCommandBatch(resumed, true);
    }

    /**
     * The same network half DeleteGd's verification runs on cmd-net, and the same factory reset on a
     * DELETED verdict - applied on the loop, one after the other with a DeleteGd in flight. Not on a
     * module the server had not registered when the boot began: a module it does not know yet is
     * not one it has deleted.
     */
    void checkModuleDeletion() override
    {
        if (!_registeredAtBoot)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: the boot's deletion check skipped - the module was not registered when the boot began");
            return;
        }
        if (!moduleCommandUseCase)
        {
            return;
        }
        if (_batches.size() >= MAX_PENDING_BATCHES)
        {
            _droppedBatchCount++;
            logError("CommandService: batch queue full (%u), the boot's deletion check dropped; total dropped %u",
                     (unsigned)MAX_PENDING_BATCHES, (unsigned)_droppedBatchCount);
            return;
        }

        ModuleCommandDto check;
        check.command = getModuleCommandName(ModuleCommandEnum::DELETE_MODULE);
        check.uid = "boot-deletion-check";
        check.options = "{}";

        log(DebugFeatures::MODULE_COMMAND_DEBUG, "CommandService: the boot asks whether the module was deleted");
        _batches.emplace_back();
        PendingBatch &batch = _batches.back();
        batch.unreported = true;
        batch.slots.resize(1);
        batch.slots[0].bootDeletionCheck = true;
        routeComplexCommand(check, batch.slots[0]);
        finalizeBatches();
    }

    // --- Observation ---------------------------------------------------------------------------

    uint32_t droppedSignalCount() const { return _droppedSignalCount; }
    uint32_t droppedReportCount() const { return _droppedReportCount; }
    uint32_t refusedBusCommandCount() const { return _refusedBusCommandCount; }
    uint32_t deviceFetchCount() const { return _deviceFetchCount; }
    uint32_t moduleFetchCount() const { return _moduleFetchCount; }
    size_t pendingReportCount() const { return _pendingReports.size(); }
    size_t pendingBatchCount() const { return _batches.size(); }
    size_t catalogWaitCount() const { return _catalogWaits; }
    uint32_t catalogWaitParkCount() const { return _catalogWaitParks; }
    uint32_t droppedBatchCount() const { return _droppedBatchCount; }

private:
    /**
     * @brief Start the firmware update the boot held back, once the running image is confirmed.
     *
     * A boot that finds a kept update while its own image is still on probation may not download
     * one: the download writes the slot that holds the image this one falls back to. The update
     * waits, and this is where it starts - through the same pipeline a command from the server
     * takes, so it runs on the network task and its outcome reaches the backend the same way.
     */
    void resumeDeferredFirmwareUpdate()
    {
        if (firmwareUpdateRecovery == nullptr)
        {
            return;
        }

        if ((_loopIterations++ % DEFERRED_UPDATE_CHECK_EVERY_ITERATIONS) != 0)
        {
            return;
        }

        // Room is checked before the update is taken, not after: taking it empties the file it
        // was waiting in, and a batch queue with no room would leave the update nowhere at all.
        if (_batches.size() >= MAX_PENDING_BATCHES)
        {
            return;
        }

        ModuleCommandDto deferred;
        if (!firmwareUpdateRecovery->takeDeferredUpdate(deferred))
        {
            return;
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: starting the firmware update held back at boot, uid=%s",
            deferred.uid.c_str());
        std::vector<ModuleCommandDto> resumed = {deferred};
        runModuleCommandBatch(resumed, true);
    }

    // --- Reporting: everything this service tells the server ------------------------------------

    void reportCommandResult(const CommandResultDto &result, ApiPriority priority)
    {
        PendingReport report;
        report.kind = OutboxKind::COMMAND_RESULT;
        report.payload.reset(new CommandReportDto());
        report.payload->commandResult = result;
        report.payload->priority = priority;
        queueReport(std::move(report));
    }

    void reportDeviceCommandResult(const DeviceCommandUpdateDto &update)
    {
        PendingReport report;
        report.kind = OutboxKind::DEVICE_COMMAND_RESULT;
        report.payload.reset(new CommandReportDto());
        report.payload->deviceCommandUpdate = update;
        queueReport(std::move(report));
    }

    void reportModuleCommandResults(const std::vector<ModuleCommandUpdateDto> &updates)
    {
        PendingReport report;
        report.kind = OutboxKind::MODULE_COMMAND_RESULT;
        report.payload.reset(new CommandReportDto());
        report.payload->moduleCommandUpdates = updates;
        queueReport(std::move(report));
    }

    void reportRawIoResult(const RawIoResultDto &result)
    {
        PendingReport report;
        report.kind = OutboxKind::RAW_IO_RESULT;
        report.payload.reset(new CommandReportDto());
        report.payload->rawIoResult = result;
        queueReport(std::move(report));
    }

    void reportModuleStructure(const ModuleStructure &structure, ApiPriority priority)
    {
        PendingReport report;
        report.kind = OutboxKind::MODULE_STRUCTURE;
        report.payload.reset(new CommandReportDto());
        report.payload->moduleStructure = structure;
        report.payload->priority = priority;
        queueReport(std::move(report));
    }

    void reportRuleState(const std::string &ruleUid, const std::string &json)
    {
        PendingReport report;
        report.kind = OutboxKind::RULE_STATE_BY_UID;
        report.uid = ruleUid;
        report.json = json;
        queueReport(std::move(report));
    }

    void queueReport(PendingReport report)
    {
        if (_pendingReports.size() >= MAX_PENDING_REPORTS)
        {
            _droppedReportCount++;
            logError("CommandService: report queue full (%u), dropped a report of kind %u; total dropped %u",
                     (unsigned)MAX_PENDING_REPORTS, (unsigned)report.kind, (unsigned)_droppedReportCount);
            return;
        }
        _pendingReports.push_back(std::move(report));
    }

    /**
     * @brief Hand as many queued reports to the outbox as it will take.
     *
     * Stops at the first refusal instead of skipping past it, so that reports reach the server in
     * the order they were produced - an "InProgress" must not arrive after the "Done" it precedes.
     */
    void flushReports()
    {
        while (!_pendingReports.empty())
        {
            if (outbox == nullptr)
            {
                // No outbox means no way to reach the server at all. Dropping is the honest
                // outcome; sending from here would put a REST call back on the control loop.
                _droppedReportCount += static_cast<uint32_t>(_pendingReports.size());
                _pendingReports.clear();
                logError("CommandService: no outbox, dropped every pending report; total dropped %u",
                         (unsigned)_droppedReportCount);
                return;
            }

            if (submitReport(_pendingReports.front()) == IOutbox::kRejected)
            {
                return;
            }
            _pendingReports.pop_front();
        }
    }

    uint32_t submitReport(const PendingReport &report)
    {
        switch (report.kind)
        {
        case OutboxKind::COMMAND_RESULT:
            return outbox->submitCommandResult(report.payload->commandResult, report.payload->priority);
        case OutboxKind::DEVICE_COMMAND_RESULT:
            return outbox->submitDeviceCommandResult(report.payload->deviceCommandUpdate);
        case OutboxKind::MODULE_COMMAND_RESULT:
            return outbox->submitModuleCommandResult(report.payload->moduleCommandUpdates);
        case OutboxKind::RAW_IO_RESULT:
            return outbox->submitRawIoResult(report.payload->rawIoResult);
        case OutboxKind::MODULE_STRUCTURE:
            return outbox->submitModuleStructure(report.payload->moduleStructure,
                                                 report.payload->priority);
        case OutboxKind::RULE_STATE_BY_UID:
            return outbox->submitRuleStateByUid(report.uid, report.json);
        default:
            return IOutbox::kRejected;
        }
    }

    bool checkCommandTime(const std::string& createdDate)
    {
        if (createdDate.empty())
        {
            logError("CommandService: Empty createdDate provided");
            return false;
        }

        unsigned long currentEpoch = 0;
        if (timeAdapter)
        {
            currentEpoch = timeAdapter->getEpochTime();
            if (currentEpoch == 0)
            {
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Could not get current time from adapter");
                return false;
            }
        }
        else
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: No time adapter available for validation");
            return false;
        }

        return timeAdapter->isCommandTimeValid(createdDate.c_str(), commandMaxAgeSeconds);
    }

    /**
     * @brief Take a device-command signal and ask the server for the real list.
     *
     * The MQTT message is only a signal - the authority is the GET. What is kept here is its
     * payload, because it is the fallback: when the GET does not come back, the command is
     * executed out of the message that announced it.
     */
    bool beginDeviceCommand(const CommandDto &command)
    {
        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Device command signal - deviceUid: %s, state: %s, commandId: %d",
            command.deviceUid.c_str(), command.requestedState.c_str(), command.commandId);

        if (!outbox)
        {
            logError("CommandService: Outbox not available for the device command fetch");
            return false;
        }

        if (_deviceSignals.size() >= MAX_PENDING_SIGNALS)
        {
            _droppedSignalCount++;
            logError("CommandService: too many device command signals waiting (%u), dropped one; total dropped %u",
                     (unsigned)MAX_PENDING_SIGNALS, (unsigned)_droppedSignalCount);
        }
        else
        {
            _deviceSignals.push_back(command);
        }

        // A second signal while a fetch is in flight collapses into it: both would GET the same
        // `status=new` list. Fewer executions than before, never more - the synchronous path made
        // one GET per signal and executed every command each list contained.
        const uint32_t ticket = outbox->submitDeviceCommandFetch();
        if (ticket == IOutbox::kRejected)
        {
            logError("CommandService: device command fetch refused by the outbox");
            return false;
        }

        _deviceFetchCount++;
        return true;
    }

    void onDeviceCommandFetched(const OutboxCompletion &completion)
    {
        std::vector<CommandDto> signals;
        signals.swap(_deviceSignals);
        // The swap handed the reserved buffer away with the signals; put the ceiling back so
        // the next burst of signals never reallocates either.
        _deviceSignals.reserve(MAX_PENDING_SIGNALS);

        std::vector<DeviceCommandDto> deviceCommands;
        const bool haveResponse = (outbox != nullptr) && outbox->takeDeviceCommandResponse(deviceCommands);

        if (completion.delivered && haveResponse && !deviceCommands.empty())
        {
            executeDeviceCommands(deviceCommands);
            return;
        }

        if (completion.delivered)
        {
            // The server answered, and answered that there is nothing pending. A legitimate reply,
            // and the one case the fallback must not fire on.
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: No device commands received from server");
            return;
        }

        for (const auto &signal : signals)
        {
            if (signal.deviceUid.empty())
            {
                continue;
            }

            DeviceCommandDto fallback;
            fallback.uid = signal.deviceUid;
            fallback.requestedState = signal.requestedState;
            fallback.id = signal.commandId;
            fallback.needToSwitchBack = signal.needToSwitchBack;
            fallback.secondsToSwitchBack = signal.secondsToSwitchBack;
            fallback.analogControlPinValue = signal.analogControlPinValue;
            fallback.switchType = signal.switchType;
            fallback.status = signal.status;
            fallback.createdDate = signal.createdDate;

            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: HTTP unavailable, executing device command from MQTT payload - deviceUid: %s, state: %s, commandId: %d",
                fallback.uid.c_str(), fallback.requestedState.c_str(), fallback.id);

            std::vector<DeviceCommandDto> fromPayload;
            fromPayload.push_back(fallback);
            executeDeviceCommands(fromPayload);
        }
    }

    bool executeDeviceCommands(const std::vector<DeviceCommandDto> &deviceCommands)
    {
        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Received %d device commands from server", deviceCommands.size());

        bool allCommandsSuccessful = true;

        for (const auto& deviceCommand : deviceCommands)
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: Processing command - deviceUid: %s, state: %s, commandId: %d",
                deviceCommand.uid.c_str(), deviceCommand.requestedState.c_str(), deviceCommand.id);

            if (!deviceCommand.createdDate.empty())
            {
                bool isTimeValid = checkCommandTime(deviceCommand.createdDate);
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Command time validation - createdDate: %s, isValid: %s",
                    deviceCommand.createdDate.c_str(), isTimeValid ? "true" : "false");

                if (!isTimeValid)
                {
                    logError("CommandService: Command is too old - createdDate: %s, maxAge: %lu seconds",
                            deviceCommand.createdDate.c_str(), commandMaxAgeSeconds);

                    DeviceCommandUpdateDto updateDto;
                    updateDto.uid = deviceCommand.uid;
                    updateDto.status = "error";
                    updateDto.errorMessage = "Command expired: too old to execute";
                    if (timeAdapter)
                        updateDto.timestamp = timeAdapter->getFormattedTime();
                    reportDeviceCommandResult(updateDto);

                    allCommandsSuccessful = false;
                    continue;
                }
            }
            else
            {
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: No createdDate provided for command %d, proceeding without time validation",
                    deviceCommand.id);
            }

            if (deviceCommandUseCase)
            {
                bool executionResult = deviceCommandUseCase->execute(
                    deviceCommand.uid,
                    deviceCommand.requestedState,
                    deviceCommand.id,
                    deviceCommand.needToSwitchBack,
                    deviceCommand.secondsToSwitchBack,
                    timeAdapter ? timeAdapter->getFormattedTime() : ""
                );

                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Device command execution %s for command %d",
                    executionResult ? "successful" : "failed", deviceCommand.id);

                DeviceCommandUpdateDto updateDto;
                updateDto.uid = deviceCommand.uid;
                updateDto.status = executionResult ? "done" : "error";
                if (!executionResult)
                {
                    updateDto.errorMessage = "Device command execution failed";
                    allCommandsSuccessful = false;
                }
                if (timeAdapter)
                    updateDto.timestamp = timeAdapter->getFormattedTime();
                reportDeviceCommandResult(updateDto);
            }
            else
            {
                logError("CommandService: DeviceCommandExecutionUseCase not available");
                DeviceCommandUpdateDto updateDto;
                updateDto.uid = deviceCommand.uid;
                updateDto.status = "error";
                updateDto.errorMessage = "Device command execution service not available";
                if (timeAdapter)
                    updateDto.timestamp = timeAdapter->getFormattedTime();
                reportDeviceCommandResult(updateDto);
                allCommandsSuccessful = false;
            }
        }

        return allCommandsSuccessful;
    }

    const char *ruleModeName(RuleMode mode) const
    {
        return mode == RuleMode::ON ? "On" : (mode == RuleMode::OFF ? "Off" : "WaitingForUserConfirm");
    }

    bool processRuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::RULE_DEBUG,
            "CommandService: RULE_PUSH recv ruleUid=%s len=%d",
            command.ruleUid.c_str(), (int)command.rulesPayload.size());

        if (!jsonConverter || !ruleRepository)
        {
            log(DebugFeatures::RULE_DEBUG,
                "CommandService: RULE_PUSH drop reason=deps ruleUid=%s", command.ruleUid.c_str());
            return true;
        }

        std::vector<Rule> rules;
        // Identical in meaning to what was here: Ok is exactly "parsed and non-empty", so an
        // empty payload and an unreadable one are both refused, as they were. This path applies a
        // rule snapshot pushed over MQTT, and an empty snapshot deletes rules - it is the one
        // caller that must keep treating "empty" as "do nothing".
        if (jsonConverter->rulesFromJson(command.rulesPayload, rules) != JsonListParse::Ok || rules.empty())
        {
            log(DebugFeatures::RULE_DEBUG,
                "CommandService: RULE_PUSH drop reason=%s ruleUid=%s",
                rules.empty() ? "empty" : "parse", command.ruleUid.c_str());
            return true;
        }

        std::string timestamp = timeAdapter ? timeAdapter->getFormattedTime() : "";
        int applied = 0;
        int errors = 0;

        for (const auto &rule : rules)
        {
            RuleValidationResult vr = _ruleValidator.validateRule(rule);

            RuleStateDto ackItem;
            bool emitAck = true;

            if (vr.isValid)
            {
                int synced = ruleRepository->synchronizeRules({rule});
                if (synced > 0)
                {
                    ackItem = RuleStateDto(rule.uid, "Synchronized", ruleModeName(rule.mode),
                                           rule.modeReason, "", rule.version);
                    applied++;
                    log(DebugFeatures::RULE_DEBUG,
                        "CommandService: RULE_PUSH applied ruleUid=%s version=%d state=Synchronized",
                        rule.uid.c_str(), rule.version);
                }
                else
                {
                    emitAck = false;
                    errors++;
                    log(DebugFeatures::RULE_DEBUG,
                        "CommandService: RULE_PUSH store_fail ruleUid=%s version=%d",
                        rule.uid.c_str(), rule.version);
                }
            }
            else
            {
                ackItem = RuleStateDto(rule.uid, "Error", ruleModeName(rule.mode),
                                       vr.errorMessage, "wrong_format", rule.version);
                errors++;
                log(DebugFeatures::RULE_DEBUG,
                    "CommandService: RULE_PUSH error ruleUid=%s version=%d errorType=wrong_format",
                    rule.uid.c_str(), rule.version);
            }

            if (emitAck)
            {
                ackItem.accountUid = accountUid();
                std::string json = jsonConverter->ruleStateUpdateSingleToJson(ackItem, timestamp);
                log(DebugFeatures::RULE_DEBUG,
                    "CommandService: RULE_PUSH ack uid=%s body=%s", ackItem.uid.c_str(), json.c_str());
                reportRuleState(ackItem.uid, json);
            }
        }

        if (_remoteSensorReceiver)
        {
            _remoteSensorReceiver->onRuleSetChanged();
        }

        log(DebugFeatures::RULE_DEBUG,
            "CommandService: RULE_PUSH done applied=%d errors=%d", applied, errors);

        return true;
    }

    bool processModuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Processing module command - command: %s",
            command.command.c_str());

        bool isComplex = isComplexModuleCommand(command.command);
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: isComplexModuleCommand('%s') = %s",
            command.command.c_str(), isComplex ? "true" : "false");

        if (isComplex)
        {
            return beginComplexModuleCommand(command);
        }

        if (isRawModuleCommand(command.command))
        {
            return beginRawModuleCommand(command);
        }

        if (getModuleCommandEnum(command.command) == ModuleCommandEnum::REBOOT_REMOTE_MODULE)
        {
            return beginRawModuleCommand(command);
        }

        if (moduleCommandUseCase)
        {
            return moduleCommandUseCase->execute(command.command, command.options, command.commandUid, command.createdAt);
        }
        else
        {
            logError("CommandService: ModuleCommandExecutionUseCase not available");
            return false;
        }
    }

    /**
     * @brief The complex commands whose slow part is a scan on the sensor line.
     *
     * Only a ModBus scan can find anything - the analog and I2C scans answer nothing
     * (SensorReader::scanSensor()) - so on a module with no line these have nothing to find.
     */
    bool isLineScanModuleCommand(const std::string& commandName) const
    {
        ModuleCommandEnum command = getModuleCommandEnum(commandName);
        return command == ModuleCommandEnum::PING_PIN ||
               command == ModuleCommandEnum::SET_SENSOR_MODEL ||
               command == ModuleCommandEnum::SCAN_SENSORS;
    }

    /** @brief What a command for the sensor line is answered with on a module that has none. */
    static const char *noSensorLineMessage() { return "No sensor line on this module"; }

    bool isRawModuleCommand(const std::string& commandName) const
    {
        ModuleCommandEnum command = getModuleCommandEnum(commandName);
        return command == ModuleCommandEnum::RAW_MODBUS_WRITE ||
               command == ModuleCommandEnum::RAW_MODBUS_READ ||
               command == ModuleCommandEnum::RAW_SERIAL_WRITE;
    }

    /**
     * @brief Does this module have an RS-485 sensor line for bus work to go to?
     *
     * Only a sensor module does: setup begins the line there and nowhere else
     * (ModuleSetupService::setupSensorBus()). On the other types the UART pins drive something
     * else - a hydro module's pumps 2 and 3, a relay module's red LEDs - and a scan begins the
     * UARTs by itself (SensorReader::scanModbusSensor()), so no bus work may start there. The type
     * is the board's own, read in setup before the first command can arrive. Given no
     * configuration, the line is taken to be there; globals.h always gives one.
     */
    bool theModuleHasASensorLine() const
    {
        return moduleConfig == nullptr || moduleConfig->getModuleType() == MODULE_TYPE_SENSOR;
    }

    /**
     * @brief Ask the task that owns the RS-485 line to exist before work is queued to it - on a
     *        module that has the line.
     *
     * The task is created on demand: by the sweep when there are sensors to poll, by the discovery
     * when it hands out a step. A sensor module with no sensor to poll and no step to take - a
     * registry that is empty or has not read back, a discovery that waits for it - has created
     * neither, and a command queued to a task nobody created is never answered. A task that cannot
     * be created now leaves the command queued; the next one to ask creates it.
     */
    void askTheBusTaskToExist()
    {
        if (theModuleHasASensorLine())
        {
            busCommands->ensureRunning();
        }
    }

    void logNoSensorLine(const std::string& commandName)
    {
        logError("CommandService: %s needs the sensor line, and this module (type %d) has none",
                 commandName.c_str(), moduleConfig != nullptr ? moduleConfig->getModuleType() : -1);
    }

    /**
     * @brief Hand a raw bus command to the task that owns the RS-485 line.
     *
     * These are real transactions - up to about a second each against a device that does not
     * answer. Run on the control loop they would break its iteration bound, which is why they go
     * to the task that owns the line instead of executing here.
     */
    bool beginRawModuleCommand(const CommandDto &command)
    {
        if (!busCommands)
        {
            logError("CommandService: no bus command executor for raw command %s", command.command.c_str());
            reportCommandResult(CommandResultDto("error", command.command, "Bus command executor not available"),
                                ApiPriority::MQTT_PREFERRED);
            return false;
        }

        // A raw ModbusRead, ModbusWrite or SerialWrite is a transaction on the sensor line, and is
        // answered at once where there is none. A RebootRemoteModule is a UDP packet and takes this
        // path only to be executed off the loop; on a module with no line it waits for a task that
        // nothing there starts.
        if (isRawModuleCommand(command.command) && !theModuleHasASensorLine())
        {
            logNoSensorLine(command.command);
            reportCommandResult(CommandResultDto("error", command.command, noSensorLineMessage()),
                                ApiPriority::MQTT_PREFERRED);
            return false;
        }

        askTheBusTaskToExist();
        const uint32_t ticket = busCommands->submitBusCommand(command.command, command.options);
        if (ticket == ISensorAcquisition::kBusCommandRejected)
        {
            _refusedBusCommandCount++;
            logError("CommandService: raw command %s refused, the bus queue is full; total refused %u",
                     command.command.c_str(), (unsigned)_refusedBusCommandCount);
            reportCommandResult(CommandResultDto("error", command.command, "Bus command queue full"),
                                ApiPriority::MQTT_PREFERRED);
            return false;
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Raw command queued for the bus task - command: %s, ticket: %u",
            command.command.c_str(), (unsigned)ticket);
        return true;
    }

    /**
     * @brief Report every raw bus command the acquisition task has finished.
     *
     * Same two messages, in the same order, with the same statuses as when the transaction ran
     * inline - only later, and off the control loop's own stack.
     */
    void drainBusCommandResults()
    {
        if (!busCommands)
        {
            return;
        }

        if (!busCommands->drainBusCommandResults(_busCompletions))
        {
            return;
        }

        for (const auto &completion : _busCompletions)
        {
            if (completion.startupScan)
            {
                if (startupSensorScan != nullptr)
                {
                    startupSensorScan->applyStepOutcome(completion.startupOutcome);
                }
                continue;
            }

            if (completion.complex)
            {
                onComplexWorkFinished(completion.ticket, true,
                                      completion.complexWork, completion.complexOutcome);
                continue;
            }

            const RawBusCommandResult &result = completion.result;
            const bool isDone = (result.status == ModuleCommandStatus::DONE);
            const std::string status = isDone ? "accepted" : "error";

            reportCommandResult(CommandResultDto(status, completion.command, result.errorMessage),
                                ApiPriority::MQTT_PREFERRED);

            if (result.hasData)
            {
                reportRawIoResult(RawIoResultDto(completion.command, result.channel, result.dataHex,
                                                 !isDone, result.errorMessage));
            }

            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Raw command processed - command: %s, status: %s, hasData: %s",
                completion.command.c_str(), status.c_str(), result.hasData ? "true" : "false");
        }
        // Emptied but kept: the capacity goes back to the acquisition task on the next swap.
        _busCompletions.clear();
    }

    /**
     * @brief Hand the next step of the startup discovery to the task that owns the line.
     *
     * One step is in flight at a time - the use case produces the next only once the answer to
     * this one has been applied - and a step the acquisition task could not take is given back
     * and offered again on the next iteration rather than lost.
     *
     * The task is asked to exist here rather than only on the polling tick: a module whose
     * registry is empty has nothing to poll, and the discovery is exactly what such a module
     * boots to do.
     */
    void pumpStartupSensorScan()
    {
        if (startupSensorScan == nullptr || busCommands == nullptr)
        {
            return;
        }

        StartupScanStep step;
        if (!startupSensorScan->takeNextStep(step))
        {
            return;
        }

        if (!busCommands->ensureRunning() ||
            busCommands->submitStartupScanStep(step) == ISensorAcquisition::kBusCommandRejected)
        {
            startupSensorScan->returnStep();
        }
    }

    /**
     * @brief Let the discovery compare the sensors against the structure the server holds.
     *
     * Separate from pumpStartupSensorScan(), and not behind its bus-command guard: this needs no
     * line and no queue, and it is owed to a module long after the discovery is over - it is the
     * only thing that describes the module again when a sensor comes back or goes away.
     */
    void reviewSensorLiveness()
    {
        if (startupSensorScan != nullptr)
        {
            startupSensorScan->reviewSensorLiveness();
        }
    }

    bool isComplexModuleCommand(const std::string& commandName) const
    {
        ModuleCommandEnum command = getModuleCommandEnum(commandName);
        return command == ModuleCommandEnum::UPDATE_FIRMWARE ||
               command == ModuleCommandEnum::PING_PIN ||
               command == ModuleCommandEnum::SET_SENSOR_MODEL ||
               command == ModuleCommandEnum::SCAN_SENSORS ||
               command == ModuleCommandEnum::DELETE_SENSORS ||
               command == ModuleCommandEnum::ADD_WIFI;
    }

    /**
     * @brief The commands whose slow part must leave the control loop.
     *
     * AddWifi is deliberately not here, although the fetch path treats it as complex: it waits
     * for nothing - no bus, no network. It writes EEPROM, SPIFFS and the WiFiMulti list, and
     * those are exactly the things the control loop writes too, so it executes inline where a
     * second writer cannot appear.
     */
    bool isSlowModuleCommand(const std::string& commandName) const
    {
        ModuleCommandEnum command = getModuleCommandEnum(commandName);
        return command == ModuleCommandEnum::UPDATE_FIRMWARE ||
               command == ModuleCommandEnum::PING_PIN ||
               command == ModuleCommandEnum::SET_SENSOR_MODEL ||
               command == ModuleCommandEnum::SCAN_SENSORS ||
               command == ModuleCommandEnum::DELETE_MODULE;
    }

    // --- Complex commands: routing between the tasks that run their slow parts ------------------

    /**
     * @brief Prepare a complex command on the loop and hand its slow part to the right task.
     *
     * The slot is done immediately when validation already answers, and left waiting on a ticket
     * otherwise. Nothing here waits: prepare is validation plus a repository snapshot, and the
     * submits are queue pushes.
     */
    void routeComplexCommand(const ModuleCommandDto &command, PendingSlot &slot)
    {
        slot.commandName = command.command;

        // Whichever door the command came through - a fetched list, the file a restart kept it in, the
        // wait a probation image put it in - an update whose download this boot has started is not
        // started again.
        if (isUpdateStartedThisBoot(command))
        {
            slot.done = true;
            slot.silent = true;
            return;
        }

        ComplexCommandWork work;
        ModuleCommandExecutionResult immediate("", "");
        if (!moduleCommandUseCase->prepareComplexCommand(command, work, immediate))
        {
            slot.done = true;
            slot.result = immediate;
            return;
        }

        // Validated, and answered before any stage: on a module with no sensor line the scan would
        // begin the UARTs on pins that drive something else, and a catalog fetched for it would
        // serve nothing. A command that failed validation keeps the answer validation gave it.
        if (isLineScanModuleCommand(command.command) && !theModuleHasASensorLine())
        {
            logNoSensorLine(command.command);
            slot.done = true;
            slot.result = ModuleCommandExecutionResult(
                command.uid, getModuleCommandStatusName(ModuleCommandStatus::ERROR), noSensorLineMessage());
            return;
        }

        dispatchWork(work, slot);

        // A slot still waiting on a ticket is a download the network task has been handed.
        if (isFirmwareUpdate(command) && !slot.done)
        {
            noteUpdateStarted(command.uid);
        }
    }

    static bool isFirmwareUpdate(const ModuleCommandDto &command)
    {
        return getModuleCommandEnum(command.command) == ModuleCommandEnum::UPDATE_FIRMWARE;
    }

    /**
     * @brief Whether this is an UpdateFirmware command whose download this boot has started; says so
     *        on the log when it is.
     */
    bool isUpdateStartedThisBoot(const ModuleCommandDto &command)
    {
        if (!isFirmwareUpdate(command))
        {
            return false;
        }
        for (size_t i = 0; i < _startedUpdates.size(); i++)
        {
            if (_startedUpdates[i] == command.uid)
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "CommandService: firmware update uid=%s was started on this boot already - not carried out again",
                    command.uid.c_str());
                return true;
            }
        }
        return false;
    }

    void noteUpdateStarted(const std::string &uid)
    {
        if (uid.empty())
        {
            return;
        }
        if (_startedUpdates.size() >= MAX_STARTED_UPDATES)
        {
            _startedUpdates.erase(_startedUpdates.begin());
        }
        _startedUpdates.push_back(uid);
    }

    void dispatchWork(const ComplexCommandWork &work, PendingSlot &slot)
    {
        if (work.awaitCatalogStore)
        {
            // No task executes this stage: it is held here, on the loop, and handed back by
            // resumeCatalogWaits() once the catalog has landed.
            slot.catalogWait.reset(new ComplexCommandWork(work));
            _catalogWaits++;
            _catalogWaitParks++;
            return;
        }

        if (work.onBus)
        {
            if (busCommands != nullptr)
            {
                askTheBusTaskToExist();
                const uint32_t ticket = busCommands->submitComplexBusCommand(work);
                if (ticket != ISensorAcquisition::kBusCommandRejected)
                {
                    slot.busTicket = ticket;
                    askTheDiscoveryToFollowAScan(work);
                    return;
                }
                _refusedBusCommandCount++;
            }
            logError("CommandService: bus work for command %s could not be queued",
                     work.command.command.c_str());
            slot.done = true;
            slot.result = ModuleCommandExecutionResult(
                work.command.uid, getModuleCommandStatusName(ModuleCommandStatus::ERROR),
                busCommands != nullptr ? "Bus command queue full" : "Bus command executor not available");
            return;
        }

        if (networkCommands != nullptr)
        {
            const uint32_t ticket = networkCommands->submit(work);
            if (ticket != INetworkCommandRunner::kRejected)
            {
                slot.netTicket = ticket;
                return;
            }
        }
        logError("CommandService: network work for command %s could not be queued",
                 work.command.command.c_str());
        slot.done = true;
        slot.result = ModuleCommandExecutionResult(
            work.command.uid, getModuleCommandStatusName(ModuleCommandStatus::ERROR),
            networkCommands != nullptr ? "Network command queue full" : "Network command runner not available");
    }

    /**
     * @brief A scan asked for by a person is also the way back for the discovery.
     *
     * The discovery gives up on a port whose occupant matches no model of the catalog, and this is
     * what makes it ask that port again. Asked here rather than where the list is walked, and for
     * two reasons. The pin is the one this command was validated with: prepareComplexCommand()
     * refuses a ScanSensors that comes without options and one that names a pin this module has
     * not, and neither reaches here. Options that name no pin - `{}`, or the key spelled another
     * way - or a pin that does not read as a number are read as pin 0, which is port 0: that
     * command is a scan of port 0 and orders the pass on port 0. And the scan itself is
     * already in the queue by now, which the line serves before any step of the discovery - so the
     * pass runs behind the identification procedure this command is about to spend on that port, which is
     * the whole reason the pass is worth anything: the port has had its power throughout, and a
     * device too slow to answer a pass of the module's own is awake by the time it is asked again.
     */
    void askTheDiscoveryToFollowAScan(const ComplexCommandWork &work)
    {
        if (startupSensorScan == nullptr ||
            getModuleCommandEnum(work.command.command) != ModuleCommandEnum::SCAN_SENSORS)
        {
            return;
        }
        startupSensorScan->rescan(work.pin);
    }

    /** @brief Apply one finished slow part to its slot: complete it, or dispatch the next part. */
    void onComplexWorkFinished(uint32_t ticket, bool fromBus,
                               const ComplexCommandWork &work, const ComplexCommandOutcome &outcome)
    {
        PendingSlot *slot = findSlot(ticket, fromBus);
        if (slot == nullptr)
        {
            logError("CommandService: no slot waits for %s work ticket %u",
                     fromBus ? "bus" : "network", (unsigned)ticket);
            return;
        }

        slot->busTicket = 0;
        slot->netTicket = 0;

        finishStage(*slot, work, outcome);
    }

    /** @brief Apply a finished stage to its slot: complete the slot, or dispatch the next stage. */
    void finishStage(PendingSlot &slot, const ComplexCommandWork &work,
                     const ComplexCommandOutcome &outcome)
    {
        ModuleCommandExecutionResult result("", "");
        ComplexCommandWork nextWork;
        if (slot.bootDeletionCheck &&
            !(outcome.status == ModuleCommandStatus::DONE && outcome.deletionStatus == ModuleDeletionStatusEnum::DELETED))
        {
            // The answer to a question no command asked: anything but DELETED is the ordinary case.
            log(DebugFeatures::MODULE_COMMAND_DEBUG, "CommandService: the boot's deletion check - verdict %s, nothing to do",
                outcome.status == ModuleCommandStatus::DONE ? getModuleDeletionStatusName(outcome.deletionStatus)
                                                            : "not reached");
            slot.done = true;
            return;
        }
        if (slot.bootDeletionCheck)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG, "CommandService: the boot's deletion check - verdict DELETED, factory reset");
        }
        if (moduleCommandUseCase->finishComplexCommand(work, outcome, result, nextWork))
        {
            slot.done = true;
            slot.result = result;
            return;
        }

        // A finished stage may hand back the next stage of the same command. Today the only such
        // chain is ScanSensors: catalog fetch (network) -> wait for that catalog to reach flash,
        // when it has to (loop) -> the scan itself (bus).
        dispatchWork(nextWork, slot);
    }

    /**
     * @brief Hand every stage that waited for the sensor type catalog back to the use case, once
     *        no catalog is on its way to flash any more.
     *
     * One question per iteration while a write is in flight, and the slots are walked only after
     * it has landed - by then the loop has applied the write's outcome, so the stage finds the
     * catalog the write left in the repository.
     */
    void resumeCatalogWaits()
    {
        if (_catalogWaits == 0 || moduleCommandUseCase->catalogStorePending())
        {
            return;
        }

        for (auto &batch : _batches)
        {
            for (auto &slot : batch.slots)
            {
                if (!slot.catalogWait)
                {
                    continue;
                }
                std::unique_ptr<ComplexCommandWork> work(std::move(slot.catalogWait));
                _catalogWaits--;
                finishStage(slot, *work, ComplexCommandOutcome());
            }
        }
    }

    PendingSlot *findSlot(uint32_t ticket, bool fromBus)
    {
        for (auto &batch : _batches)
        {
            for (auto &slot : batch.slots)
            {
                if (slot.done)
                {
                    continue;
                }
                if ((fromBus && slot.busTicket == ticket) || (!fromBus && slot.netTicket == ticket))
                {
                    return &slot;
                }
            }
        }
        return nullptr;
    }

    void drainNetworkCommandResults()
    {
        if (networkCommands == nullptr)
        {
            return;
        }

        if (!networkCommands->drainResults(_networkCompletions))
        {
            return;
        }

        for (const auto &completion : _networkCompletions)
        {
            onComplexWorkFinished(completion.ticket, false, completion.work, completion.outcome);
        }
        // Emptied but kept: the capacity goes back to the runner on the next swap.
        _networkCompletions.clear();
    }

    /**
     * @brief Report every batch whose commands have all finished, oldest first.
     *
     * Stops at the first incomplete batch so that lists reach the server in the order they
     * arrived, exactly as the synchronous path reported them.
     */
    void finalizeBatches()
    {
        while (!_batches.empty())
        {
            PendingBatch &front = _batches.front();

            bool complete = true;
            for (const auto &slot : front.slots)
            {
                if (!slot.done)
                {
                    complete = false;
                    break;
                }
            }
            if (!complete)
            {
                return;
            }

            if (front.direct)
            {
                reportDirectDeleteModule(front.slots[0]);
            }
            else if (!front.unreported)
            {
                reportBatch(front);
            }
            _batches.pop_front();
        }
    }

    /** @brief The batch of outcomes the backend expects for one command list - same shape,
     *         same order, same statuses the synchronous path produced. */
    void reportBatch(PendingBatch &batch)
    {
        std::vector<ModuleCommandUpdateDto> updates;
        updates.reserve(batch.slots.size());
        bool structureChanged = false;

        for (const auto &slot : batch.slots)
        {
            if (slot.silent)
            {
                continue;
            }
            const ModuleCommandExecutionResult &result = slot.result;

            ModuleCommandUpdateDto updateDto;
            updateDto.uid = result.uid;
            updateDto.status = result.status;
            updateDto.result = result.result;
            if (timeAdapter)
            {
                updateDto.timestamp = timeAdapter->getFormattedTime();
            }

            ModuleCommandStatus status = getModuleCommandStatus(result.status);
            if (status == ModuleCommandStatus::ERROR)
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "CommandService: Command uid=%s returned ERROR: %s",
                    result.uid.c_str(), result.errorMessage.c_str());
                updateDto.errorMessage = result.errorMessage;
            }
            else if (status == ModuleCommandStatus::DONE)
            {
                ModuleCommandEnum commandEnum = getModuleCommandEnum(slot.commandName);
                if (commandEnum == ModuleCommandEnum::SET_SENSOR_MODEL ||
                    commandEnum == ModuleCommandEnum::SCAN_SENSORS ||
                    commandEnum == ModuleCommandEnum::DELETE_SENSORS)
                {
                    structureChanged = true;
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "CommandService: Structure change flag set to TRUE for command: %s",
                        slot.commandName.c_str());
                }
            }
            updates.push_back(updateDto);

            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Sent result - uid: %s, status: %s, error: %s, sensors count: %d",
                result.uid.c_str(), result.status.c_str(), result.errorMessage.c_str(),
                (int)result.result.sensors.size());
        }

        if (!updates.empty())
        {
            reportModuleCommandResults(updates);
        }

        if (structureChanged && moduleStructure)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Queueing the updated module structure after sensor changes");
            // Queued after the results above, and the queue preserves order, so the backend still
            // sees the command outcomes before the structure they explain.
            reportModuleStructure(*moduleStructure, ApiPriority::BOTH);
        }
        else if (structureChanged && !moduleStructure)
        {
            logError("CommandService: Structure changed but moduleStructure is NULL!");
        }
    }

    /** @brief The final acknowledgement of a DeleteGdCommandNotification, once the verdict is in. */
    void reportDirectDeleteModule(const PendingSlot &slot)
    {
        const ModuleCommandExecutionResult &result = slot.result;
        ModuleCommandStatus status = getModuleCommandStatus(result.status);

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Delete command execution completed - status: %s, error: %s",
            result.status.c_str(), result.errorMessage.c_str());

        CommandResultDto finalResult;
        if (status == ModuleCommandStatus::DONE)
        {
            finalResult = CommandResultDto("done", "DeleteModule", "Module successfully deleted and reset");
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Module deletion and factory reset completed successfully");
        }
        else
        {
            finalResult = CommandResultDto("error", "DeleteModule", result.errorMessage);
            logError("CommandService: Module deletion failed - %s", result.errorMessage.c_str());
        }

        reportCommandResult(finalResult, ApiPriority::MQTT_PREFERRED);
    }

    std::vector<ModuleCommandDto> validateAndFilterModuleCommands(
        const std::vector<ModuleCommandDto>& moduleCommands,
        bool& allCommandsSuccessful)
    {
        std::vector<ModuleCommandDto> validCommands;
        validCommands.reserve(moduleCommands.size());

        for (const auto& moduleCommand : moduleCommands)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Processing module command - uid: %s, command: %s, createdAt: %s",
                moduleCommand.uid.c_str(), moduleCommand.command.c_str(), moduleCommand.createdAt.c_str());

            // Before anything is reported for it: the update is running already, and its own outcome
            // is what the server is waiting for.
            if (isUpdateStartedThisBoot(moduleCommand))
            {
                continue;
            }

            if (moduleCommand.createdAt.empty())
            {
                logError("CommandService: Module command has empty createdAt - uid: %s",
                        moduleCommand.uid.c_str());

                ModuleCommandUpdateDto updateDto;
                updateDto.uid = moduleCommand.uid;
                updateDto.status = getModuleCommandStatusName(ModuleCommandStatus::ERROR);
                updateDto.errorMessage = "Command has no timestamp";
                if (timeAdapter)
                {
                    updateDto.timestamp = timeAdapter->getFormattedTime();
                }
                std::vector<ModuleCommandUpdateDto> updates = {updateDto};
                reportModuleCommandResults(updates);

                allCommandsSuccessful = false;
                continue;
            }

            bool isTimeValid = checkCommandTime(moduleCommand.createdAt);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Module command time validation - createdAt: %s, isValid: %s",
                moduleCommand.createdAt.c_str(), isTimeValid ? "true" : "false");

            if (!isTimeValid)
            {
                logError("CommandService: Module command is too old - createdAt: %s, maxAge: %lu seconds",
                        moduleCommand.createdAt.c_str(), commandMaxAgeSeconds);

                ModuleCommandUpdateDto updateDto;
                updateDto.uid = moduleCommand.uid;
                updateDto.status = getModuleCommandStatusName(ModuleCommandStatus::ERROR);
                updateDto.errorMessage = "Command expired: too old to execute";
                if (timeAdapter)
                {
                    updateDto.timestamp = timeAdapter->getFormattedTime();
                }
                std::vector<ModuleCommandUpdateDto> updates = {updateDto};
                reportModuleCommandResults(updates);

                allCommandsSuccessful = false;
                continue;
            }

            // Command is valid, set to InProgress and add to valid list
            ModuleCommandUpdateDto inProgressDto;
            inProgressDto.uid = moduleCommand.uid;
            inProgressDto.status = getModuleCommandStatusName(ModuleCommandStatus::IN_PROGRESS);
            if (timeAdapter)
            {
                inProgressDto.timestamp = timeAdapter->getFormattedTime();
            }
            std::vector<ModuleCommandUpdateDto> updates = {inProgressDto};
            reportModuleCommandResults(updates);

            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Set command to InProgress - uid: %s, command: %s",
                moduleCommand.uid.c_str(), moduleCommand.command.c_str());

            validCommands.push_back(moduleCommand);
        }
        
        return validCommands;
    }

    /**
     * @brief Take a complex module-command signal and ask the server for the real list.
     *
     * Same shape as the device command: the MQTT message is the signal, the GET is the authority,
     * and the answer is applied on the control loop when it arrives.
     */
    bool beginComplexModuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: complex module command signal - command: %s", command.command.c_str());

        if (!outbox)
        {
            logError("CommandService: Outbox not available for the module command fetch");
            return false;
        }

        const uint32_t ticket = outbox->submitModuleCommandFetch();
        if (ticket == IOutbox::kRejected)
        {
            logError("CommandService: module command fetch refused by the outbox");
            return false;
        }

        _moduleFetchCount++;
        return true;
    }

    void onModuleCommandFetched(const OutboxCompletion &completion)
    {
        std::vector<ModuleCommandDto> moduleCommands;
        const bool haveResponse = (outbox != nullptr) && outbox->takeModuleCommandResponse(moduleCommands);

        if (!completion.delivered)
        {
            // The synchronous path could not tell this from "no commands" - getModuleCommand()
            // returned an empty vector for both. There is no payload fallback for these: a complex
            // command carries its parameters in the list, not in the signal.
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: module command fetch failed after %u attempts - nothing executed",
                (unsigned)completion.attempts);
            return;
        }

        if (!haveResponse || moduleCommands.empty())
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: No module commands received from server");
            return;
        }

        executeComplexModuleCommands(moduleCommands);
    }

    /**
     * @brief Route a fetched command list: the fast parts run here, the slow parts leave the loop.
     *
     * A scan against a silent pin is seconds on the bus, an OTA download is minutes on the
     * network - neither may be control-loop time, so those commands only get a slot here and
     * execute elsewhere. What the backend sees is independent of that routing: the InProgress
     * reports at validation, one batch of outcomes per list in list order, and the structure
     * report after a command that changed the sensors.
     */
    bool executeComplexModuleCommands(const std::vector<ModuleCommandDto> &moduleCommands)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: executing %d module command(s)", (int)moduleCommands.size());

        bool allCommandsSuccessful = true;

        if (!moduleCommandUseCase)
        {
            logError("CommandService: ModuleCommandExecutionUseCase not available");
            return false;
        }

        // Checked before anything is reported: a list refused here leaves no trace on the
        // backend - no InProgress, no outcome - so its commands stay `new` and are fetched
        // again by the next signal, once the queue has drained.
        if (_batches.size() >= MAX_PENDING_BATCHES)
        {
            _droppedBatchCount++;
            logError("CommandService: batch queue full (%u), dropped a list of %u commands; total dropped %u",
                     (unsigned)MAX_PENDING_BATCHES, (unsigned)moduleCommands.size(),
                     (unsigned)_droppedBatchCount);
            return false;
        }

        std::vector<ModuleCommandDto> validCommands = validateAndFilterModuleCommands(moduleCommands, allCommandsSuccessful);
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: validateAndFilter: %d valid of %d, allSuccess=%s",
            (int)validCommands.size(), (int)moduleCommands.size(),
            allCommandsSuccessful ? "true" : "false");

        if (validCommands.empty())
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: No valid module commands to execute (all expired or invalid)");
            return allCommandsSuccessful;
        }

        return runModuleCommandBatch(validCommands, allCommandsSuccessful);
    }

    /**
     * @brief Execute a list that has already passed the gates, as one batch.
     *
     * Split from executeComplexModuleCommands() so that a command the module resumes itself can
     * reach the same pipeline - the same routing, the same batch, the same report to the server -
     * without going through the age gate a second time. That gate is about a list arriving late
     * from the server; a resumed update was gated when it arrived and has since survived a
     * restart, which is precisely what the gate would punish it for.
     */
    bool runModuleCommandBatch(const std::vector<ModuleCommandDto> &validCommands,
                               bool allCommandsSuccessful)
    {
        _batches.emplace_back();
        PendingBatch &batch = _batches.back();
        // One allocation, straight to the list's size - resize on a fresh vector never doubles.
        batch.slots.resize(validCommands.size());

        for (size_t i = 0; i < validCommands.size(); i++)
        {
            const ModuleCommandDto &command = validCommands[i];
            PendingSlot &slot = batch.slots[i];

            if (isSlowModuleCommand(command.command))
            {
                routeComplexCommand(command, slot);
                continue;
            }

            // Local commands - BlinkingStart/Stop, Reboot, DeleteSensors, AddWifi - are state
            // writes and GPIO, no waiting. They stay on the loop, executed as they always were.
            slot.commandName = command.command;
            std::vector<ModuleCommandExecutionResult> results =
                moduleCommandUseCase->executeCommands({command});
            slot.done = true;
            if (!results.empty())
            {
                slot.result = results[0];
            }
            else
            {
                slot.result = ModuleCommandExecutionResult(
                    command.uid, getModuleCommandStatusName(ModuleCommandStatus::ERROR),
                    "Unknown command");
            }
        }

        finalizeBatches();
        return allCommandsSuccessful;
    }

    //TODO: there is no such command type, deside use or not or delete
    bool processSensorTypeCommand(const CommandDto &command)
    {
        log(DebugFeatures::SETUP_SENSOR_DEBUG,
            "CommandService: Processing sensor type command - sensorUid: %s, type: %s, model: %s, command: %s",
            command.sensorUid.c_str(), command.sensorType.c_str(), command.model.c_str(), command.command.c_str());

        CommandResultDto result("accepted", "SensorType");
        reportCommandResult(result, ApiPriority::UNDEFINED);

        SensorEntity sensor = createSensorFromCommand(command);

        if (sensor.uid.empty() || sensor.sensorType.empty())
        {
            std::string errorMsg = "Invalid sensor data in command - missing UID or sensor type";
            logError("CommandService: %s", errorMsg.c_str());
            
            CommandResultDto errorResult("error", "SensorType", errorMsg);
            reportCommandResult(errorResult, ApiPriority::UNDEFINED);

            return false;
        }

        if (sensorCommandUseCase)
        {
            SensorCommandResult commandResult = sensorCommandUseCase->execute(sensor);

            log(DebugFeatures::SETUP_SENSOR_DEBUG,
                "CommandService: Sensor command result - success: %s, status: %s, message: %s",
                commandResult.success ? "true" : "false",
                commandResult.status.c_str(),
                commandResult.message.c_str());

            CommandResultDto finalResult;
            if (commandResult.success)
            {
                if (commandResult.status == "warning")
                {
                    finalResult = CommandResultDto("warning", "SensorType", commandResult.message);
                }
                else
                {
                    finalResult = CommandResultDto("done", "SensorType", commandResult.message);
                }
                
                log(DebugFeatures::SETUP_SENSOR_DEBUG,
                    "CommandService: Sensor type command executed successfully - %s", commandResult.message.c_str());
            }
            else
            {
                finalResult = CommandResultDto("error", "SensorType", commandResult.message);
                logError("CommandService: Failed to execute sensor type command - %s", commandResult.message.c_str());
            }

            reportCommandResult(finalResult, ApiPriority::UNDEFINED);

            return commandResult.success;
        }
        else
        {
            std::string errorMsg = "SensorCommandUseCase not available";
            logError("CommandService: %s", errorMsg.c_str());
            
            CommandResultDto errorResult("error", "SensorType", errorMsg);
            reportCommandResult(errorResult, ApiPriority::UNDEFINED);

            return false;
        }
    }

    bool processDeleteModuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Processing delete module command - commandUid: %s, moduleUid: %s",
            command.commandUid.c_str(), command.growDirectorUid.c_str());

        // Before the acknowledgement: an accepted command must get a slot, so a command that
        // cannot get one is refused up front rather than accepted and forgotten.
        if (_batches.size() >= MAX_PENDING_BATCHES)
        {
            _droppedBatchCount++;
            logError("CommandService: batch queue full (%u), refused a DeleteModule command; total dropped %u",
                     (unsigned)MAX_PENDING_BATCHES, (unsigned)_droppedBatchCount);
            reportCommandResult(CommandResultDto("error", "DeleteModule", "Command queue full"),
                                ApiPriority::MQTT_PREFERRED);
            return false;
        }

        // Send immediate acknowledgment
        CommandResultDto acceptedResult("accepted", "DeleteModule");
        reportCommandResult(acceptedResult, ApiPriority::MQTT_PREFERRED);

        // Create module command DTO for execution
        ModuleCommandDto moduleCommand;
        moduleCommand.command = "DeleteModule";
        moduleCommand.uid = command.commandUid;
        moduleCommand.createdAt = command.createdAt;
        moduleCommand.options = "{}"; // No options needed for delete

        if (!moduleCommandUseCase)
        {
            logError("CommandService: ModuleCommandUseCase not available for delete command");

            CommandResultDto errorResult("error", "DeleteModule", "Module command service not available");
            reportCommandResult(errorResult, ApiPriority::MQTT_PREFERRED);

            return false;
        }

        // The verification is a REST call the server may spend ninety seconds not answering, so
        // it runs on the network command task. The final acknowledgement goes out when the
        // verdict is back - reportDirectDeleteModule(), with the same statuses as before.
        _batches.emplace_back();
        PendingBatch &batch = _batches.back();
        batch.direct = true;
        batch.slots.resize(1);
        routeComplexCommand(moduleCommand, batch.slots[0]);

        finalizeBatches();
        return true;
    }

private:
    SensorEntity createSensorFromCommand(const CommandDto& command)
    {
        SensorEntity sensor;
        sensor.uid = command.sensorUid;
        sensor.sensorType = command.sensorType;
        sensor.model = command.model;
        sensor.createdAt = command.createdAt;
        sensor.isActive = true;
        sensor.isError = false;

        if (!command.options.empty())
        {
            parseSensorOptions(command.options, sensor);
        }

        log(DebugFeatures::SETUP_SENSOR_DEBUG,
            "CommandService: Created sensor entity - UID: %s, Type: %s, Model: %s, Protocol: %s",
            sensor.uid.c_str(), sensor.sensorType.c_str(), sensor.model.c_str(), sensor.protocol.c_str());

        return sensor;
    }

    void parseSensorOptions(const std::string& optionsJson, SensorEntity& sensor)
    {
        sensor.protocol = extractJsonValue(optionsJson, "protocol");
        if (sensor.protocol == "ModBus")
        {
            sensor.modbusDeviceAddress = extractJsonIntValue(optionsJson, "modbusDeviceAddress");
            sensor.baudRate = extractJsonIntValue(optionsJson, "baudRate");
            parseModbusRegisterConfig(optionsJson, sensor);
        }
        else if (sensor.protocol == "I2C")
        {
            sensor.i2cAddress = extractJsonValue(optionsJson, "i2cAddress");
            sensor.chipType = extractJsonValue(optionsJson, "chipType");
            parseI2CRegisterConfig(optionsJson, sensor);
        }

        log(DebugFeatures::SETUP_SENSOR_DEBUG,
            "CommandService: Parsed sensor options - Protocol: %s, Address: %s/%d, ChipType: %s",
            sensor.protocol.c_str(), sensor.i2cAddress.c_str(), sensor.modbusDeviceAddress, sensor.chipType.c_str());
    }

    std::string extractJsonValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return "";
        size_t valueStart = json.find("\"", colonPos);
        if (valueStart == std::string::npos) return "";
        size_t valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd == std::string::npos) return "";
        return json.substr(valueStart + 1, valueEnd - valueStart - 1);
    }

    int extractJsonIntValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return 0;
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return 0;
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && std::isspace(json[valueStart])) valueStart++;
        size_t valueEnd = valueStart;
        while (valueEnd < json.size() && (std::isdigit(json[valueEnd]) || json[valueEnd] == '-')) valueEnd++;
        if (valueEnd > valueStart)
        {
            try
            {
                return std::stoi(json.substr(valueStart, valueEnd - valueStart));
            }
            catch (const std::exception& e)
            {
                logError("CommandService: Failed to parse integer value for key '%s'", key.c_str());
            }
        }
        return 0;
    }

    void parseModbusRegisterConfig(const std::string& json, SensorEntity& sensor)
    {
        std::string key = "\"modbusRegisterConfig\"";
        size_t keyPos = json.find(key);
        if (keyPos == std::string::npos) return;

        size_t arrayStart = json.find('[', keyPos);
        size_t arrayEnd = std::string::npos;
        if (arrayStart != std::string::npos) {
            int depth = 1;
            size_t i = arrayStart + 1;
            while (i < json.size() && depth > 0) {
                if (json[i] == '[') depth++;
                else if (json[i] == ']') depth--;
                i++;
            }
            if (depth == 0) arrayEnd = i;
        }
        if (arrayStart == std::string::npos || arrayEnd == std::string::npos) return;

        std::string arrayJson = json.substr(arrayStart, arrayEnd - arrayStart);

        if (jsonConverter) {
            std::vector<ModbusRegisterConfig> configs;
            if (jsonConverter->modbusRegisterConfigsFromJson(arrayJson, configs)) {
                sensor.modbusRegisterConfigs = configs;
            }
        }
    }

    void parseI2CRegisterConfig(const std::string& json, SensorEntity& sensor)
    {
        std::string key = "\"i2cRegisterConfig\"";
        size_t keyPos = json.find(key);
        if (keyPos == std::string::npos) return;

        size_t arrayStart = json.find('[', keyPos);
        size_t arrayEnd = std::string::npos;
        if (arrayStart != std::string::npos) {
            int depth = 1;
            size_t i = arrayStart + 1;
            while (i < json.size() && depth > 0) {
                if (json[i] == '[') depth++;
                else if (json[i] == ']') depth--;
                i++;
            }
            if (depth == 0) arrayEnd = i;
        }
        if (arrayStart == std::string::npos || arrayEnd == std::string::npos) return;

        std::string arrayJson = json.substr(arrayStart, arrayEnd - arrayStart);

        if (jsonConverter) {
            std::vector<I2CRegisterConfig> configs;
            if (jsonConverter->i2cRegisterConfigsFromJson(arrayJson, configs)) {
                sensor.i2cRegisterConfigs = configs;
            }
        }
    }

    void log(const char *feature, const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(feature) && logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* debugFeature = debugSystem->getFeature(feature);
            if (debugFeature)
            {
                logger->println(feature, buffer, debugFeature->getLogChannel());
            }
            else
            {
                logger->println(feature, buffer);
            }
        }
    }

    void logError(const char *format, ...)
    {
        if (logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println("ERROR", buffer);
        }
    }

    void logInfo(const char *format, ...)
    {
        if (logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println("INFO", buffer);
        }
    }
};
