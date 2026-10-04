#ifndef HEAP_FAILURE_HOOK_H
#define HEAP_FAILURE_HOOK_H

#include <esp_heap_caps.h>

#include "Core/Domain/Diagnostics/AllocationFailureCounter.h"

/**
 * @brief Connects the heap's allocation-failure callback to the failure counter.
 *
 * The heap invokes the callback for every request it turns down - malloc, calloc, realloc and
 * the capability-aware allocators alike - which makes it the one place that sees refusals no
 * call site checks for. Installing it also tells the counter that its call sites can stop
 * counting, since they would report the same failures a second time.
 *
 * The callback runs on the task whose allocation failed and does nothing but increment a
 * counter.
 */
class HeapFailureHook
{
public:
    /** @return false if the platform refused the registration; call sites keep counting then. */
    static bool install()
    {
        if (heap_caps_register_failed_alloc_callback(&onAllocationFailed) != ESP_OK)
        {
            return false;
        }
        AllocationFailureCounter::setHeapHookInstalled(true);
        return true;
    }

private:
    static void onAllocationFailed(size_t, uint32_t, const char*)
    {
        AllocationFailureCounter::recordFromHeapHook();
    }
};

#endif // HEAP_FAILURE_HOOK_H
