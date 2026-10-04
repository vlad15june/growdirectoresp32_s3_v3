#ifndef I_LINK_PROBE_H
#define I_LINK_PROBE_H

#include <cstdint>

/**
 * @brief What one look at a piece of the path to the server found.
 *
 * Four answers and not two, because "it did not answer" and "nothing could be asked" lead to
 * opposite decisions: a router that stays silent is a reason to restart the radio, a probe that
 * could not run - no address on the station, no socket - is a reason to decide nothing.
 */
enum class LinkReach : uint8_t
{
    /** Not looked at: the plan did not ask for it. */
    NOT_RUN = 0,
    /** It answered within the wait. */
    ANSWERED = 1,
    /** It was asked and stayed silent for the whole wait. */
    SILENT = 2,
    /** It could not be asked: the station has no address or no gateway, or the stack refused. */
    UNAVAILABLE = 3
};

/**
 * @brief The two looks the module can take at its own link, as the radio and the network stack
 *        offer them. Hardware, not judgement.
 *
 * The board answers through lwIP: an ARP request to the gateway, an ICMP echo to an address. What is
 * asked, in which order and how many times, and what the answers mean for the link, is decided above
 * this interface - so a host stand-in replaces the radio and nothing else.
 *
 * Both calls block their caller for at most the wait they are given, and both run on the network
 * task.
 */
class ILinkProbe
{
public:
    virtual ~ILinkProbe() = default;

    /**
     * @brief Ask the station's gateway for its hardware address afresh and wait for its answer.
     *
     * Afresh: what the stack remembers from an earlier answer does not count. ANSWERED means the
     * gateway answered after this call began.
     */
    virtual LinkReach askGateway(uint32_t waitMs) = 0;

    /**
     * @brief Send one ICMP echo request to this address and wait for its reply.
     *
     * @param address IPv4 address, first octet in the most significant byte (8.8.8.8 is 0x08080808)
     */
    virtual LinkReach echo(uint32_t address, uint32_t waitMs) = 0;
};

#endif // I_LINK_PROBE_H
