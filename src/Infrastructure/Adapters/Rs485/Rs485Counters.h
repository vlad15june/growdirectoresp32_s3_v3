#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Application/Services/CounterReport.h"
#include "ModbusRS485.h"

/**
 * @brief Add the RS-485 column to the periodic counters line.
 *
 * `rs485_drop_uninit` is the number of frames the ports refused because their UART had never been
 * begun (ModbusRS485::sendData()), summed over the ports given. Each port logs its first refusal
 * once; this column is where the rest of them are seen. The boot begins the UARTs before any frame,
 * so on a sensor module it reads zero, and anything else means a frame was offered to a line that
 * could not carry it.
 *
 * The registration lives here instead of inline in the composition root so that the column name
 * and the sum it reports are the ones a host test executes.
 *
 * @param ports The concrete ports; the pointers are kept, so they must outlive the report
 */
inline void registerRs485Counters(CounterReport& report, const std::vector<ModbusRS485*>& ports)
{
    static_assert(__builtin_strlen(ModbusRS485::DROP_COLUMN) <= CounterReport::MAX_NAME_CHARS,
                  "the RS-485 drop column name is over the bound the worst line is derived from");

    report.addCounter(ModbusRS485::DROP_COLUMN, [ports] {
        uint64_t dropped = 0;
        for (size_t i = 0; i < ports.size(); i++)
        {
            dropped += ports[i]->droppedFrameCount();
        }
        return dropped;
    });
}
