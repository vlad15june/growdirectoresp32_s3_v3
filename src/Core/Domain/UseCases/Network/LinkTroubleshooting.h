#ifndef LINK_TROUBLESHOOTING_H
#define LINK_TROUBLESHOOTING_H

#include <cstdint>

#include "Core/Domain/UseCases/Network/LinkProbeRun.h"

/**
 * @brief What the module does when it cannot reach its server: which part of the path failed, and
 *        whether restarting the radio or the module can help.
 *
 * A restart is only worth what it repairs. Restarting the radio repairs the radio's side of the link
 * - the association, the driver, the address - and nothing beyond the router; restarting the module
 * repairs the module - its heap, its tasks, its drivers - and nothing outside it. So the module looks
 * before it acts:
 *
 *  - with no association, the link is down on the radio's side: the radio is restarted at once and
 *    again after 1, 2, 5 and then every 10 minutes (LINK_DOWN);
 *  - with an association but the broker or the server failing for a minute, the module asks its
 *    gateway for its address (check A). Silent twice, thirty seconds apart, and the router is
 *    unreachable: LINK_DOWN, as above. Answering, and the failure is past the router - the radio is
 *    left alone, a public address (check B) tells the internet from the services for the record, and
 *    the gateway is asked again every five minutes while the failure lasts (UPSTREAM_DOWN);
 *  - the module restarts only on a fault of the module itself that the module's parts have reported
 *    and that has held, with the link in trouble, for FAULT_HOLD_MS (MODULE_FAULT). Never on time
 *    offline alone.
 *
 * Every outage - the way from leaving OK to coming back - is recorded with what it was found to be,
 * when it started and how long it took, for the diagnostic report to carry.
 *
 * This is the whole decision and nothing else: step() is a pure function of the state and one event,
 * and answers the next state and what to do. Time arrives with the event. The supervision is what
 * turns the module's readings into events and carries out the actions.
 */
class LinkTroubleshooting
{
public:
    enum class Phase : uint8_t
    {
        /** The services work. */
        OK,
        /** The services fail with the station associated; not for a minute yet, or a silent gateway
         *  waits for its second check. */
        SUSPECT,
        /** A probe of the link is in flight. */
        PROBING,
        /**
         * The router is out of reach: no association, or a gateway that stayed silent. The radio is
         * restarted with a growing pause. "No association" is not a phase of its own: what it calls
         * for is what a silent gateway calls for, and its entry differs only in needing no probe.
         */
        LINK_DOWN,
        /** The gateway answers, the services do not: past the router. The radio is left alone. */
        UPSTREAM_DOWN,
        /** A fault of the module is reported while the link is in trouble: the module is restarted
         *  once the fault has held for FAULT_HOLD_MS. */
        MODULE_FAULT
    };

    /** What an outage was found to be. Ordered by how close to the module it is: the record keeps
     *  the closest one the outage reached. */
    enum class Cause : uint8_t
    {
        NONE = 0,
        SERVICES = 1,
        INTERNET = 2,
        ROUTER = 3,
        MODULE = 4
    };

    enum class EventKind : uint8_t
    {
        /** The supervision's tick. Every timed decision is taken on it. */
        TICK,
        WIFI_LOST,
        WIFI_BACK,
        /** The broker or the server fails. */
        SERVICES_FAILING,
        /** The broker and the server both work again. */
        SERVICES_RESTORED,
        /** The probe asked for came back. */
        PROBE_DONE,
        /** The radio restart asked for is over. Whether the station associated, the next tick tells. */
        RESTART_DONE,
        /** A fault of the module is reported. */
        MODULE_FAULT,
        /** No fault of the module is reported any more. */
        MODULE_FAULT_GONE
    };

    struct Event
    {
        EventKind kind = EventKind::TICK;
        uint32_t nowMs = 0;
        /** PROBE_DONE: what the probe found. */
        LinkProbeResult probe;
        /** PROBE_DONE: whether the server answered a request with a status since the outage began. */
        bool serverAnswered = false;
    };

    /** What step() asks to be done, as bits. */
    enum Action : uint8_t
    {
        NO_ACTION = 0,
        /** Take a probe of the link (check A, then B). */
        PROBE = 1,
        /** Restart the radio. */
        RESTART_RADIO = 2,
        /** Restart the module. */
        RESTART_MODULE = 4,
        /** An outage is over: record it. */
        RECORD_OUTAGE = 8
    };

    struct Outage
    {
        Cause cause = Cause::NONE;
        uint32_t startedAtMs = 0;
        uint32_t durationMs = 0;
    };

    struct State
    {
        Phase phase = Phase::OK;
        /** When the phase was entered. */
        uint32_t enteredAtMs = 0;
        /** When OK was left; meaningful outside OK. */
        uint32_t outageStartedAtMs = 0;
        /** The phase's wait: its next timed decision is due waitMs after waitFromMs. */
        uint32_t waitFromMs = 0;
        uint32_t waitMs = 0;
        /** When the module fault was first seen with the link in trouble. */
        uint32_t faultSinceMs = 0;
        /** When the radio restart in flight was asked for. */
        uint32_t restartAskedAtMs = 0;
        /** Which pause of RESTART_PAUSES_MS follows the next restart. */
        uint8_t restartStep = 0;
        /** Silent gateways in a row, for the second check. */
        uint8_t silentGateways = 0;
        bool restartInFlight = false;
        /** What the module last said about these three. */
        bool wifiUp = true;
        bool servicesUp = true;
        bool faultReported = false;
        /** The closest cause this outage has reached so far. */
        Cause cause = Cause::NONE;
    };

    struct Step
    {
        State next;
        /** Bits of Action. */
        uint8_t actions = NO_ACTION;
        /** RECORD_OUTAGE: the outage that ended. */
        Outage outage;
        /** Why, for the tape. Never null. */
        const char* why = "";
        /** False for an event this state cannot receive; nothing changes then. */
        bool expected = true;
    };

    // --- the times, and where each comes from (every one of them is to be confirmed on hardware) ---

    /**
     * How long the services may fail with the station associated before the module looks at its own
     * link: one minute. The broker reconnect backs off from 5 s to at most 30 s between attempts and
     * one connect act takes at most 2 s of close and 10 s for the CONNACK (MqttManager), so a broker
     * that is reachable again is back within 42 s - the minute gives it one whole cycle first.
     */
    static const uint32_t SUSPECT_MS = 60000;

    /**
     * Between the two checks of a silent gateway: thirty seconds, from the start of one to the start
     * of the next. One silent check can be one lost frame of a busy radio; the second one, a
     * supervision tick later, is what makes it the router.
     */
    static const uint32_t GATEWAY_RECHECK_MS = 30000;

    /** How often a module whose gateway answers looks again while the services stay down, from the
     *  start of one look to the start of the next. */
    static const uint32_t UPSTREAM_RECHECK_MS = 5 * 60 * 1000;

    /**
     * The pauses between radio restarts, from the end of one to the start of the next: 1, 2 and 5
     * minutes, then 10 minutes for as long as the router stays out of reach. The first restart
     * follows the loss at once; each later one gives the router more time to come back by itself,
     * which is what an access point that reboots or a router that lost power does.
     */
    static const uint8_t RESTART_PAUSE_COUNT = 4;
    static uint32_t restartPauseMs(uint8_t step);

    /**
     * How long a fault of the module must hold, with the link in trouble the whole time, before the
     * module is restarted: one whole life of a request in the outbox (4 attempts of at most 170 s
     * each and 10.5 s of backoff between them: 690 500 ms) and one supervision tick (30 000 ms). The
     * request that started as the fault appeared had every attempt it gets with the fault present,
     * and the supervision looked once more after the last one. The 170 s of an attempt counts the
     * TLS handshake at WiFiClientSecure's default 120 s; HttpClientAdapter bounds it at 20 s, so an
     * attempt takes at most 70 s now and this hold is longer than the life it waits out.
     */
    static const uint32_t FAULT_HOLD_MS = 4u * 170000u + 10500u + 30000u;

    /**
     * How long a probe or a radio restart asked for is waited for before it counts as lost: the job
     * waits at most for one whole request ahead of it on the network task (690 500 ms, counted with
     * the handshake at 120 s as FAULT_HOLD_MS is - wider than the 20 s HttpClientAdapter allows) and
     * one MQTT connect act (12 000 ms), and then runs at most for the radio restart's own bound - a
     * minute of association and the scan after it, about 25 s (85 000 ms). Only a lost completion
     * reaches it.
     */
    static const uint32_t LINK_JOB_CEILING_MS = 690500u + 12000u + 85000u;

    /** @brief The decision for one event. Pure: the state in, the next state and the actions out. */
    static Step step(const State& state, const Event& event);

    static const char* phaseName(Phase phase);
    static const char* causeName(Cause cause);
    static const char* eventName(EventKind kind);
    static const char* reachName(LinkReach reach);
};

#endif // LINK_TROUBLESHOOTING_H
