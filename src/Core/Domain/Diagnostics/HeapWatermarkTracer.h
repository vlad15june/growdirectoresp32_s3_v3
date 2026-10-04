#ifndef HEAP_WATERMARK_TRACER_H
#define HEAP_WATERMARK_TRACER_H

#include <cstdint>

#include "Core/Domain/Diagnostics/BackgroundActBoard.h"

#ifdef GD_HEAP_FORENSICS
#include "Core/Domain/Diagnostics/HeapTaskLedger.h"
#endif

/**
 * @brief Notices the step on which the heap watermark broke lower, so that a dip has a moment.
 *
 * The platform keeps one number about how bad the heap ever got: the minimum free heap since
 * boot (ESP.getMinFreeHeap() -> heap_caps_get_minimum_free_size), which the diagnostic snapshot
 * carries every thirty seconds as minFreeHeap. It says how deep the module went and nothing about
 * when. A dip that opens and closes between two snapshots leaves the free-heap figure of both of
 * them at the steady level: the snapshots describe a module that never came near the floor, and
 * the one number that says it did belongs to no moment and to no piece of work.
 *
 * So the watermark is read once per control-loop step and compared with the previous reading. A
 * step is milliseconds, which puts the report beside the log lines of whatever was running when
 * the floor broke - the "on what", which no snapshot can answer.
 *
 * sample() and takeReport() are separate on purpose. sample() compares and remembers; it formats
 * nothing, prints nothing and allocates nothing, so it is safe at the very moment the heap is at
 * its floor. The line is handed out afterwards, to a caller in a position to print it once the
 * memory has been given back.
 *
 * Two gates keep the channel usable. A watermark at or above REPORT_BELOW_BYTES is not reported at
 * all - the module passes those levels in ordinary work and they say nothing - and after a report
 * the watermark has to fall another REPORT_STEP_BYTES before the next one, so a slow sag costs a
 * handful of lines instead of one per step.
 *
 * The first reading is a baseline and is never reported: a drop that happened before the tracer
 * was watching is not news it can date, and the level it inherits is in the snapshot already.
 *
 * A report also carries what the background tasks were doing, when there is a board to ask. The
 * board is copied by the reading that found the low, not by whoever prints the line a step later:
 * by then the work that took the memory has usually moved on. The window the copy is read against
 * is the span since the previous reading - the only span the new low can have happened in - so an
 * act that was over before that reading is not held against its task.
 *
 * ### When the memory came back
 *
 * A low is a moment; what took the memory may give it back within the step or hold it for
 * seconds, and the two are different kinds of work. So the reading that finds a low is also told
 * the free heap at that moment and the length of the step it closes (noteTheMoment()). Free heap
 * back at an ordinary level says the dip opened and closed inside the step. Free heap still down
 * says something is holding on past the end of the step - which the step's own work cannot do -
 * and opens a watch: the caller asks the free heap once per step, and only while the watch is
 * open, until it is back at an ordinary level (pollRelease()), and that moment is one more line.
 * A watch that sees no release within WATCH_LIMIT_MS says so once and ends: what outlives it is
 * not a dip but a level the module has moved to, and the diagnostic snapshot shows levels.
 *
 * "Ordinary" is REPORT_BELOW_BYTES, the same line the trace already draws between levels worth a
 * report and levels that are not. A lower watermark found while a watch is open belongs to the
 * same episode - a sag is one holding-on, not many - so the watch keeps the moment it opened and
 * closes once.
 *
 * None of this costs a settled module anything. needsAttention() is one flag that covers a line
 * waiting and a watch open; while it is clear the caller neither prints nor asks for the free
 * heap.
 *
 * Owned by the control loop and deliberately not thread-safe: the task that samples is the task
 * that reports.
 *
 * ### The heap-forensics build (GD_HEAP_FORENSICS)
 *
 * Two things the trace above cannot do, both at a price no fleet image pays - so neither exists
 * without the flag.
 *
 * Episodes. The platform's watermark only ever falls and cannot be reset, so after one deep low
 * every shallower dip is invisible for the rest of the uptime. That build therefore also
 * looks at the free heap itself, once per step (sampleFreeHeap()): a free heap below
 * REPORT_BELOW_BYTES that is not this reading's since-boot low is an "episode low", with a line of
 * its own and the same watch for the release. Inside an episode the same REPORT_STEP_BYTES
 * hysteresis applies, against the last low said in it. The episode is forgotten - and the next dip
 * is said again, however shallow - only once the heap has been seen back at the ordinary level:
 * by the watch that closes with "released", or, after a watch that ran out of time, by the first
 * reading that finds it back. Never while a watch is open. What this sampling cannot see is a dip
 * that opens and closes inside one step; that one still reaches the log only as a new watermark.
 * One corner of the two hysteresises: a since-boot low less than REPORT_STEP_BYTES under the last
 * one reported is not a line of its own, so when it opens an episode it is said as an episode low
 * whose watermark is the low itself - "episode low 10000 (watermark 10000)".
 *
 * Holders. A low that finds every task of the board idle was made by a task the firmware does not
 * own, and the platform can say which: the reading that found the low, while the memory is still
 * held, is followed by a walk of the heap that adds the blocks up per task (holdersAreDue(),
 * noteTheHolders()). The walk locks the heap for milliseconds, so a sag does not get one per line:
 * the first low of an episode is accounted for, and after that a low only when the free heap is a
 * quarter below the last one that was - closer together the nearer the floor, thirteen at the
 * very most between the threshold and a kilobyte, four for a fall from 39 K to 10 K.
 *
 * And a module that hovers at the ordinary level does not get one per crossing. The first account
 * of an episode is rationed: ACCOUNTED_EPISODES_IN_A_ROW episodes may have it back to back - a link
 * coming back is a handshake and another right behind it, and both are wanted - and after that
 * one episode per ACCOUNTED_EPISODE_EVERY_MS. An episode that goes without is still said, line
 * for line; it has no account at its low and none after its release, it is counted, and the count
 * goes out with the next account taken at a low ("skipped="). A low a quarter deeper is never
 * rationed: hovering does not go a quarter deeper, a sag does. Nor is the account after a watch
 * that ran out: a level the module has moved to is accounted for a minute on whatever the ration
 * said, since that is the one account such a watch exists for, and it comes once a minute at most.
 * That one is read straight from the platform, so it carries no "skipped=" of its own; the count
 * waits for the next account taken at a low.
 */
class HeapWatermarkTracer
{
public:
    /** Watermarks at or above this are ordinary operating levels and are not reported. */
    static const uint32_t REPORT_BELOW_BYTES = 40000;

    /** How much deeper the watermark has to go before the next report. */
    static const uint32_t REPORT_STEP_BYTES = 1024;

    /** Free heap at or above this is the memory given back, by the trace's own definition above. */
    static const uint32_t RELEASED_AT_BYTES = REPORT_BELOW_BYTES;

    /**
     * How long a watch waits for the release before it says "still held" and ends.
     *
     * Longer than anything that legitimately holds a large transient: one REST attempt is at most
     * ~37 s (the 30 s read timeout, the 2 s connection check, the connect) and the MQTT connect
     * act 12 s, so a transient always closes its own watch. What is still down after a minute -
     * a firmware download, a leak, a session somebody keeps - is a level, and two diagnostic
     * snapshots have carried it by then.
     */
    static const uint32_t WATCH_LIMIT_MS = 60000;

#ifdef GD_HEAP_FORENSICS
    /** How many episodes in a row may have the heap walked for them. */
    static const uint32_t ACCOUNTED_EPISODES_IN_A_ROW = 3;

    /** How long it takes to earn one such episode back once the row is used up. */
    static const uint32_t ACCOUNTED_EPISODE_EVERY_MS = 10000;
#endif

    /** One line's worth: the new low, the reading it replaced and who was at work. */
    struct Report
    {
        uint32_t watermarkBytes;
        uint32_t previousBytes;
        /** False when the reading was given no board; `acts` is then not filled. */
        bool hasActs;
        BackgroundActBoard::Snapshot acts;
        /** False when the caller did not describe the moment; the two below are then not filled. */
        bool hasMoment;
        /** Free heap when the low was found: at the floor still, or back already. */
        uint32_t freeBytes;
        /** Length of the control-loop step the reading closed. */
        uint32_t stepMs;
#ifdef GD_HEAP_FORENSICS
        /**
         * True for an episode low: `watermarkBytes` is then the free heap the reading found and
         * `previousBytes` the since-boot watermark as that reading found it.
         */
        bool episode;
        /** False when this low was not accounted for; `holders` is then not filled. */
        bool hasHolders;
        HeapHolders holders;
#endif
    };

    /** How a watch ended: the memory came back, or the limit ran out first. */
    struct Release
    {
        bool released;
        /** From the reading that opened the watch to the poll that closed it. */
        uint32_t afterMs;
        /** Free heap at the poll that closed it. */
        uint32_t freeBytes;
#ifdef GD_HEAP_FORENSICS
        /** True when the heap was walked for this episode; the account after a release is for those. */
        bool walked;
#endif
    };

    HeapWatermarkTracer()
        : _previousBytes(0),
          _previousSampledAtMs(0),
          _hasPrevious(false),
          _lastReportedBytes(0),
          _hasReported(false),
          _hasPending(false),
          _watching(false),
          _attention(false),
          _pendingSampledAtMs(0),
          _watchOpenedAtMs(0)
#ifdef GD_HEAP_FORENSICS
          ,
          _foundByThisReading(false),
          _windowOpenedAtMs(0),
          _episodeLowBytes(0),
          _hasEpisodeLow(false),
          _accountedAtBytes(0),
          _hasAccounted(false),
          _holdersDue(false),
          _episodeWalked(false),
          _accountsLeft(ACCOUNTED_EPISODES_IN_A_ROW),
          _rationDatedAtMs(0),
          _unaccountedEpisodes(0)
#endif
    {
        _pending.watermarkBytes = 0;
        _pending.previousBytes = 0;
        _pending.hasActs = false;
        _pending.hasMoment = false;
        _pending.freeBytes = 0;
        _pending.stepMs = 0;
#ifdef GD_HEAP_FORENSICS
        _pending.episode = false;
        _pending.hasHolders = false;
#endif
    }

    /**
     * @brief Take one reading of the platform's since-boot minimum free heap.
     *
     * Silent and allocation-free by construction; whether the reading produced a line is asked
     * with takeReport().
     *
     * @param nowMs When the reading is taken; the next reading's window opens here
     * @param acts  The background tasks' board, copied only by a reading that found a new low
     * @return true when this reading found a low worth a line - the one case in which the caller
     *         has more to tell (noteTheMoment()); false on every ordinary step
     */
    bool sample(uint32_t minFreeHeapBytes, uint32_t nowMs = 0,
                const BackgroundActBoard* acts = nullptr)
    {
        const uint32_t previousBytes = _previousBytes;
        const uint32_t previousSampledAtMs = _previousSampledAtMs;
        const bool hadPrevious = _hasPrevious;

        _previousBytes = minFreeHeapBytes;
        _previousSampledAtMs = nowMs;
        _hasPrevious = true;
#ifdef GD_HEAP_FORENSICS
        _foundByThisReading = false;
        _windowOpenedAtMs = previousSampledAtMs;
#endif

        // A watermark only ever falls, and a reading that did not fall is not news; the first
        // reading has nothing to fall from.
        if (!hadPrevious || minFreeHeapBytes >= previousBytes)
        {
            return false;
        }

        // Checked before the hysteresis below, which is also what keeps that sum from
        // overflowing: anything near the top of the range has already left here.
        if (minFreeHeapBytes >= REPORT_BELOW_BYTES)
        {
            return false;
        }

        if (_hasReported && minFreeHeapBytes + REPORT_STEP_BYTES > _lastReportedBytes)
        {
            return false;
        }

        // A line still waiting is replaced rather than queued: the caller takes reports every
        // step, so what is dropped is at most one step old, and of two readings a step apart the
        // deeper one is the one worth a line.
        _pending.watermarkBytes = minFreeHeapBytes;
        _pending.previousBytes = previousBytes;
        _pending.hasActs = (acts != nullptr);
        if (acts != nullptr)
        {
            acts->capture(previousSampledAtMs, _pending.acts);
        }
        _pending.hasMoment = false;
#ifdef GD_HEAP_FORENSICS
        _pending.episode = false;
        _pending.hasHolders = false;
        _foundByThisReading = true;
#endif
        _pendingSampledAtMs = nowMs;
        _hasPending = true;
        _attention = true;

        // The hysteresis follows what was detected, not what was printed: a caller whose log
        // channel is off must not leave the tracer owing a line per step for when it comes back.
        _lastReportedBytes = minFreeHeapBytes;
        _hasReported = true;
        return true;
    }

    /**
     * @brief Describe the moment of the low sample() has just found. Call only when it said so.
     *
     * Kept out of sample() so that the ordinary step does not pay for two figures it has no use
     * for: the free heap is a platform call, and the caller makes it only here.
     *
     * Free heap still below RELEASED_AT_BYTES opens the watch, dated by the reading. A watch that
     * is open already stays as it is - see "When the memory came back" above.
     *
     * Without a line waiting there is no low to describe, and the call does nothing: a watch
     * opened here would be dated by a low long gone and, with no line to raise the flag, would
     * sleep until the next real low and then report that one's release with the wrong length.
     */
    void noteTheMoment(uint32_t freeHeapBytes, uint32_t stepMs)
    {
        if (!_hasPending)
        {
            return;
        }

        _pending.hasMoment = true;
        _pending.freeBytes = freeHeapBytes;
        _pending.stepMs = stepMs;

        if (freeHeapBytes < RELEASED_AT_BYTES && !_watching)
        {
            _watching = true;
            _watchOpenedAtMs = _pendingSampledAtMs;
        }

#ifdef GD_HEAP_FORENSICS
        // Memory still held is what an episode is made of, whichever reading found it: the level
        // is the one the episode's hysteresis continues from, and the holders can still be seen.
        if (freeHeapBytes < RELEASED_AT_BYTES)
        {
            rememberTheEpisodeLow(freeHeapBytes);
            considerTheHolders(freeHeapBytes, _pendingSampledAtMs);
        }
#endif
    }

    /** @brief True while a line is waiting or a watch is open; the one check of an ordinary step. */
    bool needsAttention() const { return _attention; }

    /** @brief True while the caller is to ask the free heap once per step and pollRelease() it. */
    bool isWatchingRelease() const { return _watching; }

    /**
     * @brief One look at the free heap while a watch is open.
     * @param out Filled only when this returns true
     * @return true when the watch ended with this look - released, or out of time - and there is
     *         a line to write; the watch is over either way
     */
    bool pollRelease(uint32_t freeHeapBytes, uint32_t nowMs, Release& out)
    {
        if (!_watching)
        {
            return false;
        }

        // Unsigned subtraction, so a watch that straddles a millis() wraparound keeps its length.
        const uint32_t afterMs = nowMs - _watchOpenedAtMs;
        const bool released = freeHeapBytes >= RELEASED_AT_BYTES;
        if (!released && afterMs < WATCH_LIMIT_MS)
        {
            return false;
        }

        out.released = released;
        out.afterMs = afterMs;
        out.freeBytes = freeHeapBytes;
        _watching = false;
        _attention = _hasPending;
#ifdef GD_HEAP_FORENSICS
        out.walked = _episodeWalked;

        // Released is the heap seen back at its level: the episode is over. A watch that ran out
        // of time leaves it open - the memory is still down, and saying the same low again every
        // minute would be a ribbon; sampleFreeHeap() closes it when the heap does come back.
        if (released)
        {
            forgetTheEpisode();
        }
#endif
        return true;
    }

    /**
     * @brief Hand out the line a sample flagged, if there is one, and forget it.
     * @param out Filled only when this returns true
     * @return false when nothing is waiting
     */
    bool takeReport(Report& out)
    {
        if (!_hasPending)
        {
            return false;
        }

        out = _pending;
        _hasPending = false;
        _attention = _watching;
        return true;
    }

#ifdef GD_HEAP_FORENSICS
    /**
     * @brief One look at the free heap itself, on every step, after sample() and on the same
     *        reading - sample()'s date and window are this look's too.
     *
     * @return true when this look found an episode low worth a line; the line is handed out by
     *         takeReport() like any other, and the watch for the release is open
     */
    bool sampleFreeHeap(uint32_t freeHeapBytes, uint32_t stepMs,
                        const BackgroundActBoard* acts = nullptr)
    {
        if (freeHeapBytes >= RELEASED_AT_BYTES)
        {
            // Back at an ordinary level with no watch left to say so: the episode a watch gave up
            // on is over. While a watch is open the forgetting is its closing's, not this look's.
            if (!_watching)
            {
                forgetTheEpisode();
            }
            return false;
        }

        // A since-boot low found by this very reading has its line waiting, and noteTheMoment()
        // has entered it in the episode. This look is a second one on the same reading, and
        // another core may have taken more in between: a line made of it would take the place
        // of the one that waits. What it saw is the next reading's to find.
        if (_foundByThisReading)
        {
            return false;
        }

        if (_hasEpisodeLow && freeHeapBytes + REPORT_STEP_BYTES > _episodeLowBytes)
        {
            return false;
        }

        _pending.watermarkBytes = freeHeapBytes;
        _pending.previousBytes = _previousBytes;
        _pending.hasActs = (acts != nullptr);
        if (acts != nullptr)
        {
            acts->capture(_windowOpenedAtMs, _pending.acts);
        }
        _pending.hasMoment = true;
        _pending.freeBytes = freeHeapBytes;
        _pending.stepMs = stepMs;
        _pending.episode = true;
        _pending.hasHolders = false;
        _pendingSampledAtMs = _previousSampledAtMs;
        _hasPending = true;
        _attention = true;

        if (!_watching)
        {
            _watching = true;
            _watchOpenedAtMs = _pendingSampledAtMs;
        }

        rememberTheEpisodeLow(freeHeapBytes);
        considerTheHolders(freeHeapBytes, _pendingSampledAtMs);
        return true;
    }

    /** @brief True when the low this reading found is one the caller is to read the ledger for. */
    bool holdersAreDue() const { return _holdersDue; }

    /**
     * @brief Hand over the account holdersAreDue() asked for; it goes out with the waiting line.
     * @param holders nullptr when the platform had no ledger to give - the line goes out alone
     */
    void noteTheHolders(const HeapHolders* holders)
    {
        _holdersDue = false;
        if (!_hasPending || holders == nullptr)
        {
            return;
        }
        _pending.holders = *holders;
        _pending.hasHolders = true;

        // The episodes that went without since the last account are said with this one.
        _pending.holders.skippedEpisodes = _unaccountedEpisodes;
        _unaccountedEpisodes = 0;
    }
#endif

private:
#ifdef GD_HEAP_FORENSICS
    void rememberTheEpisodeLow(uint32_t freeHeapBytes)
    {
        if (!_hasEpisodeLow || freeHeapBytes < _episodeLowBytes)
        {
            _episodeLowBytes = freeHeapBytes;
            _hasEpisodeLow = true;
        }
    }

    /**
     * The first low of an episode, then each low a quarter below the last one considered - and
     * the first of them only while the ration lasts; see "Holders" above.
     */
    void considerTheHolders(uint32_t freeHeapBytes, uint32_t nowMs)
    {
        // Both sides stay far inside 32 bits: every figure here is below RELEASED_AT_BYTES.
        if (_hasAccounted && freeHeapBytes * 4 > _accountedAtBytes * 3)
        {
            return;
        }

        const bool firstOfTheEpisode = !_hasAccounted;
        _accountedAtBytes = freeHeapBytes;
        _hasAccounted = true;

        if (firstOfTheEpisode && !takeAnAccount(nowMs))
        {
            _unaccountedEpisodes++;
            return;
        }

        _holdersDue = true;
        _episodeWalked = true;
    }

    /** One accounted episode out of the ration, if there is one left at `nowMs`. */
    bool takeAnAccount(uint32_t nowMs)
    {
        // Unsigned subtraction, so the ration keeps its dates across a millis() wraparound.
        const uint32_t earned = (nowMs - _rationDatedAtMs) / ACCOUNTED_EPISODE_EVERY_MS;
        if (earned > 0)
        {
            _rationDatedAtMs += earned * ACCOUNTED_EPISODE_EVERY_MS;
            _accountsLeft += earned;
        }

        // A full ration earns nothing further, and its date is the moment it is next broken into:
        // the wait for the fourth episode runs from the first of the row, not from a grid laid at
        // boot - on which a row that began late in a slot would be four long.
        if (_accountsLeft >= ACCOUNTED_EPISODES_IN_A_ROW)
        {
            _accountsLeft = ACCOUNTED_EPISODES_IN_A_ROW;
            _rationDatedAtMs = nowMs;
        }

        if (_accountsLeft == 0)
        {
            return false;
        }
        _accountsLeft--;
        return true;
    }

    void forgetTheEpisode()
    {
        _hasEpisodeLow = false;
        _hasAccounted = false;
        _episodeWalked = false;
    }
#endif

    uint32_t _previousBytes;
    uint32_t _previousSampledAtMs;
    bool _hasPrevious;
    uint32_t _lastReportedBytes;
    bool _hasReported;
    Report _pending;
    bool _hasPending;
    bool _watching;
    // _hasPending || _watching, kept as a byte of its own so that an ordinary step tests one flag.
    bool _attention;
    uint32_t _pendingSampledAtMs;
    uint32_t _watchOpenedAtMs;
#ifdef GD_HEAP_FORENSICS
    // sample() found a since-boot low on the current reading; sampleFreeHeap() then adds no line.
    bool _foundByThisReading;
    // The date of the reading before the current one: the window an episode low's account of the
    // background tasks is read against, which sample() has overwritten by the time it is needed.
    uint32_t _windowOpenedAtMs;
    // The lowest free heap said in the open episode - by either kind of line.
    uint32_t _episodeLowBytes;
    bool _hasEpisodeLow;
    // The free heap at the last low of the open episode that was considered for an account.
    uint32_t _accountedAtBytes;
    bool _hasAccounted;
    bool _holdersDue;
    // The heap was walked for the open episode at least once.
    bool _episodeWalked;
    // The ration of accounted episodes: how many are left, and the date it is counted from.
    uint32_t _accountsLeft;
    uint32_t _rationDatedAtMs;
    // Episodes whose first account was withheld since the last account taken at a low.
    uint32_t _unaccountedEpisodes;
#endif
};

#endif // HEAP_WATERMARK_TRACER_H
