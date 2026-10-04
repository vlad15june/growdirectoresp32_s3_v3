#include "Infrastructure/Adapters/WiFi/LwipLinkProbe.h"

#include <Arduino.h>
#include <cstring>

#include "Core/Domain/UseCases/Network/LinkProbeRun.h"

#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/etharp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
// tcpip_api_call(): the stack's own way to run a function on its task and wait for it, without
// asking the heap for anything (the message is on the caller's stack, the semaphore is the task's).
#include "lwip/priv/tcpip_priv.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/ip4.h"
#include "lwip/sockets.h"

namespace {

/** How often the ARP table is looked at while the gateway's answer is awaited. */
const uint32_t kGatewayPollMs = 100;

/** The echo identifier of this module's probes, so that other ICMP traffic is told apart. */
const uint16_t kEchoIdentifier = 0x4744;

/** One call onto the network stack's task. The call data must come first: lwIP hands back that. */
struct GatewayCall
{
    struct tcpip_api_call_data call;
    esp_netif_t* station;
    bool forget;
    bool usable;
    bool answered;
};

/**
 * Runs on the network stack's task. With `forget`: drop what the station's ARP table holds and ask
 * the gateway - the entry is made pending, and lwIP asks again every second while it stays so.
 * Without: look whether the gateway's entry is resolved.
 */
err_t gatewayOnTheStackTask(struct tcpip_api_call_data* data)
{
    GatewayCall* call = reinterpret_cast<GatewayCall*>(data);
    struct netif* netif = static_cast<struct netif*>(esp_netif_get_netif_impl(call->station));

    call->usable = netif != nullptr && netif_is_up(netif) && netif_is_link_up(netif) &&
                   !ip4_addr_isany_val(*netif_ip4_addr(netif)) && !ip4_addr_isany_val(*netif_ip4_gw(netif));
    if (!call->usable)
    {
        return ERR_OK;
    }

    if (call->forget)
    {
        etharp_cleanup_netif(netif);
        etharp_query(netif, netif_ip4_gw(netif), nullptr);
        call->answered = false;
        return ERR_OK;
    }

    struct eth_addr* hardwareAddress = nullptr;
    const ip4_addr_t* address = nullptr;
    call->answered = etharp_find_addr(netif, netif_ip4_gw(netif), &hardwareAddress, &address) >= 0;
    return ERR_OK;
}

bool callOnTheStackTask(GatewayCall& call)
{
    return tcpip_api_call(&gatewayOnTheStackTask, &call.call) == ERR_OK && call.usable;
}

} // namespace

LinkReach LwipLinkProbe::askGateway(uint32_t waitMs)
{
    GatewayCall call;
    memset(&call, 0, sizeof(call));
    call.station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (call.station == nullptr)
    {
        return LinkReach::UNAVAILABLE;
    }

    call.forget = true;
    if (!callOnTheStackTask(call))
    {
        return LinkReach::UNAVAILABLE;
    }

    call.forget = false;
    const unsigned long startedAt = millis();
    do
    {
        delay(kGatewayPollMs);
        if (!callOnTheStackTask(call))
        {
            return LinkReach::UNAVAILABLE;
        }
        if (call.answered)
        {
            return LinkReach::ANSWERED;
        }
    } while (millis() - startedAt < waitMs);

    return LinkReach::SILENT;
}

LinkReach LwipLinkProbe::echo(uint32_t address, uint32_t waitMs)
{
    const int sock = lwip_socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
    if (sock < 0)
    {
        return LinkReach::UNAVAILABLE;
    }

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = lwip_htonl(address);

    struct icmp_echo_hdr request;
    memset(&request, 0, sizeof(request));
    request.type = ICMP_ECHO;
    request.code = 0;
    request.id = lwip_htons(kEchoIdentifier);
    request.seqno = lwip_htons(++_echoSequence);
    request.chksum = inet_chksum(&request, sizeof(request));

    // No route - no address on the station - is the stack refusing to send, not a silent peer.
    if (lwip_sendto(sock, &request, sizeof(request), 0, reinterpret_cast<struct sockaddr*>(&to), sizeof(to)) !=
        (int)sizeof(request))
    {
        lwip_close(sock);
        return LinkReach::UNAVAILABLE;
    }

    // How long to wait, read by read, is awaitEchoReply()'s; here is one read with the time it is given.
    const LinkReach reach = awaitEchoReply(
        waitMs, []() -> uint32_t { return (uint32_t)millis(); },
        [&](uint32_t leftMs) -> EchoRead {
            struct timeval timeout;
            timeout.tv_sec = (long)(leftMs / 1000);
            timeout.tv_usec = (long)((leftMs % 1000) * 1000);
            // A socket without a receive timeout waits forever, so a read whose timeout was not set is not made.
            if (lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
            {
                return EchoRead::NOTHING;
            }

            uint8_t reply[64];
            struct sockaddr_in from;
            socklen_t fromLength = sizeof(from);
            const int length = lwip_recvfrom(sock, reply, sizeof(reply), 0, reinterpret_cast<struct sockaddr*>(&from),
                                             &fromLength);
            if (length < 0)
            {
                return EchoRead::NOTHING;
            }
            if (from.sin_addr.s_addr != to.sin_addr.s_addr || length < IP_HLEN)
            {
                return EchoRead::STRAY;
            }
            const struct ip_hdr* header = reinterpret_cast<const struct ip_hdr*>(reply);
            const int headerLength = IPH_HL(header) * 4;
            if (length < headerLength + (int)sizeof(struct icmp_echo_hdr))
            {
                return EchoRead::STRAY;
            }
            const struct icmp_echo_hdr* answer = reinterpret_cast<const struct icmp_echo_hdr*>(reply + headerLength);
            return (ICMPH_TYPE(answer) == ICMP_ER && answer->id == request.id && answer->seqno == request.seqno)
                       ? EchoRead::REPLY
                       : EchoRead::STRAY;
        });

    lwip_close(sock);
    return reach;
}
