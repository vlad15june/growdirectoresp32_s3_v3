#pragma once

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <vector>

#include "Application/Interfaces/INetworkCommandRunner.h"
#include "Application/Services/BackgroundActs.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"

/**
 * @brief One task that waits on the network for module commands, so that the control loop does not.
 *
 * Same shape as the outbox network task and the acquisition task, because it answers the same
 * question for a third kind of wait. The outbox carries the firmware's own periodic traffic; this
 * task carries the waits a *command* asks for - the OTA image download, the deletion check -
 * which have no business inside the outbox's retry budget and no place on the control loop.
 *
 * ### The watchdog
 *
 * The task subscribes its own watchdog entry and feeds it around every job, exactly like the
 * outbox task. During an OTA download `Update.onProgress` calls `esp_task_wdt_reset()`, which
 * feeds *the task that is downloading* - this one. So liveness is vouched for by the task
 * actually making progress, and every task answers for itself: the download feeds the
 * downloader's entry, the control loop feeds its own, and neither can cover for the other. The
 * download must keep feeding while it progresses - an image takes longer than the watchdog
 * period, and a healthy transfer must not be killed for being large; a *stalled* transfer stops
 * feeding and the watchdog fires.
 */
class NetworkCommandRunner : public INetworkCommandRunner
{
public:
    /** Name of the command network task. */
    static constexpr const char* TASK_NAME = "cmd-net";

    /**
     * Stack: same as the outbox network task. The deepest path is the OTA download - HTTPClient
     * over TLS - which is the same TLS stack the outbox's REST calls already run on 8 KB.
     */
    static constexpr uint32_t DEFAULT_STACK_BYTES = 8192;

    /** How long the task sleeps when there is nothing to run. */
    static constexpr uint32_t IDLE_POLL_MS = 100;

    NetworkCommandRunner(ITaskScheduler* scheduler,
                         ILogger* logger = nullptr,
                         IDebugSystem* debugSystem = nullptr,
                         uint32_t stackBytes = DEFAULT_STACK_BYTES,
                         uint8_t priority = 1)
        : _scheduler(scheduler),
          _logger(logger),
          _debugSystem(debugSystem),
          _stackBytes(stackBytes),
          _priority(priority)
    {
        // Allocated once, to the realistic burst rather than the queue cap - see the constant.
        // drainResults() swaps this vector with the consumer's reserved drain buffer, so the
        // two buffers circulate between the sides and neither grows again in steady state.
        _results.reserve(RESULT_BUFFER_RESERVE);
    }

    /** @brief The use case that knows how to run the network part of a command. */
    void setExecutor(IModuleCommandExecutionUseCase* executor) { _executor = executor; }

    /**
     * @brief Where this task says what it is doing: which kind of network work a command asked for.
     *
     * Optional, and observation only - one store at each boundary of an act, read by whoever has
     * to name the work a heap low fell on. Nothing here depends on it.
     */
    void setActBoard(BackgroundActBoard* acts) { _acts = acts; }

    // --- INetworkCommandRunner -----------------------------------------------------------------

    uint32_t submit(const ComplexCommandWork& work) override
    {
        ensureTaskStarted();
        if (!_taskStarted)
        {
            return kRejected;
        }

        std::lock_guard<std::mutex> guard(_commandMutex);

        if (_commands.size() >= MAX_PENDING_COMMANDS)
        {
            _refusedCount++;
            return kRejected;
        }

        _lastTicket++;
        if (_lastTicket == kRejected)
        {
            _lastTicket = 1;
        }

        Job job;
        job.ticket = _lastTicket;
        job.work = work;
        _commands.push_back(job);
        return job.ticket;
    }

    bool drainResults(std::vector<NetworkCommandCompletion>& out) override
    {
        std::lock_guard<std::mutex> guard(_resultMutex);
        if (_results.empty())
        {
            return false;
        }
        out.swap(_results);
        _results.clear();
        // A no-op while the reserved pair circulates; restores the reservation when a caller
        // swapped in a smaller buffer and carried the capacity away.
        _results.reserve(RESULT_BUFFER_RESERVE);
        return true;
    }

    // --- Observation ---------------------------------------------------------------------------

    bool taskStarted() const { return _taskStarted; }
    uint32_t taskStartFailureCount() const { return _taskStartFailureCount; }
    uint32_t refusedCount() const { return _refusedCount; }
    uint64_t executedCount() const { return _executedCount; }

    /**
     * @brief The task body, exposed so a test can drive it without owning a scheduler.
     */
    void run()
    {
        if (_scheduler != nullptr)
        {
            _scheduler->watchdogSubscribeCurrentTask();
        }

        while (true)
        {
            if (_scheduler != nullptr)
            {
                _scheduler->watchdogFeed();
            }

            if (!serveOneCommand())
            {
                if (_scheduler == nullptr)
                {
                    return;
                }
                _scheduler->delayMs(IDLE_POLL_MS);
            }
        }
    }

private:
    struct Job
    {
        uint32_t ticket = 0;
        ComplexCommandWork work;
    };

    void ensureTaskStarted()
    {
        if (_taskStarted || _scheduler == nullptr)
        {
            return;
        }

        // Started on first use rather than in the composition root, like the outbox task: modules
        // that never receive a network command should not pay for the stack. The flag is set from
        // the result so that a creation refused for want of heap is retried on the next command.
        if (_scheduler->createTask(TASK_NAME, _stackBytes, _priority, [this]() { run(); }))
        {
            _taskStarted = true;
            return;
        }

        _taskStartFailureCount++;
        log("NetworkCommandRunner: could not create the task (%u bytes of stack); failures so far %u",
            (unsigned)_stackBytes, (unsigned)_taskStartFailureCount);
    }

    bool serveOneCommand()
    {
        Job job;

        // The lock covers extraction only. Execution below runs unlocked - a download
        // takes minutes, and submit() on the control loop must never wait behind it.
        {
            std::lock_guard<std::mutex> guard(_commandMutex);
            if (_commands.empty())
            {
                return false;
            }
            job = _commands.front();
            _commands.pop_front();
        }   // lock released before execution starts

        // Once per command, and a command is seconds to minutes of network: the comparison of
        // the command's name is nothing beside what follows it.
        const uint8_t act = actOf(job.work);
        if (_acts != nullptr)
        {
            _acts->actStarted(BackgroundActBoard::COMMAND_NETWORK, act);
        }

        NetworkCommandCompletion completion;
        completion.ticket = job.ticket;
        completion.work = job.work;

        if (_executor != nullptr)
        {
            completion.outcome = _executor->executeNetworkWork(job.work);
        }
        else
        {
            // A slot is never orphaned: every accepted command produces exactly one completion.
            // Answered rather than left pending: the caller holds a report slot open for every
            // command it accepted, and silence here would leave it open forever.
            completion.outcome.status = ModuleCommandStatus::ERROR;
            completion.outcome.errorMessage = "Network command executor not available";
        }

        _executedCount++;

        {
            std::lock_guard<std::mutex> guard(_resultMutex);
            _results.push_back(completion);
        }

        if (_acts != nullptr)
        {
            _acts->actFinished(BackgroundActBoard::COMMAND_NETWORK, act);
        }
        return true;
    }

    /** @brief The kind of network work, as executeNetworkWork() tells the kinds apart. */
    static uint8_t actOf(const ComplexCommandWork& work)
    {
        if (work.fetchCatalog)
        {
            return BackgroundActs::CMD_CATALOG_FETCH;
        }
        switch (getModuleCommandEnum(work.command.command))
        {
        case ModuleCommandEnum::UPDATE_FIRMWARE:
            return BackgroundActs::CMD_FIRMWARE_UPDATE;
        case ModuleCommandEnum::DELETE_MODULE:
            return BackgroundActs::CMD_DELETION_CHECK;
        default:
            return BackgroundActs::CMD_OTHER;
        }
    }

    void log(const char* format, ...)
    {
        if (_logger == nullptr)
        {
            return;
        }
        char buffer[192];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _logger->println("INFO", buffer);
    }

    ITaskScheduler* _scheduler;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    uint32_t _stackBytes;
    uint8_t _priority;

    IModuleCommandExecutionUseCase* _executor = nullptr;
    BackgroundActBoard* _acts = nullptr;

    // Two locks because these are two independent flows: the loop feeds _commands, the task
    // feeds _results. Neither side ever holds both, so the pair cannot deadlock.
    std::mutex _commandMutex;
    std::deque<Job> _commands;
    uint32_t _lastTicket = 0;
    uint32_t _refusedCount = 0;

    std::mutex _resultMutex;
    std::vector<NetworkCommandCompletion> _results;

    bool _taskStarted = false;
    uint32_t _taskStartFailureCount = 0;
    uint64_t _executedCount = 0;
};
