#pragma once

#include <mutex>

#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"
#include "Infrastructure/Scheduling/TaskWatermarkTable.h"

/**
 * @brief Device implementation of ITaskScheduler: a thin wrapper over FreeRTOS.
 *
 * One FreeRTOS call per method, and no policy of its own beyond the bookkeeping that lets a
 * stack watermark be asked for by name: every task reports its start and, before it deletes
 * itself, its end (taskEntry(); the decisions are TaskWatermarkTable's). It exists so that
 * Application code can create tasks without including FreeRTOS headers, which would make that
 * code impossible to build - and therefore impossible to prove - on the host.
 */
class FreeRtosTaskScheduler : public ITaskScheduler {
public:
    /**
     * @param coreId Core the created tasks are pinned to. The Arduino loop() runs on core 1,
     *               so background work is pinned to core 0 by default, matching the existing
     *               ButtonUpdate task in main.cpp.
     */
    explicit FreeRtosTaskScheduler(int coreId = 0);

    bool createTask(const char* name, uint32_t stackBytes, uint8_t priority, TaskBody body) override;
    void yieldTask() override;
    void delayMs(uint32_t ms) override;
    void watchdogSubscribeCurrentTask() override;
    void watchdogFeed() override;
    void watchdogUnsubscribeCurrentTask() override;
    bool stackHighWaterMarkBytes(const char* taskName, uint32_t& outBytes) override;

    /** @brief Tasks started past the tracking table's capacity - running but not measurable. */
    uint32_t untrackedTaskCount();

private:
    /** What a created task is started with: its body, and where to report its start and end. */
    struct TaskLaunch
    {
        TaskBody body;
        FreeRtosTaskScheduler* scheduler;
        const char* name;
    };

    /** The entry point of every task created here. */
    static void taskEntry(void* arg);

    /** Called on the task itself, before its body runs. */
    void noteTaskStarted(const char* name);

    /** Called on the task itself, after its body has returned and before it deletes itself. */
    void noteTaskEnded();

    /**
     * Tasks run here, so their stack watermarks can be asked for by name - see TaskWatermarkTable
     * for why a handle in it is safe to read. Guarded: tasks enter and leave it on core 0 while
     * the diagnostic line reads it on the control loop's core.
     */
    TaskWatermarkTable _tracked;
    std::mutex _trackedLock;

    int _coreId;
};
