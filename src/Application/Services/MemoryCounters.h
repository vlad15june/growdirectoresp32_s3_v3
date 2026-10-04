#pragma once

#include <cstdint>

#include "Application/Services/CounterReport.h"
#include "Core/Domain/Diagnostics/AllocationFailureCounter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

/**
 * @brief Add the memory columns to the periodic counters line.
 *
 * `alloc_fails` counts the allocations the heap refused since boot; `psram_largest` is the
 * biggest block a single allocation can still get. The two belong together: a heap can report
 * plenty of free bytes and still refuse an 18 KiB TLS buffer because none of them are adjacent,
 * and only the pair tells that apart from a plain leak.
 *
 * The registration lives here instead of inline in the composition root so that the column
 * names, their order and the sources they read are the ones a host test executes.
 */
inline void registerMemoryCounters(CounterReport& report, IArduinoAdapter& arduinoAdapter)
{
    // Column names are what makes the worst line a compile-time number, so these two have to fit
    // the bound that arithmetic assumes - a longer name would be refused at run time and the
    // column would simply not be there.
    static_assert(sizeof("alloc_fails") - 1 <= CounterReport::MAX_NAME_CHARS,
                  "the alloc_fails column name is over the bound the worst line is derived from");
    static_assert(sizeof("psram_largest") - 1 <= CounterReport::MAX_NAME_CHARS,
                  "the psram_largest column name is over the bound the worst line is derived from");

    report.addCounter("alloc_fails", [] { return (uint64_t)AllocationFailureCounter::count(); });
    report.addCounter("psram_largest",
                      [&arduinoAdapter] { return (uint64_t)arduinoAdapter.getMaxAllocPsram(); });
}
