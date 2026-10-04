#pragma once

#include <cstdint>

/**
 * @brief Decides whether the external watchdog may still be kicked on the control loop's behalf.
 *
 * The external watchdog chip is kicked by a small high-priority task, because the chip's own
 * period is a hardware property that must not depend on how long a loop iteration happens to
 * take. But a kicker that runs no matter what would keep a dead module alive: the hardware
 * watchdog would never learn that the control loop stopped. This gate is the missing link -
 * the kicker asks it before every kick, and the gate only says yes while the control loop has
 * reported itself alive recently.
 *
 * The stall limit is the task watchdog's threshold: a loop that has not completed an iteration
 * for that long has either already been caught by the task watchdog (panic and reset), or the
 * task watchdog itself is broken - and a broken software watchdog is exactly the case the
 * hardware one exists for. Withholding the kick then lets the chip reset the module on its own
 * period. Legal long pauses on the loop are shorter than the task watchdog threshold by that
 * threshold's own construction, so they never starve the chip.
 *
 * The refusal latches: once the gate has withheld a kick, it withholds every kick after it,
 * even if the loop beats again. A loop that went silent past the threshold on a module that is
 * still running means two mechanisms failed at once - the loop stalled *and* the task watchdog
 * did not reset it when it was due to. Nothing in that module deserves trust any more, a
 * "revival" least of all; the device must come back clean, through the hardware reset, with
 * the cause on record. A gate that could be talked out of firing would be a weaker watchdog.
 *
 * Unarmed until the loop's first beat: before the loop exists (setup), the startup task
 * watchdog is the guard, and the chip must keep being kicked through a legally long boot.
 *
 * Header-only and free of device headers, so the decision is the same code on the device and
 * under the host tests.
 */
class LoopLivenessGate
{
public:
    explicit LoopLivenessGate(uint32_t stallLimitMs = 0) : _stallLimitMs(stallLimitMs) {}

    /** @brief The control loop reports it completed an iteration. Arms the gate on first call. */
    void noteAlive(uint32_t nowMs)
    {
        _lastBeatMs = nowMs;
        _armed = true;
    }

    /** @brief May the kicker still vouch for the module? Latches on the first refusal. */
    bool allowKick(uint32_t nowMs)
    {
        if (_latched)
        {
            return false;
        }
        if (!_armed || _stallLimitMs == 0)
        {
            return true;
        }
        // The kicker samples the clock and then asks, while the loop beats on another core: the
        // last beat can therefore be a millisecond or two newer than nowMs, and that is a live
        // loop, not a silence of the whole counter range. Read as signed, the difference of two
        // close values is the true age with its sign - which also survives the millis()
        // wraparound, where the two values sit on either side of the boundary.
        const int32_t sinceLastBeatMs = (int32_t)(nowMs - _lastBeatMs);
        if (sinceLastBeatMs > 0 && (uint32_t)sinceLastBeatMs > _stallLimitMs)
        {
            _latched = true;
            return false;
        }
        return true;
    }

    bool armed() const { return _armed; }
    /** @brief True once a kick was withheld; only a reset clears it. */
    bool latched() const { return _latched; }
    uint32_t stallLimitMs() const { return _stallLimitMs; }
    void setStallLimitMs(uint32_t stallLimitMs) { _stallLimitMs = stallLimitMs; }

private:
    uint32_t _stallLimitMs;
    uint32_t _lastBeatMs = 0;
    bool _armed = false;
    bool _latched = false;
};
