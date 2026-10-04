#pragma once

#include <cstdint>

/**
 * @brief What a sensor port's power pin has done: whether it is up, and when it last went up and
 *        down.
 *
 * Kept by whatever drives the pin (ModbusRS485 on the board), and read by whoever must not talk to
 * what is behind the port before it has come up - the address write, the model scan, the raw
 * commands, the sweep - whoever it was that switched it on. One record per port, updated at every
 * transition and at no other time.
 */
struct PortPower
{
    /**
     * @brief How long a sensor needs its port's power before it answers anything.
     *
     * This firmware's belief, not a measurement: the model scan and the address write have always
     * waited this long after they switch a port on (SensorReader::scanModbusSensor(),
     * ModbusSensorReader::isolatePortForAddressWrite()).
     */
    static const uint32_t WAKE_UP_MS = 10000;

    /** The pin is up now. */
    bool on = false;
    /** Transitions so far, and the moment of the last of each, in the driver's clock. */
    uint32_t ons = 0;
    uint32_t onAtMs = 0;
    uint32_t offs = 0;
    uint32_t offAtMs = 0;

    /** @brief The pin has gone up, and the power is there to use from this moment. */
    void wentOn(uint32_t nowMs)
    {
        on = true;
        ons++;
        onAtMs = nowMs;
    }

    /** @brief The pin has gone down at this moment. */
    void wentOff(uint32_t nowMs)
    {
        on = false;
        offs++;
        offAtMs = nowMs;
    }

    /**
     * @brief How much longer what is behind the port needs before it can answer; 0 when it can.
     *
     * A dark port owes the whole of it, from whenever its power comes. A port that is up without
     * ever having gone up - a stand whose line is simply there - has been up for good. Read off an
     * age rather than a moment, so it stays right across the wrap of the clock.
     */
    uint32_t wakeUpOwedMs(uint32_t nowMs) const
    {
        if (!on)
        {
            return WAKE_UP_MS;
        }
        if (ons == 0)
        {
            return 0;
        }
        const uint32_t upForMs = nowMs - onAtMs;
        return (upForMs < WAKE_UP_MS) ? WAKE_UP_MS - upForMs : 0;
    }
};
