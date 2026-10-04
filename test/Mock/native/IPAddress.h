#pragma once

// IPAddress stand-in for the native (host) test environment.
//
// The core offers connect() in two families, one taking a host name and one taking an address,
// and production code that brackets both names this type. Only what such a test needs is
// declared: build an address, hand it back, and compare which one arrived.
//
// Reachable only through the include path of env:native (see platformio.ini); the device build
// uses the real header from the Arduino core.

#include <Arduino.h>

#include <cstdint>
#include <cstdio>

class IPAddress
{
public:
    IPAddress() : _value(0) {}

    IPAddress(uint8_t first, uint8_t second, uint8_t third, uint8_t fourth)
        : _value((static_cast<uint32_t>(first) << 24) | (static_cast<uint32_t>(second) << 16) |
                 (static_cast<uint32_t>(third) << 8) | static_cast<uint32_t>(fourth))
    {
    }

    uint32_t hostValue() const { return _value; }

    // The dotted form the core prints, for production code that puts an address into a log line.
    String toString() const
    {
        char text[16];
        snprintf(text, sizeof(text), "%u.%u.%u.%u",
                 static_cast<unsigned>((_value >> 24) & 0xFF), static_cast<unsigned>((_value >> 16) & 0xFF),
                 static_cast<unsigned>((_value >> 8) & 0xFF), static_cast<unsigned>(_value & 0xFF));
        return String(text);
    }

    bool operator==(const IPAddress &other) const { return _value == other._value; }
    bool operator!=(const IPAddress &other) const { return _value != other._value; }

private:
    uint32_t _value;
};
