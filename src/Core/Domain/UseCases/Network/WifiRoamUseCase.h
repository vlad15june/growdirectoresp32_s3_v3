#ifndef WIFI_ROAM_USE_CASE_H
#define WIFI_ROAM_USE_CASE_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Data/Entities/WifiNode.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiRoamRadio.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/UseCases/Network/IWifiRoamUseCase.h"
#include "Core/Domain/UseCases/Network/WifiRoamPolicy.h"

/**
 * @brief Looking around while the signal is poor, and moving the station when it is worth it.
 *
 * The rules of when to move are WifiRoamPolicy's; this is what feeds them and what acts on the
 * answer. Three things happen here and nowhere else:
 *
 * ### Looking costs airtime, so it is rationed
 *
 * The radio leaves its channel for the length of a look, and while it is away frames addressed to
 * the module are not heard. So: nothing at all while the signal is good, one round every
 * SCAN_INTERVAL_MS while it is not, and a round is one narrow look per channel the module has seen
 * its own network on. Every look is one channel of one network, including the looks of the round
 * that learns the channels in the first place - it walks the band a channel at a time rather than
 * asking for a scan of everything at once.
 *
 * ### The radio belongs to the network task
 *
 * The scan and the radio restart are the same driver and the same scan result, so nothing is asked
 * of the radio while the act board says the network task is at work. A round that cannot go on
 * waits rather than being thrown away; a round whose link has gone is let go of, results and all,
 * because a restart of the radio is what happens next.
 *
 * ### A round is a unit
 *
 * Everything the looks of one round found is weighed once, together, at the end of the round. That
 * is what makes "the same access point was the better one in two scans in a row" a statement about
 * two rounds and not about two channels of one round.
 *
 * ### The move itself is not made here
 *
 * The decision is the control loop's, where the clock and the timers live; the move holds its
 * caller for the seconds an association takes and belongs on the network task, so it is submitted
 * as a job of the outbox and the outcome comes back as a completion - exactly as the radio restart
 * of the diagnostics does. Between the two this use case holds a ticket and decides nothing.
 */
class WifiRoamUseCase : public IWifiRoamUseCase
{
public:
    /** Access points of its own network the module weighs at once. More are dropped and counted. */
    static const size_t MAX_NODES = 8;

    /** Channels the module remembers its network on. More are dropped and counted. */
    static const size_t MAX_CHANNELS = 4;

    /** Channels a round walks when the module is learning where its own network is. */
    static const size_t BAND_CHANNELS = 13;

    /**
     * Rounds between two walks of the whole band.
     *
     * Twelve is an hour at the usual interval. The channel list is learned from what was on the
     * air at one moment; an access point that was down then would otherwise be missing from the
     * module's picture of its own network until the next reboot.
     */
    static const uint32_t ROUNDS_BETWEEN_BAND_WALKS = 12;

    /**
     * A look still said to be running after this is given up on.
     *
     * A backstop and nothing more: the core gives a scan a deadline of twenty times the
     * per-channel dwell - about two and a half seconds for the dwell this asks for - and reports
     * the scan failed after it. Reaching this figure means the core's own bookkeeping is stuck,
     * not that the radio is slow.
     */
    static const uint32_t LOOK_DEADLINE_MS = 5000;

    /**
     * A round left open this long is closed with what it has.
     *
     * A round waits for the network task to leave the radio alone, so on a busy module it can be
     * spread over many control-loop iterations. This bounds that: a picture of the network that
     * took longer than a minute to collect is stale enough that a fresh round is worth more.
     */
    static const uint32_t ROUND_DEADLINE_MS = 60000;

    /** "aa:bb:cc:dd:ee:ff" and its terminator. */
    static const size_t BSSID_TEXT_BYTES = 18;

    WifiRoamUseCase(IWifiRoamRadio* radio = nullptr, IOutbox* outbox = nullptr,
                    IArduinoAdapter* arduinoAdapter = nullptr, BackgroundActBoard* acts = nullptr,
                    ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : _radio(radio), _outbox(outbox), _arduinoAdapter(arduinoAdapter), _acts(acts),
          _logger(logger), _debugSystem(debugSystem)
    {
    }

    void setOutbox(IOutbox* outbox) { _outbox = outbox; }
    void setActBoard(BackgroundActBoard* acts) { _acts = acts; }

    void onControlLoopIteration() override
    {
        if (_radio == nullptr)
        {
            return;
        }

        const uint32_t now = nowMs();

        // A move is with the network task: there is nothing to decide until it says what happened.
        // The ticket is held with no timeout of its own because the outbox owes exactly one
        // completion per job it accepted, and it counts the one case where it cannot deliver that
        // (lostCompletionCount, a column of the periodic counters line) rather than dropping it in
        // silence. A timeout here would be a second opinion about the same fact.
        if (_roamTicket != IOutbox::kRejected)
        {
            return;
        }

        // The radio is one device and the network task is entitled to it: a restart of the radio
        // and a scan started from here would be two tasks inside the same driver, over the same
        // single scan result the core keeps. So nothing at all is asked of the radio while the
        // board says that task is at work - not the first look of a round, not the next one, and
        // not the reading or the release of a look already in the air.
        const bool quiet = nothingElseIsRunning(now);

        WifiNode current;
        if (!_radio->associatedNode(current))
        {
            // The link is the link-down path's business, and a round in the air is about to be
            // overtaken by a radio restart on the network task: let go of it, results and all.
            dropTheRound(quiet);
            _policy.forgetCandidate();
            return;
        }

        // The radio watches the average signal between the looks and says once when it has fallen
        // through the threshold. All that buys is the next look, sooner than the interval; what is
        // done with what the look finds is unchanged.
        if (_radio->signalWarningRaised())
        {
            _policy.askForScanNow();
        }

        if (_looking)
        {
            carryOnLooking(now, current, quiet);
            return;
        }

        if (_roundOpen)
        {
            carryOnRound(now, current, quiet);
            return;
        }

        if (!quiet || !_policy.scanIsDue(now, current.rssi))
        {
            return;
        }

        beginRound(now, current);
    }

    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        if (_roamTicket == IOutbox::kRejected || completion.kind != OutboxKind::WIFI_ROAM ||
            completion.ticket != _roamTicket)
        {
            return;
        }
        _roamTicket = IOutbox::kRejected;

        WifiNode node;
        const bool associated = (_radio != nullptr) && _radio->associatedNode(node);

        if (completion.delivered)
        {
            _roams++;
            log("WIFI: roam ok rssi=%d", associated ? node.rssi : 0);
            return;
        }

        _roamFails++;
        char where[BSSID_TEXT_BYTES];
        if (associated)
        {
            writeBssid(node.bssid, where);
        }
        else
        {
            snprintf(where, sizeof(where), "none");
        }
        log("WIFI: roam failed, back on %s", where);
    }

    /** @brief Moves asked for that ended with the station associated with the chosen node. */
    uint32_t roamCount() const { return _roams; }

    /** @brief Moves asked for that did not end there - the station is back where it can be. */
    uint32_t roamFailCount() const { return _roamFails; }

    /** @brief Access points and channels a look found that there was no room to keep. */
    uint32_t droppedCount() const { return _dropped; }

    /** @brief Looks that answered nothing: refused by the radio, failed, or given up on. */
    uint32_t unansweredLookCount() const { return _looksWithNoAnswer; }

    /** @brief Rounds that ran out of time before they reached the end of their channel list. */
    uint32_t cutShortRoundCount() const { return _roundsCutShort; }

    /** @brief Whether the module has walked the whole band at least once since it booted. */
    bool bandWalked() const { return _bandWalked; }

private:
    uint32_t nowMs() const
    {
        return _arduinoAdapter != nullptr ? (uint32_t)_arduinoAdapter->millis() : 0u;
    }

    /**
     * @brief Open a round: decide which channels it walks and start the first look of it.
     *
     * A round that walks the band is how the module learns where its own network is. It walks it
     * one channel at a time like every other round, and not as a single scan of everything: the
     * core gives a scan a deadline of twenty times the per-channel dwell, which a scan of the whole
     * band can exceed - and a wide scan that times out returns nothing, teaches the module nothing,
     * and is therefore repeated at the next interval, for ever. One channel per look cannot fail
     * that way: each look is well inside its own deadline, and what it finds is kept whatever the
     * looks after it do.
     *
     * The band is walked again every ROUNDS_BETWEEN_BAND_WALKS rounds, so that a channel list
     * learned while a node was down is not the module's picture of the network for ever.
     */
    void beginRound(uint32_t now, const WifiNode& current)
    {
        _roundOpen = true;
        _roundStartedAtMs = now;
        _channelIndex = 0;
        _seenCount = 0;
        _noAnswersThisRound = 0;
        _bandRound = (!_bandWalked || _roundsSinceBandWalk >= ROUNDS_BETWEEN_BAND_WALKS);

        // The one channel the module knows for certain: the one it is associated on. It keeps the
        // channel list from ever being empty, whatever the looks do.
        rememberChannel(current.channel);

        _policy.noteScanStarted(now);

        if (!startTheNextLook(now))
        {
            finishRound(now, current, true);
        }
    }

    void carryOnLooking(uint32_t now, const WifiNode& current, bool quiet)
    {
        if (!quiet)
        {
            // The network task went to the radio while this look was in the air. Its result is
            // neither read nor released now: both go through the one scan result the core keeps,
            // and the task at the radio may be freeing it this instant. The look waits, and the
            // round's own deadline is what keeps the waiting bounded.
            if ((uint32_t)(now - _roundStartedAtMs) >= ROUND_DEADLINE_MS)
            {
                finishRound(now, current, quiet);
            }
            return;
        }

        const int state = _radio->nodeScanState();

        if (state == IWifiRoamRadio::SCAN_RUNNING)
        {
            // Only a backstop: the core gives up on a scan of its own accord well before this, and
            // a look still said to be running here means the core's own bookkeeping is stuck.
            if ((uint32_t)(now - _lookStartedAtMs) >= LOOK_DEADLINE_MS)
            {
                _looking = false;
                noteALookWithNoAnswer();
                _channelIndex++;
            }
            return;
        }

        _looking = false;

        if (state >= 0)
        {
            WifiNode found[MAX_NODES];
            const size_t taken = _radio->takeNodeScan(found, MAX_NODES);
            if (taken == MAX_NODES)
            {
                // The radio writes what fits and says how much that was; a look that filled the
                // buffer may have had more behind it. Counted once, because how much more is
                // exactly what was not asked for.
                _dropped++;
            }
            keep(found, taken);
        }
        else
        {
            noteALookWithNoAnswer();
        }

        _channelIndex++;
        carryOnRound(now, current, quiet);
    }

    /**
     * @brief Between the looks of a round: take the next channel, wait, or end the round.
     *
     * A round that cannot go on because the network task is at the radio is not thrown away - it
     * waits. Throwing it away would mean a module whose network task is busy often enough never
     * finishes a round at all, which is the same failure as the wide scan that never completes.
     */
    void carryOnRound(uint32_t now, const WifiNode& current, bool quiet)
    {
        if (_channelIndex >= channelsThisRound() ||
            (uint32_t)(now - _roundStartedAtMs) >= ROUND_DEADLINE_MS)
        {
            finishRound(now, current, quiet);
            return;
        }

        if (!quiet)
        {
            return;
        }

        if (!startTheNextLook(now))
        {
            finishRound(now, current, quiet);
        }
    }

    /** @brief Start looks from the current channel on until one is accepted, or the round is out. */
    bool startTheNextLook(uint32_t now)
    {
        while (_channelIndex < channelsThisRound())
        {
            if (_radio->startNodeScan(channelOfThisRound(_channelIndex)))
            {
                _looking = true;
                _lookStartedAtMs = now;
                return true;
            }
            // The radio would not take it - most often because a scan of somebody else's is still
            // running. That channel is not looked at this round, and it is counted as such.
            noteALookWithNoAnswer();
            _channelIndex++;
        }
        return false;
    }

    void finishRound(uint32_t now, const WifiNode& current, bool quiet)
    {
        const bool walkedToTheEnd = _channelIndex >= channelsThisRound();

        if (_looking)
        {
            // A look still in the air when the round ends answers nothing. Whatever the core holds
            // for it is left to the next scan to free - see carryOnLooking() for why.
            noteALookWithNoAnswer();
        }
        _roundOpen = false;
        _looking = false;

        if (!walkedToTheEnd)
        {
            // Ended by the round's deadline rather than by its channel list. It is not a walk of
            // anything, least of all of the band, and it does not pass in silence.
            _roundsCutShort++;
            log("WIFI: roam scan round cut short after %u of %u channel(s)",
                (unsigned)_channelIndex, (unsigned)channelsThisRound());
        }

        if (_bandRound)
        {
            // A walk of the band counts as done only if it reached the end of the band and any of
            // it answered. One cut short, or one that answered nowhere, has taught the module
            // nothing about where its network is - so the next round walks the band again instead
            // of spending the hour between walks looking at one channel.
            if (walkedToTheEnd)
            {
                _roundsSinceBandWalk = 0;
                if (_noAnswersThisRound < channelsThisRound())
                {
                    _bandWalked = true;
                }
            }
        }
        else
        {
            _roundsSinceBandWalk++;
        }

        if (_noAnswersThisRound > 0)
        {
            log("WIFI: roam scan got no answer on %u of %u channel(s)",
                (unsigned)_noAnswersThisRound, (unsigned)channelsThisRound());
        }

        const WifiRoamPolicy::Verdict verdict =
            _policy.weigh(now, current, _seen, _seenCount, quiet);
        if (!verdict.move)
        {
            return;
        }

        askForTheMove(now, current, verdict.target);
    }

    /**
     * @brief Let go of a round, and of the radio's copy of what the look in the air found.
     *
     * The release is itself a call into the core's scan result, so it is made only while the
     * network task is not at the radio. When it is, the copy is left where it is: the next scan
     * of anybody's - the restart's own, most likely - frees it before it starts.
     */
    void dropTheRound(bool quiet)
    {
        if (_looking && quiet)
        {
            _radio->takeNodeScan(nullptr, 0);
        }
        _looking = false;
        _roundOpen = false;
    }

    size_t channelsThisRound() const { return _bandRound ? BAND_CHANNELS : _channelCount; }

    uint8_t channelOfThisRound(size_t index) const
    {
        return _bandRound ? (uint8_t)(index + 1) : _channels[index];
    }

    void noteALookWithNoAnswer()
    {
        _noAnswersThisRound++;
        _looksWithNoAnswer++;
    }

    void askForTheMove(uint32_t now, const WifiNode& from, const WifiNode& to)
    {
        if (_outbox == nullptr)
        {
            return;
        }

        const uint32_t ticket = _outbox->submitWifiRoam(to.bssid, to.channel);
        if (ticket == IOutbox::kRejected)
        {
            log("WIFI: roam refused by the outbox");
            return;
        }

        _roamTicket = ticket;
        // From the moment it was asked for: the association is spent either way, and the interval
        // exists so that a module cannot spend it twice in a row.
        _policy.noteRoamStarted(now, from);

        char fromText[BSSID_TEXT_BYTES];
        char toText[BSSID_TEXT_BYTES];
        writeBssid(from.bssid, fromText);
        writeBssid(to.bssid, toText);
        log("WIFI: roam %s rssi=%d -> %s rssi=%d ch=%u", fromText, from.rssi, toText, to.rssi,
            (unsigned)to.channel);
    }

    /**
     * @brief Is this a moment the module can afford to lose its sockets in?
     *
     * The board answers for the three tasks whose work a re-association would break: the network
     * task's own request or broker act, the command task - which is where a firmware download runs
     * - and the catalog write. Their absence is what "quiet" means here.
     */
    bool nothingElseIsRunning(uint32_t now) const
    {
        if (_acts == nullptr)
        {
            return true;
        }

        BackgroundActBoard::Snapshot snapshot;
        _acts->capture(now, snapshot);
        return slotIsIdle(snapshot, BackgroundActBoard::NETWORK) &&
               slotIsIdle(snapshot, BackgroundActBoard::COMMAND_NETWORK) &&
               slotIsIdle(snapshot, BackgroundActBoard::CATALOG_STORAGE);
    }

    static bool slotIsIdle(const BackgroundActBoard::Snapshot& snapshot,
                           BackgroundActBoard::Slot slot)
    {
        return BackgroundActBoard::read(snapshot, slot).act == BackgroundActBoard::NO_ACT;
    }

    /** @brief Add what one look found to the round, and remember the channels it was found on. */
    void keep(const WifiNode* found, size_t count)
    {
        for (size_t index = 0; index < count; index++)
        {
            rememberChannel(found[index].channel);

            bool known = false;
            for (size_t kept = 0; kept < _seenCount; kept++)
            {
                if (sameBssid(_seen[kept].bssid, found[index].bssid))
                {
                    _seen[kept] = found[index];
                    known = true;
                    break;
                }
            }
            if (known)
            {
                continue;
            }

            if (_seenCount >= MAX_NODES)
            {
                _dropped++;
                continue;
            }
            _seen[_seenCount++] = found[index];
        }
    }

    void rememberChannel(uint8_t channel)
    {
        if (channel == 0)
        {
            return;
        }
        for (size_t index = 0; index < _channelCount; index++)
        {
            if (_channels[index] == channel)
            {
                return;
            }
        }
        if (_channelCount >= MAX_CHANNELS)
        {
            _dropped++;
            return;
        }
        _channels[_channelCount++] = channel;
    }

    /** @brief The six bytes as a log line says them. `out` holds BSSID_TEXT_BYTES. */
    static void writeBssid(const uint8_t* bssid, char* out)
    {
        snprintf(out, BSSID_TEXT_BYTES, "%02x:%02x:%02x:%02x:%02x:%02x", bssid[0], bssid[1],
                 bssid[2], bssid[3], bssid[4], bssid[5]);
    }

    void log(const char* format, ...)
    {
        if (_logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::WIFI_DEBUG))
        {
            return;
        }

        char buffer[160];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        _logger->println(DebugFeatures::WIFI_DEBUG, buffer);
    }

    IWifiRoamRadio* _radio;
    IOutbox* _outbox;
    IArduinoAdapter* _arduinoAdapter;
    BackgroundActBoard* _acts;
    ILogger* _logger;
    IDebugSystem* _debugSystem;

    WifiRoamPolicy _policy;

    bool _looking = false;
    bool _roundOpen = false;
    bool _bandRound = false;
    bool _bandWalked = false;
    uint32_t _roundsSinceBandWalk = 0;
    uint32_t _roundStartedAtMs = 0;
    uint32_t _lookStartedAtMs = 0;
    size_t _channelIndex = 0;
    uint32_t _noAnswersThisRound = 0;

    WifiNode _seen[MAX_NODES];
    size_t _seenCount = 0;

    uint8_t _channels[MAX_CHANNELS] = {0, 0, 0, 0};
    size_t _channelCount = 0;

    uint32_t _roamTicket = IOutbox::kRejected;

    uint32_t _roams = 0;
    uint32_t _roamFails = 0;
    uint32_t _dropped = 0;
    uint32_t _looksWithNoAnswer = 0;
    uint32_t _roundsCutShort = 0;
};

#endif // WIFI_ROAM_USE_CASE_H
