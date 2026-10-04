#pragma once

#include <cstdint>

#include "Core/Domain/Interfaces/Adapters/Sensors/IBusLock.h"
#include "Core/Domain/Interfaces/Scheduling/IBusLockPlatform.h"

/**
 * @brief The RS-485 lock: first come, first served, and reentrant for its own owner.
 *
 * ### Why a ticket and not a mutex
 *
 * Because a mutex does not bound the wait, and the bound is the point. A mutex - std::mutex or a
 * FreeRTOS semaphore under it - hands a free lock to whoever asks, ahead of anyone already
 * waiting. The acquisition task releases the line at the end of a transaction and asks again for
 * the next channel immediately, so a control-loop command that arrived a moment too late is
 * overtaken and waits out a second transaction. Measured on the host with a barging lock: 550 ms
 * against a 400 ms transaction, and nothing in that arrangement stops it being a third.
 *
 * With a ticket a waiter is served in the order it arrived, so a command waits for the tail of the
 * transaction in flight and nothing else. Measured: 290 ms.
 *
 * ### Reentrancy
 *
 * The owner takes its own lock again without a new ticket. `ModbusSensorReader::writeSensorAddress()`
 * and `SensorReader::scanModbusSensor()` both hold the line across a sequence of transactions and
 * call nested public methods that take it again; see IBusLock.
 *
 * ### Platform
 *
 * Everything platform-specific is behind IBusLockPlatform - four calls. This class is plain C++
 * and is the one that runs both on the device and in the host tests; see IBusLockPlatform for the
 * constraint that makes that non-negotiable.
 */
class TicketBusLock : public IBusLock
{
public:
    explicit TicketBusLock(IBusLockPlatform* platform) : _platform(platform) {}

    void lock() override
    {
        if (_platform == nullptr)
        {
            return;
        }

        const IBusLockPlatform::OwnerId me = _platform->currentOwner();

        _platform->guardEnter();

        if (_depth > 0 && _owner == me)
        {
            _depth++;
            _reentrantTakes++;
            _platform->guardExit();
            return;
        }

        const uint64_t myTicket = _nextTicket++;

        if (_depth == 0 && _nowServing == myTicket)
        {
            takeOwnership(me);
            _platform->guardExit();
            return;
        }

        _waiters++;
        _platform->guardExit();

        for (;;)
        {
            _platform->waitForWake();

            _platform->guardEnter();
            if (_depth == 0 && _nowServing == myTicket)
            {
                _waiters--;
                takeOwnership(me);
                _platform->guardExit();
                return;
            }
            _platform->guardExit();
        }
    }

    void unlock() override
    {
        if (_platform == nullptr)
        {
            return;
        }

        _platform->guardEnter();

        if (_depth == 0)
        {
            _platform->guardExit();
            return;
        }

        if (--_depth > 0)
        {
            _platform->guardExit();
            return;
        }

        _owner = 0;
        _nowServing++;
        const int toWake = _waiters;

        _platform->guardExit();

        if (toWake > 0)
        {
            // Every waiter is released and re-checks its own ticket; all but one go back to
            // waiting. With at most a handful of tasks on this line that is cheaper and far
            // simpler than keeping a queue of task handles, and it cannot wake the wrong one into
            // ownership - the ticket decides that, not the wake.
            _platform->wakeWaiters(toWake);
        }
    }

    // --- Observation ---------------------------------------------------------------------------

    /** @brief How many times an owner took a lock it already held. */
    uint64_t reentrantTakes() const { return _reentrantTakes; }

    /** @brief How many times the lock was granted to a new owner. */
    uint64_t takes() const { return _takes; }

    /** @brief Whether the line is held right now. */
    bool held() const { return _depth > 0; }

private:
    void takeOwnership(IBusLockPlatform::OwnerId me)
    {
        _owner = me;
        _depth = 1;
        _takes++;
    }

    IBusLockPlatform* _platform;

    IBusLockPlatform::OwnerId _owner = 0;
    unsigned int _depth = 0;
    uint64_t _nextTicket = 0;
    uint64_t _nowServing = 0;
    int _waiters = 0;

    uint64_t _takes = 0;
    uint64_t _reentrantTakes = 0;
};
