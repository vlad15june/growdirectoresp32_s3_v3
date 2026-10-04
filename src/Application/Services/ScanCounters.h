#pragma once

#include <cstdint>

#include "Application/Services/CounterReport.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"

/**
 * @brief Add the sensor discovery columns to the periodic counters line.
 *
 * Three of the counters the discovery keeps, and three only: the line is one log line, and a column
 * that tells a field reader nothing takes the room of one that would. The three are the ones a
 * reader cannot get from a module as it stands: whatever the log has to say about them it says at
 * the moment it happens, and a reader who was not there then has nothing to ask.
 *
 * `scan_adopted` - sensors the discovery wrote into the registry as NEW over the life of the
 * module. The line a pass ends with counts that pass alone, so this is the only number that says
 * how many times this module has ever taken a sensor in - the question asked when a row is not
 * there any more and nobody knows whether it ever was.
 *
 * `scan_addr_refused` - rows whose device answered the module and would not take the address it was
 * written. The state is terminal: such a row is written to once and then no address write of any
 * pass goes near it again, so the device stays on the address its model leaves the factory with,
 * ready to collide with the next sensor of that model, until a person asks for its port or the
 * module restarts. The refusal itself writes one line as it happens, carrying this same total; the
 * column is what says the total now, to a reader who was not watching the log that day.
 *
 * `scan_identify_steps` - steps of the full identification procedure: the expensive rung the
 * ladder exists to ration. Every one of them takes the power off the ports sharing the wire, so a
 * number that keeps climbing from line to line on a settled module is the rationing not holding -
 * and that is the one failure of the ladder that makes no sound of its own.
 *
 * The registration lives here instead of inline in the composition root so that the column names,
 * their order and the sources they read are the ones a host test executes.
 */
inline void registerScanCounters(CounterReport& report, StartupSensorScanUseCase& scan)
{
    // Column names are what makes the worst line a compile-time number, so these have to fit the
    // bound that arithmetic assumes - a longer name would be refused at run time and the column
    // would simply not be there.
    static_assert(sizeof("scan_adopted") - 1 <= CounterReport::MAX_NAME_CHARS,
                  "the scan_adopted column name is over the bound the worst line is derived from");
    static_assert(sizeof("scan_addr_refused") - 1 <= CounterReport::MAX_NAME_CHARS,
                  "the scan_addr_refused column name is over the bound the worst line is derived "
                  "from");
    static_assert(sizeof("scan_identify_steps") - 1 <= CounterReport::MAX_NAME_CHARS,
                  "the scan_identify_steps column name is over the bound the worst line is derived "
                  "from");

    report.addCounter("scan_adopted", [&scan] { return (uint64_t)scan.adoptedCount(); });
    report.addCounter("scan_addr_refused",
                      [&scan] { return (uint64_t)scan.factoryAddressMoveRefusedCount(); });
    report.addCounter("scan_identify_steps",
                      [&scan] { return (uint64_t)scan.identifyStepCount(); });
}
