#pragma once

// Host stand-in for the system clock the Arduino ESP32 core exposes through Arduino.h:
// configTime() hands the SNTP client its servers and offsets, getLocalTime() reads the clock the
// SNTP client keeps. Reachable only through the include path of env:native (see platformio.ini).
//
// This is the device wall clock - the thing NTP sets and a reboot loses - and it is deliberately
// separate from Sim/VirtualClock.h, which is uptime for the scheduler. Code whose behaviour
// depends on the difference between the two (a clock that steps while uptime does not) needs both
// and drives them apart on purpose.
//
// The clock is held as civil seconds interpreted as UTC, so the host's own timezone never enters
// the picture: what a test sets is exactly what the code under test reads back.

#include <cstdint>
#include <ctime>

inline time_t hostWallClockSeconds = 0;
inline bool hostWallClockValid = false;

// Offsets the last configTime() call asked for, so a test can assert the adapter passed on what
// it was configured with.
inline long hostConfigTimeGmtOffsetSec = 0;
inline int hostConfigTimeDaylightOffsetSec = 0;
inline int hostConfigTimeCalls = 0;

inline void hostWallClockSet(const struct tm& value)
{
    struct tm copy = value;
    hostWallClockSeconds = timegm(&copy);
    hostWallClockValid = true;
}

inline void hostWallClockSetSeconds(time_t seconds)
{
    hostWallClockSeconds = seconds;
    hostWallClockValid = true;
}

inline void hostWallClockAdvance(long seconds)
{
    hostWallClockSeconds += seconds;
}

/** @brief Come up as a board without a battery-backed RTC does: no time at all. */
inline void hostWallClockInvalidate()
{
    hostWallClockSeconds = 0;
    hostWallClockValid = false;
}

inline struct tm hostWallClockGet()
{
    struct tm out;
    memset(&out, 0, sizeof(out));
    gmtime_r(&hostWallClockSeconds, &out);
    return out;
}

// The core waits up to `ms` for the clock to become valid and reports failure if it never does.
// Nothing here sleeps: the clock is whatever the test set, so the answer is immediate.
inline bool getLocalTime(struct tm* info, uint32_t ms = 5000)
{
    (void)ms;
    if (info == nullptr || !hostWallClockValid)
    {
        return false;
    }
    *info = hostWallClockGet();
    return true;
}

inline void configTime(long gmtOffsetSec, int daylightOffsetSec,
                       const char* server1, const char* server2 = nullptr,
                       const char* server3 = nullptr)
{
    (void)server1;
    (void)server2;
    (void)server3;
    ++hostConfigTimeCalls;
    hostConfigTimeGmtOffsetSec = gmtOffsetSec;
    hostConfigTimeDaylightOffsetSec = daylightOffsetSec;
}
