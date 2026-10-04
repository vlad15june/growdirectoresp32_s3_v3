#pragma once

// Host stand-in for the ESP32Time library, reachable only through the include path of env:native
// (see platformio.ini). The library is a thin wrapper over settimeofday()/localtime(); the only
// entry point production code uses is setTimeStruct(), which pushes a time into the system clock.
// Here it pushes into the host wall clock that getLocalTime() reads back.

#include <Arduino.h>

class ESP32Time
{
public:
    ESP32Time() {}
    explicit ESP32Time(long offsetSeconds) { (void)offsetSeconds; }

    void setTimeStruct(struct tm value) { hostWallClockSet(value); }

    struct tm getTimeStruct() { return hostWallClockGet(); }

    unsigned long getEpoch() { return static_cast<unsigned long>(hostWallClockSeconds); }
};
