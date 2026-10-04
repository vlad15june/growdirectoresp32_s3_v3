#ifndef ALLOCATION_FAILURE_COUNTER_H
#define ALLOCATION_FAILURE_COUNTER_H

#include <atomic>
#include <cstdint>

/**
 * @brief Allocations the heap refused, counted so that a refusal is never silent.
 *
 * A failed allocation returns a null pointer and nothing else: the caller either handles it or
 * crashes, and either way the event leaves nothing a field log can be grepped for. The number
 * belongs next to the free-memory ones - free bytes falling is a leak, refusals rising while
 * free bytes hold steady is fragmentation.
 *
 * Two kinds of reporter feed it. The platform's own allocation-failure hook sees every request
 * the heap turns down; a call site that checks its own result sees only its own. While the hook
 * is installed the call sites step aside: the hook has already reported that very failure, and
 * one event must move the counter by one.
 *
 * The count is monotonic and process-wide - read it as a delta. Both fields are atomic because
 * a reporter runs on whichever task asked for the memory.
 */
class AllocationFailureCounter
{
public:
    /** Report a failure seen by the platform's allocation-failure hook. */
    static void recordFromHeapHook();

    /**
     * @brief Report a failure seen by a call site that checked its own result.
     *
     * Does nothing while the platform hook is installed, which reports the same failure.
     */
    static void recordAtCallSite();

    /** Told by whoever registers or drops the platform hook. */
    static void setHeapHookInstalled(bool installed);

    /** Whether the platform hook is reporting: without it a zero count means far less. */
    static bool isHeapHookInstalled();

    /** Failures counted since boot. */
    static uint32_t count();

private:
    static std::atomic<uint32_t> _count;
    static std::atomic<bool> _heapHookInstalled;
};

#endif // ALLOCATION_FAILURE_COUNTER_H
