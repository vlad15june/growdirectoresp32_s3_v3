#ifndef LINK_PROBE_RUN_H
#define LINK_PROBE_RUN_H

#include <cstddef>
#include <cstdint>

#include "Core/Domain/Interfaces/Adapters/ILinkProbe.h"

/** @brief What one probe of the link found: the gateway (A) and a public address (B). */
struct LinkProbeResult
{
    LinkReach gateway = LinkReach::NOT_RUN;
    LinkReach publicAddress = LinkReach::NOT_RUN;

    /** @brief One byte, for the completion the network task sends back to the control loop. */
    uint8_t encode() const
    {
        return (uint8_t)(((uint8_t)gateway & 0x03u) | (((uint8_t)publicAddress & 0x03u) << 2));
    }

    static LinkProbeResult decode(uint8_t encoded)
    {
        LinkProbeResult result;
        result.gateway = (LinkReach)(encoded & 0x03u);
        result.publicAddress = (LinkReach)((encoded >> 2) & 0x03u);
        return result;
    }
};

/**
 * @brief How a probe is taken: what is asked, how long each answer is waited for, how many times.
 *
 * The plan the firmware runs is boardLinkProbePlan(), below, with the reason for every number.
 */
struct LinkProbePlan
{
    static const size_t MAX_PUBLIC_ADDRESSES = 2;

    /** How long the gateway's answer is waited for. */
    uint32_t gatewayWaitMs = 0;
    /** The public addresses tried in turn; 0 marks an unused entry. */
    uint32_t publicAddresses[MAX_PUBLIC_ADDRESSES] = {0, 0};
    /** Echo requests per address before the next one is tried. */
    uint8_t echoesPerAddress = 0;
    /** How long each echo's reply is waited for. */
    uint32_t echoWaitMs = 0;
    /**
     * Whether a public address is tried after a silent gateway as well. A public address that
     * answers then proves that the gateway forwards, whatever its silence to ARP said.
     */
    bool publicAddressAfterASilentGateway = false;
};

/**
 * @brief The probe this firmware takes, and why each number is what it is.
 *
 *  - the gateway's answer is waited for 3 s: lwIP asks again every second while the entry is pending
 *    (ARP_TMR_INTERVAL), so that is three requests - one lost frame does not make a router silent;
 *  - two public addresses of two operators (Google's and Cloudflare's anycast resolvers), so that one
 *    operator's trouble is not taken for the internet's; three echoes each, a second each;
 *  - a public address is asked after a silent gateway too, until it is shown on hardware that ARP on this
 *    board answers what the router answers: an echo that comes back through a gateway proves the
 *    gateway forwards.
 *
 * The whole probe is at most 3 + 2 x 3 x 1 = 9 s of the network task, one unfed stretch for its
 * watchdog (constants.h).
 */
inline LinkProbePlan boardLinkProbePlan()
{
    LinkProbePlan plan;
    plan.gatewayWaitMs = 3000;
    plan.publicAddresses[0] = 0x08080808; // 8.8.8.8
    plan.publicAddresses[1] = 0x01010101; // 1.1.1.1
    plan.echoesPerAddress = 3;
    plan.echoWaitMs = 1000;
    plan.publicAddressAfterASilentGateway = true;
    return plan;
}

/**
 * @brief Take one probe of the link: the gateway first, then a public address.
 *
 * The public address is asked only when there is a station to ask from (the gateway's answer was
 * not UNAVAILABLE) and, after a silent gateway, only if the plan says so. The first echo answered
 * ends the probe; an address that cannot be asked at all ends it as UNAVAILABLE - the next address
 * would be sent the same way.
 */
inline LinkProbeResult runLinkProbe(ILinkProbe& probe, const LinkProbePlan& plan)
{
    LinkProbeResult result;
    result.gateway = probe.askGateway(plan.gatewayWaitMs);

    if (result.gateway == LinkReach::UNAVAILABLE ||
        (result.gateway == LinkReach::SILENT && !plan.publicAddressAfterASilentGateway))
    {
        return result;
    }

    for (size_t i = 0; i < LinkProbePlan::MAX_PUBLIC_ADDRESSES; i++)
    {
        if (plan.publicAddresses[i] == 0)
        {
            continue;
        }
        for (uint8_t echo = 0; echo < plan.echoesPerAddress; echo++)
        {
            const LinkReach reach = probe.echo(plan.publicAddresses[i], plan.echoWaitMs);
            if (reach == LinkReach::ANSWERED || reach == LinkReach::UNAVAILABLE)
            {
                result.publicAddress = reach;
                return result;
            }
            result.publicAddress = LinkReach::SILENT;
        }
    }
    return result;
}

/** @brief What one read of an echo's socket brought back. */
enum class EchoRead : uint8_t
{
    /** This echo's reply. */
    REPLY,
    /** Some other ICMP packet the station received - a raw socket sees every one of them. */
    STRAY,
    /** Nothing within the time the read was given, or the read failed. */
    NOTHING
};

/**
 * @brief Wait up to waitMs for an echo's reply, one read at a time.
 *
 * Each round reads the clock once, ends the wait when it is spent, and gives the read what is left of
 * it - at least 1 ms, never 0: a receive timeout of 0 is no timeout at all to the board's stack (lwIP
 * takes SO_RCVTIMEO 0 as "wait for ever"), and the network task would hang on a read nothing answers.
 * A stray packet costs the time it took and the wait goes on with what is left; nothing, or a failed
 * read, ends it.
 *
 * @param clockMs  milliseconds now
 * @param readFor  one read that returns within the milliseconds it is given
 */
template <typename Clock, typename Read>
LinkReach awaitEchoReply(uint32_t waitMs, Clock clockMs, Read readFor)
{
    const uint32_t startedAt = clockMs();
    for (;;)
    {
        const uint32_t elapsed = clockMs() - startedAt;
        if (elapsed >= waitMs)
        {
            return LinkReach::SILENT;
        }
        const EchoRead got = readFor(waitMs - elapsed);
        if (got == EchoRead::REPLY)
        {
            return LinkReach::ANSWERED;
        }
        if (got == EchoRead::NOTHING)
        {
            return LinkReach::SILENT;
        }
    }
}

#endif // LINK_PROBE_RUN_H
