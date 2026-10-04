#include "Core/Domain/UseCases/Network/LinkTroubleshooting.h"

// Out-of-line definitions: a constant bound to a reference anywhere needs one under gnu++11.
const uint32_t LinkTroubleshooting::SUSPECT_MS;
const uint32_t LinkTroubleshooting::GATEWAY_RECHECK_MS;
const uint32_t LinkTroubleshooting::UPSTREAM_RECHECK_MS;
const uint8_t LinkTroubleshooting::RESTART_PAUSE_COUNT;
const uint32_t LinkTroubleshooting::FAULT_HOLD_MS;
const uint32_t LinkTroubleshooting::LINK_JOB_CEILING_MS;

namespace {

typedef LinkTroubleshooting LT;

const uint32_t kRestartPausesMs[LT::RESTART_PAUSE_COUNT] = {
    1u * 60u * 1000u, 2u * 60u * 1000u, 5u * 60u * 1000u, 10u * 60u * 1000u};

LT::Cause closer(LT::Cause a, LT::Cause b)
{
    return (uint8_t)a >= (uint8_t)b ? a : b;
}

bool waitIsOver(const LT::State& state, uint32_t nowMs)
{
    return nowMs - state.waitFromMs >= state.waitMs;
}

void enter(LT::Step& step, LT::Phase phase, uint32_t nowMs, const char* why)
{
    step.next.phase = phase;
    step.next.enteredAtMs = nowMs;
    step.why = why;
}

void waitFor(LT::State& state, uint32_t nowMs, uint32_t waitMs)
{
    state.waitFromMs = nowMs;
    state.waitMs = waitMs;
}

/** OK is left: a new outage, nothing known about it yet. */
void startOutage(LT::State& state, uint32_t nowMs)
{
    state.outageStartedAtMs = nowMs;
    state.cause = LT::Cause::NONE;
    state.silentGateways = 0;
    state.restartStep = 0;
}

void askForARadioRestart(LT::Step& step, uint32_t nowMs)
{
    step.actions |= LT::RESTART_RADIO;
    step.next.restartInFlight = true;
    step.next.restartAskedAtMs = nowMs;
}

void moduleFault(LT::Step& step, uint32_t nowMs, const char* why)
{
    enter(step, LT::Phase::MODULE_FAULT, nowMs, why);
    step.next.faultSinceMs = nowMs;
    step.next.cause = closer(step.next.cause, LT::Cause::MODULE);
}

/** The router is out of reach: restart the radio now - unless the module itself is at fault. */
void linkDown(LT::Step& step, uint32_t nowMs, const char* why)
{
    if (step.next.faultReported)
    {
        moduleFault(step, nowMs, "a fault of the module is reported");
        return;
    }
    enter(step, LT::Phase::LINK_DOWN, nowMs, why);
    step.next.cause = closer(step.next.cause, LT::Cause::ROUTER);
    step.next.silentGateways = 0;
    askForARadioRestart(step, nowMs);
}

void suspect(LT::Step& step, uint32_t nowMs, uint32_t waitMs, const char* why)
{
    enter(step, LT::Phase::SUSPECT, nowMs, why);
    waitFor(step.next, nowMs, waitMs);
}

void probe(LT::Step& step, uint32_t nowMs, const char* why)
{
    enter(step, LT::Phase::PROBING, nowMs, why);
    step.actions |= LT::PROBE;
}

void backToOk(LT::Step& step, uint32_t nowMs)
{
    LT::State& next = step.next;
    if (next.cause != LT::Cause::NONE)
    {
        step.actions |= LT::RECORD_OUTAGE;
        step.outage.cause = next.cause;
        step.outage.startedAtMs = next.outageStartedAtMs;
        step.outage.durationMs = nowMs - next.outageStartedAtMs;
    }
    enter(step, LT::Phase::OK, nowMs, "services restored");
    next.cause = LT::Cause::NONE;
    next.silentGateways = 0;
    next.restartStep = 0;
}

/** A probe that could not run, or never came back: another gateway check, a recheck after this one. */
void inconclusive(LT::Step& step, uint32_t askedAtMs, uint32_t nowMs, const char* why)
{
    suspect(step, nowMs, 0, why);
    waitFor(step.next, askedAtMs, LT::GATEWAY_RECHECK_MS);
}

/** The radio restart is over: the next one, if the router stays out of reach, after the pause. */
void afterTheRestart(LT::State& state, uint32_t nowMs)
{
    waitFor(state, nowMs, LT::restartPauseMs(state.restartStep));
    if (state.restartStep + 1 < LT::RESTART_PAUSE_COUNT)
    {
        state.restartStep++;
    }
}

/**
 * The waits that follow a probe are counted from when it was asked, which is a supervision tick: the
 * next check then falls on a tick exactly, rather than on the first tick after the probe's own
 * seconds.
 */
void probeCameBack(LT::Step& step, const LT::Event& event)
{
    const uint32_t now = event.nowMs;
    const uint32_t askedAt = step.next.enteredAtMs;
    const LinkReach gateway = event.probe.gateway;
    const LinkReach publicAddress = event.probe.publicAddress;
    LT::State& next = step.next;

    if (gateway == LinkReach::UNAVAILABLE || gateway == LinkReach::NOT_RUN)
    {
        inconclusive(step, askedAt, now, "the gateway could not be asked");
        return;
    }

    // A public address that answers is reached through the gateway, whatever the gateway's silence
    // to ARP says.
    if (gateway == LinkReach::ANSWERED || publicAddress == LinkReach::ANSWERED)
    {
        next.silentGateways = 0;
        next.restartStep = 0;
        // The server's own answer proves the internet as well as an echo does, and is not lost to a
        // network that drops ICMP.
        const LT::Cause upstream = (publicAddress == LinkReach::ANSWERED || event.serverAnswered)
                                       ? LT::Cause::SERVICES
                                       : LT::Cause::INTERNET;
        next.cause = closer(next.cause, upstream);
        enter(step, LT::Phase::UPSTREAM_DOWN, now,
              upstream == LT::Cause::SERVICES ? "gateway answers, so does the internet"
                                              : "gateway answers, the internet does not");
        waitFor(next, askedAt, LT::UPSTREAM_RECHECK_MS);
        return;
    }

    next.silentGateways++;
    // Once the router has been found out of reach in this outage, one silent check is enough for the
    // next restart: the second check exists against a single lost frame, not against a known outage.
    if (next.cause == LT::Cause::ROUTER || next.silentGateways >= 2)
    {
        linkDown(step, now, "gateway silent");
        return;
    }
    suspect(step, now, 0, "gateway silent once");
    waitFor(next, askedAt, LT::GATEWAY_RECHECK_MS);
}

LT::Step unexpected(const LT::State& state, const char* why)
{
    LT::Step step;
    step.next = state;
    step.why = why;
    step.expected = false;
    return step;
}

} // namespace

uint32_t LinkTroubleshooting::restartPauseMs(uint8_t step)
{
    return kRestartPausesMs[step < RESTART_PAUSE_COUNT ? step : RESTART_PAUSE_COUNT - 1];
}

LinkTroubleshooting::Step LinkTroubleshooting::step(const State& state, const Event& event)
{
    const uint32_t now = event.nowMs;

    // What the module says about itself, first: an edge that repeats the known level is not one.
    switch (event.kind)
    {
    case EventKind::WIFI_LOST:
        if (!state.wifiUp) return unexpected(state, "the station was not associated");
        break;
    case EventKind::WIFI_BACK:
        if (state.wifiUp) return unexpected(state, "the station was associated");
        break;
    case EventKind::SERVICES_FAILING:
        if (!state.servicesUp) return unexpected(state, "the services were failing already");
        break;
    case EventKind::SERVICES_RESTORED:
        if (state.servicesUp) return unexpected(state, "the services were working");
        break;
    case EventKind::MODULE_FAULT:
        if (state.faultReported) return unexpected(state, "a fault was reported already");
        break;
    case EventKind::MODULE_FAULT_GONE:
        if (!state.faultReported) return unexpected(state, "no fault was reported");
        break;
    case EventKind::TICK:
    case EventKind::PROBE_DONE:
    case EventKind::RESTART_DONE:
        break;
    }

    Step step;
    step.next = state;
    State& next = step.next;

    switch (event.kind)
    {
    case EventKind::WIFI_LOST:         next.wifiUp = false; break;
    case EventKind::WIFI_BACK:         next.wifiUp = true; break;
    case EventKind::SERVICES_FAILING:  next.servicesUp = false; break;
    case EventKind::SERVICES_RESTORED: next.servicesUp = true; break;
    case EventKind::MODULE_FAULT:      next.faultReported = true; break;
    case EventKind::MODULE_FAULT_GONE: next.faultReported = false; break;
    case EventKind::RESTART_DONE:
        if (!state.restartInFlight)
        {
            step.why = "no restart was waited for";
            return step;
        }
        next.restartInFlight = false;
        break;
    case EventKind::PROBE_DONE:
        if (state.phase != Phase::PROBING)
        {
            step.why = "no probe was waited for";
            return step;
        }
        break;
    case EventKind::TICK:
        break;
    }

    switch (state.phase)
    {
    case Phase::OK:
        if (event.kind == EventKind::WIFI_LOST)
        {
            startOutage(next, now);
            linkDown(step, now, "Wi-Fi lost");
        }
        else if (event.kind == EventKind::SERVICES_FAILING)
        {
            startOutage(next, now);
            if (next.faultReported)
            {
                moduleFault(step, now, "services failing with a fault of the module reported");
            }
            else
            {
                suspect(step, now, SUSPECT_MS, "services failing");
            }
        }
        break;

    case Phase::SUSPECT:
        if (event.kind == EventKind::TICK && waitIsOver(state, now))
        {
            probe(step, now, state.silentGateways > 0 ? "second gateway check" : "services still failing");
        }
        else if (event.kind == EventKind::WIFI_LOST)
        {
            linkDown(step, now, "Wi-Fi lost");
        }
        else if (event.kind == EventKind::SERVICES_RESTORED)
        {
            backToOk(step, now);
        }
        else if (event.kind == EventKind::MODULE_FAULT)
        {
            moduleFault(step, now, "a fault of the module is reported");
        }
        break;

    case Phase::PROBING:
        if (event.kind == EventKind::PROBE_DONE)
        {
            probeCameBack(step, event);
        }
        else if (event.kind == EventKind::TICK && now - state.enteredAtMs >= LINK_JOB_CEILING_MS)
        {
            inconclusive(step, state.enteredAtMs, now, "the probe never came back");
        }
        else if (event.kind == EventKind::WIFI_LOST)
        {
            linkDown(step, now, "Wi-Fi lost");
        }
        else if (event.kind == EventKind::SERVICES_RESTORED)
        {
            backToOk(step, now);
        }
        else if (event.kind == EventKind::MODULE_FAULT)
        {
            moduleFault(step, now, "a fault of the module is reported");
        }
        break;

    case Phase::LINK_DOWN:
        if (event.kind == EventKind::RESTART_DONE)
        {
            afterTheRestart(next, now);
            step.why = "radio restart over";
        }
        else if (event.kind == EventKind::TICK && state.restartInFlight)
        {
            if (now - state.restartAskedAtMs >= LINK_JOB_CEILING_MS)
            {
                next.restartInFlight = false;
                afterTheRestart(next, now);
                step.why = "the radio restart never came back";
            }
        }
        else if (event.kind == EventKind::TICK && waitIsOver(state, now))
        {
            if (!state.wifiUp)
            {
                askForARadioRestart(step, now);
                step.why = "pause over, still no association";
            }
            else
            {
                probe(step, now, "pause over, associated: gateway check");
            }
        }
        else if (event.kind == EventKind::SERVICES_RESTORED)
        {
            backToOk(step, now);
        }
        else if (event.kind == EventKind::MODULE_FAULT)
        {
            moduleFault(step, now, "a fault of the module is reported");
        }
        break;

    case Phase::UPSTREAM_DOWN:
        if (event.kind == EventKind::TICK && waitIsOver(state, now))
        {
            probe(step, now, "recheck");
        }
        else if (event.kind == EventKind::WIFI_LOST)
        {
            linkDown(step, now, "Wi-Fi lost");
        }
        else if (event.kind == EventKind::SERVICES_RESTORED)
        {
            backToOk(step, now);
        }
        else if (event.kind == EventKind::MODULE_FAULT)
        {
            moduleFault(step, now, "a fault of the module is reported");
        }
        break;

    case Phase::MODULE_FAULT:
        if (event.kind == EventKind::TICK && now - state.faultSinceMs >= FAULT_HOLD_MS)
        {
            step.actions |= RESTART_MODULE;
            next.faultSinceMs = now;
            step.why = "the fault of the module held";
        }
        else if (event.kind == EventKind::MODULE_FAULT_GONE)
        {
            if (!next.wifiUp)
            {
                linkDown(step, now, "fault cleared, no association");
            }
            else
            {
                suspect(step, now, 0, "fault cleared");
            }
        }
        else if (event.kind == EventKind::SERVICES_RESTORED)
        {
            backToOk(step, now);
        }
        break;
    }

    return step;
}

const char* LinkTroubleshooting::phaseName(Phase phase)
{
    switch (phase)
    {
    case Phase::OK:            return "OK";
    case Phase::SUSPECT:       return "SUSPECT";
    case Phase::PROBING:       return "PROBING";
    case Phase::LINK_DOWN:     return "LINK_DOWN";
    case Phase::UPSTREAM_DOWN: return "UPSTREAM_DOWN";
    case Phase::MODULE_FAULT:  return "MODULE_FAULT";
    }
    return "?";
}

const char* LinkTroubleshooting::causeName(Cause cause)
{
    switch (cause)
    {
    case Cause::NONE:     return "none";
    case Cause::SERVICES: return "services";
    case Cause::INTERNET: return "internet";
    case Cause::ROUTER:   return "router";
    case Cause::MODULE:   return "module";
    }
    return "?";
}

const char* LinkTroubleshooting::eventName(EventKind kind)
{
    switch (kind)
    {
    case EventKind::TICK:              return "tick";
    case EventKind::WIFI_LOST:         return "Wi-Fi lost";
    case EventKind::WIFI_BACK:         return "Wi-Fi back";
    case EventKind::SERVICES_FAILING:  return "services failing";
    case EventKind::SERVICES_RESTORED: return "services restored";
    case EventKind::PROBE_DONE:        return "probe done";
    case EventKind::RESTART_DONE:      return "radio restart done";
    case EventKind::MODULE_FAULT:      return "module fault";
    case EventKind::MODULE_FAULT_GONE: return "module fault gone";
    }
    return "?";
}

const char* LinkTroubleshooting::reachName(LinkReach reach)
{
    switch (reach)
    {
    case LinkReach::NOT_RUN:     return "not-run";
    case LinkReach::ANSWERED:    return "answered";
    case LinkReach::SILENT:      return "silent";
    case LinkReach::UNAVAILABLE: return "unavailable";
    }
    return "?";
}
