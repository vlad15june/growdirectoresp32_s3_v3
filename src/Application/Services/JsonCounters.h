#pragma once

#include <cstdint>

#include "Application/Services/CounterReport.h"
#include "Core/Domain/Diagnostics/MalformedJsonNumberCounter.h"

/**
 * @brief Add the JSON reading column to the periodic counters line.
 *
 * `json_bad_numbers` counts the numeric fields the JSON converter read out of a stored file or an
 * incoming body and could not read as numbers: the field's default stood in instead, and the record
 * around it was kept. Nothing else reports that. A rule whose threshold arrived as text, or a
 * channel whose stored multiplier did, works on a number nobody configured and looks entirely
 * healthy doing it - the count is the only place the substitution is visible from the field.
 *
 * The reach is the converter's own reads, and that is short of every number the module reads: the
 * sensor model catalog is taken apart by SensorTypeStreamParser, which stands its own defaults in
 * without telling this column. A zero here means no substitution on the paths the converter reads,
 * not on all of them.
 *
 * Monotonic since boot, so a line is read against the one before it. It is not a failure count:
 * one step and no more, right after a configuration change, says which change carried the field.
 *
 * The registration lives here instead of inline in the composition root so that the column name,
 * its place in the line and the source it reads are the ones a host test executes.
 */
inline void registerJsonCounters(CounterReport& report)
{
    // The column name is what makes the worst line a compile-time number, so it has to fit the
    // bound that arithmetic assumes - a longer name would be refused at run time and the column
    // would simply not be there.
    static_assert(sizeof("json_bad_numbers") - 1 <= CounterReport::MAX_NAME_CHARS,
                  "the json_bad_numbers column name is over the bound the worst line is derived "
                  "from");

    report.addCounter("json_bad_numbers",
                      [] { return (uint64_t)MalformedJsonNumberCounter::count(); });
}
