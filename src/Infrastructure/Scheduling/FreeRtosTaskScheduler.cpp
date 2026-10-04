#include "FreeRtosTaskScheduler.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <utility>

namespace {

// On ESP-IDF the watermark is reported in bytes (StackType_t is a byte): the closest the task
// has ever come to the bottom of its stack.
uint32_t readStackWatermark(void* handle)
{
    return (uint32_t)uxTaskGetStackHighWaterMark(static_cast<TaskHandle_t>(handle));
}

} // namespace

FreeRtosTaskScheduler::FreeRtosTaskScheduler(int coreId)
    : _coreId(coreId)
{
}

// The task body is a std::function, which does not fit into the void* FreeRTOS hands to a
// task entry point, so it is copied to the heap with the rest of the launch and the copy is
// passed instead. It is freed when the body returns; a body that loops forever keeps it alive
// for the lifetime of the task.
void FreeRtosTaskScheduler::taskEntry(void* arg)
{
    TaskLaunch* launch = static_cast<TaskLaunch*>(arg);
    if (launch != nullptr)
    {
        launch->scheduler->noteTaskStarted(launch->name);
        launch->body();
        // Before the task deletes itself: once it has, the idle task frees its control block
        // and its stack, and the handle must not be read again.
        launch->scheduler->noteTaskEnded();
        delete launch;
    }
    vTaskDelete(nullptr);
}

void FreeRtosTaskScheduler::noteTaskStarted(const char* name)
{
    std::lock_guard<std::mutex> guard(_trackedLock);
    _tracked.started(name, xTaskGetCurrentTaskHandle());
}

void FreeRtosTaskScheduler::noteTaskEnded()
{
    const uint32_t watermarkBytes = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
    std::lock_guard<std::mutex> guard(_trackedLock);
    _tracked.ended(xTaskGetCurrentTaskHandle(), watermarkBytes);
}

bool FreeRtosTaskScheduler::createTask(const char* name, uint32_t stackBytes, uint8_t priority, TaskBody body)
{
    TaskLaunch* launch = new TaskLaunch();
    launch->body = std::move(body);
    launch->scheduler = this;
    launch->name = name;

    TaskHandle_t handle = nullptr;
    BaseType_t result = xTaskCreatePinnedToCore(
        &FreeRtosTaskScheduler::taskEntry,
        name,
        stackBytes,
        launch,
        priority,
        &handle,
        _coreId);

    if (result != pdPASS)
    {
        delete launch;
        return false;
    }

    return true;
}

bool FreeRtosTaskScheduler::stackHighWaterMarkBytes(const char* taskName, uint32_t& outBytes)
{
    // The live tasks are read under the lock, so none of them can leave the table - and go on to
    // delete itself - halfway through the reading.
    std::lock_guard<std::mutex> guard(_trackedLock);
    return _tracked.lowest(taskName, &readStackWatermark, outBytes);
}

uint32_t FreeRtosTaskScheduler::untrackedTaskCount()
{
    std::lock_guard<std::mutex> guard(_trackedLock);
    return _tracked.untrackedCount();
}

void FreeRtosTaskScheduler::yieldTask()
{
    taskYIELD();
}

void FreeRtosTaskScheduler::delayMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void FreeRtosTaskScheduler::watchdogSubscribeCurrentTask()
{
    // main.cpp arms the task watchdog with esp_task_wdt_init(WDT_TIMEOUT_S, true) and subscribes
    // loopTask. Passing NULL subscribes whichever task is calling, which is the task body itself.
    // A failure is not fatal here and not worth a branch: the caller keeps running, it is simply
    // not watched.
    esp_task_wdt_add(NULL);
}

void FreeRtosTaskScheduler::watchdogFeed()
{
    esp_task_wdt_reset();
}

void FreeRtosTaskScheduler::watchdogUnsubscribeCurrentTask()
{
    // NULL is the calling task, as for the subscription. A task that was never subscribed gets
    // ESP_ERR_INVALID_ARG back and nothing changes.
    esp_task_wdt_delete(NULL);
}
