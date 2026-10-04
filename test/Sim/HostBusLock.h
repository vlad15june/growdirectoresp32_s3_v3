#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "Core/Domain/Interfaces/Adapters/Sensors/IBusLock.h"
#include "Core/Domain/Interfaces/Scheduling/IBusLockPlatform.h"
#include "Infrastructure/Scheduling/TicketBusLock.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/VirtualClock.h"

namespace hostsim {

/**
 * @brief IBusLockPlatform for the cooperative host scheduler.
 *
 * The four primitives, and only the four. The lock algorithm is not here - it is the production
 * `TicketBusLock`, which these tests drive directly. That is the whole point of the split:
 * a second implementation of the ticket algorithm in this file would leave the production
 * class compiled into no test at all, and a defect of the platform layer (a pthread call from a
 * FreeRTOS task) would live exactly in the gap between them.
 *
 * `guardEnter`/`guardExit` are no-ops on purpose. Every task here is a fiber on one thread and a
 * fiber is only ever descheduled where it says so; TicketBusLock never suspends inside its guard,
 * so nothing can interleave there. A real mutex would be worse than useless - it would deadlock
 * the single thread all the fibers share.
 */
class HostBusLockPlatform : public IBusLockPlatform {
public:
    HostBusLockPlatform(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock)
    {
    }

    OwnerId currentOwner() override
    {
        // +1 so that the first task (index 0) is not mistaken for "nobody", and so that the test
        // body (index -1) has an identity of its own rather than sharing one.
        return static_cast<OwnerId>(_scheduler.currentTaskIndex() + 2);
    }

    void guardEnter() override {}
    void guardExit() override {}

    void waitForWake() override
    {
        _waitedByOwner[currentOwner()] = true;
        _waitCalls++;

        if (_scheduler.currentTaskIndex() < 0)
        {
            // The test body itself is waiting for a lock a suspended task holds. Only
            // runUntilMs() resumes fibers, so no amount of waiting here can ever be satisfied.
            // Hanging would report this as a stuck test; recording it lets a test name it.
            _deadlockDetected = true;
            return;
        }

        // delayMs, not yieldTask: a yielding waiter stays runnable, and the host scheduler only
        // advances the clock when nothing is runnable - so a waiter that yields spins forever
        // against an owner asleep inside a 5 ms poll step. The price is that a wait is measured to
        // a 1 ms granularity and rounded up, which errs against the claim being made, not for it.
        _scheduler.delayMs(1);
    }

    void wakeWaiters(int) override
    {
        // Nothing to do: a waiter here re-checks after its 1 ms sleep. Counted so that a test can
        // see the production class did try to release its waiters.
        _wakeCalls++;
    }

    // --- Host-only observation ---------------------------------------------------------------

    bool didWait(OwnerId owner) const
    {
        std::map<OwnerId, bool>::const_iterator it = _waitedByOwner.find(owner);
        return it != _waitedByOwner.end() && it->second;
    }

    void clearWaitFlag(OwnerId owner) { _waitedByOwner[owner] = false; }

    bool deadlockDetected() const { return _deadlockDetected; }
    uint64_t waitCalls() const { return _waitCalls; }
    uint64_t wakeCalls() const { return _wakeCalls; }

private:
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
    std::map<OwnerId, bool> _waitedByOwner;
    bool _deadlockDetected = false;
    uint64_t _waitCalls = 0;
    uint64_t _wakeCalls = 0;
};

/**
 * @brief The production TicketBusLock on the host platform, with the receipts a test needs.
 *
 * A decorator, not a reimplementation: `lock()`/`unlock()` go straight through to the same class
 * `globals.h` constructs on the device. What is added is bookkeeping - who waited, how often, and
 * for how long - because the claim to be proven is about waiting, and waiting cannot be proven by
 * measuring the loop's cadence alone: a loop that happens not to collide with the acquisition task
 * looks exactly like a loop that structurally cannot.
 */
class HostBusLock : public IBusLock {
public:
    HostBusLock(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _platform(scheduler, clock), _lock(&_platform), _scheduler(scheduler), _clock(clock)
    {
    }

    void lock() override
    {
        const std::string name = _scheduler.currentTaskName();
        const IBusLockPlatform::OwnerId owner = _platform.currentOwner();
        const uint64_t startedAtMs = _clock.nowMs();

        _platform.clearWaitFlag(owner);
        _lock.lock();

        if (_platform.didWait(owner))
        {
            const uint64_t waitedMs = _clock.nowMs() - startedAtMs;
            _waitsByTask[name]++;
            _waitedMsByTask[name] += waitedMs;
            if (waitedMs > _worstWaitMsByTask[name])
            {
                _worstWaitMsByTask[name] = waitedMs;
            }
        }

        _takesByTask[name]++;
        _totalTakes++;
    }

    void unlock() override { _lock.unlock(); }

    /** @brief How many times `taskName` had to wait because somebody else held the line. */
    uint64_t waitsBy(const std::string& taskName) const { return lookup(_waitsByTask, taskName); }

    /** @brief How long `taskName` spent waiting for the line in total, in virtual milliseconds. */
    uint64_t waitedMsBy(const std::string& taskName) const
    {
        return lookup(_waitedMsByTask, taskName);
    }

    /**
     * @brief The longest single wait `taskName` ever had for the line.
     *
     * This, not the total, is the number that says how long a bus command issued from the control
     * loop can be held up: the total grows with how long the test ran, the worst single wait does
     * not.
     */
    uint64_t worstWaitMsBy(const std::string& taskName) const
    {
        return lookup(_worstWaitMsByTask, taskName);
    }

    /** @brief How many times `taskName` took the line at all (waiting or not). */
    uint64_t takesBy(const std::string& taskName) const { return lookup(_takesByTask, taskName); }

    uint64_t totalTakes() const { return _totalTakes; }
    uint64_t reentrantTakes() const { return _lock.reentrantTakes(); }
    bool held() const { return _lock.held(); }

    /** @brief True if a wait could never have been satisfied - a fault in the test, not the code. */
    bool deadlockDetected() const { return _platform.deadlockDetected(); }

    HostBusLockPlatform& platform() { return _platform; }

private:
    static uint64_t lookup(const std::map<std::string, uint64_t>& counters, const std::string& key)
    {
        std::map<std::string, uint64_t>::const_iterator it = counters.find(key);
        return (it == counters.end()) ? 0 : it->second;
    }

    HostBusLockPlatform _platform;
    TicketBusLock _lock;
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;

    std::map<std::string, uint64_t> _waitsByTask;
    std::map<std::string, uint64_t> _waitedMsByTask;
    std::map<std::string, uint64_t> _worstWaitMsByTask;
    std::map<std::string, uint64_t> _takesByTask;
    uint64_t _totalTakes = 0;
};

} // namespace hostsim
