#pragma once

#include <cstdint>
#include <functional>

/**
 * @brief Facade over the RTOS task API.
 *
 * Application and Core code that needs to run work outside the control loop asks for a task
 * through this interface instead of calling xTaskCreatePinnedToCore()/vTaskDelay() directly.
 * The reason is testability, not portability: FreeRTOS calls compiled into Application code
 * cannot be built or observed on the host, so the behaviour of anything built on them cannot
 * be proven by a test. Behind this interface the device gets a thin FreeRTOS wrapper and the
 * host gets a deterministic cooperative scheduler driven by a virtual clock.
 *
 * A task body is expected to run its own endless loop and to give the CPU up regularly via
 * delayMs() or yieldTask(); returning from the body ends the task, and the stack goes back to the
 * heap. A body that returns and had put itself under the watchdog takes itself out first
 * (watchdogUnsubscribeCurrentTask()).
 */
class ITaskScheduler {
public:
    using TaskBody = std::function<void()>;

    virtual ~ITaskScheduler() = default;

    /**
     * @brief Create a task and make it runnable.
     * @param name Task name, for RTOS introspection and host-side diagnostics
     * @param stackBytes Stack size in bytes
     * @param priority Task priority; higher runs first when several tasks are ready
     * @param body Task body, copied and owned by the scheduler
     * @return true if the task was created
     */
    virtual bool createTask(const char* name, uint32_t stackBytes, uint8_t priority, TaskBody body) = 0;

    /**
     * @brief Give other ready tasks a chance to run without sleeping.
     *
     * Named yieldTask() rather than yield() because the Arduino core already declares a free
     * function yield() and the two are easy to confuse at a call site.
     */
    virtual void yieldTask() = 0;

    /**
     * @brief Block the calling task for the given number of milliseconds.
     *
     * This is a task-level sleep, not a busy wait: other tasks run meanwhile.
     */
    virtual void delayMs(uint32_t ms) = 0;

    /**
     * @brief Put the calling task under the task watchdog.
     *
     * Part of the RTOS task API on ESP-IDF (esp_task_wdt_add), which is why it lives here rather
     * than behind an interface of its own: Application code must be able to ask for it without
     * including esp_task_wdt.h, or it could not be built - and therefore could not be proven - on
     * the host.
     *
     * Only worth asking for from a task whose work is bounded. The outbox network task is: one
     * request costs at most the retry budget its dispatcher enforces, which is a number the code
     * chooses rather than a number the server chooses.
     *
     * Default: does nothing, so a scheduler with no watchdog behind it needs no override.
     */
    virtual void watchdogSubscribeCurrentTask() {}

    /**
     * @brief Tell the task watchdog the calling task is still making progress.
     *
     * Harmless when the task never subscribed.
     */
    virtual void watchdogFeed() {}

    /**
     * @brief Take the calling task out from under the task watchdog.
     *
     * Required of a task whose body returns after watchdogSubscribeCurrentTask(): the ESP-IDF
     * watchdog keeps every subscribed task on its list until esp_task_wdt_delete() takes it off,
     * deleting the task does not, and an entry nobody feeds any more fires the watchdog one
     * timeout later.
     *
     * Default: does nothing. Harmless when the task never subscribed.
     */
    virtual void watchdogUnsubscribeCurrentTask() {}

    /**
     * @brief Minimum free stack a task created through this scheduler has ever had, in bytes.
     *
     * The distance to a stack overflow, as the RTOS measures it. Per task name: a task that ends
     * and is created again under the same name answers with the least over all its lifetimes,
     * the ended ones included. Returns false when the value is not known - a scheduler with no
     * such instrumentation (the host cooperative scheduler) or a task name never run here. The
     * caller must report an unknown as unknown, not as zero: a zero here means "the stack was
     * completely exhausted", which is an alarm, not an absence of data.
     */
    virtual bool stackHighWaterMarkBytes(const char* taskName, uint32_t& outBytes)
    {
        (void)taskName;
        (void)outBytes;
        return false;
    }
};
