#pragma once

/**
 * @brief What the module listens to on its own network: the UDP port and multicast group the
 *        modules around it talk on.
 *
 * Started once a boot, by the boot sequence, when the link first has an address - setup does
 * nothing on the network, and a group joined before the station interface is up is a join nobody
 * has shown to hold.
 */
class INetworkListeners
{
public:
    virtual ~INetworkListeners() = default;

    /** @brief Open the listeners and route what arrives on them. Control loop only, once. */
    virtual void startListening() = 0;
};
