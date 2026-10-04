#ifndef LWIP_LINK_PROBE_H
#define LWIP_LINK_PROBE_H

#include <cstdint>

#include "Core/Domain/Interfaces/Adapters/ILinkProbe.h"

/**
 * @brief The two looks at the link, as lwIP on this board can take them: ARP to the station's
 *        gateway and an ICMP echo to an address.
 *
 * Mechanics only: what is asked when, and what the answers mean, is LinkProbeRun's and the link
 * supervision's. Both calls run on the network task and block it for at most the wait they are given.
 *
 * ARP has no public way to ask for a fresh answer: lwIP keeps a resolved address for minutes and
 * hands it out whatever its age (etharp_find_addr), and no call removes a single learned entry. So the
 * station's entries are dropped (etharp_cleanup_netif) before the gateway is asked (etharp_query), which
 * leaves the gateway's entry pending. What resolves it afterwards is any ARP packet sent from the
 * gateway's address after the drop - the reply to this request, a request of the gateway's own to
 * anyone, a gratuitous announcement (lwIP updates a pending entry from all of them) - and never what
 * the table held before. The cost is one ARP exchange for each neighbour on its next packet. The ARP table belongs to the network stack's own task - the stack is built without core
 * locking - so every touch of it is made there, through tcpip_api_call().
 *
 * The echo is the raw ICMP socket esp_ping would open, used directly on the calling task, so no task
 * of its own is created for it.
 */
class LwipLinkProbe : public ILinkProbe
{
public:
    LinkReach askGateway(uint32_t waitMs) override;
    LinkReach echo(uint32_t address, uint32_t waitMs) override;

private:
    uint16_t _echoSequence = 0;
};

#endif // LWIP_LINK_PROBE_H
