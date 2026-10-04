// What the module does when it cannot reach its server: the troubleshooting machine, the probe it
// asks for, and the statuses the module's parts report.
//
//  1. **The machine** (LinkTroubleshooting::step) - every pair of phase and event, the impossible
//     ones included, and then the sequences the algorithm is made of: the two silent gateway checks
//     thirty seconds apart, the radio restarts after 1, 2, 5 and 10 minutes, the five-minute
//     recheck past the router, the restart of the module only on a fault that held, and the record
//     of the outage.
//  2. **The probe** (runLinkProbe) - the gateway, then a public address; what it asks and when it
//     stops, over a stand-in for the radio that answers what the test says the network answers.
//  3. **The statuses** (ModuleStatusBoard) - which TLS errors are a lack of memory, how long a
//     handshake that failed for memory counts, and whether the server answered since a moment.
//
// Nothing here is a copy of the logic under test: the machine is a pure function and is called as
// it is; the stand-in replaces the ARP request and the echo, which the board makes through lwIP.

#include <unity.h>

#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Core/Domain/UseCases/Network/LinkProbeRun.h"
#include "Core/Domain/UseCases/Network/LinkTroubleshooting.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

typedef LinkTroubleshooting LT;
typedef LT::Phase Phase;
typedef LT::EventKind Kind;

const uint32_t T0 = 100000;

// --------------------------------------------------------------------------------------------------
// The machine's canonical states: one per phase, consistent with how the phase is entered, with a
// wait that is not over at T0 + 1 s.
// --------------------------------------------------------------------------------------------------

LT::State stateIn(Phase phase)
{
    LT::State state;
    state.phase = phase;
    state.enteredAtMs = T0;
    state.outageStartedAtMs = T0;
    state.waitFromMs = T0;
    switch (phase)
    {
    case Phase::OK:
        state.outageStartedAtMs = 0;
        state.waitFromMs = 0;
        break;
    case Phase::SUSPECT:
        state.servicesUp = false;
        state.waitMs = LT::SUSPECT_MS;
        break;
    case Phase::PROBING:
        state.servicesUp = false;
        break;
    case Phase::LINK_DOWN:
        state.wifiUp = false;
        state.servicesUp = false;
        state.cause = LT::Cause::ROUTER;
        state.restartStep = 1;
        state.waitMs = LT::restartPauseMs(0);
        break;
    case Phase::UPSTREAM_DOWN:
        state.servicesUp = false;
        state.cause = LT::Cause::INTERNET;
        state.waitMs = LT::UPSTREAM_RECHECK_MS;
        break;
    case Phase::MODULE_FAULT:
        state.servicesUp = false;
        state.faultReported = true;
        state.faultSinceMs = T0;
        state.cause = LT::Cause::MODULE;
        break;
    }
    return state;
}

LT::Event eventOf(Kind kind, uint32_t nowMs)
{
    LT::Event event;
    event.kind = kind;
    event.nowMs = nowMs;
    if (kind == Kind::PROBE_DONE)
    {
        event.probe.gateway = LinkReach::ANSWERED;
        event.probe.publicAddress = LinkReach::ANSWERED;
    }
    return event;
}

struct Pair
{
    Phase from;
    Kind event;
    bool expected;
    Phase to;
    uint8_t actions;
};

const Pair kEveryPair[] = {
    // OK
    {Phase::OK, Kind::TICK, true, Phase::OK, LT::NO_ACTION},
    {Phase::OK, Kind::WIFI_LOST, true, Phase::LINK_DOWN, LT::RESTART_RADIO},
    {Phase::OK, Kind::WIFI_BACK, false, Phase::OK, LT::NO_ACTION},
    {Phase::OK, Kind::SERVICES_FAILING, true, Phase::SUSPECT, LT::NO_ACTION},
    {Phase::OK, Kind::SERVICES_RESTORED, false, Phase::OK, LT::NO_ACTION},
    {Phase::OK, Kind::PROBE_DONE, true, Phase::OK, LT::NO_ACTION},
    {Phase::OK, Kind::RESTART_DONE, true, Phase::OK, LT::NO_ACTION},
    {Phase::OK, Kind::MODULE_FAULT, true, Phase::OK, LT::NO_ACTION},
    {Phase::OK, Kind::MODULE_FAULT_GONE, false, Phase::OK, LT::NO_ACTION},
    // SUSPECT
    {Phase::SUSPECT, Kind::TICK, true, Phase::SUSPECT, LT::NO_ACTION},
    {Phase::SUSPECT, Kind::WIFI_LOST, true, Phase::LINK_DOWN, LT::RESTART_RADIO},
    {Phase::SUSPECT, Kind::WIFI_BACK, false, Phase::SUSPECT, LT::NO_ACTION},
    {Phase::SUSPECT, Kind::SERVICES_FAILING, false, Phase::SUSPECT, LT::NO_ACTION},
    {Phase::SUSPECT, Kind::SERVICES_RESTORED, true, Phase::OK, LT::NO_ACTION},
    {Phase::SUSPECT, Kind::PROBE_DONE, true, Phase::SUSPECT, LT::NO_ACTION},
    {Phase::SUSPECT, Kind::RESTART_DONE, true, Phase::SUSPECT, LT::NO_ACTION},
    {Phase::SUSPECT, Kind::MODULE_FAULT, true, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::SUSPECT, Kind::MODULE_FAULT_GONE, false, Phase::SUSPECT, LT::NO_ACTION},
    // PROBING
    {Phase::PROBING, Kind::TICK, true, Phase::PROBING, LT::NO_ACTION},
    {Phase::PROBING, Kind::WIFI_LOST, true, Phase::LINK_DOWN, LT::RESTART_RADIO},
    {Phase::PROBING, Kind::WIFI_BACK, false, Phase::PROBING, LT::NO_ACTION},
    {Phase::PROBING, Kind::SERVICES_FAILING, false, Phase::PROBING, LT::NO_ACTION},
    {Phase::PROBING, Kind::SERVICES_RESTORED, true, Phase::OK, LT::NO_ACTION},
    {Phase::PROBING, Kind::PROBE_DONE, true, Phase::UPSTREAM_DOWN, LT::NO_ACTION},
    {Phase::PROBING, Kind::RESTART_DONE, true, Phase::PROBING, LT::NO_ACTION},
    {Phase::PROBING, Kind::MODULE_FAULT, true, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::PROBING, Kind::MODULE_FAULT_GONE, false, Phase::PROBING, LT::NO_ACTION},
    // LINK_DOWN, no association, between two restarts
    {Phase::LINK_DOWN, Kind::TICK, true, Phase::LINK_DOWN, LT::NO_ACTION},
    {Phase::LINK_DOWN, Kind::WIFI_LOST, false, Phase::LINK_DOWN, LT::NO_ACTION},
    {Phase::LINK_DOWN, Kind::WIFI_BACK, true, Phase::LINK_DOWN, LT::NO_ACTION},
    {Phase::LINK_DOWN, Kind::SERVICES_FAILING, false, Phase::LINK_DOWN, LT::NO_ACTION},
    {Phase::LINK_DOWN, Kind::SERVICES_RESTORED, true, Phase::OK, LT::RECORD_OUTAGE},
    {Phase::LINK_DOWN, Kind::PROBE_DONE, true, Phase::LINK_DOWN, LT::NO_ACTION},
    {Phase::LINK_DOWN, Kind::RESTART_DONE, true, Phase::LINK_DOWN, LT::NO_ACTION},
    {Phase::LINK_DOWN, Kind::MODULE_FAULT, true, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::LINK_DOWN, Kind::MODULE_FAULT_GONE, false, Phase::LINK_DOWN, LT::NO_ACTION},
    // UPSTREAM_DOWN
    {Phase::UPSTREAM_DOWN, Kind::TICK, true, Phase::UPSTREAM_DOWN, LT::NO_ACTION},
    {Phase::UPSTREAM_DOWN, Kind::WIFI_LOST, true, Phase::LINK_DOWN, LT::RESTART_RADIO},
    {Phase::UPSTREAM_DOWN, Kind::WIFI_BACK, false, Phase::UPSTREAM_DOWN, LT::NO_ACTION},
    {Phase::UPSTREAM_DOWN, Kind::SERVICES_FAILING, false, Phase::UPSTREAM_DOWN, LT::NO_ACTION},
    {Phase::UPSTREAM_DOWN, Kind::SERVICES_RESTORED, true, Phase::OK, LT::RECORD_OUTAGE},
    {Phase::UPSTREAM_DOWN, Kind::PROBE_DONE, true, Phase::UPSTREAM_DOWN, LT::NO_ACTION},
    {Phase::UPSTREAM_DOWN, Kind::RESTART_DONE, true, Phase::UPSTREAM_DOWN, LT::NO_ACTION},
    {Phase::UPSTREAM_DOWN, Kind::MODULE_FAULT, true, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::UPSTREAM_DOWN, Kind::MODULE_FAULT_GONE, false, Phase::UPSTREAM_DOWN, LT::NO_ACTION},
    // MODULE_FAULT
    {Phase::MODULE_FAULT, Kind::TICK, true, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::MODULE_FAULT, Kind::WIFI_LOST, true, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::MODULE_FAULT, Kind::WIFI_BACK, false, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::MODULE_FAULT, Kind::SERVICES_FAILING, false, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::MODULE_FAULT, Kind::SERVICES_RESTORED, true, Phase::OK, LT::RECORD_OUTAGE},
    {Phase::MODULE_FAULT, Kind::PROBE_DONE, true, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::MODULE_FAULT, Kind::RESTART_DONE, true, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::MODULE_FAULT, Kind::MODULE_FAULT, false, Phase::MODULE_FAULT, LT::NO_ACTION},
    {Phase::MODULE_FAULT, Kind::MODULE_FAULT_GONE, true, Phase::SUSPECT, LT::NO_ACTION},
};

bool sameState(const LT::State& a, const LT::State& b)
{
    return a.phase == b.phase && a.enteredAtMs == b.enteredAtMs && a.outageStartedAtMs == b.outageStartedAtMs &&
           a.waitFromMs == b.waitFromMs && a.waitMs == b.waitMs && a.faultSinceMs == b.faultSinceMs &&
           a.restartAskedAtMs == b.restartAskedAtMs && a.restartStep == b.restartStep &&
           a.silentGateways == b.silentGateways && a.restartInFlight == b.restartInFlight && a.wifiUp == b.wifiUp &&
           a.servicesUp == b.servicesUp && a.faultReported == b.faultReported && a.cause == b.cause;
}

/** The first tick of the 30 s grid from `fromMs` that is at least `ms` after it. */
uint32_t firstTickAtLeast(uint32_t fromMs, uint32_t ms)
{
    return fromMs + (ms + 29999) / 30000 * 30000;
}

std::string describe(const Pair& pair)
{
    char text[160];
    snprintf(text, sizeof(text), "%s x %s", LT::phaseName(pair.from), LT::eventName(pair.event));
    return text;
}

/** Feeds one event and returns the step; the state follows it unless the event was unexpected. */
LT::Step feed(LT::State& state, Kind kind, uint32_t nowMs)
{
    const LT::Step step = LT::step(state, eventOf(kind, nowMs));
    if (step.expected)
    {
        state = step.next;
    }
    return step;
}

LT::Step probeBack(LT::State& state, uint32_t nowMs, LinkReach gateway, LinkReach publicAddress,
                   bool serverAnswered = false)
{
    LT::Event event = eventOf(Kind::PROBE_DONE, nowMs);
    event.probe.gateway = gateway;
    event.probe.publicAddress = publicAddress;
    event.serverAnswered = serverAnswered;
    const LT::Step step = LT::step(state, event);
    state = step.next;
    return step;
}

LT::Step restartBack(LT::State& state, uint32_t nowMs)
{
    const LT::Event event = eventOf(Kind::RESTART_DONE, nowMs);
    const LT::Step step = LT::step(state, event);
    state = step.next;
    return step;
}

/** Ticks every 30 s from `fromMs` until the step asks for one of `actions`; returns its moment, or 0. */
uint32_t tickUntil(LT::State& state, uint32_t fromMs, uint32_t untilMs, uint8_t actions)
{
    for (uint32_t now = fromMs; now <= untilMs; now += 30000)
    {
        const LT::Step step = feed(state, Kind::TICK, now);
        if (step.actions & actions)
        {
            return now;
        }
    }
    return 0;
}

} // namespace

// ==================================================================================================
// 1. The machine
// ==================================================================================================

/**
 * Every phase against every event. The impossible ones - an edge that repeats the level the machine
 * already holds - are refused and change nothing; a result nobody waits for any more is taken and
 * changes nothing; the rest go where the algorithm says, asking for what it says.
 */
void test_every_phase_meets_every_event(void)
{
    TEST_ASSERT_EQUAL_UINT32(6u * 9u, (uint32_t)(sizeof(kEveryPair) / sizeof(kEveryPair[0])));

    for (size_t i = 0; i < sizeof(kEveryPair) / sizeof(kEveryPair[0]); i++)
    {
        const Pair& pair = kEveryPair[i];
        const LT::State state = stateIn(pair.from);
        const LT::Step step = LT::step(state, eventOf(pair.event, T0 + 1000));
        const std::string what = describe(pair);

        TEST_ASSERT_EQUAL_MESSAGE(pair.expected, step.expected, what.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(LT::phaseName(pair.to), LT::phaseName(step.next.phase), what.c_str());
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(pair.actions, step.actions, what.c_str());
        TEST_ASSERT_NOT_NULL_MESSAGE(step.why, what.c_str());
        if (!pair.expected)
        {
            TEST_ASSERT_TRUE_MESSAGE(sameState(state, step.next), what.c_str());
        }
    }
}

/** The services failing with the station associated: a minute of patience, then a gateway check. */
void test_failing_services_are_given_a_minute_before_the_gateway_is_asked(void)
{
    LT::State state;
    feed(state, Kind::SERVICES_FAILING, T0);
    TEST_ASSERT_EQUAL(Phase::SUSPECT, state.phase);

    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(state, Kind::TICK, T0 + 30000).actions);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(state, Kind::TICK, T0 + 59999).actions);
    const LT::Step step = feed(state, Kind::TICK, T0 + 60000);
    TEST_ASSERT_EQUAL_UINT8(LT::PROBE, step.actions);
    TEST_ASSERT_EQUAL(Phase::PROBING, state.phase);
}

/**
 * A silent gateway is the router only on the second silent check, thirty seconds after the first:
 * then the radio is restarted - asked once, with no public address in the decision.
 */
void test_a_gateway_silent_twice_thirty_seconds_apart_restarts_the_radio(void)
{
    LT::State state;
    feed(state, Kind::SERVICES_FAILING, T0);
    feed(state, Kind::TICK, T0 + 60000);
    LT::Step step = probeBack(state, T0 + 63000, LinkReach::SILENT, LinkReach::SILENT);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(LT::NO_ACTION, step.actions, "the radio was restarted on one silent check");
    TEST_ASSERT_EQUAL(Phase::SUSPECT, state.phase);

    // Thirty seconds from the start of the first check, not from its result.
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(state, Kind::TICK, T0 + 89999).actions);
    TEST_ASSERT_EQUAL_UINT8(LT::PROBE, feed(state, Kind::TICK, T0 + 90000).actions);
    step = probeBack(state, T0 + 96000, LinkReach::SILENT, LinkReach::SILENT);
    TEST_ASSERT_EQUAL_UINT8(LT::RESTART_RADIO, step.actions);
    TEST_ASSERT_EQUAL(Phase::LINK_DOWN, state.phase);
    TEST_ASSERT_EQUAL(LT::Cause::ROUTER, state.cause);
}

/** One silent check and then an answer: the router is there, the radio is left alone. */
void test_a_gateway_silent_once_and_then_answering_is_not_the_router(void)
{
    LT::State state;
    feed(state, Kind::SERVICES_FAILING, T0);
    feed(state, Kind::TICK, T0 + 60000);
    probeBack(state, T0 + 63000, LinkReach::SILENT, LinkReach::SILENT);
    feed(state, Kind::TICK, T0 + 93000);
    const LT::Step step = probeBack(state, T0 + 94000, LinkReach::ANSWERED, LinkReach::SILENT);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, step.actions);
    TEST_ASSERT_EQUAL(Phase::UPSTREAM_DOWN, state.phase);
    TEST_ASSERT_EQUAL(LT::Cause::INTERNET, state.cause);
}

/**
 * A silent gateway with a public address that answers: the echo came back through the gateway, so
 * the gateway forwards whatever its silence to ARP said. The radio is left alone.
 */
void test_a_public_address_that_answers_overrules_a_silent_gateway(void)
{
    LT::State state;
    feed(state, Kind::SERVICES_FAILING, T0);
    feed(state, Kind::TICK, T0 + 60000);
    const LT::Step step = probeBack(state, T0 + 61000, LinkReach::SILENT, LinkReach::ANSWERED);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, step.actions);
    TEST_ASSERT_EQUAL(Phase::UPSTREAM_DOWN, state.phase);
    TEST_ASSERT_EQUAL(LT::Cause::SERVICES, state.cause);
    TEST_ASSERT_EQUAL_UINT8(0, state.silentGateways);
}

/** Past the router: the radio is never restarted, and the gateway is asked again every five minutes. */
void test_past_the_router_the_gateway_is_asked_again_every_five_minutes(void)
{
    LT::State state;
    feed(state, Kind::SERVICES_FAILING, T0);
    feed(state, Kind::TICK, T0 + 60000);
    probeBack(state, T0 + 61000, LinkReach::ANSWERED, LinkReach::SILENT);

    // Five minutes from the start of one look to the start of the next: on the tick grid, exactly.
    uint32_t asked = 0;
    uint32_t lastAskedAt = T0 + 60000;
    for (uint32_t now = T0 + 90000; now <= T0 + 60000 + 3 * LT::UPSTREAM_RECHECK_MS; now += 30000)
    {
        const LT::Step step = feed(state, Kind::TICK, now);
        TEST_ASSERT_EQUAL_UINT8(0, step.actions & (LT::RESTART_RADIO | LT::RESTART_MODULE));
        if (step.actions & LT::PROBE)
        {
            asked++;
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(LT::UPSTREAM_RECHECK_MS, now - lastAskedAt,
                                             "the gateway was not asked again five minutes after the last look");
            lastAskedAt = now;
            probeBack(state, now + 9000, LinkReach::ANSWERED, LinkReach::SILENT);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3, asked);
}

/**
 * No association: the radio is restarted at once, then after pauses of 1, 2, 5 and 10 minutes, each
 * measured from the end of the restart before - and never the module.
 */
void test_with_no_association_the_radio_is_restarted_after_one_two_five_and_ten_minutes(void)
{
    LT::State state;
    LT::Step step = feed(state, Kind::WIFI_LOST, T0);
    TEST_ASSERT_EQUAL_UINT8(LT::RESTART_RADIO, step.actions);

    const uint32_t expectedPauses[] = {60000, 120000, 300000, 600000, 600000, 600000};
    uint32_t restartEndedAt = T0 + 20000;
    restartBack(state, restartEndedAt);
    for (size_t i = 0; i < sizeof(expectedPauses) / sizeof(expectedPauses[0]); i++)
    {
        // A tick a millisecond short of the pause asks for nothing; the one at the pause asks.
        TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(state, Kind::TICK, restartEndedAt + expectedPauses[i] - 1).actions);
        step = feed(state, Kind::TICK, restartEndedAt + expectedPauses[i]);
        char message[96];
        snprintf(message, sizeof(message), "restart %u did not follow a pause of %lu ms", (unsigned)(i + 2),
                 (unsigned long)expectedPauses[i]);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(LT::RESTART_RADIO, step.actions, message);
        restartEndedAt += expectedPauses[i] + 15000;
        restartBack(state, restartEndedAt);
    }
    TEST_ASSERT_EQUAL(Phase::LINK_DOWN, state.phase);
    TEST_ASSERT_EQUAL(0, tickUntil(state, restartEndedAt + 30000, restartEndedAt + 4u * 3600u * 1000u,
                                   LT::RESTART_MODULE));
}

/** While a restart runs nothing more is asked; one that never comes back is given up on. */
void test_a_radio_restart_in_flight_is_waited_for_and_a_lost_one_given_up_on(void)
{
    LT::State state;
    feed(state, Kind::WIFI_LOST, T0);
    TEST_ASSERT_TRUE(state.restartInFlight);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(state, Kind::TICK, T0 + 10u * 60u * 1000u).actions);

    const LT::Step lost = feed(state, Kind::TICK, T0 + LT::LINK_JOB_CEILING_MS);
    TEST_ASSERT_FALSE(state.restartInFlight);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, lost.actions);
    TEST_ASSERT_EQUAL_UINT8(LT::RESTART_RADIO,
                            feed(state, Kind::TICK, T0 + LT::LINK_JOB_CEILING_MS + LT::restartPauseMs(0)).actions);
}

/**
 * In LINK_DOWN with the station associated again but the services still failing, the pause ends in a
 * gateway check, not in a restart: a router that came back is not restarted under.
 */
void test_an_association_back_with_failing_services_is_checked_before_the_next_restart(void)
{
    LT::State state;
    feed(state, Kind::WIFI_LOST, T0);
    feed(state, Kind::SERVICES_FAILING, T0);
    restartBack(state, T0 + 5000);
    feed(state, Kind::WIFI_BACK, T0 + 6000);

    LT::Step step = feed(state, Kind::TICK, T0 + 5000 + LT::restartPauseMs(0));
    TEST_ASSERT_EQUAL_UINT8(LT::PROBE, step.actions);

    step = probeBack(state, T0 + 70000, LinkReach::ANSWERED, LinkReach::ANSWERED);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, step.actions);
    TEST_ASSERT_EQUAL(Phase::UPSTREAM_DOWN, state.phase);

    // Once the router has been out of reach in this outage, one silent check is enough again.
    LT::State routerStill;
    feed(routerStill, Kind::WIFI_LOST, T0);
    feed(routerStill, Kind::SERVICES_FAILING, T0);
    restartBack(routerStill, T0 + 5000);
    feed(routerStill, Kind::WIFI_BACK, T0 + 6000);
    feed(routerStill, Kind::TICK, T0 + 5000 + LT::restartPauseMs(0));
    step = probeBack(routerStill, T0 + 70000, LinkReach::SILENT, LinkReach::SILENT);
    TEST_ASSERT_EQUAL_UINT8(LT::RESTART_RADIO, step.actions);
}

/** A probe that could not ask the gateway decides nothing: another check thirty seconds on. */
void test_a_probe_that_could_not_ask_decides_nothing(void)
{
    LT::State state;
    feed(state, Kind::SERVICES_FAILING, T0);
    feed(state, Kind::TICK, T0 + 60000);
    LT::Step step = probeBack(state, T0 + 61000, LinkReach::UNAVAILABLE, LinkReach::NOT_RUN);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, step.actions);
    TEST_ASSERT_EQUAL(Phase::SUSPECT, state.phase);
    TEST_ASSERT_EQUAL_UINT8(0, state.silentGateways);
    TEST_ASSERT_EQUAL_UINT8(LT::PROBE, feed(state, Kind::TICK, T0 + 91000).actions);

    // ... nor does one that never comes back.
    LT::State lost;
    feed(lost, Kind::SERVICES_FAILING, T0);
    feed(lost, Kind::TICK, T0 + 60000);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(lost, Kind::TICK, T0 + 60000 + LT::LINK_JOB_CEILING_MS - 1).actions);
    step = feed(lost, Kind::TICK, T0 + 60000 + LT::LINK_JOB_CEILING_MS);
    TEST_ASSERT_EQUAL(Phase::SUSPECT, lost.phase);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, step.actions);
}

/**
 * The module is restarted only on a fault of its own that held, with the link in trouble, for
 * FAULT_HOLD_MS - once, and again only after another hold.
 */
void test_a_fault_of_the_module_that_holds_restarts_the_module(void)
{
    LT::State state;
    feed(state, Kind::MODULE_FAULT, T0 - 5000);
    TEST_ASSERT_EQUAL(Phase::OK, state.phase);
    feed(state, Kind::SERVICES_FAILING, T0);
    TEST_ASSERT_EQUAL(Phase::MODULE_FAULT, state.phase);

    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(state, Kind::TICK, T0 + LT::FAULT_HOLD_MS - 1).actions);
    TEST_ASSERT_EQUAL_UINT8(LT::RESTART_MODULE, feed(state, Kind::TICK, T0 + LT::FAULT_HOLD_MS).actions);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(state, Kind::TICK, T0 + LT::FAULT_HOLD_MS + 30000).actions);
    TEST_ASSERT_EQUAL_UINT8(LT::RESTART_MODULE, feed(state, Kind::TICK, T0 + 2 * LT::FAULT_HOLD_MS).actions);
}

/** A fault that clears before it held restarts nothing; the outage is diagnosed again. */
void test_a_fault_that_clears_before_it_held_restarts_nothing(void)
{
    LT::State state;
    feed(state, Kind::SERVICES_FAILING, T0);
    feed(state, Kind::MODULE_FAULT, T0 + 30000);
    TEST_ASSERT_EQUAL(Phase::MODULE_FAULT, state.phase);
    feed(state, Kind::MODULE_FAULT_GONE, T0 + 60000);
    TEST_ASSERT_EQUAL(Phase::SUSPECT, state.phase);
    TEST_ASSERT_EQUAL_UINT8(LT::PROBE, feed(state, Kind::TICK, T0 + 60000).actions);
    TEST_ASSERT_EQUAL(0, tickUntil(state, T0 + 90000, T0 + 4u * 3600u * 1000u, LT::RESTART_MODULE));
}

/** No association with a fault of the module reported: the fault decides, not the radio. */
void test_a_lost_association_with_a_fault_reported_waits_for_the_fault_to_hold(void)
{
    LT::State state;
    feed(state, Kind::MODULE_FAULT, T0);
    const LT::Step step = feed(state, Kind::WIFI_LOST, T0 + 30000);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, step.actions);
    TEST_ASSERT_EQUAL(Phase::MODULE_FAULT, state.phase);
    TEST_ASSERT_EQUAL_UINT32(firstTickAtLeast(T0 + 30000, LT::FAULT_HOLD_MS),
                             tickUntil(state, T0 + 30000, T0 + 30000 + 2 * LT::FAULT_HOLD_MS, LT::RESTART_MODULE));

    // The fault cleared with the station still not associated: the radio is restarted.
    LT::State cleared;
    feed(cleared, Kind::MODULE_FAULT, T0);
    feed(cleared, Kind::WIFI_LOST, T0 + 30000);
    TEST_ASSERT_EQUAL_UINT8(LT::RESTART_RADIO, feed(cleared, Kind::MODULE_FAULT_GONE, T0 + 60000).actions);
    TEST_ASSERT_EQUAL(Phase::LINK_DOWN, cleared.phase);
}

/**
 * Coming back records the outage: what it was found to be - the closest cause it reached - when it
 * started and how long it lasted. One that ended before anything was found is not recorded.
 */
void test_the_outage_is_recorded_with_its_closest_cause_start_and_length(void)
{
    LT::State state;
    feed(state, Kind::WIFI_LOST, T0);
    feed(state, Kind::SERVICES_FAILING, T0);
    restartBack(state, T0 + 10000);
    feed(state, Kind::WIFI_BACK, T0 + 10000);
    feed(state, Kind::TICK, T0 + 10000 + LT::restartPauseMs(0));
    probeBack(state, T0 + 75000, LinkReach::ANSWERED, LinkReach::SILENT);
    TEST_ASSERT_EQUAL(Phase::UPSTREAM_DOWN, state.phase);

    const LT::Step step = feed(state, Kind::SERVICES_RESTORED, T0 + 900000);
    TEST_ASSERT_EQUAL_UINT8(LT::RECORD_OUTAGE, step.actions);
    TEST_ASSERT_EQUAL_STRING("router", LT::causeName(step.outage.cause));
    TEST_ASSERT_EQUAL_UINT32(T0, step.outage.startedAtMs);
    TEST_ASSERT_EQUAL_UINT32(900000, step.outage.durationMs);
    TEST_ASSERT_EQUAL(LT::Cause::NONE, state.cause);

    LT::State brief;
    feed(brief, Kind::SERVICES_FAILING, T0);
    TEST_ASSERT_EQUAL_UINT8(LT::NO_ACTION, feed(brief, Kind::SERVICES_RESTORED, T0 + 30000).actions);

    LT::State internet;
    feed(internet, Kind::SERVICES_FAILING, T0);
    feed(internet, Kind::TICK, T0 + 60000);
    probeBack(internet, T0 + 61000, LinkReach::ANSWERED, LinkReach::SILENT);
    TEST_ASSERT_EQUAL_STRING("internet",
                             LT::causeName(feed(internet, Kind::SERVICES_RESTORED, T0 + 120000).outage.cause));

    LT::State services;
    feed(services, Kind::SERVICES_FAILING, T0);
    feed(services, Kind::TICK, T0 + 60000);
    probeBack(services, T0 + 61000, LinkReach::ANSWERED, LinkReach::SILENT, true);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("services",
                                     LT::causeName(feed(services, Kind::SERVICES_RESTORED, T0 + 120000).outage.cause),
                                     "the server's own answer did not count against the internet");

    LT::State module;
    feed(module, Kind::SERVICES_FAILING, T0);
    feed(module, Kind::MODULE_FAULT, T0 + 30000);
    TEST_ASSERT_EQUAL_STRING("module", LT::causeName(feed(module, Kind::SERVICES_RESTORED, T0 + 60000).outage.cause));
}

/** The pauses, as the table holds them: 1, 2, 5, then 10 minutes for good. */
void test_the_restart_pauses_are_one_two_five_and_ten_minutes(void)
{
    TEST_ASSERT_EQUAL_UINT32(60000, LT::restartPauseMs(0));
    TEST_ASSERT_EQUAL_UINT32(120000, LT::restartPauseMs(1));
    TEST_ASSERT_EQUAL_UINT32(300000, LT::restartPauseMs(2));
    TEST_ASSERT_EQUAL_UINT32(600000, LT::restartPauseMs(3));
    TEST_ASSERT_EQUAL_UINT32(600000, LT::restartPauseMs(200));
}

/** The hold, as its derivation adds up: one request's life in the outbox and one tick. */
void test_the_fault_hold_is_one_request_life_and_one_tick(void)
{
    TEST_ASSERT_EQUAL_UINT32(720500, LT::FAULT_HOLD_MS);
    TEST_ASSERT_EQUAL_UINT32(787500, LT::LINK_JOB_CEILING_MS);
}

// ==================================================================================================
// 2. The probe
// ==================================================================================================

namespace {

/** The radio as the probe sees it: what each ask returns, and what was asked. */
class ScriptedProbe : public ILinkProbe
{
public:
    LinkReach gateway = LinkReach::ANSWERED;
    /** By address; anything not listed is silent. */
    std::vector<std::pair<uint32_t, LinkReach> > echoes;
    std::vector<uint32_t> echoedTo;
    std::vector<uint32_t> waits;
    int gatewayAsks = 0;

    LinkReach askGateway(uint32_t waitMs) override
    {
        gatewayAsks++;
        waits.push_back(waitMs);
        return gateway;
    }

    LinkReach echo(uint32_t address, uint32_t waitMs) override
    {
        echoedTo.push_back(address);
        waits.push_back(waitMs);
        for (size_t i = 0; i < echoes.size(); i++)
        {
            if (echoes[i].first == address)
            {
                return echoes[i].second;
            }
        }
        return LinkReach::SILENT;
    }
};

LinkProbePlan planOfTwo(bool afterASilentGateway)
{
    LinkProbePlan plan;
    plan.gatewayWaitMs = 3000;
    plan.publicAddresses[0] = 0x08080808;
    plan.publicAddresses[1] = 0x01010101;
    plan.echoesPerAddress = 3;
    plan.echoWaitMs = 1000;
    plan.publicAddressAfterASilentGateway = afterASilentGateway;
    return plan;
}

} // namespace

/** The gateway first, with its wait; then the first address answered ends the probe. */
void test_the_probe_asks_the_gateway_then_stops_at_the_first_echo_answered(void)
{
    ScriptedProbe radio;
    radio.echoes.push_back(std::make_pair(0x08080808u, LinkReach::ANSWERED));
    const LinkProbeResult result = runLinkProbe(radio, planOfTwo(true));
    TEST_ASSERT_EQUAL(LinkReach::ANSWERED, result.gateway);
    TEST_ASSERT_EQUAL(LinkReach::ANSWERED, result.publicAddress);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)radio.echoedTo.size());
    TEST_ASSERT_EQUAL_UINT32(3000, radio.waits[0]);
    TEST_ASSERT_EQUAL_UINT32(1000, radio.waits[1]);
}

/** Silent echoes: three to the first address, three to the second, and the public address is silent. */
void test_the_probe_tries_three_echoes_per_address_before_it_calls_the_internet_silent(void)
{
    ScriptedProbe radio;
    const LinkProbeResult result = runLinkProbe(radio, planOfTwo(true));
    TEST_ASSERT_EQUAL(LinkReach::SILENT, result.publicAddress);
    TEST_ASSERT_EQUAL_UINT32(6, (uint32_t)radio.echoedTo.size());
    TEST_ASSERT_EQUAL_HEX32(0x08080808, radio.echoedTo[2]);
    TEST_ASSERT_EQUAL_HEX32(0x01010101, radio.echoedTo[3]);

    // The second address answering is enough.
    ScriptedProbe second;
    second.echoes.push_back(std::make_pair(0x01010101u, LinkReach::ANSWERED));
    TEST_ASSERT_EQUAL(LinkReach::ANSWERED, runLinkProbe(second, planOfTwo(true)).publicAddress);
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)second.echoedTo.size());
}

/** After a silent gateway the public address is asked only if the plan says so. */
void test_after_a_silent_gateway_the_public_address_is_asked_only_by_plan(void)
{
    ScriptedProbe radio;
    radio.gateway = LinkReach::SILENT;
    LinkProbeResult result = runLinkProbe(radio, planOfTwo(false));
    TEST_ASSERT_EQUAL(LinkReach::SILENT, result.gateway);
    TEST_ASSERT_EQUAL(LinkReach::NOT_RUN, result.publicAddress);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)radio.echoedTo.size());

    result = runLinkProbe(radio, planOfTwo(true));
    TEST_ASSERT_EQUAL(LinkReach::SILENT, result.publicAddress);
    TEST_ASSERT_EQUAL_UINT32(6, (uint32_t)radio.echoedTo.size());
}

/** With no station to ask from nothing more is sent; an echo the stack refuses ends the probe. */
void test_a_probe_with_nothing_to_ask_from_sends_nothing_more(void)
{
    ScriptedProbe radio;
    radio.gateway = LinkReach::UNAVAILABLE;
    LinkProbeResult result = runLinkProbe(radio, planOfTwo(true));
    TEST_ASSERT_EQUAL(LinkReach::UNAVAILABLE, result.gateway);
    TEST_ASSERT_EQUAL(LinkReach::NOT_RUN, result.publicAddress);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)radio.echoedTo.size());

    ScriptedProbe refused;
    refused.echoes.push_back(std::make_pair(0x08080808u, LinkReach::UNAVAILABLE));
    result = runLinkProbe(refused, planOfTwo(true));
    TEST_ASSERT_EQUAL(LinkReach::UNAVAILABLE, result.publicAddress);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)refused.echoedTo.size());
}

/** The result survives the one byte it travels in, every combination. */
/**
 * The wait for an echo's reply, read by read, over a clock that moves on by a millisecond every time
 * it is read and stray packets that answer at once: every read is given at least 1 ms and no more
 * than the wait, and the wait ends silent when it is spent. Every wait from 1 to 12 ms is taken, so
 * that one round's reading of the clock falls on each millisecond of it.
 *
 * Mutation: read the clock a second time for the time left - the read due on the last millisecond is
 * then given 0, which the board's stack takes as "wait for ever".
 */
void test_an_echo_read_is_never_given_zero_and_the_wait_ends_when_spent(void)
{
    for (uint32_t waitMs = 1; waitMs <= 12; waitMs++)
    {
        uint32_t now = 1000;
        std::vector<uint32_t> given;
        const LinkReach reach = awaitEchoReply(
            waitMs, [&]() -> uint32_t { return now++; },
            [&](uint32_t leftMs) -> EchoRead {
                given.push_back(leftMs);
                return EchoRead::STRAY;
            });

        char message[96];
        snprintf(message, sizeof(message), "a wait of %u ms", (unsigned)waitMs);
        TEST_ASSERT_EQUAL_MESSAGE(LinkReach::SILENT, reach, message);
        // A wait of 1 ms is spent by the first reading of the clock alone; every longer one reads.
        TEST_ASSERT_TRUE_MESSAGE(waitMs < 2 || !given.empty(), message);
        for (size_t i = 0; i < given.size(); i++)
        {
            snprintf(message, sizeof(message), "a wait of %u ms gave read %u %u ms", (unsigned)waitMs,
                     (unsigned)(i + 1), (unsigned)given[i]);
            TEST_ASSERT_TRUE_MESSAGE(given[i] >= 1 && given[i] <= waitMs, message);
        }
    }
}

/** A read that takes past the end of the wait ends it: no further read, whatever the clock says. */
void test_an_echo_read_that_overruns_the_wait_ends_it(void)
{
    uint32_t now = 5000;
    int reads = 0;
    const LinkReach reach = awaitEchoReply(
        1000, [&]() -> uint32_t { return now; },
        [&](uint32_t leftMs) -> EchoRead {
            reads++;
            now += leftMs + 7;
            return EchoRead::STRAY;
        });
    TEST_ASSERT_EQUAL(LinkReach::SILENT, reach);
    TEST_ASSERT_EQUAL_INT(1, reads);
}

/** Strays before the reply do not end the wait; the reply does; a read that brought nothing ends it. */
void test_an_echo_wait_ends_on_the_reply_or_on_a_read_that_brought_nothing(void)
{
    uint32_t now = 0;
    int reads = 0;
    TEST_ASSERT_EQUAL(LinkReach::ANSWERED,
                      awaitEchoReply(
                          1000, [&]() -> uint32_t { return now; },
                          [&](uint32_t) -> EchoRead {
                              now += 10;
                              return ++reads < 3 ? EchoRead::STRAY : EchoRead::REPLY;
                          }));
    TEST_ASSERT_EQUAL_INT(3, reads);

    reads = 0;
    TEST_ASSERT_EQUAL(LinkReach::SILENT,
                      awaitEchoReply(
                          1000, [&]() -> uint32_t { return now; },
                          [&](uint32_t) -> EchoRead {
                              reads++;
                              return EchoRead::NOTHING;
                          }));
    TEST_ASSERT_EQUAL_INT(1, reads);
}

void test_the_probe_result_travels_in_one_byte(void)
{
    for (uint8_t gateway = 0; gateway < 4; gateway++)
    {
        for (uint8_t publicAddress = 0; publicAddress < 4; publicAddress++)
        {
            LinkProbeResult result;
            result.gateway = (LinkReach)gateway;
            result.publicAddress = (LinkReach)publicAddress;
            const LinkProbeResult back = LinkProbeResult::decode(result.encode());
            TEST_ASSERT_EQUAL_UINT8(gateway, (uint8_t)back.gateway);
            TEST_ASSERT_EQUAL_UINT8(publicAddress, (uint8_t)back.publicAddress);
        }
    }
}

// ==================================================================================================
// 3. The statuses
// ==================================================================================================

/** Which TLS endings are a lack of memory: every allocation code, alone and inside a sum. */
void test_only_allocation_failures_of_the_tls_stack_are_a_lack_of_memory(void)
{
    typedef ModuleStatusBoard B;
    TEST_ASSERT_EQUAL(B::TlsConnect::COMPLETED, B::classifyTlsConnect(1, 54));

    const int memory[] = {B::kSslAllocFailed, B::kEcpAllocFailed, B::kPkAllocFailed, B::kX509AllocFailed,
                          B::kMdAllocFailed, B::kCipherAllocFailed, B::kDhmAllocFailed, B::kPemAllocFailed,
                          B::kMpiAllocFailed, B::kAsn1AllocFailed,
                          // X509_INVALID_FORMAT with ASN1_ALLOC_FAILED, PK_KEY_INVALID_FORMAT with MPI_ALLOC_FAILED
                          -0x2180 + B::kAsn1AllocFailed, -0x3D00 + B::kMpiAllocFailed};
    for (size_t i = 0; i < sizeof(memory) / sizeof(memory[0]); i++)
    {
        char message[48];
        snprintf(message, sizeof(message), "code -0x%04X", (unsigned)-memory[i]);
        TEST_ASSERT_EQUAL_MESSAGE(B::TlsConnect::OUT_OF_MEMORY, B::classifyTlsConnect(0, memory[i]), message);
    }

    // What a device has shown, and a name the lookup did not find.
    const int other[] = {-1, -0x0050 /* NET_CONN_RESET */, -0x7280 /* SSL_CONN_EOF */,
                         -0x2700 /* X509_CERT_VERIFY_FAILED */, 0};
    for (size_t i = 0; i < sizeof(other) / sizeof(other[0]); i++)
    {
        TEST_ASSERT_EQUAL(B::TlsConnect::OTHER_FAILURE, B::classifyTlsConnect(0, other[i]));
    }
}

/** A handshake failed for memory counts for the window, until a handshake completes. */
void test_a_handshake_short_of_memory_counts_until_one_completes_or_the_window_ends(void)
{
    ModuleStatusBoard board;
    TEST_ASSERT_FALSE(board.tlsOutOfMemoryWithin(T0, 90000));
    board.noteTlsConnect(T0, ModuleStatusBoard::TlsConnect::OUT_OF_MEMORY);
    TEST_ASSERT_TRUE(board.tlsOutOfMemoryWithin(T0 + 89999, 90000));
    TEST_ASSERT_FALSE(board.tlsOutOfMemoryWithin(T0 + 90000, 90000));

    board.noteTlsConnect(T0 + 100000, ModuleStatusBoard::TlsConnect::OUT_OF_MEMORY);
    board.noteTlsConnect(T0 + 101000, ModuleStatusBoard::TlsConnect::OTHER_FAILURE);
    TEST_ASSERT_TRUE_MESSAGE(board.tlsOutOfMemoryWithin(T0 + 102000, 90000),
                             "a failure that says nothing about memory cleared the status");
    board.noteTlsConnect(T0 + 103000, ModuleStatusBoard::TlsConnect::COMPLETED);
    TEST_ASSERT_FALSE(board.tlsOutOfMemoryWithin(T0 + 104000, 90000));

    // A failure at millis 0 is a failure.
    ModuleStatusBoard atZero;
    atZero.noteTlsConnect(0, ModuleStatusBoard::TlsConnect::OUT_OF_MEMORY);
    TEST_ASSERT_TRUE(atZero.tlsOutOfMemoryWithin(1000, 90000));
}

/** Any status from the server is an answer; a negative code from the client is not. */
void test_the_server_answered_is_any_status_since_the_moment_asked_about(void)
{
    ModuleStatusBoard board;
    board.noteRequestOutcome(T0, -1);
    TEST_ASSERT_FALSE(board.serverAnsweredSince(T0 + 1000, T0 - 1000));
    TEST_ASSERT_TRUE(board.requestNotReachedSince(T0 + 1000, T0 - 1000));

    board.noteRequestOutcome(T0 + 2000, 503);
    TEST_ASSERT_TRUE(board.serverAnsweredSince(T0 + 3000, T0));
    TEST_ASSERT_TRUE(board.serverAnsweredSince(T0 + 3000, T0 + 2000));
    TEST_ASSERT_FALSE(board.serverAnsweredSince(T0 + 3000, T0 + 2001));

    board.noteNetworkTask(false);
    board.noteRadioStart(false);
    TEST_ASSERT_TRUE(board.networkTaskRefused());
    TEST_ASSERT_TRUE(board.radioNotStarted());
    board.noteNetworkTask(true);
    board.noteRadioStart(true);
    TEST_ASSERT_FALSE(board.networkTaskRefused());
    TEST_ASSERT_FALSE(board.radioNotStarted());
}

extern "C" void run_link_troubleshooting_tests(void)
{
    RUN_TEST(test_every_phase_meets_every_event);
    RUN_TEST(test_failing_services_are_given_a_minute_before_the_gateway_is_asked);
    RUN_TEST(test_a_gateway_silent_twice_thirty_seconds_apart_restarts_the_radio);
    RUN_TEST(test_a_gateway_silent_once_and_then_answering_is_not_the_router);
    RUN_TEST(test_a_public_address_that_answers_overrules_a_silent_gateway);
    RUN_TEST(test_past_the_router_the_gateway_is_asked_again_every_five_minutes);
    RUN_TEST(test_with_no_association_the_radio_is_restarted_after_one_two_five_and_ten_minutes);
    RUN_TEST(test_a_radio_restart_in_flight_is_waited_for_and_a_lost_one_given_up_on);
    RUN_TEST(test_an_association_back_with_failing_services_is_checked_before_the_next_restart);
    RUN_TEST(test_a_probe_that_could_not_ask_decides_nothing);
    RUN_TEST(test_a_fault_of_the_module_that_holds_restarts_the_module);
    RUN_TEST(test_a_fault_that_clears_before_it_held_restarts_nothing);
    RUN_TEST(test_a_lost_association_with_a_fault_reported_waits_for_the_fault_to_hold);
    RUN_TEST(test_the_outage_is_recorded_with_its_closest_cause_start_and_length);
    RUN_TEST(test_the_restart_pauses_are_one_two_five_and_ten_minutes);
    RUN_TEST(test_the_fault_hold_is_one_request_life_and_one_tick);
    RUN_TEST(test_the_probe_asks_the_gateway_then_stops_at_the_first_echo_answered);
    RUN_TEST(test_the_probe_tries_three_echoes_per_address_before_it_calls_the_internet_silent);
    RUN_TEST(test_after_a_silent_gateway_the_public_address_is_asked_only_by_plan);
    RUN_TEST(test_a_probe_with_nothing_to_ask_from_sends_nothing_more);
    RUN_TEST(test_an_echo_read_is_never_given_zero_and_the_wait_ends_when_spent);
    RUN_TEST(test_an_echo_read_that_overruns_the_wait_ends_it);
    RUN_TEST(test_an_echo_wait_ends_on_the_reply_or_on_a_read_that_brought_nothing);
    RUN_TEST(test_the_probe_result_travels_in_one_byte);
    RUN_TEST(test_only_allocation_failures_of_the_tls_stack_are_a_lack_of_memory);
    RUN_TEST(test_a_handshake_short_of_memory_counts_until_one_completes_or_the_window_ends);
    RUN_TEST(test_the_server_answered_is_any_status_since_the_moment_asked_about);
}
