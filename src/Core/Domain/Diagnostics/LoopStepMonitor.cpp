#include "Core/Domain/Diagnostics/LoopStepMonitor.h"

// Constant-initialised, so the monitor is usable before any static constructor has run.
std::atomic<uint32_t> LoopStepMonitor::_worstStepMs(0);

void LoopStepMonitor::record(uint32_t stepMs)
{
    // A compare-and-swap maximum rather than a plain store: only the loop task writes today, but
    // a lost update here would be a silently shortened worst step, which is exactly the number
    // this class exists to be trusted about.
    uint32_t worst = _worstStepMs.load(std::memory_order_relaxed);
    while (stepMs > worst &&
           !_worstStepMs.compare_exchange_weak(worst, stepMs, std::memory_order_relaxed))
    {
    }
}

uint32_t LoopStepMonitor::take()
{
    return _worstStepMs.exchange(0, std::memory_order_relaxed);
}

uint32_t LoopStepMonitor::peek()
{
    return _worstStepMs.load(std::memory_order_relaxed);
}

void LoopStepMonitor::reset()
{
    _worstStepMs.store(0, std::memory_order_relaxed);
}
