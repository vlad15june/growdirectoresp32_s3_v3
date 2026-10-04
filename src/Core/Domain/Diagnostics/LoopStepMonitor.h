#ifndef LOOP_STEP_MONITOR_H
#define LOOP_STEP_MONITOR_H

#include <atomic>
#include <cstdint>

/**
 * @brief How long the slowest control-loop step took, so that a stalled loop leaves a trace.
 *
 * The control loop drives every relay, every rule and the status LED, and the length of its worst
 * step is the one number that says whether the module is responsive. Nothing on the device measured
 * it: a step that spent two seconds inside a POST looked exactly like one that spent two
 * milliseconds, and the only witness was the LED somebody happened to be watching.
 *
 * Process-wide and static for the same reason AllocationFailureCounter is. The loop service is
 * built by a factory and replaced whenever the module changes mode, while "the worst step this
 * module has taken" outlives any one of those instances; and the reader - the DIAG-COUNTERS line -
 * is wired in the composition root, before any loop service exists. Atomic because the value is
 * written by the loop task and read by whichever task formats the line.
 *
 * take() is a read that consumes. What it returns is the worst step since the previous take(), so
 * the periodic reader reports one interval instead of a high-water mark that saturates on the first
 * bad step and never moves again. The consequence is worth stating: with no reader - the diagnostic
 * log channel switched off - nothing takes the value and it keeps the worst step since boot.
 * peek() is the same number without consuming it.
 */
class LoopStepMonitor
{
public:
    /** Report one finished control-loop step. */
    static void record(uint32_t stepMs);

    /** The worst step since the last take(), and start a new interval. */
    static uint32_t take();

    /** The worst step since the last take(), leaving the interval open. */
    static uint32_t peek();

    /** Forget what has been measured. For a test fixture that owns the whole process. */
    static void reset();

private:
    static std::atomic<uint32_t> _worstStepMs;
};

#endif // LOOP_STEP_MONITOR_H
