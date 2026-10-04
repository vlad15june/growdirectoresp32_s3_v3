#pragma once

#include <cstdint>

#include "Core/Domain/Interfaces/Adapters/Sensors/PortPower.h"

/**
 * @brief The stretches a sensor port could not answer in - dark, or up for less than
 *        PortPower::WAKE_UP_MS - read off the record the port keeps of its own power.
 *
 * A stretch runs from the moment the pin went down to the moment what is behind the port has had
 * its power back for WAKE_UP_MS, whoever moved the pin at either end: a step of the discovery, a
 * bus command, the preparation, a raw command, the sweep. Fed the record after every piece of work
 * on the line and at every visit of the sweep; one piece of work moves a port's pin at most once
 * each way, so no transition goes by between two looks unseen.
 */
class PortWarmUpWatch
{
public:
    struct Stretch
    {
        /** A stretch has just come to its end, at this look. */
        bool ended = false;
        uint32_t fromMs = 0;
        uint32_t untilMs = 0;
    };

    /** @brief Take in the record as it stands now; the stretch that has just ended, if one has. */
    Stretch look(const PortPower &power, uint32_t nowMs)
    {
        if (!_seen)
        {
            // The first look: a port dark or still coming up is taken as such from here - there is
            // no earlier moment on record to count it from.
            _seen = true;
            takeTheCounts(power);
            if (power.wakeUpOwedMs(nowMs) > 0)
            {
                begin((power.on && power.ons > 0) ? power.onAtMs : nowMs);
            }
        }

        if (power.offs != _offsSeen)
        {
            begin(power.offAtMs);
        }
        if (power.ons != _onsSeen)
        {
            // Up without a power-off on record since the last look: the port was dark from before
            // anything was on record, and is counted from the moment it came up.
            begin(power.onAtMs);
        }
        takeTheCounts(power);

        Stretch stretch;
        if (!_dark || power.wakeUpOwedMs(nowMs) > 0)
        {
            return stretch;
        }
        _dark = false;
        stretch.ended = true;
        stretch.fromMs = _darkSinceMs;
        stretch.untilMs = (power.ons > 0) ? power.onAtMs + PortPower::WAKE_UP_MS : nowMs;
        return stretch;
    }

    /**
     * @brief Take the record as it stands without starting a stretch: the port recovery switches a
     *        port off and on on purpose, and that darkness is its own to count.
     */
    void acknowledge(const PortPower &power)
    {
        _seen = true;
        takeTheCounts(power);
    }

    /** @brief Is the port in a stretch it cannot answer in, as of the last look? */
    bool dark() const { return _dark; }

private:
    void begin(uint32_t atMs)
    {
        if (!_dark)
        {
            _dark = true;
            _darkSinceMs = atMs;
        }
    }

    void takeTheCounts(const PortPower &power)
    {
        _onsSeen = power.ons;
        _offsSeen = power.offs;
    }

    bool _seen = false;
    uint32_t _onsSeen = 0;
    uint32_t _offsSeen = 0;
    bool _dark = false;
    uint32_t _darkSinceMs = 0;
};
