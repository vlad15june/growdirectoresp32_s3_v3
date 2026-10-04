#ifndef STORED_TIME_SEED_POLICY_H
#define STORED_TIME_SEED_POLICY_H

#include <stdint.h>
#include <time.h>

/**
 * @brief Decides when the time kept in EEPROM is worth another flash write.
 *
 * The board has no battery-backed RTC, so the stored time is what the system clock starts from
 * after a reset, until a time server answers. That makes it a seed, not a clock: it has to be
 * good enough to bridge a boot, and every refresh of it costs a full EEPROM commit - an erase and
 * reprogram of the settings sector, whose endurance is finite.
 *
 * Two reasons to write, and nothing else:
 *  - the seed has aged past PLANNED_WRITE_INTERVAL_SEC, so a boot would start from a stale time;
 *  - the clock stepped, which means the stored seed no longer describes reality at all.
 *
 * How stale the seed is allowed to get is the whole trade: it is how far behind the module comes
 * back from a power cut it takes with no network to correct it, and the rules that switch relays
 * by time of day start running long before a server answers.
 *
 * A step is told from ordinary running by comparing the clock against uptime, which no time
 * server can move: between two moments the wall clock and the monotonic counter must advance by
 * the same amount, and if they disagree by more than CLOCK_STEP_THRESHOLD_SEC the clock was set,
 * not run. That covers the correction after a boot from a stale seed, a module that comes back
 * from days offline, and a change of the UTC or daylight-saving offset the adapter is configured
 * with.
 *
 * The class holds no clock of its own: the caller supplies both readings.
 */
class StoredTimeSeedPolicy
{
public:
    /**
     * @brief Age at which the seed is refreshed even though nothing happened.
     *
     * Five minutes, which is the interval a time server is asked at, so a settled module refreshes
     * the seed once per answer: 288 commits a day. That is affordable because the EEPROM is
     * emulated over NVS - a commit rewrites the settings blob into free space and erases a sector
     * only when the partition runs out of it, so the 288 commits cost a fraction of that many
     * sector erases, and the flash fitted to this board is rated for 100k of them.
     */
    static const long PLANNED_WRITE_INTERVAL_SEC = 300;

    /**
     * @brief Disagreement between clock and uptime that counts as the clock having been set.
     *
     * Well above anything running can produce - the crystal drifts by seconds a day, so tens of
     * milliseconds over an interval between writes - and well below the smallest step worth
     * recording: the shortest offset change any timezone uses is 15 minutes.
     */
    static const long CLOCK_STEP_THRESHOLD_SEC = 60;

    StoredTimeSeedPolicy()
        : _hasSeed(false),
          _seedWallSeconds(0),
          _seedUptimeMs(0)
    {
    }

    /** @brief True until the first write of this boot, then once the seed has aged out. */
    bool isPlannedWriteDue(long long wallSeconds) const
    {
        if (!_hasSeed)
        {
            return true;
        }
        return (wallSeconds - _seedWallSeconds) >= PLANNED_WRITE_INTERVAL_SEC;
    }

    /** @brief True when the clock moved by something uptime cannot account for. */
    bool isClockStepDetected(long long wallSeconds, uint32_t uptimeMs) const
    {
        if (!_hasSeed)
        {
            return false;
        }

        // Unsigned arithmetic, so the millisecond counter rolling over at 2^32 (49.7 days)
        // subtracts correctly instead of producing a step out of nowhere.
        const uint32_t elapsedMs = uptimeMs - _seedUptimeMs;
        const long long expected = _seedWallSeconds + static_cast<long long>(elapsedMs / 1000U);

        long long drift = wallSeconds - expected;
        if (drift < 0)
        {
            drift = -drift;
        }
        return drift > CLOCK_STEP_THRESHOLD_SEC;
    }

    /** @brief Remember the reading that has just been stored. */
    void recordWrite(long long wallSeconds, uint32_t uptimeMs)
    {
        _hasSeed = true;
        _seedWallSeconds = wallSeconds;
        _seedUptimeMs = uptimeMs;
    }

    /**
     * @brief Seconds of the calendar time in `value`, counted from 1970-01-01 00:00:00.
     *
     * Not mktime(): mktime reads the fields as local time and converts them through the
     * timezone database, which would cancel exactly the offset changes this policy exists to
     * notice, and it rewrites the struct it is given. This is plain calendar arithmetic on the
     * fields - proleptic Gregorian, valid for any year the fields can hold.
     */
    static long long civilSeconds(const struct tm &value)
    {
        long long year = static_cast<long long>(value.tm_year) + 1900;
        const unsigned month = static_cast<unsigned>(value.tm_mon) + 1;
        const unsigned day = static_cast<unsigned>(value.tm_mday);

        // Days since 1970-01-01, counting March as the first month so that the leap day falls at
        // the end of the year and the century rules stay a plain division.
        year -= (month <= 2) ? 1 : 0;
        const long long era = (year >= 0 ? year : year - 399) / 400;
        const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
        const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
        const long long days = era * 146097LL + static_cast<long long>(dayOfEra) - 719468LL;

        return days * 86400LL +
               static_cast<long long>(value.tm_hour) * 3600LL +
               static_cast<long long>(value.tm_min) * 60LL +
               static_cast<long long>(value.tm_sec);
    }

private:
    bool _hasSeed;
    long long _seedWallSeconds;
    uint32_t _seedUptimeMs;
};

#endif // STORED_TIME_SEED_POLICY_H
