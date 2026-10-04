#include "Core/Domain/Diagnostics/AllocationFailureCounter.h"

// Constant-initialised, so the counter is usable before any static constructor has run: the
// platform hook can fire during early start-up.
std::atomic<uint32_t> AllocationFailureCounter::_count(0);
std::atomic<bool> AllocationFailureCounter::_heapHookInstalled(false);

void AllocationFailureCounter::recordFromHeapHook()
{
    _count.fetch_add(1, std::memory_order_relaxed);
}

void AllocationFailureCounter::recordAtCallSite()
{
    if (_heapHookInstalled.load(std::memory_order_relaxed))
    {
        return;
    }
    _count.fetch_add(1, std::memory_order_relaxed);
}

void AllocationFailureCounter::setHeapHookInstalled(bool installed)
{
    _heapHookInstalled.store(installed, std::memory_order_relaxed);
}

bool AllocationFailureCounter::isHeapHookInstalled()
{
    return _heapHookInstalled.load(std::memory_order_relaxed);
}

uint32_t AllocationFailureCounter::count()
{
    return _count.load(std::memory_order_relaxed);
}
