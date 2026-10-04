#ifndef I_BUS_LOCK_PLATFORM_H
#define I_BUS_LOCK_PLATFORM_H

#include <cstdint>

/**
 * @brief The four platform primitives a ticket lock needs, and nothing else.
 *
 * This split exists because of a platform constraint, stated here so the shape is not mistaken
 * for architecture for its own sake. The lock cannot identify its owner with
 * `std::this_thread::get_id()`: on arduino-esp32 that is `pthread_self()`, which
 * asserts when the calling task was not created through `pthread_create` - and neither of the two
 * tasks that take this lock is: `sensor-acq` comes from `xTaskCreatePinnedToCore()` and the
 * Arduino `loopTask` comes from the core. Using it aborts the module on its first Modbus transaction,
 * inside `setupModule()`, before ever reaching `loop()`.
 *
 * A host test that ran a *second implementation* of the algorithm would not catch it, and the
 * production class would be compiled into no test at all. So the algorithm lives in one
 * place - `TicketBusLock` - which is plain C++ and runs on the host, and only these four calls
 * differ between the device and a test. What stays untested on the host is the FreeRTOS adapter
 * behind them, and that is deliberately as small as it can be made.
 *
 * @see TicketBusLock, FreeRtosBusLockPlatform, hostsim::HostBusLockPlatform
 */
class IBusLockPlatform
{
public:
    /** Identity of a task. Any stable non-zero value; the lock only ever compares two of them. */
    using OwnerId = uintptr_t;

    virtual ~IBusLockPlatform() = default;

    /**
     * @brief Who is asking, as a value that is the same for the whole of one task and unique
     *        between tasks. Must never return 0.
     */
    virtual OwnerId currentOwner() = 0;

    /**
     * @brief Take mutual exclusion over the lock's own bookkeeping.
     *
     * Held for a handful of assignments and never across a wait, so it cannot itself be the thing
     * a caller waits on for any length of time.
     */
    virtual void guardEnter() = 0;

    /** @brief Release the bookkeeping guard. */
    virtual void guardExit() = 0;

    /**
     * @brief Block until wakeWaiters() is called, without losing a wake that arrives first.
     *
     * May return spuriously: the caller re-checks its ticket and blocks again.
     */
    virtual void waitForWake() = 0;

    /**
     * @brief Release `count` waiters, so that the one holding the next ticket can re-check.
     * @param count How many tasks are known to be waiting
     */
    virtual void wakeWaiters(int count) = 0;
};

#endif // I_BUS_LOCK_PLATFORM_H
