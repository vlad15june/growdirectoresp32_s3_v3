#include "FreeRtosBusLockPlatform.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

// More than the number of tasks that can ever queue for the RS-485 line (the acquisition task, the
// Arduino loop task carrying a command, and the setup path), with room to spare.
const UBaseType_t kMaxPendingWakes = 8;

} // namespace

FreeRtosBusLockPlatform::FreeRtosBusLockPlatform()
{
    _guard = xSemaphoreCreateMutex();
    _wakeup = xSemaphoreCreateCounting(kMaxPendingWakes, 0);
}

FreeRtosBusLockPlatform::~FreeRtosBusLockPlatform()
{
    if (_guard != nullptr)
    {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_guard));
    }
    if (_wakeup != nullptr)
    {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_wakeup));
    }
}

IBusLockPlatform::OwnerId FreeRtosBusLockPlatform::currentOwner()
{
    // The task handle: valid for any FreeRTOS task, unique while the task exists, and - unlike
    // pthread_self() - it does not care how the task was created. Never NULL once the scheduler
    // is running; the +1 keeps the contract "never 0" true even if it somehow were.
    return static_cast<OwnerId>(reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle())) + 1u;
}

void FreeRtosBusLockPlatform::guardEnter()
{
    if (_guard != nullptr)
    {
        xSemaphoreTake(static_cast<SemaphoreHandle_t>(_guard), portMAX_DELAY);
    }
}

void FreeRtosBusLockPlatform::guardExit()
{
    if (_guard != nullptr)
    {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(_guard));
    }
}

void FreeRtosBusLockPlatform::waitForWake()
{
    if (_wakeup == nullptr)
    {
        // No semaphore to wait on: fall back to a slow poll rather than spinning on the CPU. The
        // lock stays correct - the ticket still decides - it just re-checks on a timer.
        vTaskDelay(pdMS_TO_TICKS(1));
        return;
    }

    // A timeout, not portMAX_DELAY: a lost wake would otherwise park a task forever and take the
    // RS-485 line with it. Waking early is free - the caller re-checks its ticket and waits again.
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(_wakeup), pdMS_TO_TICKS(50));
}

void FreeRtosBusLockPlatform::wakeWaiters(int count)
{
    if (_wakeup == nullptr)
    {
        return;
    }

    for (int i = 0; i < count; i++)
    {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(_wakeup));
    }
}
