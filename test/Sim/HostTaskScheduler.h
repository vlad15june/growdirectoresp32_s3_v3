#pragma once

#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"
#include "Sim/HostAllocationCounter.h"
#include "Sim/HostFiber.h"
#include "Sim/VirtualClock.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hostsim {

/**
 * @brief Host implementation of ITaskScheduler: cooperative, deterministic, virtual-time.
 *
 * Not std::thread. Real threads would make the order in which tasks observe each other depend
 * on the OS scheduler, which turns a race into a test that fails once every few hundred runs -
 * exactly the kind of test a team learns to ignore. Here the scheduler picks the next task
 * itself, so a run is a function of the test and nothing else, and a race can be provoked on
 * purpose by choosing where tasks yield.
 *
 * Policy: among the tasks whose wake time has arrived, the highest priority runs; ties go
 * round-robin in creation order. When no task is ready, time jumps straight to the earliest
 * wake time - nothing waits in real time.
 */
class HostTaskScheduler : public ITaskScheduler {
public:
    /**
     * @brief Least stack a fiber gets, however few bytes the firmware asked for.
     *
     * The stack size a task is created with is a device measurement: Xtensa frames at -Os, where
     * the largest frame in the whole firmware image is 240 bytes. A host fiber runs the same
     * functions as x86-64 frames at -O0, where every temporary gets a slot of its own, and one
     * call of ArduinoJsonConverter::sensorTypeToJson - fifty ArduinoJson expressions in a single
     * function - measures 21 968 bytes under clang and 10 128 under gcc 11. Handing such a task
     * the 8192 bytes the device gives it does not fail as a refusal: the fiber writes past its
     * stack into the heap behind it, and the run dies somewhere else entirely.
     *
     * So the host stack is sized by the host's own frames, and the floor is what the test bench
     * gives - not what the firmware asked for and not anything the firmware can see. The number
     * below is an order of magnitude over the largest frame measured, which leaves room for those
     * frames to nest.
     *
     * The fiber's bytes are not metered at all for the same reason (Sim/HostFiber: its stack is a
     * raw allocation): their size is a decision of this file, so a census or a peak that counted
     * them would report a stack the board does not have. What the board does have - the stack the
     * firmware asked for - is kept per task and read with liveStackBytes().
     */
    static const uint32_t kMinimumFiberStackBytes = 256 * 1024;

    /**
     * @brief Task slots taken up front, so that creating a task allocates no slot.
     *
     * A task that ends leaves its slot empty and a new one takes the next slot: indices never move,
     * because currentTaskIndex() is an identity (Sim/HostBusLock.h). Taken at construction, the
     * slots keep a window measured around a whole task life - created, run, ended - free of the
     * bench's own growth.
     */
    static const size_t kReservedTaskSlots = 32;

    explicit HostTaskScheduler(VirtualClock& clock)
        : _clock(clock)
    {
        _tasks.reserve(kReservedTaskSlots);
    }

    // --- ITaskScheduler -------------------------------------------------------------------

    bool createTask(const char* name, uint32_t stackBytes, uint8_t priority, TaskBody body) override
    {
        if (name != nullptr && _refusedName == name && _refusalsOfTheNameLeft > 0)
        {
            // The same refusal, for one task only: the heap that cannot spare one task's stack.
            _refusalsOfTheNameLeft--;
            _refusedTaskCount++;
            return false;
        }

        if (_createTaskFailuresLeft > 0)
        {
            // Modelling the one failure FreeRtosTaskScheduler can actually report:
            // xTaskCreatePinnedToCore() returning pdFAIL because the heap cannot spare the stack.
            // Without this the host could never exercise what a caller does with a refusal.
            _createTaskFailuresLeft--;
            _refusedTaskCount++;
            return false;
        }

        std::unique_ptr<Task> task(new Task());
        task->name = (name != nullptr) ? name : "";
        task->priority = priority;
        task->body = std::move(body);
        task->wakeAtUs = _clock.nowUs();
        task->stackBytes = stackBytes;
        task->serial = ++_lastTaskSerial;
        HostAllocationCensus::initContext(task->census, censusOwnerFor(task->name));
        {
            HostAllocationCensus::Scope harnessStack(HostAllocationCensus::kBench);
            task->fiber.reset(new Fiber(&HostTaskScheduler::taskEntry, task.get(),
                                        stackBytes < kMinimumFiberStackBytes
                                            ? kMinimumFiberStackBytes
                                            : stackBytes));
            _createdByName[task->name]++;
        }
        _tasks.push_back(std::move(task));

        if (_newTasksStartAtOnce)
        {
            runTheNewTaskUntilItWaits(_tasks.size() - 1);
        }
        return true;
    }

    /**
     * @brief Let every task created from now on run at once, until it first waits, before its
     *        creator goes on.
     *
     * The board creates the network and bus tasks on the other core: a task the control loop
     * creates starts working while the loop is still finishing its iteration, so whatever the new
     * task takes first it takes before anything the loop hands over after creating it. Without this
     * a new task runs only once its creator yields, and a stand sees the other order and nothing
     * else. The two are the two orders the board can produce; a test of a race runs both.
     *
     * Off by default: a stand runs in the cooperative order unless it asks otherwise.
     * Nothing of the task under test changes - only where the scheduler lets it start.
     */
    void setNewTasksStartAtOnce(bool atOnce) { _newTasksStartAtOnce = atOnce; }

    /**
     * @brief Charge everything a named task allocates to a subsystem.
     *
     * Registered by name rather than by handle because the tasks that matter are created by the
     * firmware itself - the outbox and the acquisition service start theirs, and the harness never
     * sees the object. A name may be registered before or after the task exists; a task created
     * later picks the mapping up, and one that already runs is retagged in place.
     */
    void setTaskCensusOwner(const char* name, HostAllocationCensus::Owner owner)
    {
        if (name == nullptr) return;
        _censusOwnerByTask[std::string(name)] = (int)owner;
        for (size_t i = 0; i < _tasks.size(); i++)
        {
            if (_tasks[i] && _tasks[i]->name == name)
            {
                HostAllocationCensus::initContext(_tasks[i]->census, owner);
            }
        }
    }

    void yieldTask() override
    {
        if (_current < 0)
        {
            return;
        }
        _tasks[static_cast<size_t>(_current)]->wakeAtUs = _clock.nowUs();
        Fiber::suspendCurrent();
    }

    void delayMs(uint32_t ms) override
    {
        if (_current < 0)
        {
            // Called from the test body rather than from a task: there is nothing to suspend,
            // so the only faithful thing left is to move time.
            _clock.advanceMs(ms);
            return;
        }
        _tasks[static_cast<size_t>(_current)]->wakeAtUs = _clock.nowUs() + static_cast<uint64_t>(ms) * 1000ULL;
        Fiber::suspendCurrent();
    }

    void watchdogSubscribeCurrentTask() override
    {
        _watchdogSubscriptions++;
        if (_current >= 0)
        {
            const Task& task = *_tasks[static_cast<size_t>(_current)];
            if (!subscribedEver(task.name))
            {
                _watchdogSubscribedTaskNames.push_back(task.name);
            }
            // Subscribing counts as the first feed, as esp_task_wdt_add() does: the gap a
            // watchdog model measures starts at the moment the task put itself under watch.
            _lastFeedAtUsByTask[task.name] = _clock.nowUs();
            // esp_task_wdt_add() refuses a task that is already on the list and leaves its entry
            // as it was. The list is the bench's own record, and is charged to the bench.
            if (findWatchdogEntry(task.serial) == nullptr)
            {
                HostAllocationCensus::Scope bookkeeping(HostAllocationCensus::kBench);
                WatchdogEntry entry;
                entry.taskSerial = task.serial;
                entry.taskName = task.name;
                entry.lastFeedAtUs = _clock.nowUs();
                _watchdogEntries.push_back(entry);
            }
        }
    }

    /**
     * Takes the calling task's own entry off the list, as esp_task_wdt_delete(NULL) does. Another
     * task of the same name keeps its entry: the watchdog lists tasks, not names.
     */
    void watchdogUnsubscribeCurrentTask() override
    {
        if (_current < 0)
        {
            return;
        }
        const uint64_t serial = _tasks[static_cast<size_t>(_current)]->serial;
        for (size_t i = 0; i < _watchdogEntries.size(); i++)
        {
            if (_watchdogEntries[i].taskSerial == serial)
            {
                _watchdogEntries.erase(_watchdogEntries.begin() + static_cast<std::ptrdiff_t>(i));
                _watchdogUnsubscriptions++;
                return;
            }
        }
    }

    void watchdogFeed() override
    {
        _watchdogFeeds++;
        // Attributed to the task that made the call, because that is what the ESP-IDF task
        // watchdog does: esp_task_wdt_reset() vouches for the current task and for nobody else.
        // A test that must tell "the loop fed itself" from "something fed on the loop's stack"
        // reads these per-task counts.
        _feedsByTask[currentTaskName()]++;
        _lastFeedAtUsByTask[currentTaskName()] = _clock.nowUs();
        if (_current >= 0)
        {
            WatchdogEntry* entry = findWatchdogEntry(_tasks[static_cast<size_t>(_current)]->serial);
            if (entry != nullptr)
            {
                entry->lastFeedAtUs = _clock.nowUs();
            }
        }
    }

    // --- Host-only controls ---------------------------------------------------------------

    /** @brief How many tasks put themselves under the watchdog. */
    uint32_t watchdogSubscriptions() const { return _watchdogSubscriptions; }

    /** @brief How many tasks took themselves off it again. */
    uint32_t watchdogUnsubscriptions() const { return _watchdogUnsubscriptions; }

    /** @brief Names of the tasks that ever did, each once, in the order they first did it. */
    const std::vector<std::string>& watchdogSubscribedTaskNames() const
    {
        return _watchdogSubscribedTaskNames;
    }

    /**
     * @brief One task on the watchdog's list, kept the way esp_task_wdt keeps it.
     *
     * By task and not by name: two tasks created under one name are two entries, and a task
     * whose body returned without unsubscribing stays on the list - deleting a task does not take
     * it off (FreeRTOS tasks.c has no call into the task watchdog) - and is never fed again.
     */
    struct WatchdogEntry
    {
        uint64_t taskSerial = 0;
        std::string taskName;
        uint64_t lastFeedAtUs = 0;
    };

    /** @brief The watchdog's list as it stands: what Sim/SimTaskWatchdog.h checks. */
    const std::vector<WatchdogEntry>& watchdogEntries() const { return _watchdogEntries; }

    /** @brief How many times a task reported progress to the watchdog. */
    uint64_t watchdogFeeds() const { return _watchdogFeeds; }

    /** @brief How many of those feeds were made from the named task's own stack. */
    uint64_t watchdogFeedsBy(const char* taskName) const
    {
        std::map<std::string, uint64_t>::const_iterator it = _feedsByTask.find(taskName);
        return (it == _feedsByTask.end()) ? 0 : it->second;
    }

    /** @brief Virtual time of the named task's most recent feed (or subscription), ms. */
    uint64_t lastWatchdogFeedAtMs(const std::string& taskName) const
    {
        std::map<std::string, uint64_t>::const_iterator it = _lastFeedAtUsByTask.find(taskName);
        return (it == _lastFeedAtUsByTask.end()) ? 0 : it->second / 1000ULL;
    }


    /** @brief Run tasks until virtual time reaches untilMs, or until no task is left. */
    void runUntilMs(uint64_t untilMs)
    {
        const uint64_t untilUs = untilMs * 1000ULL;
        _stopRequested = false;
        _budgetExhausted = false;

        uint64_t steps = 0;

        while (!_stopRequested && _clock.nowUs() < untilUs)
        {
            if (++steps > _stepBudget)
            {
                // A task that yields without ever letting time move would spin here forever.
                // Reporting an exhausted budget lets the test say so; hanging would not.
                _budgetExhausted = true;
                return;
            }

            const int next = pickReadyTask();
            if (next < 0)
            {
                const uint64_t wake = earliestWakeUs();
                if (wake == kNoWake)
                {
                    // Every task has finished; time still belongs to the test.
                    _clock.advanceToUs(untilUs);
                    return;
                }
                _clock.advanceToUs(wake > untilUs ? untilUs : wake);
                continue;
            }

            _current = next;
            _lastRun = next;
            _contextSwitches++;
            // The tag stack follows the task, not the process: a task can be suspended inside a
            // measured scope, and without the swap that scope would go on charging its owner
            // while somebody else's code ran.
            Task& task = *_tasks[static_cast<size_t>(next)];
            HostAllocationCensus::swapContext(task.census);
            task.fiber->resume();
            HostAllocationCensus::swapContext(task.census);
            _current = -1;

            if (task.fiber->finished())
            {
                // A body that returned ends its task, and what the RTOS gave the task goes back:
                // FreeRTOS deletes a task that deletes itself from the idle task, stack and
                // control block both (tasks.c, prvCheckTasksWaitingTermination/prvDeleteTCB). Its
                // watchdog entry is not part of that and stays where it is.
                _tasks[static_cast<size_t>(next)].reset();
            }
        }

        _clock.advanceToUs(untilUs);
    }

    void runForMs(uint64_t ms) { runUntilMs(_clock.nowMs() + ms); }

    /** @brief Ask runUntilMs() to return after the current task suspends. */
    void requestStop() { _stopRequested = true; }

    size_t taskCount() const { return _tasks.size(); }
    uint64_t contextSwitches() const { return _contextSwitches; }

    /** @brief Whether a task of this name exists: created, and its body has not returned. */
    bool isTaskAlive(const char* name) const { return liveTaskCount(name) > 0; }

    /** @brief How many tasks of this name exist right now. */
    size_t liveTaskCount(const char* name) const
    {
        size_t count = 0;
        for (size_t i = 0; i < _tasks.size(); i++)
        {
            if (_tasks[i] && name != nullptr && _tasks[i]->name == name)
            {
                count++;
            }
        }
        return count;
    }

    /**
     * @brief Stack the firmware asked for, summed over the live tasks of this name.
     *
     * The device's figure - the bytes xTaskCreatePinnedToCore() takes from the heap for the
     * stack - and not the fiber's, which the bench sizes for host frames.
     */
    uint32_t liveStackBytes(const char* name) const
    {
        uint32_t bytes = 0;
        for (size_t i = 0; i < _tasks.size(); i++)
        {
            if (_tasks[i] && name != nullptr && _tasks[i]->name == name)
            {
                bytes += _tasks[i]->stackBytes;
            }
        }
        return bytes;
    }

    /** @brief How many tasks were ever created under this name. */
    uint32_t createdTaskCount(const char* name) const
    {
        if (name == nullptr) return 0;
        std::map<std::string, uint32_t>::const_iterator it = _createdByName.find(name);
        return (it == _createdByName.end()) ? 0 : it->second;
    }

    /** @brief True if runUntilMs() gave up because tasks kept running without time moving. */
    bool stepBudgetExhausted() const { return _budgetExhausted; }
    void setStepBudget(uint64_t steps) { _stepBudget = steps; }

    /**
     * @brief Index of the task running right now, or -1 when called from the test body.
     *
     * Host-only, and deliberately not on ITaskScheduler: nothing in the firmware needs to know
     * which task it is. A host stand-in for a lock does, because it has to tell "this owner is
     * taking the lock again" from "somebody else is waiting for it".
     */
    int currentTaskIndex() const { return _current; }

    /** @brief Name of the task running right now, or "" when called from the test body. */
    const char* currentTaskName() const
    {
        return (_current < 0) ? "" : _tasks[static_cast<size_t>(_current)]->name.c_str();
    }

    /** @brief Make the next `count` createTask() calls fail, as an out-of-heap device would. */
    void failNextCreateTasks(uint32_t count) { _createTaskFailuresLeft = count; }

    /** @brief Make the next `count` createTask() calls for one task name fail, and no other. */
    void failNextCreateTasksNamed(const char* name, uint32_t count)
    {
        _refusedName = (name != nullptr) ? name : "";
        _refusalsOfTheNameLeft = count;
    }

    /** @brief How many createTask() calls were refused because of failNextCreateTasks(). */
    uint32_t refusedTaskCount() const { return _refusedTaskCount; }

private:
    static const uint64_t kNoWake = UINT64_MAX;

    struct Task {
        std::unique_ptr<Fiber> fiber;
        std::string name;
        TaskBody body;
        uint64_t wakeAtUs = 0;
        uint8_t priority = 0;
        /** The stack the firmware asked for: the device's figure, see liveStackBytes(). */
        uint32_t stackBytes = 0;
        /** Which task this is, for as long as the run lasts; names repeat, serials do not. */
        uint64_t serial = 0;
        /** This task's parked allocation-census tag stack. Plain storage; allocates nothing. */
        HostAllocationCensus::Context census;
    };

    bool subscribedEver(const std::string& name) const
    {
        for (size_t i = 0; i < _watchdogSubscribedTaskNames.size(); i++)
        {
            if (_watchdogSubscribedTaskNames[i] == name)
            {
                return true;
            }
        }
        return false;
    }

    WatchdogEntry* findWatchdogEntry(uint64_t serial)
    {
        for (size_t i = 0; i < _watchdogEntries.size(); i++)
        {
            if (_watchdogEntries[i].taskSerial == serial)
            {
                return &_watchdogEntries[i];
            }
        }
        return nullptr;
    }

    HostAllocationCensus::Owner censusOwnerFor(const std::string& name) const
    {
        std::map<std::string, int>::const_iterator it = _censusOwnerByTask.find(name);
        return (it == _censusOwnerByTask.end())
                   ? HostAllocationCensus::kOther
                   : (HostAllocationCensus::Owner)it->second;
    }

    /**
     * @brief Run a task that has just been created until it suspends, from inside createTask().
     *
     * The creator - a task or the test body - is suspended in the middle of its createTask() call
     * for that long, exactly as a fiber is resumed from runUntilMs(): the same bookkeeping of which
     * task runs and whose allocations are whose, swapped in and out around the resume.
     */
    void runTheNewTaskUntilItWaits(size_t index)
    {
        const int creator = _current;
        Task& task = *_tasks[index];

        _current = static_cast<int>(index);
        _contextSwitches++;
        HostAllocationCensus::swapContext(task.census);
        task.fiber->resume();
        HostAllocationCensus::swapContext(task.census);
        _current = creator;

        if (task.fiber->finished())
        {
            _tasks[index].reset();
        }
    }

    static void taskEntry(void* arg)
    {
        Task* task = static_cast<Task*>(arg);
        if (task != nullptr && task->body)
        {
            task->body();
        }
    }

    bool isRunnable(const Task* task) const
    {
        return task != nullptr && !task->fiber->finished() && task->wakeAtUs <= _clock.nowUs();
    }

    int pickReadyTask() const
    {
        int best = -1;
        const size_t count = _tasks.size();

        for (size_t offset = 1; offset <= count; ++offset)
        {
            // Start scanning just after the task that ran last, so equal-priority tasks take
            // turns instead of the first one starving the rest.
            const size_t index = (static_cast<size_t>(_lastRun + 1) + offset - 1) % count;
            const Task* task = _tasks[index].get();
            if (!isRunnable(task))
            {
                continue;
            }
            if (best < 0 || task->priority > _tasks[static_cast<size_t>(best)]->priority)
            {
                best = static_cast<int>(index);
            }
        }

        return best;
    }

    uint64_t earliestWakeUs() const
    {
        uint64_t earliest = kNoWake;
        for (const std::unique_ptr<Task>& task : _tasks)
        {
            if (!task || task->fiber->finished())
            {
                continue;
            }
            if (task->wakeAtUs < earliest)
            {
                earliest = task->wakeAtUs;
            }
        }
        return earliest;
    }

    VirtualClock& _clock;
    std::vector<std::unique_ptr<Task>> _tasks;
    int _current = -1;
    int _lastRun = -1;
    bool _stopRequested = false;
    bool _budgetExhausted = false;
    uint64_t _contextSwitches = 0;
    uint64_t _stepBudget = 2000000;
    uint32_t _createTaskFailuresLeft = 0;
    std::string _refusedName;
    uint32_t _refusalsOfTheNameLeft = 0;
    bool _newTasksStartAtOnce = false;
    std::map<std::string, int> _censusOwnerByTask;
    uint32_t _refusedTaskCount = 0;
    uint64_t _lastTaskSerial = 0;
    std::map<std::string, uint32_t> _createdByName;
    uint32_t _watchdogSubscriptions = 0;
    uint32_t _watchdogUnsubscriptions = 0;
    uint64_t _watchdogFeeds = 0;
    std::map<std::string, uint64_t> _feedsByTask;
    std::map<std::string, uint64_t> _lastFeedAtUsByTask;
    std::vector<std::string> _watchdogSubscribedTaskNames;
    std::vector<WatchdogEntry> _watchdogEntries;
};

} // namespace hostsim
