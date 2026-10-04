#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>

#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"

/**
 * @brief One periodic, greppable log line out of counters nothing on the device reports.
 *
 * Several services count the events they refuse or drop - queue full, task failed to start,
 * pool exhausted - but the counters are only reachable from tests: in the field the events
 * leave no trace. This class carries named probes to those counters and formats a single
 * line of `name=value` pairs separated by single spaces, so the numbers can be grepped out
 * of the device log by the "DIAG-COUNTERS:" prefix and plotted over time.
 *
 * The line goes to the log only. The diagnostic JSON sent to the backend is a wire contract
 * and deliberately gets nothing from here.
 *
 * Stack watermark columns come from the task scheduler. A watermark the scheduler cannot
 * measure - the host cooperative scheduler, a task that was never created - prints as "n/a":
 * an honest unknown, because a zero in this column means "stack exhausted".
 *
 * Fixed capacity on both tables; registrations past the cap are refused and counted, never
 * silent. Column names are bounded too (MAX_NAME_CHARS, refusal counted), which is what makes
 * the worst possible line a compile-time number: a caller that sizes its buffer with
 * LINE_BUFFER_CHARS can never lose a column to silent truncation - the watermark columns sit
 * at the end and would be the first to go.
 *
 * A probe is allowed to consume what it reports, and one does: max_loop_step_ms answers with the
 * worst control-loop step since the previous line and starts a new interval, because a high-water
 * mark since boot saturates on the first bad step and never moves again. So format() is not
 * idempotent, and calling it for any reason other than emitting the line takes an interval's
 * measurement with it.
 */
class CounterReport
{
public:
    using Probe = std::function<uint64_t()>;

    /**
     * Column capacity.
     *
     * Thirty-one for twenty-nine columns in use, and the headroom is the point: addCounter()
     * refuses everything past the cap, and a refused counter is one that disappears from the field
     * log without a word. A cap raised by exactly the number of columns being added leaves the next
     * addition over it again, so the room is for the column nobody has written yet. Two spare
     * columns cost 2 * sizeof(a name pointer plus a std::function) of static RAM and 2 * 46 bytes
     * in the worst-line figure below, which sizes the caller's line buffer.
     *
     * The refusal itself is reported by the column addRefusalColumn() registers, so a table that
     * does fill up says so; the headroom is what keeps a column from being lost in the first place.
     */
    static constexpr size_t MAX_COUNTERS = 31;
    static constexpr size_t MAX_TASK_STACKS = 4;

    /** The grep anchor every emitted line starts with. */
    static constexpr const char* LINE_PREFIX = "DIAG-COUNTERS:";

    /** LINE_PREFIX's length as a constant expression; the assert pins the two together. */
    static constexpr size_t LINE_PREFIX_CHARS = sizeof("DIAG-COUNTERS:") - 1;
    static_assert(__builtin_strlen(LINE_PREFIX) == LINE_PREFIX_CHARS,
                  "LINE_PREFIX and LINE_PREFIX_CHARS must describe the same string");

    /** Longest registrable column name; longer ones are refused and counted. */
    static constexpr size_t MAX_NAME_CHARS = 24;

    /**
     * The longest line format() can produce, from the same numbers the tables enforce:
     * the prefix, then per column a space, a bounded name, '=' and the widest value the
     * format can print (a uint64_t counter is up to 20 digits; a uint32_t watermark up to
     * 10, which also covers "n/a").
     */
    static constexpr size_t WORST_LINE_CHARS =
        LINE_PREFIX_CHARS
        + MAX_COUNTERS * (1 + MAX_NAME_CHARS + 1 + 20)
        + MAX_TASK_STACKS * (1 + MAX_NAME_CHARS + 1 + 10);

    /** What a caller's line buffer must hold: the worst line plus the terminator. */
    static constexpr size_t LINE_BUFFER_CHARS = WORST_LINE_CHARS + 1;

    void setTaskScheduler(ITaskScheduler* scheduler) { _scheduler = scheduler; }

    /**
     * @brief Register one counter column.
     * @param name Column name as it appears in the line; a single token, stored by pointer,
     *             so it must outlive this object (string literals do)
     * @param probe Read the current value; called at format time
     * @return false when the table is full or the arguments are unusable (refusal is counted)
     */
    bool addCounter(const char* name, Probe probe)
    {
        if (_counterCount >= MAX_COUNTERS || name == nullptr || !probe
            || strlen(name) > MAX_NAME_CHARS)
        {
            _refusedAddCount++;
            return false;
        }
        _counters[_counterCount].name = name;
        _counters[_counterCount].probe = std::move(probe);
        _counterCount++;
        return true;
    }

    /**
     * @brief Register one stack watermark column.
     * @param label Column name as it appears in the line (single token, e.g. "stack_cmd_net")
     * @param taskName Name the task was created under (e.g. "cmd-net")
     * @return false when the table is full or the arguments are unusable (refusal is counted)
     */
    bool addTaskStack(const char* label, const char* taskName)
    {
        if (_taskCount >= MAX_TASK_STACKS || label == nullptr || taskName == nullptr
            || strlen(label) > MAX_NAME_CHARS)
        {
            _refusedAddCount++;
            return false;
        }
        _tasks[_taskCount].label = label;
        _tasks[_taskCount].taskName = taskName;
        _taskCount++;
        return true;
    }

    /**
     * @brief Register the column that reports this table's own refused registrations.
     * @param name Column name as it appears in the line (single token, e.g. "counters_refused")
     * @return false when the table is full or the name is unusable (refusal is counted)
     *
     * Called before the columns it reports on, because the table refuses whatever is offered past
     * the cap: a refusal column registered last is the first one to be refused itself, and then the
     * loss it exists to report is the one nobody sees. The value is read at format time like every
     * other column, so a refusal that happens after this call shows in every line after it.
     */
    bool addRefusalColumn(const char* name)
    {
        return addCounter(name, [this] { return (uint64_t)_refusedAddCount; });
    }

    /** Registrations refused because a table was full or an argument unusable. */
    uint32_t refusedAddCount() const { return _refusedAddCount; }

    /**
     * @brief Build the line into a fixed buffer; values are read at call time.
     * @return Characters written (without the terminator); output is truncated to cap
     */
    size_t format(char* out, size_t cap) const
    {
        if (out == nullptr || cap == 0)
        {
            return 0;
        }
        out[0] = '\0';

        size_t used = appendf(out, cap, 0, "%s", LINE_PREFIX);
        for (size_t i = 0; i < _counterCount; i++)
        {
            used = appendf(out, cap, used, " %s=%llu", _counters[i].name,
                           (unsigned long long)_counters[i].probe());
        }
        for (size_t i = 0; i < _taskCount; i++)
        {
            uint32_t bytes = 0;
            if (_scheduler != nullptr
                && _scheduler->stackHighWaterMarkBytes(_tasks[i].taskName, bytes))
            {
                used = appendf(out, cap, used, " %s=%lu", _tasks[i].label, (unsigned long)bytes);
            }
            else
            {
                used = appendf(out, cap, used, " %s=n/a", _tasks[i].label);
            }
        }
        return used;
    }

private:
    struct Counter
    {
        const char* name = nullptr;
        Probe probe;
    };

    struct TaskStack
    {
        const char* label = nullptr;
        const char* taskName = nullptr;
    };

    /** snprintf at offset `used`, clamped to the buffer; returns the new used length. */
    static size_t appendf(char* out, size_t cap, size_t used, const char* format, ...)
    {
        if (used >= cap)
        {
            return used;
        }
        va_list args;
        va_start(args, format);
        int written = vsnprintf(out + used, cap - used, format, args);
        va_end(args);
        if (written < 0)
        {
            return used;
        }
        size_t next = used + (size_t)written;
        return next < cap ? next : cap - 1;
    }

    ITaskScheduler* _scheduler = nullptr;
    Counter _counters[MAX_COUNTERS];
    size_t _counterCount = 0;
    TaskStack _tasks[MAX_TASK_STACKS];
    size_t _taskCount = 0;
    uint32_t _refusedAddCount = 0;
};
