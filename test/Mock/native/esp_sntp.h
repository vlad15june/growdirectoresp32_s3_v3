#pragma once

// Host stand-in for the ESP-IDF SNTP client, reachable only through the include path of
// env:native (see platformio.ini). Only what NTPTimeAdapter uses is here: the sync interval, the
// notification callback and start/stop.
//
// On the device the callback is what fires when a server answers and the system clock has been
// set. hostSntpNotifySync() is that moment, and it is the entry point a test drives - the
// production registration in NTPTimeAdapter::setup() is what puts a callback here to fire.

#include <sys/time.h>

typedef void (*sntp_sync_time_cb_t)(struct timeval* tv);

inline sntp_sync_time_cb_t hostSntpSyncCallback = nullptr;
inline uint32_t hostSntpSyncIntervalMs = 0;
inline int hostSntpRestartCalls = 0;
inline int hostSntpStopCalls = 0;

inline void sntp_set_sync_interval(uint32_t intervalMs)
{
    hostSntpSyncIntervalMs = intervalMs;
}

inline void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t callback)
{
    hostSntpSyncCallback = callback;
}

inline void sntp_stop()
{
    ++hostSntpStopCalls;
}

inline void sntp_restart()
{
    ++hostSntpRestartCalls;
}

inline void hostSntpReset()
{
    hostSntpSyncCallback = nullptr;
    hostSntpSyncIntervalMs = 0;
    hostSntpRestartCalls = 0;
    hostSntpStopCalls = 0;
}

/** @brief Deliver one "the server answered and the clock is set" notification. */
inline bool hostSntpNotifySync()
{
    if (hostSntpSyncCallback == nullptr)
    {
        return false;
    }

    struct timeval now;
    gettimeofday(&now, nullptr);
    hostSntpSyncCallback(&now);
    return true;
}
