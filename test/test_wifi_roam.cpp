// Keeping the station on the near access point of a mesh, and the price of getting there.
//
// The module associates once, with the strongest access point it can see at that moment, and never
// looks again: one flap of the near node leaves it on the far one until something restarts the
// radio. This file is about the three parts of the answer and what each of them is allowed to do.
//
//  1. **The policy** - when a move is worth making. Every rule of it is about *not* moving, because
//     a move on this stack costs an association and an address: the hysteresis, the weak threshold,
//     the interval, the quiet moment, the ban on going back, and the two looks in a row. One test
//     per rule, each of which fails if that rule alone is taken out.
//  2. **The use case** - what feeds the policy. A look is a round of narrow scans, one per channel
//     the module has seen its network on; the channels are learned from the first, wide look;
//     nothing is looked at at all while the signal is good; the decision leaves as a job of the
//     outbox and the outcome comes back as a completion.
//  3. **The job** - the real Outbox serving that job on the network task, ahead of the requests,
//     with the act on the board under its own name.
//
// What is replaced: the radio (a stand-in that reports what is in range and records the moves it
// was asked to make), the clock, and - in the third part - the scheduler and the server. The
// policy, the use case and the outbox under test are the production ones.
//
// What no host test here can prove is inside WifiAdapter::roamTo(): esp_wifi_set_config() with a
// BSSID, the disconnect, the connect and the wait. That path is device-only and is named as such
// in the report; what this file pins is that it is reached, on the right task, with the access
// point the policy chose.

#include <unity.h>

#include "Application/Services/BackgroundActs.h"
#include "Application/Services/Net/Outbox.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/UseCases/Network/WifiRoamPolicy.h"
#include "Core/Domain/UseCases/Network/WifiRoamUseCase.h"

#include "Application/Services/Net/LinkSupervisor.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"

#include "Mock/Application/MockOutbox.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::VirtualClock;

namespace {

const uint32_t MINUTE_MS = 60u * 1000u;

/** The two access points of the test network, and a third for the moves that need somewhere else to go. */
WifiNode nodeOf(uint8_t last, uint8_t channel, int rssi)
{
    WifiNode node;
    node.bssid[0] = 0xAA;
    node.bssid[1] = 0xBB;
    node.bssid[2] = 0xCC;
    node.bssid[3] = 0xDD;
    node.bssid[4] = 0xEE;
    node.bssid[5] = last;
    node.channel = channel;
    node.rssi = rssi;
    return node;
}

WifiNode withRssi(WifiNode node, int rssi)
{
    node.rssi = rssi;
    return node;
}

// ================================================================================================
// 1. The policy
// ================================================================================================

/**
 * A move has to buy something. Two access points whose signals wander a few dB around each other
 * must never be able to trade the station back and forth, so the gain has to clear HYSTERESIS_DB.
 *
 * Mutation: accept any candidate stronger than the current one.
 */
void test_a_move_needs_more_than_the_hysteresis(void)
{
    WifiRoamPolicy policy;
    const WifiNode here = nodeOf(0x01, 1, -57);
    const WifiNode nearlyTheSame = nodeOf(0x02, 6, -57 + WifiRoamPolicy::HYSTERESIS_DB - 1);
    const WifiNode worthIt = nodeOf(0x02, 6, -57 + WifiRoamPolicy::HYSTERESIS_DB);

    // Two looks, so that the confirmation rule is satisfied and the only thing left deciding is
    // the gain.
    TEST_ASSERT_FALSE(policy.weigh(0, here, &nearlyTheSame, 1, true).move);
    TEST_ASSERT_FALSE_MESSAGE(policy.weigh(MINUTE_MS, here, &nearlyTheSame, 1, true).move,
                              "one dB short of the hysteresis was taken as a reason to move");

    TEST_ASSERT_FALSE(policy.weigh(2 * MINUTE_MS, here, &worthIt, 1, true).move);
    const WifiRoamPolicy::Verdict verdict = policy.weigh(3 * MINUTE_MS, here, &worthIt, 1, true);
    TEST_ASSERT_TRUE_MESSAGE(verdict.move, "exactly the hysteresis was not enough to move");
    TEST_ASSERT_TRUE(sameBssid(verdict.target.bssid, worthIt.bssid));
    TEST_ASSERT_EQUAL_UINT8(6, verdict.target.channel);
}

/**
 * A module with a good signal is left alone entirely - it does not even look around. There is
 * nothing to buy at -45 dBm, and the module must not spend an association finding that out.
 *
 * Mutation: drop the isWeak() gate from weigh() and from scanIsDue().
 */
void test_a_strong_signal_is_left_alone(void)
{
    WifiRoamPolicy policy;
    const WifiNode here = nodeOf(0x01, 1, WifiRoamPolicy::WEAK_DBM);
    const WifiNode splendid = nodeOf(0x02, 6, -20);

    TEST_ASSERT_FALSE_MESSAGE(policy.scanIsDue(0, here.rssi),
                              "a module with a good signal was sent looking around");
    TEST_ASSERT_FALSE(policy.weigh(0, here, &splendid, 1, true).move);
    TEST_ASSERT_FALSE_MESSAGE(policy.weigh(MINUTE_MS, here, &splendid, 1, true).move,
                              "a much stronger access point moved a station that had no problem");

    // One dB below the threshold is where looking starts.
    TEST_ASSERT_TRUE(policy.scanIsDue(0, WifiRoamPolicy::WEAK_DBM - 1));
}

/**
 * Whatever the looks say, a second move does not follow the first inside the interval. A module
 * that cannot settle is worse off than one on a weak access point.
 *
 * Mutation: remove the MIN_ROAM_INTERVAL_MS check.
 */
void test_no_second_move_before_the_interval(void)
{
    WifiRoamPolicy policy;
    const WifiNode here = nodeOf(0x01, 1, -57);
    const WifiNode elsewhere = nodeOf(0x03, 11, -30);

    policy.noteRoamStarted(0, here);

    uint32_t now = MINUTE_MS;
    while (now < WifiRoamPolicy::MIN_ROAM_INTERVAL_MS)
    {
        TEST_ASSERT_FALSE_MESSAGE(policy.weigh(now, here, &elsewhere, 1, true).move,
                                  "a second move was made inside the interval");
        now += MINUTE_MS;
    }

    TEST_ASSERT_TRUE_MESSAGE(policy.weigh(WifiRoamPolicy::MIN_ROAM_INTERVAL_MS, here, &elsewhere, 1,
                                          true)
                                 .move,
                             "the interval passed and the move still did not happen");
}

/**
 * The move is not made while the module is in the middle of something a lost socket would break.
 * The confirmations still accumulate meanwhile: a module that is busy every time it looks would
 * otherwise never be able to move at all.
 *
 * Mutation: ignore the `quiet` argument.
 */
void test_a_busy_module_is_not_moved(void)
{
    WifiRoamPolicy policy;
    const WifiNode here = nodeOf(0x01, 1, -57);
    const WifiNode better = nodeOf(0x02, 6, -30);

    for (uint32_t look = 0; look < 5; look++)
    {
        TEST_ASSERT_FALSE_MESSAGE(policy.weigh(look * MINUTE_MS, here, &better, 1, false).move,
                                  "the station was moved while the module was busy");
    }

    TEST_ASSERT_TRUE_MESSAGE(policy.weigh(5 * MINUTE_MS, here, &better, 1, true).move,
                             "the first quiet moment did not move a candidate confirmed long ago");
}

/**
 * An access point that reboots in a loop must not be able to pull the station after it every time
 * it comes back, so the one just left is no candidate for STAY_PUT_AFTER_ROAM_MS - however good it
 * looks.
 *
 * Mutation: remove isJustLeft() from the candidate loop.
 */
void test_the_access_point_just_left_is_not_taken_back(void)
{
    WifiRoamPolicy policy;
    const WifiNode wasHere = nodeOf(0x01, 1, -57);
    const WifiNode nowHere = nodeOf(0x02, 6, -55);
    const WifiNode andItIsBack = withRssi(wasHere, -30);

    policy.noteRoamStarted(0, wasHere);

    uint32_t now = WifiRoamPolicy::MIN_ROAM_INTERVAL_MS;
    while (now < WifiRoamPolicy::STAY_PUT_AFTER_ROAM_MS)
    {
        TEST_ASSERT_FALSE_MESSAGE(policy.weigh(now, nowHere, &andItIsBack, 1, true).move,
                                  "the station went straight back to the access point it left");
        now += MINUTE_MS;
    }

    TEST_ASSERT_FALSE(policy.weigh(WifiRoamPolicy::STAY_PUT_AFTER_ROAM_MS, nowHere, &andItIsBack, 1,
                                   true)
                          .move);
    TEST_ASSERT_TRUE_MESSAGE(
        policy.weigh(WifiRoamPolicy::STAY_PUT_AFTER_ROAM_MS + MINUTE_MS, nowHere, &andItIsBack, 1,
                     true)
            .move,
        "the ban never expired");
}

/**
 * One look catches every access point at one moment of its own traffic. The same one has to be the
 * better one CONFIRMATIONS_NEEDED looks running, and a look that names somebody else starts the
 * count again.
 *
 * Mutation: CONFIRMATIONS_NEEDED = 1, or keeping the count when the candidate changes.
 */
void test_one_look_is_not_enough(void)
{
    WifiRoamPolicy policy;
    const WifiNode here = nodeOf(0x01, 1, -57);
    const WifiNode first = nodeOf(0x02, 6, -30);
    const WifiNode second = nodeOf(0x03, 11, -28);

    TEST_ASSERT_FALSE_MESSAGE(policy.weigh(0, here, &first, 1, true).move,
                              "a single look was enough to move the station");

    // A different access point wins the next look: the first one's confirmation is gone with it.
    TEST_ASSERT_FALSE(policy.weigh(MINUTE_MS, here, &second, 1, true).move);
    TEST_ASSERT_FALSE_MESSAGE(policy.weigh(2 * MINUTE_MS, here, &first, 1, true).move,
                              "confirmations from before the candidate changed were counted");

    TEST_ASSERT_TRUE_MESSAGE(policy.weigh(3 * MINUTE_MS, here, &first, 1, true).move,
                             "two looks in a row named it and it still was not taken");

    // And a candidate that has more than the two confirmations does not lend them to the next one
    // either. Several looks while the module is busy build the count up; the access point that
    // wins the next look after that starts from its own first confirmation.
    WifiRoamPolicy patient;
    for (int look = 0; look < 4; look++)
    {
        TEST_ASSERT_FALSE(patient.weigh((uint32_t)look * MINUTE_MS, here, &first, 1, false).move);
    }
    TEST_ASSERT_FALSE_MESSAGE(patient.weigh(4 * MINUTE_MS, here, &second, 1, true).move,
                              "a well-confirmed candidate lent its confirmations to another one");
}

/**
 * Looking costs airtime, so it is rationed: never on a good signal, once per interval on a poor
 * one, and at once when the radio itself says the signal has fallen through the threshold.
 *
 * Mutation: drop the interval, or make askForScanNow() a no-op.
 */
void test_looking_around_is_rationed(void)
{
    WifiRoamPolicy policy;
    const int weak = WifiRoamPolicy::WEAK_DBM - 7;

    TEST_ASSERT_TRUE(policy.scanIsDue(0, weak));
    policy.noteScanStarted(0);

    TEST_ASSERT_FALSE_MESSAGE(policy.scanIsDue(WifiRoamPolicy::SCAN_INTERVAL_MS - 1, weak),
                              "the module looked again before the interval was up");
    TEST_ASSERT_TRUE(policy.scanIsDue(WifiRoamPolicy::SCAN_INTERVAL_MS, weak));

    // The radio's own warning brings the next look forward - as far as the floor, and no further.
    const uint32_t lastLook = WifiRoamPolicy::SCAN_INTERVAL_MS;
    policy.noteScanStarted(lastLook);
    policy.askForScanNow();
    TEST_ASSERT_FALSE_MESSAGE(
        policy.scanIsDue(lastLook + WifiRoamPolicy::MIN_GAP_BETWEEN_LOOKS_MS - 1, weak),
        "a warning put two looks closer together than the floor");
    TEST_ASSERT_TRUE_MESSAGE(
        policy.scanIsDue(lastLook + WifiRoamPolicy::MIN_GAP_BETWEEN_LOOKS_MS, weak),
        "the radio said the signal had fallen and nothing looked");
    TEST_ASSERT_FALSE_MESSAGE(
        policy.scanIsDue(lastLook + WifiRoamPolicy::MIN_GAP_BETWEEN_LOOKS_MS,
                         WifiRoamPolicy::WEAK_DBM),
        "the warning sent a module with a good signal looking");
}

// ================================================================================================
// 2. The use case: looking around, and what comes of it
// ================================================================================================

/**
 * @brief The radio as the roaming decision sees it: where the station stands, what a look finds,
 *        and the moves it was asked to make.
 *
 * The look answers asynchronously, as the driver's does: the first reading of its state says it is
 * still going on, and the reading after that carries the count.
 */
class FakeRoamRadio : public IWifiRoamRadio
{
public:
    bool associated = true;
    WifiNode here = nodeOf(0x01, 1, -57);
    /** Every access point of this network, each on its own channel. */
    std::vector<WifiNode> around;
    bool refuseLooks = false;
    bool neverAnswers = false;
    bool moveSucceeds = true;
    bool warning = false;
    uint32_t readingsBeforeTheAnswer = 1;

    std::vector<uint8_t> channelsLookedAt;
    std::vector<WifiNode> movesAsked;
    /** Times the caller let go of a look without reading it - `takeNodeScan(nullptr, 0)`. */
    uint32_t releases = 0;

    bool associatedNode(WifiNode& out) override
    {
        if (!associated)
        {
            return false;
        }
        out = here;
        return true;
    }

    bool startNodeScan(uint8_t channel) override
    {
        if (refuseLooks)
        {
            return false;
        }
        // A look whose answer nobody collected is not a reason to refuse the next one: the core
        // throws its previous scan result away at the start of every scan. Only a radio that
        // refuses outright (refuseLooks) says no.
        _looking = true;
        _readings = 0;
        _channel = channel;
        channelsLookedAt.push_back(channel);
        return true;
    }

    int nodeScanState() override
    {
        if (!_looking)
        {
            return SCAN_NONE;
        }
        if (neverAnswers || _readings++ < readingsBeforeTheAnswer)
        {
            return SCAN_RUNNING;
        }
        return (int)onThisChannel().size();
    }

    size_t takeNodeScan(WifiNode* out, size_t max) override
    {
        if (out == nullptr || max == 0)
        {
            releases++;
            _looking = false;
            return 0;
        }

        const std::vector<WifiNode> result = onThisChannel();
        size_t written = 0;
        for (size_t index = 0; written < max && index < result.size(); index++)
        {
            out[written++] = result[index];
        }
        _looking = false;
        return written;
    }

    bool roamTo(const uint8_t* bssid, uint8_t channel) override
    {
        WifiNode asked;
        memcpy(asked.bssid, bssid, sizeof(asked.bssid));
        asked.channel = channel;
        asked.rssi = 0;
        movesAsked.push_back(asked);

        if (!associated || !moveSucceeds)
        {
            return false;
        }
        for (size_t index = 0; index < around.size(); index++)
        {
            if (sameBssid(around[index].bssid, bssid))
            {
                here = around[index];
                return true;
            }
        }
        return false;
    }

    bool signalWarningRaised() override
    {
        const bool raised = warning;
        warning = false;
        return raised;
    }

private:
    std::vector<WifiNode> onThisChannel() const
    {
        std::vector<WifiNode> result;
        for (size_t index = 0; index < around.size(); index++)
        {
            if (_channel == 0 || around[index].channel == _channel)
            {
                result.push_back(around[index]);
            }
        }
        return result;
    }

    bool _looking = false;
    uint32_t _readings = 0;
    uint8_t _channel = 0;
};

/** The use case on the control loop, with the board it reads and the outbox it submits to. */
struct RoamStand
{
    MockArduinoAdapter clock;
    MockLogger logger;
    MockDebugSystem debug;
    BackgroundActBoard acts{&clock};
    FakeRoamRadio radio;
    MockOutbox outbox;
    WifiRoamUseCase roam{&radio, &outbox, &clock, &acts, &logger, &debug};

    RoamStand()
    {
        debug.featureEnabled = true;
        clock.setMillis(1000);
    }

    void tick(unsigned long ms = 1)
    {
        clock.setMillis(clock.millis() + ms);
        roam.onControlLoopIteration();
    }

    /**
     * @brief Drive the control loop long enough for a whole round to start and finish.
     *
     * A round is at most one look per channel of the band, and a look takes two iterations: one
     * that finds it still running and one that takes its answer.
     */
    void aWholeRound()
    {
        for (int iteration = 0; iteration < 4 * (int)WifiRoamUseCase::BAND_CHANNELS; iteration++)
        {
            tick();
        }
    }

    /**
     * @brief Make the move and hand the outcome back, as the network task and the loop's drain do.
     *
     * The radio is driven through the same call the outbox makes, so what the use case is told
     * afterwards is what the radio actually did rather than what the test says it did.
     */
    void networkTaskAnswers()
    {
        const WifiNode target = outbox.wifiRoamTargets.back();
        OutboxCompletion completion;
        completion.kind = OutboxKind::WIFI_ROAM;
        completion.ticket = outbox.lastIssuedTicket();
        completion.delivered = radio.roamTo(target.bssid, target.channel);
        completion.attempts = 1;
        roam.onOutboxCompletion(completion);
    }

    bool tapeSays(const char* needle) const { return logger.hasMessageContaining(needle); }
};

/**
 * The band is learned by walking it one channel at a time, never by asking for a scan of
 * everything at once: a scan of the whole band can exceed the deadline the core gives it, and a
 * scan that times out returns nothing, teaches nothing and is therefore repeated for ever. Every
 * round after that looks only at the channels the network was actually seen on.
 *
 * Mutation: ask for one scan of every channel (`startNodeScan(0)`); always walk the band.
 */
void test_the_band_is_walked_one_channel_at_a_time_and_then_only_the_known_ones(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -55));

    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)WifiRoamUseCase::BAND_CHANNELS,
                                     (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "the first round did not walk the band channel by channel");
    for (size_t index = 0; index < stand.radio.channelsLookedAt.size(); index++)
    {
        char message[96];
        snprintf(message, sizeof(message), "look %u asked for channel %u", (unsigned)index,
                 (unsigned)stand.radio.channelsLookedAt[index]);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(index + 1), stand.radio.channelsLookedAt[index],
                                        message);
    }
    TEST_ASSERT_TRUE_MESSAGE(stand.roam.bandWalked(), "the walk of the band did not count as done");

    const size_t afterTheWalk = stand.radio.channelsLookedAt.size();
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(afterTheWalk + 2,
                                     (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "the second round did not look at the two known channels");
    TEST_ASSERT_EQUAL_UINT8(1, stand.radio.channelsLookedAt[afterTheWalk]);
    TEST_ASSERT_EQUAL_UINT8(6, stand.radio.channelsLookedAt[afterTheWalk + 1]);
}

/**
 * A look the radio will not answer is counted and said, and a walk of the band that answered
 * nowhere is walked again - the module must not pay for a look every interval and learn nothing,
 * in silence, until it is rebooted.
 *
 * Mutation: count only the deadline; treat a walk that answered nothing as done.
 */
void test_a_look_the_radio_will_not_answer_is_counted_and_said(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.refuseLooks = true;

    stand.aWholeRound();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)WifiRoamUseCase::BAND_CHANNELS,
                                     stand.roam.unansweredLookCount(),
                                     "the looks the radio refused were not counted");
    TEST_ASSERT_TRUE_MESSAGE(stand.tapeSays("WIFI: roam scan got no answer on 13 of 13 channel(s)"),
                             "a round that found out nothing left no line on the tape");
    TEST_ASSERT_FALSE_MESSAGE(stand.roam.bandWalked(),
                              "a walk of the band that answered nowhere counted as done");

    // Nothing was looked at at all - the radio took none of it - so the band is walked again
    // rather than replaced by rounds over a channel list nobody ever learned.
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)stand.radio.channelsLookedAt.size());
    stand.radio.refuseLooks = false;
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)WifiRoamUseCase::BAND_CHANNELS,
                                     (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "the band was not walked again after a walk that taught nothing");
    TEST_ASSERT_TRUE(stand.roam.bandWalked());
}

/**
 * A look that never answers at all is given up on and the round goes on to the next channel: one
 * stuck look must not end the module's looking around for the rest of its uptime.
 *
 * Mutation: remove the deadline from carryOnLooking().
 */
void test_a_look_that_never_answers_is_given_up_on(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.neverAnswers = true;

    // Each iteration moves the clock past the look's deadline, so every look in turn is given up
    // on and the round moves to the next channel - until the round's own deadline ends it.
    for (int iteration = 0; iteration < 2 * (int)WifiRoamUseCase::BAND_CHANNELS; iteration++)
    {
        stand.tick(WifiRoamUseCase::LOOK_DEADLINE_MS);
    }

    char message[160];
    snprintf(message, sizeof(message), "%u look(s) given up on, %u channel(s) tried",
             stand.roam.unansweredLookCount(), (unsigned)stand.radio.channelsLookedAt.size());
    TEST_ASSERT_TRUE_MESSAGE(stand.roam.unansweredLookCount() >= 2, message);
    TEST_ASSERT_TRUE_MESSAGE(stand.radio.channelsLookedAt.size() >= 2, message);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, stand.radio.channelsLookedAt[1],
                                    "the round did not move on to the next channel");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stand.roam.cutShortRoundCount(),
                                     "the round of stuck looks did not end on its own deadline");
}

/**
 * What does not fit is counted rather than silently left out: a network with more access points
 * than the module weighs at once, or on more channels than it remembers, is a fact worth having in
 * the counters when somebody asks why the module is not moving.
 *
 * Mutation: drop either count.
 */
void test_access_points_that_do_not_fit_are_counted(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);

    // One channel with more access points on it than a look can hand over at once - that is one
    // look which may have had more behind it - and a second channel whose access points find the
    // round already full: two more.
    for (uint8_t extra = 0; extra < WifiRoamUseCase::MAX_NODES; extra++)
    {
        stand.radio.around.push_back(nodeOf((uint8_t)(0x10 + extra), 1, -60));
    }
    stand.radio.around.push_back(nodeOf(0x30, 6, -60));
    stand.radio.around.push_back(nodeOf(0x31, 6, -60));

    stand.aWholeRound();

    char message[96];
    snprintf(message, sizeof(message), "%u left out, and three were", stand.roam.droppedCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, stand.roam.droppedCount(), message);
}

/** And the same for the channels the module remembers its network on. */
void test_channels_that_do_not_fit_are_counted(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);

    // One access point per channel, on more channels than the module remembers.
    for (uint8_t channel = 2; channel <= (uint8_t)WifiRoamUseCase::MAX_CHANNELS + 2; channel++)
    {
        stand.radio.around.push_back(nodeOf((uint8_t)(0x20 + channel), channel, -60));
    }

    stand.aWholeRound();

    char message[96];
    snprintf(message, sizeof(message), "%u channels left out, and two were",
             stand.roam.droppedCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, stand.roam.droppedCount(), message);
}

/**
 * The band is walked again from time to time: a channel list learned while an access point was
 * down must not be the module's picture of its own network until it reboots.
 *
 * Mutation: walk the band once and never again.
 */
void test_the_band_is_walked_again_after_enough_rounds(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);

    stand.aWholeRound();
    const size_t afterTheFirstWalk = stand.radio.channelsLookedAt.size();

    // The rounds between two walks, and then one more - the one that walks the band again.
    for (uint32_t round = 0; round <= WifiRoamUseCase::ROUNDS_BETWEEN_BAND_WALKS; round++)
    {
        stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
        stand.aWholeRound();
    }

    // Narrow rounds of one channel each - the module knows only the one it is associated on - and
    // then the whole band again.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        (uint32_t)(afterTheFirstWalk + WifiRoamUseCase::ROUNDS_BETWEEN_BAND_WALKS +
                   WifiRoamUseCase::BAND_CHANNELS),
        (uint32_t)stand.radio.channelsLookedAt.size(),
        "the band was not walked again after the rounds between walks");
}

/**
 * Nothing at all happens while the signal is good: no look, no move, no line.
 *
 * Mutation: look around whatever the signal is.
 */
void test_a_module_with_a_good_signal_never_looks(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -45);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -20));

    for (int round = 0; round < 5; round++)
    {
        stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
        stand.aWholeRound();
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "the radio left its channel for a module that had no problem");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)stand.outbox.wifiRoamTargets.size());
}

/**
 * The radio's own warning is the accelerator: it brings the next look forward from the interval,
 * and nothing else. Without it the module would sit out the rest of the interval on a signal that
 * has just fallen.
 *
 * Mutation: never read the warning, or act on it with something other than a look.
 */
void test_the_radios_warning_brings_the_next_look_forward(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    stand.aWholeRound();
    const uint32_t afterTheFirstRound = (uint32_t)stand.radio.channelsLookedAt.size();

    // A minute later, a long way short of the interval: nothing looks.
    stand.clock.setMillis(stand.clock.millis() + MINUTE_MS);
    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(afterTheFirstRound,
                                     (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "the module looked again before the interval was up");

    stand.radio.warning = true;
    stand.aWholeRound();
    TEST_ASSERT_TRUE_MESSAGE((uint32_t)stand.radio.channelsLookedAt.size() > afterTheFirstRound,
                             "the radio said the signal had fallen and nothing looked");
}

/**
 * The warning has a floor under it. The driver is free to report a signal below the threshold
 * again and again, and a look per report would be a module that spends its airtime looking instead
 * of working - in exactly the state this whole mechanism exists for.
 *
 * Mutation: let a warning bring the next look forward with no floor at all.
 */
void test_the_warning_cannot_bring_the_looks_closer_than_the_floor(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);

    stand.aWholeRound();
    const size_t afterTheFirstRound = stand.radio.channelsLookedAt.size();

    // The radio warns on every pass, as it may while the signal stays below the threshold.
    for (int pass = 0; pass < 20; pass++)
    {
        stand.radio.warning = true;
        stand.tick(WifiRoamPolicy::MIN_GAP_BETWEEN_LOOKS_MS / 10);
        stand.radio.warning = true;
        stand.aWholeRound();
    }

    // Twenty passes of a tenth of the floor each: two floors' worth of time, so at most two more
    // rounds - not twenty.
    const size_t looks = stand.radio.channelsLookedAt.size() - afterTheFirstRound;
    char message[128];
    snprintf(message, sizeof(message), "%u looks in two floors' worth of warnings",
             (unsigned)looks);
    TEST_ASSERT_TRUE_MESSAGE(looks <= 2, message);
}

/**
 * The whole point, end to end on the loop side: a weak station, a much better access point of the
 * same network, two rounds - and the move goes to the network task, naming the access point and
 * its channel, with the line the field log is read by.
 *
 * Mutation: submit the current access point, or submit without the channel.
 */
void test_a_weak_station_is_moved_to_the_near_access_point(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    const WifiNode near = nodeOf(0x02, 6, -33);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(near);

    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.outbox.wifiRoamTargets.size(),
                                     "one look was enough to move the station");

    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.outbox.wifiRoamTargets.size(),
                                     "the move was never asked for");
    TEST_ASSERT_TRUE_MESSAGE(sameBssid(stand.outbox.wifiRoamTargets[0].bssid, near.bssid),
                             "the move named the wrong access point");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(6, stand.outbox.wifiRoamTargets[0].channel,
                                    "the move did not carry the channel of the access point");

    TEST_ASSERT_TRUE_MESSAGE(
        stand.tapeSays("WIFI: roam aa:bb:cc:dd:ee:01 rssi=-57 -> aa:bb:cc:dd:ee:02 rssi=-33 ch=6"),
        "the decision left no line on the tape");

    // And the outcome, when the network task has made it.
    stand.networkTaskAnswers();
    TEST_ASSERT_EQUAL_UINT32(1, stand.roam.roamCount());
    TEST_ASSERT_EQUAL_UINT32(0, stand.roam.roamFailCount());
    TEST_ASSERT_TRUE_MESSAGE(stand.tapeSays("WIFI: roam ok rssi=-33"),
                             "the outcome of the move never reached the tape");
}

/**
 * A move that did not take is counted as what it is, and the line says where the station ended up -
 * the field log has to be able to tell a module that moved from one that tried.
 *
 * Mutation: count every completion as a move made.
 */
void test_a_move_that_failed_is_counted_and_said(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));
    stand.radio.moveSucceeds = false;

    stand.aWholeRound();
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)stand.outbox.wifiRoamTargets.size());

    stand.networkTaskAnswers();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stand.roam.roamCount(),
                                     "a move that failed was counted as one that was made");
    TEST_ASSERT_EQUAL_UINT32(1, stand.roam.roamFailCount());
    TEST_ASSERT_TRUE_MESSAGE(stand.tapeSays("WIFI: roam failed, back on aa:bb:cc:dd:ee:01"),
                             "the tape does not say where the station was left");
}

/**
 * The radio is one device, and while the network task is at work it may be restarting it - the
 * same driver, the same single scan result of the whole image. So nothing is asked of the radio,
 * not even a look, while the board says that task is inside an act; and the move that a look might
 * lead to waits for the same moment.
 *
 * Mutation: read the board only for the move (or not at all).
 */
void test_nothing_is_asked_of_the_radio_while_the_network_task_is_at_it(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    stand.acts.actStarted(BackgroundActBoard::NETWORK,
                          BackgroundActs::ofOutboxKind(OutboxKind::WIFI_RECONNECT));

    for (int round = 0; round < 3; round++)
    {
        stand.aWholeRound();
        stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "a scan was started while the network task was at the radio");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.outbox.wifiRoamTargets.size(),
                                     "the station was moved out from under a request in flight");

    stand.acts.actFinished(BackgroundActBoard::NETWORK,
                           BackgroundActs::ofOutboxKind(OutboxKind::WIFI_RECONNECT));
    stand.clock.setMillis(stand.clock.millis() + 1);
    stand.aWholeRound();
    TEST_ASSERT_TRUE_MESSAGE(stand.radio.channelsLookedAt.size() > 0,
                             "nothing looked once the radio was free again");

    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.outbox.wifiRoamTargets.size(),
                                     "the move never came once the radio was free again");
}

/**
 * A round that ran out of time before the end of its channel list is not a walk of the band: a
 * walk cut short at the first channel would otherwise leave the module believing it knows where
 * its network is, and looking at that one channel for the next hour.
 *
 * Mutation: count a band round as walked without asking whether it reached the end.
 */
void test_a_band_walk_cut_short_is_not_counted_as_done(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    // A few channels of the walk, and then the network task takes the radio for long enough that
    // the round runs out of time.
    stand.tick();
    stand.tick();
    stand.tick();
    const size_t channelsDone = stand.radio.channelsLookedAt.size();
    TEST_ASSERT_TRUE_MESSAGE(channelsDone > 0 && channelsDone < WifiRoamUseCase::BAND_CHANNELS,
                             "the round did not start, or it finished in three iterations");

    stand.acts.actStarted(BackgroundActBoard::NETWORK,
                          BackgroundActs::ofOutboxKind(OutboxKind::WIFI_RECONNECT));
    stand.tick(WifiRoamUseCase::ROUND_DEADLINE_MS);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stand.roam.cutShortRoundCount(),
                                     "a round cut short by its deadline was not counted");
    TEST_ASSERT_TRUE_MESSAGE(stand.tapeSays("WIFI: roam scan round cut short after"),
                             "a round cut short left no line on the tape");
    TEST_ASSERT_FALSE_MESSAGE(stand.roam.bandWalked(),
                              "a walk of the band cut short counted as a walk of the band");

    // And the next round walks the band again rather than one remembered channel.
    stand.acts.actFinished(BackgroundActBoard::NETWORK,
                           BackgroundActs::ofOutboxKind(OutboxKind::WIFI_RECONNECT));
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        (uint32_t)(channelsDone + WifiRoamUseCase::BAND_CHANNELS),
        (uint32_t)stand.radio.channelsLookedAt.size(),
        "the round after the one cut short did not walk the band");
    TEST_ASSERT_TRUE(stand.roam.bandWalked());
}

/**
 * A firmware download runs as an act of the command task, and it is the one act a lost association
 * costs the most: the download starts again from nothing. Nothing is asked of the radio while it
 * runs, and no move is made.
 *
 * Mutation: leave COMMAND_NETWORK out of the quiet check.
 */
void test_a_firmware_download_stops_the_looking_and_the_move(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    stand.acts.actStarted(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_FIRMWARE_UPDATE);

    for (int round = 0; round < 4; round++)
    {
        stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
        stand.aWholeRound();
    }

    char message[160];
    snprintf(message, sizeof(message), "%u look(s) and %u move(s) during a firmware download",
             (unsigned)stand.radio.channelsLookedAt.size(),
             (unsigned)stand.outbox.wifiRoamTargets.size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.radio.channelsLookedAt.size(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.outbox.wifiRoamTargets.size(), message);
}

/** A catalog write holds the storage task the same way, and counts the same way. */
void test_a_catalog_write_stops_the_looking_and_the_move(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    stand.acts.actStarted(BackgroundActBoard::CATALOG_STORAGE, BackgroundActs::CATALOG_WRITE);

    for (int round = 0; round < 4; round++)
    {
        stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
        stand.aWholeRound();
    }

    char message[160];
    snprintf(message, sizeof(message), "%u look(s) and %u move(s) during a catalog write",
             (unsigned)stand.radio.channelsLookedAt.size(),
             (unsigned)stand.outbox.wifiRoamTargets.size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.radio.channelsLookedAt.size(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.outbox.wifiRoamTargets.size(), message);
}

/**
 * Whatever happens to the link, the result of a look in the air is neither read nor released while
 * the network task is at the radio: both go through the one scan result the core keeps, and that
 * task may be freeing it in the same instant.
 *
 * Mutation: read the state or release the copy without asking the board.
 */
void test_a_look_in_the_air_is_not_touched_while_the_network_task_is_at_the_radio(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    // A look is in the air, and the radio goes to the network task.
    stand.tick();
    const size_t looksInTheAir = stand.radio.channelsLookedAt.size();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)looksInTheAir, "no look was started");
    stand.acts.actStarted(BackgroundActBoard::NETWORK,
                          BackgroundActs::ofOutboxKind(OutboxKind::WIFI_RECONNECT));

    for (int iteration = 0; iteration < 10; iteration++)
    {
        stand.tick();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)looksInTheAir,
                                     (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "another look was started while the radio was taken");

    // And the link goes under it: the round is let go of, but the copy is left for whoever scans
    // next - the restart itself - rather than freed from here.
    stand.radio.associated = false;
    stand.tick();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stand.radio.releases,
                                     "the scan result was released while the radio was taken");

    // Once the radio is free again the module carries on as usual.
    stand.radio.associated = true;
    stand.acts.actFinished(BackgroundActBoard::NETWORK,
                           BackgroundActs::ofOutboxKind(OutboxKind::WIFI_RECONNECT));
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();
    TEST_ASSERT_TRUE_MESSAGE(stand.radio.channelsLookedAt.size() > looksInTheAir,
                             "nothing looked once the radio was free again");
}

/**
 * A round the network task interrupts is not thrown away and not started over - it waits. A module
 * whose network task is busy often would otherwise never finish a round at all, which is the same
 * failure as a look that never completes.
 *
 * Mutation: drop the round when the board goes busy; start the round over.
 */
void test_a_round_the_network_task_interrupts_waits_instead_of_starting_over(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    // Two iterations: the first look of the band walk starts and its answer is taken.
    stand.tick();
    stand.tick();
    stand.tick();
    const size_t looksBeforeTheInterruption = stand.radio.channelsLookedAt.size();
    TEST_ASSERT_TRUE_MESSAGE(looksBeforeTheInterruption > 0, "the round never started");

    stand.acts.actStarted(BackgroundActBoard::NETWORK,
                          BackgroundActs::ofOutboxKind(OutboxKind::DIAGNOSTIC));
    for (int iteration = 0; iteration < 10; iteration++)
    {
        stand.tick();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)looksBeforeTheInterruption,
                                     (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "a look was started while the network task was at the radio");

    stand.acts.actFinished(BackgroundActBoard::NETWORK,
                           BackgroundActs::ofOutboxKind(OutboxKind::DIAGNOSTIC));
    stand.aWholeRound();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)WifiRoamUseCase::BAND_CHANNELS,
                                     (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "the interrupted round was started over instead of carried on");
    TEST_ASSERT_TRUE_MESSAGE(stand.roam.bandWalked(), "the interrupted round never finished");
}

/**
 * One move at a time. Between the submission and the completion this use case decides nothing -
 * the station is somewhere between two access points and every reading of it is about to be stale.
 *
 * Mutation: drop the in-flight ticket check from onControlLoopIteration().
 */
void test_only_one_move_is_ever_in_flight(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    stand.aWholeRound();
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)stand.outbox.wifiRoamTargets.size());

    const uint32_t looksSoFar = (uint32_t)stand.radio.channelsLookedAt.size();
    for (int round = 0; round < 4; round++)
    {
        stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
        stand.aWholeRound();
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.outbox.wifiRoamTargets.size(),
                                     "a second move was asked for while the first was in flight");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(looksSoFar, (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "the module kept looking around while a move was in flight");
}

/**
 * The link going down while the module is looking is the link-down path's business; what a look
 * saw says nothing about a station that is not associated with anything, and the confirmations it
 * left behind must not decide the next move.
 *
 * Mutation: weigh the look without checking the association.
 */
void test_a_look_that_outlived_the_link_decides_nothing(void)
{
    RoamStand stand;
    stand.radio.here = nodeOf(0x01, 1, -57);
    stand.radio.around.push_back(stand.radio.here);
    stand.radio.around.push_back(nodeOf(0x02, 6, -33));

    // One round with the link up: the candidate has its first confirmation.
    stand.aWholeRound();

    // The second round starts on a link that goes down while a look is in the air. What the radio
    // is about to do is restart itself, so the round is let go of - and the look's result with it.
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.tick();
    const size_t looksWhenTheLinkWent = stand.radio.channelsLookedAt.size();
    TEST_ASSERT_TRUE_MESSAGE(looksWhenTheLinkWent > WifiRoamUseCase::BAND_CHANNELS,
                             "the second round never started");
    stand.radio.associated = false;
    stand.aWholeRound();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stand.radio.releases,
                                     "the look in the air was not let go of");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)looksWhenTheLinkWent,
                                     (uint32_t)stand.radio.channelsLookedAt.size(),
                                     "the module kept looking around on a link that was gone");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.outbox.wifiRoamTargets.size(),
                                     "a station with no link was sent to an access point");

    // And the confirmation from before the drop does not count towards the two the next move needs.
    stand.radio.associated = true;
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.outbox.wifiRoamTargets.size(),
                                     "confirmations from before the link went down were counted");
}

/**
 * The rules the policy holds are only worth what the use case arms. After a move, the assembled
 * module must not ask for a second one inside the interval, and must not go back to the access
 * point it left inside the ban - however good that one starts to look.
 *
 * Mutation: never call noteRoamStarted() from the use case.
 */
void test_the_interval_and_the_ban_are_armed_by_the_use_case(void)
{
    RoamStand stand;
    const WifiNode far = nodeOf(0x01, 1, -57);
    const WifiNode near = nodeOf(0x02, 6, -33);
    stand.radio.here = far;
    stand.radio.around.push_back(far);
    stand.radio.around.push_back(near);

    stand.aWholeRound();
    stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
    stand.aWholeRound();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.outbox.wifiRoamTargets.size(),
                                     "the first move was never asked for");
    stand.networkTaskAnswers();
    const unsigned long movedAt = stand.clock.millis();

    // The station is on the near one now, and the one it left comes back far stronger.
    stand.radio.here.rssi = -57;
    stand.radio.around[0].rssi = -20;
    stand.radio.around[1].rssi = -57;

    uint32_t movesInsideTheInterval = 0;
    uint32_t movesBackInsideTheBan = 0;
    for (int round = 0; round < 8; round++)
    {
        stand.clock.setMillis(stand.clock.millis() + WifiRoamPolicy::SCAN_INTERVAL_MS);
        stand.aWholeRound();
        if (stand.outbox.wifiRoamTargets.size() <= 1)
        {
            continue;
        }

        const uint32_t since = (uint32_t)(stand.clock.millis() - movedAt);
        if (since < WifiRoamPolicy::MIN_ROAM_INTERVAL_MS)
        {
            movesInsideTheInterval++;
        }
        if (since < WifiRoamPolicy::STAY_PUT_AFTER_ROAM_MS &&
            sameBssid(stand.outbox.wifiRoamTargets.back().bssid, far.bssid))
        {
            movesBackInsideTheBan++;
        }
        stand.networkTaskAnswers();
    }

    char message[160];
    snprintf(message, sizeof(message), "%u moves inside the interval, %u back to the banned node",
             (unsigned)movesInsideTheInterval, (unsigned)movesBackInsideTheBan);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, movesInsideTheInterval, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, movesBackInsideTheBan, message);
}

// ================================================================================================
// 3. The job: the production outbox serving the move on the network task
// ================================================================================================

const char* const NETWORK_TASK = "outbox-net";

/** How long the radio takes to drop one association and make another. */
const uint32_t MOVE_MS = 4000;

/** How long one request of the server takes, so that the order of the two is visible. */
const uint32_t REQUEST_MS = 3000;

/**
 * @brief The radio of the network task: a move costs time on the scheduler, as an association does.
 */
class TimedRoamRadio : public IWifiRoamRadio
{
public:
    TimedRoamRadio(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock)
    {
    }

    struct Move
    {
        uint8_t bssid[6];
        uint8_t channel;
        uint64_t startedAtMs;
        uint64_t finishedAtMs;
        std::string task;
    };

    bool succeeds = true;
    std::vector<Move> moves;

    bool associatedNode(WifiNode&) override { return false; }
    bool startNodeScan(uint8_t) override { return false; }
    int nodeScanState() override { return SCAN_NONE; }
    size_t takeNodeScan(WifiNode*, size_t) override { return 0; }
    bool signalWarningRaised() override { return false; }

    bool roamTo(const uint8_t* bssid, uint8_t channel) override
    {
        Move move;
        memcpy(move.bssid, bssid, sizeof(move.bssid));
        move.channel = channel;
        move.startedAtMs = _clock.nowMs();
        move.task = _scheduler.currentTaskName();
        _scheduler.delayMs(MOVE_MS);
        move.finishedAtMs = _clock.nowMs();
        moves.push_back(move);
        return succeeds;
    }

private:
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
};

/**
 * A server that costs the calling task virtual time, as a socket does.
 *
 * The time is the whole point here: with a server that answers instantly, a request served before
 * the move and a request served after it are the same picture, and a test of the order proves
 * nothing.
 */
class SlowServer : public MockHybridAPIService
{
public:
    SlowServer(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock)
    {
    }

    /** When each call started, on the virtual clock. */
    std::vector<uint64_t> diagnosticsAtMs;

    bool sendDiagnosticData(const DiagnosticDto&, ApiPriority = ApiPriority::UNDEFINED) override
    {
        diagnosticsAtMs.push_back(_clock.nowMs());
        _scheduler.delayMs(REQUEST_MS);
        return true;
    }

private:
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
};

struct JobStand
{
    VirtualClock clock;
    HostTaskScheduler scheduler{clock};
    MockLogger logger;
    MockDebugSystem debug;
    SlowServer server{scheduler, clock};
    TimedRoamRadio radio{scheduler, clock};
    HostMessageQueue<OutboxCompletion> completions{Outbox::COMPLETION_QUEUE_DEPTH};
    Outbox outbox{&server, &scheduler, &completions, &logger, &debug};

    JobStand() { debug.featureEnabled = false; }

    std::vector<OutboxCompletion> drain()
    {
        std::vector<OutboxCompletion> collected;
        OutboxCompletion completion;
        while (outbox.pollCompletion(completion))
        {
            collected.push_back(completion);
        }
        return collected;
    }
};

/**
 * The move is a job of the link, like the radio restart: it is served before the requests waiting
 * in the queue, it runs on the network task and not on whoever asked for it, it names the access
 * point it was given, and the producer learns the outcome from a completion.
 *
 * Mutation: serve the roam slot from takeNextJob() instead of takeLinkJob().
 */
void test_the_move_is_served_ahead_of_the_requests_on_the_network_task(void)
{
    JobStand stand;
    stand.outbox.setRoamRadio(&stand.radio);

    DiagnosticDto diagnostic;
    diagnostic.timestamp = "first";
    stand.outbox.submitDiagnostic(diagnostic);

    const uint8_t target[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    const uint32_t ticket = stand.outbox.submitWifiRoam(target, 6);
    TEST_ASSERT_NOT_EQUAL(IOutbox::kRejected, ticket);

    stand.scheduler.runForMs(MOVE_MS + REQUEST_MS + 2000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.radio.moves.size(),
                                     "the move was never made");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(NETWORK_TASK, stand.radio.moves[0].task.c_str(),
                                     "the move ran on the wrong task");
    TEST_ASSERT_TRUE_MESSAGE(sameBssid(stand.radio.moves[0].bssid, target),
                             "the move was made to the wrong access point");
    TEST_ASSERT_EQUAL_UINT8(6, stand.radio.moves[0].channel);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.server.diagnosticsAtMs.size(),
                                     "the request the move was queued behind never went");

    // The order, and not just the presence of both: the server costs time, so a move served after
    // the queue would start after the request rather than before it.
    char message[192];
    snprintf(message, sizeof(message), "the move ran at %llu ms, the queued request at %llu ms",
             (unsigned long long)stand.radio.moves[0].startedAtMs,
             (unsigned long long)stand.server.diagnosticsAtMs[0]);
    TEST_ASSERT_TRUE_MESSAGE(stand.radio.moves[0].startedAtMs < stand.server.diagnosticsAtMs[0],
                             message);

    const std::vector<OutboxCompletion> completions = stand.drain();
    bool answered = false;
    for (size_t index = 0; index < completions.size(); index++)
    {
        if (completions[index].kind == OutboxKind::WIFI_ROAM)
        {
            answered = true;
            TEST_ASSERT_EQUAL_UINT32(ticket, completions[index].ticket);
            TEST_ASSERT_TRUE_MESSAGE(completions[index].delivered,
                                     "a move that was made came back as one that was not");
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, completions[index].attempts,
                                            "the move was attempted more than once");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(answered, "the producer never heard what came of the move");
}

/**
 * A move that fails is one attempt, not one per retry of the request budget: the target was chosen
 * from a look that is already stale by the time the first attempt is over, and the producer looks
 * again on its own interval anyway. Four attempts would be four associations dropped on a station
 * that has just shown it cannot reach that access point.
 *
 * Mutation: give the roam the request retry budget.
 */
void test_a_move_that_fails_is_attempted_once(void)
{
    JobStand stand;
    stand.outbox.setRoamRadio(&stand.radio);
    stand.radio.succeeds = false;

    const uint8_t target[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    const uint32_t ticket = stand.outbox.submitWifiRoam(target, 6);

    stand.scheduler.runForMs(10u * 60u * 1000u);

    char message[128];
    snprintf(message, sizeof(message), "the move that failed was attempted %u times",
             (unsigned)stand.radio.moves.size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.radio.moves.size(), message);

    const std::vector<OutboxCompletion> completions = stand.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_EQUAL_UINT32(ticket, completions[0].ticket);
    TEST_ASSERT_FALSE_MESSAGE(completions[0].delivered,
                              "a move that failed came back as one that was made");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, completions[0].attempts,
                                    "the completion reports more than one attempt");
}

/**
 * The act the move is goes on the board under its own name, so that a heap low that lands on it is
 * a line naming the move rather than a number nobody can place.
 *
 * Mutation: leave WIFI_ROAM out of the name switch (which is a compile error) or stop noting the
 * act around the attempt.
 */
void test_the_move_is_on_the_board_under_its_own_name(void)
{
    JobStand stand;
    BackgroundActBoard acts(nullptr);
    stand.outbox.setRoamRadio(&stand.radio);
    stand.outbox.setActBoard(&acts);

    const uint8_t target[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    stand.outbox.submitWifiRoam(target, 6);

    // Half way through the move: the board is read from another task, as the heap trace reads it.
    stand.scheduler.runForMs(MOVE_MS / 2);
    BackgroundActBoard::Snapshot snapshot;
    acts.capture(0, snapshot);

    const BackgroundActBoard::Reading reading =
        BackgroundActBoard::read(snapshot, BackgroundActBoard::NETWORK);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(BackgroundActs::ofOutboxKind(OutboxKind::WIFI_ROAM),
                                    reading.act, "the move is not on the board while it is made");
    TEST_ASSERT_FALSE_MESSAGE(reading.finished, "the move was over before it was made");
    TEST_ASSERT_EQUAL_STRING("WIFI_ROAM",
                             BackgroundActs::name(BackgroundActBoard::NETWORK, reading.act));

    stand.scheduler.runForMs(MOVE_MS);
    acts.capture(0, snapshot);
    TEST_ASSERT_TRUE_MESSAGE(
        BackgroundActBoard::read(snapshot, BackgroundActBoard::NETWORK).finished,
        "the move never ended on the board");
}

/**
 * A second target submitted while one waits replaces it rather than queueing behind it: the second
 * was chosen from a fresher look, and moving twice is exactly what the policy spends its rules
 * avoiding.
 *
 * Mutation: give the roam slot a queue.
 */
void test_a_fresher_target_replaces_the_one_waiting(void)
{
    JobStand stand;
    stand.outbox.setRoamRadio(&stand.radio);

    const uint8_t first[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    const uint8_t second[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
    stand.outbox.submitWifiRoam(first, 6);
    stand.outbox.submitWifiRoam(second, 11);

    stand.scheduler.runForMs(MOVE_MS + 1000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.radio.moves.size(),
                                     "both targets were moved to");
    TEST_ASSERT_TRUE_MESSAGE(sameBssid(stand.radio.moves[0].bssid, second),
                             "the stale target was the one taken");
    TEST_ASSERT_EQUAL_UINT8(11, stand.radio.moves[0].channel);
    TEST_ASSERT_EQUAL_UINT32(1, stand.outbox.collapsedCount());
}

// ================================================================================================
// 4. The diagnostics and a link the module took down itself
// ================================================================================================

/** The diagnostics with the act board, as the composition root wires them. */
struct EdgeStand
{
    MockRestAPIService restApi;
    MockHybridAPIService hybridApi;
    MockSettings settings;
    MockLogger logger;
    MockDebugSystem debug;
    MockModuleConfig config;
    MockNTPTimeAdapter timeAdapter;
    MockWifiAdapter wifi;
    MockArduinoAdapter clock;
    MockMqttManager mqtt;
    ArduinoJsonConverter json;
    BackgroundActBoard acts{&clock};
    MockOutbox outbox;
    LinkSupervisor supervisor;

    EdgeStand()
        : supervisor(&restApi, &config, logger, debug, &wifi, &timeAdapter, &clock, &mqtt)
    {
        debug.featureEnabled = true;
        config.setSettings(&settings);
        config.setModuleUid("roam-edge-uid");
        timeAdapter.setFormattedTime("2026-09-23T10:00:00Z");
        hybridApi.sendDiagnosticDataReturnValue = true;
        mqtt.isConnectedReturnValue = true;
        wifi.setConnected(true);
        clock.setMillis(1000);
        supervisor.setOutbox(&outbox);
        supervisor.setActBoard(&acts);
    }
};

/**
 * The link is down because this module took it down: the network task is between two access
 * points. Restarting the radio on top of that buys nothing and costs a second association and a
 * second handshake, so the edge waits - and is raised as usual once the move is over and the link
 * really is gone.
 *
 * Mutation: decide the edge without reading the board.
 */
void test_a_link_down_for_a_move_in_flight_is_not_answered_with_a_restart(void)
{
    EdgeStand stand;

    stand.supervisor.superviseOnce();
    TEST_ASSERT_EQUAL_UINT32(0, stand.outbox.wifiReconnectRequests);

    // The move is in flight on the network task and the association is down for its few seconds.
    stand.acts.actStarted(BackgroundActBoard::NETWORK,
                          BackgroundActs::ofOutboxKind(OutboxKind::WIFI_ROAM));
    stand.wifi.setConnected(false);

    stand.clock.setMillis(stand.clock.millis() + 30000);
    stand.supervisor.superviseOnce();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stand.outbox.wifiReconnectRequests,
                                     "the radio was restarted under a move the module was making");
    TEST_ASSERT_TRUE(stand.logger.hasMessageContaining(
        "WIFI_RECONNECT: skip (move between access points in flight)"));

    // The move is over and the link did not come back: this is a real edge again.
    stand.acts.actFinished(BackgroundActBoard::NETWORK,
                           BackgroundActs::ofOutboxKind(OutboxKind::WIFI_ROAM));
    stand.clock.setMillis(stand.clock.millis() + 30000);
    stand.supervisor.superviseOnce();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stand.outbox.wifiReconnectRequests,
                                     "the edge was never raised once the move was over");
}

/**
 * And the debounce is about the move only: a request of the network task is not a reason to leave
 * a link that really is down without a restart.
 *
 * Mutation: skip the edge for any act of the network task.
 */
void test_a_link_down_under_an_ordinary_request_is_still_answered(void)
{
    EdgeStand stand;
    stand.supervisor.superviseOnce();

    stand.acts.actStarted(BackgroundActBoard::NETWORK,
                          BackgroundActs::ofOutboxKind(OutboxKind::DIAGNOSTIC));
    stand.wifi.setConnected(false);
    stand.clock.setMillis(stand.clock.millis() + 30000);
    stand.supervisor.superviseOnce();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stand.outbox.wifiReconnectRequests,
                                     "a link that was really down was left without a restart");
}

} // namespace

extern "C" void run_wifi_roam_tests(void)
{
    printf("\n----- Running WiFi Roam Tests -----\n");

    RUN_TEST(test_a_move_needs_more_than_the_hysteresis);
    RUN_TEST(test_a_strong_signal_is_left_alone);
    RUN_TEST(test_no_second_move_before_the_interval);
    RUN_TEST(test_a_busy_module_is_not_moved);
    RUN_TEST(test_the_access_point_just_left_is_not_taken_back);
    RUN_TEST(test_one_look_is_not_enough);
    RUN_TEST(test_looking_around_is_rationed);

    RUN_TEST(test_the_band_is_walked_one_channel_at_a_time_and_then_only_the_known_ones);
    RUN_TEST(test_a_look_the_radio_will_not_answer_is_counted_and_said);
    RUN_TEST(test_a_look_that_never_answers_is_given_up_on);
    RUN_TEST(test_access_points_that_do_not_fit_are_counted);
    RUN_TEST(test_channels_that_do_not_fit_are_counted);
    RUN_TEST(test_the_band_is_walked_again_after_enough_rounds);
    RUN_TEST(test_a_module_with_a_good_signal_never_looks);
    RUN_TEST(test_the_radios_warning_brings_the_next_look_forward);
    RUN_TEST(test_the_warning_cannot_bring_the_looks_closer_than_the_floor);
    RUN_TEST(test_a_weak_station_is_moved_to_the_near_access_point);
    RUN_TEST(test_a_move_that_failed_is_counted_and_said);
    RUN_TEST(test_nothing_is_asked_of_the_radio_while_the_network_task_is_at_it);
    RUN_TEST(test_a_firmware_download_stops_the_looking_and_the_move);
    RUN_TEST(test_a_catalog_write_stops_the_looking_and_the_move);
    RUN_TEST(test_a_look_in_the_air_is_not_touched_while_the_network_task_is_at_the_radio);
    RUN_TEST(test_a_band_walk_cut_short_is_not_counted_as_done);
    RUN_TEST(test_a_round_the_network_task_interrupts_waits_instead_of_starting_over);
    RUN_TEST(test_only_one_move_is_ever_in_flight);
    RUN_TEST(test_a_look_that_outlived_the_link_decides_nothing);
    RUN_TEST(test_the_interval_and_the_ban_are_armed_by_the_use_case);

    RUN_TEST(test_the_move_is_served_ahead_of_the_requests_on_the_network_task);
    RUN_TEST(test_a_move_that_fails_is_attempted_once);
    RUN_TEST(test_the_move_is_on_the_board_under_its_own_name);
    RUN_TEST(test_a_fresher_target_replaces_the_one_waiting);

    RUN_TEST(test_a_link_down_for_a_move_in_flight_is_not_answered_with_a_restart);
    RUN_TEST(test_a_link_down_under_an_ordinary_request_is_still_answered);

    printf("----- WiFi Roam Tests Complete -----\n");
}
