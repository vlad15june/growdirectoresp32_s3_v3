#pragma once

// FreeRTOS task stand-in for the native (host) test environment.
//
// What it models is the *bookkeeping* of task creation - that a task was asked for, under which
// name, on which core, and that asking twice for the same one does not happen - and nothing else.
// The body is NOT run: the bodies this stand-in is asked for are endless loops
// (`for (;;) { work(); vTaskDelay(...); }`), and running one on the single thread of a host test
// would never return.
//
// The consequence is stated rather than hidden: a test that uses this header exercises what the
// task's body calls only if it calls it itself, through the production object's public API. What
// such a test does not prove is the *cadence* - when the firmware's own task would have made
// those calls. Anything that needs the cadence proved belongs on Sim/HostTaskScheduler.h, which
// runs real fibers on a virtual clock; this file is for production classes that create a task of
// their own instead of taking an ITaskScheduler.
//
// Reachable only through the include path of env:native (see platformio.ini).

#include "FreeRTOS.h"

#include <string>
#include <vector>

typedef void (*TaskFunction_t)(void *);

struct HostTaskControlBlock
{
    std::string name;
    TaskFunction_t entry = nullptr;
    void *argument = nullptr;
    uint32_t stackDepth = 0;
    UBaseType_t priority = 0;
    BaseType_t coreId = 0;
};

typedef HostTaskControlBlock *TaskHandle_t;

// Every task the code under test asked for, in creation order. Owned here: a test that wants a
// clean slate calls hostTasksReset().
inline std::vector<HostTaskControlBlock *> hostCreatedTasks;

inline void hostTasksReset()
{
    for (HostTaskControlBlock *task : hostCreatedTasks)
    {
        delete task;
    }
    hostCreatedTasks.clear();
}

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name, uint32_t stackDepth,
                                          void *argument, UBaseType_t priority, TaskHandle_t *handle,
                                          BaseType_t coreId)
{
    HostTaskControlBlock *task = new HostTaskControlBlock();
    task->name = (name != nullptr) ? name : "";
    task->entry = entry;
    task->argument = argument;
    task->stackDepth = stackDepth;
    task->priority = priority;
    task->coreId = coreId;
    hostCreatedTasks.push_back(task);

    if (handle != nullptr)
    {
        *handle = task;
    }
    return pdPASS;
}

inline BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stackDepth,
                              void *argument, UBaseType_t priority, TaskHandle_t *handle)
{
    return xTaskCreatePinnedToCore(entry, name, stackDepth, argument, priority, handle, 0);
}

inline void vTaskDelete(TaskHandle_t /*task*/) {}

// The host has no scheduler to yield to, so a delay is recorded the way Arduino.h records its
// own: a test that wants to say how long a loop asks to sleep reads the counter and resets it.
inline unsigned long hostTaskDelayTotalMs = 0;
inline void vTaskDelay(TickType_t ticks) { hostTaskDelayTotalMs += static_cast<unsigned long>(ticks); }

// The firmware logs which core a task of its own ended up on. One core here, and it is the one
// the MQTT recovery task is pinned to.
inline BaseType_t xPortGetCoreID() { return 0; }
