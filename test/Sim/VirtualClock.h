#pragma once

#include <cstdint>

namespace hostsim {

/**
 * @brief The single source of time on the host.
 *
 * Everything in the host harness - SimArduino::millis()/micros(), the cooperative scheduler,
 * network latencies - reads this clock and nothing else, so a test moves time by moving one
 * object. Time only ever moves forward, and only when something asks it to: a test covering a
 * 30 second timeout runs in microseconds of real time. That is not a convenience. A timing
 * test that takes 30 real seconds stops being run, and a test that is not run is not a proof.
 *
 * Microseconds are the base unit because IArduinoAdapter exposes both millis() and micros()
 * and the two have to stay consistent with each other.
 */
class VirtualClock {
public:
    uint64_t nowUs() const { return _nowUs; }
    uint64_t nowMs() const { return _nowUs / 1000ULL; }

    void advanceUs(uint64_t us) { _nowUs += us; }
    void advanceMs(uint64_t ms) { advanceUs(ms * 1000ULL); }

    /** @brief Move time to the given point. A request to move backwards is ignored. */
    void advanceToUs(uint64_t us)
    {
        if (us > _nowUs)
        {
            _nowUs = us;
        }
    }

private:
    uint64_t _nowUs = 0;
};

} // namespace hostsim
