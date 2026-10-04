#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <utility>
#include <vector>

#include "Application/Interfaces/ISensorTypeCatalogWriter.h"
#include "Application/Services/BackgroundActs.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"

/**
 * @brief A task that stores the sensor type catalog, so that the control loop does not.
 *
 * Answers the same question as the outbox network task and the command network task for a third
 * kind of wait: this one waits for flash. Storing 14 sensor types is ~30 file operations - a chunk
 * per type into a temporary file, then the renames that publish it - and a SPIFFS operation costs
 * whatever the file system decides to spend, up to tens of milliseconds when it collects garbage
 * inside a write.
 *
 * Unlike those two, the task exists only while there is a write to do. The catalog is stored when
 * a fetch finds it changed on the server - the fetch at a boot or at a change of local IP, one
 * re-armed after a failed synchronization, or the one a ScanSensors command makes on a module
 * without a catalog - a few times a year, and a task that stayed would hold
 * its stack, which on the device is heap, for the rest of the boot to write nothing. So the first
 * write creates the task, the task writes whatever is queued and ends when the queue is empty,
 * and the next write creates it again. Whether a task is
 * alive is decided under the same lock as the queue: the task ends only having seen the queue empty
 * under it, and a submission that finds no task alive queues its write and creates one - so a write
 * is never queued behind a task that has already decided to end.
 *
 * The DAO is driven from here and from nowhere else while a write is in flight. That is what makes
 * the arrangement safe without a lock around the file: the catalog the control loop reads comes from
 * the repository's cache, which stays valid until the loop itself drops it, and the loop drops it
 * only when it collects the outcome of the write - by which time this task is finished with the
 * file. A module with no catalog has no valid cache to read from, so its repository is told the
 * write is in flight for as long as it is, and answers from what it holds instead of reading
 * storage (ISensorTypeRepository::setStorageWriteInFlight()).
 *
 * ### The watchdog
 *
 * The task subscribes its own watchdog entry and feeds it around every job, like the other
 * background tasks: liveness is vouched for by the task that is making progress, and a task that
 * hangs inside a file operation is visible as itself rather than as a healthy control loop. It
 * takes the entry off again before it ends: deleting a task does not, and an entry nobody feeds
 * fires the watchdog.
 */
class SensorTypeCatalogWriter : public ISensorTypeCatalogWriter
{
public:
    /** Name of the catalog storage task. */
    static constexpr const char* TASK_NAME = "catalog-io";

    /**
     * The most stack this task was measured to use on the board: 3 040 bytes, the 8 192 it then had
     * less the 5 152 the stack_catalog_io column reported as never touched (the least free stack
     * over the task's life, which held one write of a 12-type catalog). The storage debug lines
     * were on, so the logging path is in the figure.
     */
    static constexpr uint32_t MEASURED_STACK_USE_BYTES = 3040;

    /**
     * Stack: twice the measured use - 6 080 - rounded up to 6 144. The margin is for the paths that
     * write did not take, measured by nobody: a refused write and its logging, and the C++
     * exception the DAO catches around the serialization. The stack is only held while a write
     * runs; a stack too small for a path ends in a panic and a reboot, and the reboot finds the same
     * catalog waiting to be written - so the margin is not cut further.
     */
    static constexpr uint32_t DEFAULT_STACK_BYTES = 6144;
    static_assert(DEFAULT_STACK_BYTES >= 2 * MEASURED_STACK_USE_BYTES,
                  "the storage task's stack must keep twice the measured use");

    SensorTypeCatalogWriter(ISpiffsSensorTypeDao* dao,
                            ITaskScheduler* scheduler,
                            ILogger* logger = nullptr,
                            IDebugSystem* debugSystem = nullptr,
                            uint32_t stackBytes = DEFAULT_STACK_BYTES,
                            uint8_t priority = 1)
        : _dao(dao),
          _scheduler(scheduler),
          _logger(logger),
          _debugSystem(debugSystem),
          _stackBytes(stackBytes),
          _priority(priority)
    {
        // Both buffers hold the hard cap from the start: neither side grows a vector while the
        // other one is writing flash.
        _pending.reserve(MAX_PENDING_WRITES);
        _results.reserve(MAX_PENDING_WRITES);
    }

    /**
     * @brief Where this task says it is writing a catalog.
     *
     * Optional, and observation only - one store at each boundary of a write, read by whoever has
     * to name the work a heap low fell on. Nothing here depends on it.
     */
    void setActBoard(BackgroundActBoard* acts) { _acts = acts; }

    // --- ISensorTypeCatalogWriter ------------------------------------------------------------

    uint32_t submit(std::vector<SensorType> catalog) override
    {
        std::lock_guard<std::mutex> guard(_mutex);

        // Results count towards the cap because every accepted write produces one: a slot is
        // reserved for the answer at the moment the question is accepted, so an accepted write can
        // never be left without an outcome to report.
        if (_pending.size() + _results.size() + (_running ? 1u : 0u) >= MAX_PENDING_WRITES)
        {
            _refusedCount++;
            return kRejected;
        }

        // No task, no write: a write queued with nothing to take it would never be answered. The
        // task is created under the lock it ends under, so it cannot be ending meanwhile, and it
        // cannot look at the queue before the write is in it.
        if (!_taskAlive && !startTask())
        {
            return kRejected;
        }

        _lastTicket++;
        if (_lastTicket == kRejected)
        {
            _lastTicket = 1;
        }

        Job job;
        job.ticket = _lastTicket;
        job.catalog = std::move(catalog);
        _pending.push_back(std::move(job));
        return _lastTicket;
    }

    bool drainResults(std::vector<CatalogWriteCompletion>& out) override
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_results.empty())
        {
            return false;
        }
        for (size_t i = 0; i < _results.size(); i++)
        {
            out.push_back(_results[i]);
        }
        _results.clear();
        return true;
    }

    bool takeStoredCatalog(uint32_t ticket, std::vector<SensorType>& out) override
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_storedTicket == kRejected || _storedTicket != ticket)
        {
            return false;
        }
        out.swap(_storedCatalog);
        _storedCatalog.clear();
        _storedTicket = kRejected;
        return true;
    }

    // --- Observation ---------------------------------------------------------------------------

    /** @brief Whether a storage task exists right now: a write is queued or being written. */
    bool taskRunning()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _taskAlive;
    }
    /** @brief How many storage tasks were created - one per run of writes. */
    uint32_t taskStartCount() const { return _taskStartCount; }
    uint32_t taskStartFailureCount() const { return _taskStartFailureCount; }
    uint32_t refusedCount() const { return _refusedCount; }
    uint64_t writtenCount() const { return _writtenCount; }

    /**
     * @brief The task body: every queued write, then the end of the task.
     */
    void run()
    {
        if (_scheduler != nullptr)
        {
            _scheduler->watchdogSubscribeCurrentTask();
        }

        do
        {
            if (_scheduler != nullptr)
            {
                _scheduler->watchdogFeed();
            }
        } while (serveOneWrite());

        // The queue was found empty and the task marked ended, under the lock: from here on a
        // submission creates a new task, and nothing here touches the queue again.
        if (_scheduler != nullptr)
        {
            _scheduler->watchdogUnsubscribeCurrentTask();
        }
    }

private:
    struct Job
    {
        uint32_t ticket = 0;
        std::vector<SensorType> catalog;
    };

    /** Called with _mutex held, and only when no task is alive. */
    bool startTask()
    {
        if (_scheduler == nullptr)
        {
            return false;
        }

        // A module whose catalog never changes never pays for the stack, and one whose catalog
        // does pays for it while the write lasts. A creation refused for want of heap refuses the
        // write, counted and logged here; the submitter takes it for a failed synchronization, and
        // the next write - the next synchronization's - tries to create the task again.
        if (_scheduler->createTask(TASK_NAME, _stackBytes, _priority, [this]() { run(); }))
        {
            _taskAlive = true;
            _taskStartCount++;
            return true;
        }

        _taskStartFailureCount++;
        log("SensorTypeCatalogWriter: could not create the task (%u bytes of stack); failures so far %u",
            (unsigned)_stackBytes, (unsigned)_taskStartFailureCount);
        return false;
    }

    bool serveOneWrite()
    {
        Job job;

        // The lock covers extraction only. The write below runs unlocked - it is the slow thing
        // this class exists for - and submit() on the control loop must never wait behind it.
        {
            std::lock_guard<std::mutex> guard(_mutex);
            if (_pending.empty())
            {
                // Decided here, under the lock submit() queues under: a write queued after this
                // finds no task alive and creates one.
                _taskAlive = false;
                return false;
            }
            job = std::move(_pending.front());
            _pending.erase(_pending.begin());
            _running = true;
            // A catalog nobody collected belongs to a write that is over; it is not kept across
            // the next one.
            _storedCatalog.clear();
            _storedTicket = kRejected;
        }   // lock released before the write starts

        if (_acts != nullptr)
        {
            _acts->actStarted(BackgroundActBoard::CATALOG_STORAGE, BackgroundActs::CATALOG_WRITE);
        }

        CatalogWriteCompletion completion;
        completion.ticket = job.ticket;
        completion.typeCount = job.catalog.size();

        if (_dao != nullptr)
        {
            completion.written = _dao->saveAll(job.catalog);
            completion.error = _dao->getLastSaveError();
            completion.bytes = _dao->getLastSaveBytes();
        }
        else
        {
            // Answered rather than left pending: the caller holds the catalog it submitted until
            // the outcome comes back, and silence here would hold it forever.
            completion.written = false;
            completion.error = STORAGE_WRITE_ERROR_OPEN;
        }

        _writtenCount++;

        {
            std::lock_guard<std::mutex> guard(_mutex);
            _results.push_back(completion);
            if (completion.written)
            {
                // Held for the submitter to collect: these types are what the file holds now, so
                // whoever caches the catalog can take this copy instead of reading the file back.
                // Only a write that landed leaves one - a snapshot that never reached flash would
                // be a cache no reboot could reproduce.
                _storedCatalog = std::move(job.catalog);
                _storedTicket = completion.ticket;
            }
            _running = false;
        }

        if (_acts != nullptr)
        {
            _acts->actFinished(BackgroundActBoard::CATALOG_STORAGE, BackgroundActs::CATALOG_WRITE);
        }
        return true;
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

    ISpiffsSensorTypeDao* _dao;
    ITaskScheduler* _scheduler;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    uint32_t _stackBytes;
    uint8_t _priority;
    BackgroundActBoard* _acts = nullptr;

    // One lock for both queues and for the task's life: unlike the network runner, the two sides
    // here are the same two tasks taking turns over one file, and the critical sections are a move,
    // a push, and the creation of the task.
    std::mutex _mutex;
    std::vector<Job> _pending;
    std::vector<CatalogWriteCompletion> _results;
    // The catalog of the last write that landed, waiting for its submitter to take it back.
    std::vector<SensorType> _storedCatalog;
    uint32_t _storedTicket = kRejected;
    bool _running = false;
    uint32_t _lastTicket = 0;
    uint32_t _refusedCount = 0;

    // From the creation of the task to the moment it finds the queue empty.
    bool _taskAlive = false;
    uint32_t _taskStartCount = 0;
    uint32_t _taskStartFailureCount = 0;
    uint64_t _writtenCount = 0;
};
