#ifndef WIFI_ROAM_POLICY_H
#define WIFI_ROAM_POLICY_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Core/Domain/Data/Entities/WifiNode.h"

/**
 * @brief Whether to move the station to another access point of the same network, and to which one.
 *
 * A station chooses its access point once, at the association, and keeps it until it disappears:
 * the multi-network connect scans, takes the strongest BSSID of a configured SSID and associates,
 * and nothing ever looks again. Where several access points share one SSID, one flap of the near
 * one is enough to leave the module on the far one for months - at a signal that costs every
 * transfer more airtime, more retransmissions and, because retransmitted frames are held in the
 * driver's buffers, more heap.
 *
 * Moving is not free either. This stack cannot re-associate without dropping the association and
 * the address with it (see IWifiRoamRadio::roamTo), so a move costs what a reconnect costs: the
 * sockets above it are rebuilt, the TLS session and the broker session with them. That is the shape
 * of every rule here - they are all about *not* moving:
 *
 *  - never while the current access point is strong enough (WEAK_DBM). There is nothing to buy at
 *    -45 dBm, and the module must not spend a reconnect finding that out.
 *  - never for less than HYSTERESIS_DB of gain, so two access points of similar signal cannot
 *    trade the station back and forth as the readings wander.
 *  - never more often than MIN_ROAM_INTERVAL_MS, whatever the scans say.
 *  - never back to the one just left, for STAY_PUT_AFTER_ROAM_MS. An access point that reboots in
 *    a loop must not be able to pull the station after it every time it comes back.
 *  - never on the strength of a single scan: the same node has to be the better one in
 *    CONFIRMATIONS_NEEDED scans in a row, because one scan catches every node at one moment of its
 *    own traffic.
 *  - never while the module is in the middle of something. What counts as busy is the caller's
 *    judgement, passed in as `quiet`.
 *
 * Time is passed in rather than read, nothing here allocates, logs or talks to a radio: this is the
 * part a host test can drive through a month of scans in a millisecond.
 */
class WifiRoamPolicy
{
public:
    /**
     * At or above this, the signal is good and the module stays where it is.
     *
     * -50 dBm is comfortably inside the range where the rate does not depend on the signal any
     * more; the test setup sees -3x dBm on the near access point and -5x on the far one, and the heap
     * lows of every long run were taken on the far one.
     */
    static const int WEAK_DBM = -50;

    /**
     * How much better the other access point has to be, in dB, before it is worth a reconnect.
     *
     * Twelve is four times the power. It is far outside the few dB two readings of the same
     * unchanged link differ by, and it is about the distance between the two access points of the
     * test setup - so the step this policy is meant to take is the smallest one it can take.
     */
    static const int HYSTERESIS_DB = 12;

    /** No second move within this of the last one, whatever the scans say. */
    static const uint32_t MIN_ROAM_INTERVAL_MS = 10u * 60u * 1000u;

    /** The access point just left is not a candidate again for this long. */
    static const uint32_t STAY_PUT_AFTER_ROAM_MS = 30u * 60u * 1000u;

    /** How often to look around while the signal is weak. Not at all while it is not. */
    static const uint32_t SCAN_INTERVAL_MS = 5u * 60u * 1000u;

    /**
     * The floor under everything, including the radio's own warning.
     *
     * The warning says the average signal has fallen through the threshold, and the driver is free
     * to say it again on the next measurement while the signal stays there - so a look triggered by
     * a warning alone could otherwise follow the previous one immediately, and a module on a poor
     * signal would spend its airtime looking instead of working. A minute keeps the warning worth
     * having (five times sooner than the interval) and bounds what it can cost.
     */
    static const uint32_t MIN_GAP_BETWEEN_LOOKS_MS = 60u * 1000u;

    /** Scans in a row that have to name the same better access point before it is taken. */
    static const uint8_t CONFIRMATIONS_NEEDED = 2;

    /** @brief What weigh() decided: move to `target`, or stay. */
    struct Verdict
    {
        bool move;
        WifiNode target;
    };

    /** @brief Is the signal poor enough that another access point could be worth the move? */
    static bool isWeak(int rssi) { return rssi < WEAK_DBM; }

    /**
     * @brief Is it time to look at what else is in range?
     *
     * False on a strong signal, which is what keeps the radio on its channel and this whole
     * mechanism out of the way of a module that has no problem.
     */
    bool scanIsDue(uint32_t nowMs, int currentRssi) const
    {
        if (!isWeak(currentRssi))
        {
            return false;
        }
        if (!_scanned)
        {
            return true;
        }
        const uint32_t sinceTheLast = (uint32_t)(nowMs - _lastScanAtMs);
        return sinceTheLast >= (_scanAskedFor ? MIN_GAP_BETWEEN_LOOKS_MS : SCAN_INTERVAL_MS);
    }

    /** @brief A look has begun; the next one is due an interval after this one. */
    void noteScanStarted(uint32_t nowMs)
    {
        _lastScanAtMs = nowMs;
        _scanned = true;
        _scanAskedFor = false;
    }

    /**
     * @brief The radio itself reports the signal has fallen through the threshold: look sooner.
     *
     * The accelerator, not a second decision: it brings the next look forward from the interval to
     * MIN_GAP_BETWEEN_LOOKS_MS, and every rule below still applies to what the look finds.
     */
    void askForScanNow() { _scanAskedFor = true; }

    /**
     * @brief There is nothing to weigh - no association, or one that is strong enough.
     *
     * Confirmations are counted over consecutive looks, so a stretch with no looking at all has to
     * clear them: otherwise a candidate confirmed once an hour ago would count towards the two in
     * a row it needs.
     */
    void forgetCandidate() { _confirmations = 0; }

    /**
     * @brief Weigh what one look found against where the station is.
     *
     * Called once per look, in order, with everything the look saw of this network. The candidate's
     * confirmation is counted here whatever the answer, so that a module which happens to be busy
     * every time it looks still accumulates the confirmations instead of starting over.
     *
     * @param nowMs      the module's clock
     * @param current    the access point the station is associated with
     * @param candidates what the look saw, this network only; the current one may be among them
     * @param count      how many
     * @param quiet      false when the module is in the middle of something a reconnect would break
     */
    Verdict weigh(uint32_t nowMs, const WifiNode& current, const WifiNode* candidates, size_t count,
                  bool quiet)
    {
        Verdict verdict;
        verdict.move = false;
        memset(&verdict.target, 0, sizeof(verdict.target));

        const WifiNode* best = nullptr;
        if (isWeak(current.rssi) && candidates != nullptr)
        {
            for (size_t index = 0; index < count; index++)
            {
                const WifiNode& node = candidates[index];
                if (sameBssid(node.bssid, current.bssid) || isJustLeft(nowMs, node.bssid))
                {
                    continue;
                }
                if (node.rssi - current.rssi < HYSTERESIS_DB)
                {
                    continue;
                }
                if (best == nullptr || node.rssi > best->rssi)
                {
                    best = &node;
                }
            }
        }

        if (best == nullptr)
        {
            _confirmations = 0;
            return verdict;
        }

        if (_confirmations > 0 && sameBssid(_wanted, best->bssid))
        {
            if (_confirmations < 0xFF)
            {
                _confirmations++;
            }
        }
        else
        {
            memcpy(_wanted, best->bssid, sizeof(_wanted));
            _confirmations = 1;
        }

        if (_confirmations < CONFIRMATIONS_NEEDED || !quiet || !enoughTimeSinceTheLastMove(nowMs))
        {
            return verdict;
        }

        verdict.move = true;
        verdict.target = *best;
        return verdict;
    }

    /**
     * @brief The station has been sent to another access point.
     *
     * Counted from the attempt and not from its outcome: a move that failed cost the association
     * all the same, and repeating it on the next look is exactly what the interval is for.
     */
    void noteRoamStarted(uint32_t nowMs, const WifiNode& from)
    {
        _lastRoamAtMs = nowMs;
        _roamed = true;
        memcpy(_leftBssid, from.bssid, sizeof(_leftBssid));
        _leftAtMs = nowMs;
        _leftKnown = true;
        _confirmations = 0;
    }

    /** @brief How many scans in a row have named the candidate the policy is watching. */
    uint8_t confirmations() const { return _confirmations; }

private:
    /**
     * The one access point kept out of the running, and the whole anti-ping-pong rule.
     *
     * One is enough for what this guards against: the station can only have come from one place,
     * and it cannot move again for MIN_ROAM_INTERVAL_MS anyway, so a second ban could only ever
     * apply to a move at least ten minutes older than this one.
     */
    bool isJustLeft(uint32_t nowMs, const uint8_t* bssid) const
    {
        return _leftKnown && sameBssid(bssid, _leftBssid) &&
               (uint32_t)(nowMs - _leftAtMs) < STAY_PUT_AFTER_ROAM_MS;
    }

    bool enoughTimeSinceTheLastMove(uint32_t nowMs) const
    {
        return !_roamed || (uint32_t)(nowMs - _lastRoamAtMs) >= MIN_ROAM_INTERVAL_MS;
    }

    bool _scanned = false;
    bool _scanAskedFor = false;
    uint32_t _lastScanAtMs = 0;

    uint8_t _wanted[6] = {0, 0, 0, 0, 0, 0};
    uint8_t _confirmations = 0;

    bool _roamed = false;
    uint32_t _lastRoamAtMs = 0;

    bool _leftKnown = false;
    uint8_t _leftBssid[6] = {0, 0, 0, 0, 0, 0};
    uint32_t _leftAtMs = 0;
};

#endif // WIFI_ROAM_POLICY_H
