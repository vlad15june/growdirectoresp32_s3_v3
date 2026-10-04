#pragma once

#include "Core/Domain/Interfaces/Scheduling/IBusLockPlatform.h"

/**
 * @brief IBusLockPlatform on FreeRTOS. The only part of the bus lock a host test cannot run.
 *
 * Deliberately as thin as it can be made: an identity, a mutex and a counting semaphore. There is
 * no algorithm here to get wrong - the ticket logic is in TicketBusLock, which the host tests
 * drive directly.
 *
 * **No pthreads.** The identity is `xTaskGetCurrentTaskHandle()`, not `std::this_thread::get_id()`.
 * The latter is `pthread_self()` on arduino-esp32, and `pthread_self()` asserts - `__assert_func`,
 * i.e. `abort()` and a reboot - when the calling task was not created by `pthread_create`. Neither
 * task that takes this lock is: `sensor-acq` comes from `xTaskCreatePinnedToCore()`
 * (FreeRtosTaskScheduler.cpp) and the Arduino `loopTask` comes from the core. For the same reason
 * nothing here may use `std::mutex`, `std::recursive_mutex` or `std::condition_variable`, whose
 * ESP-IDF implementations are pthread-backed.
 */
class FreeRtosBusLockPlatform : public IBusLockPlatform
{
public:
    FreeRtosBusLockPlatform();
    ~FreeRtosBusLockPlatform() override;

    OwnerId currentOwner() override;
    void guardEnter() override;
    void guardExit() override;
    void waitForWake() override;
    void wakeWaiters(int count) override;

    /** @brief False if the RTOS could not give us the two semaphores; the lock then degrades. */
    bool isReady() const { return _guard != nullptr && _wakeup != nullptr; }

private:
    // void* rather than SemaphoreHandle_t so that this header stays free of FreeRTOS includes and
    // globals.h does not drag them into every translation unit that sees it.
    void* _guard = nullptr;
    void* _wakeup = nullptr;
};
