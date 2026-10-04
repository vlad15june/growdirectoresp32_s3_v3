#ifndef I_WIFI_ROAM_RADIO_H
#define I_WIFI_ROAM_RADIO_H

#include <cstddef>
#include <cstdint>

#include "Core/Domain/Data/Entities/WifiNode.h"

/**
 * @brief The radio as the roaming decision needs it: where the station is, what else is in range,
 *        and the one command that moves it.
 *
 * Separate from IWifiAdapter, which is about the link existing at all. Everything here is about
 * *which* access point of the module's own network the link stands on, and all of it is observation
 * except the last call.
 */
class IWifiRoamRadio
{
public:
    /** nodeScanState(): a look is still going on. */
    static const int SCAN_RUNNING = -1;
    /** nodeScanState(): no look has been started, or the last one failed or was already taken. */
    static const int SCAN_NONE = -2;

    virtual ~IWifiRoamRadio() = default;

    /**
     * @brief The access point the station is associated with, with its current signal.
     * @return false when there is no association to report
     */
    virtual bool associatedNode(WifiNode& out) = 0;

    /**
     * @brief Start looking for the access points of the station's own network.
     *
     * The radio leaves its channel for the length of the look, so the look is as narrow as it can
     * be: one channel, this network's SSID only.
     *
     * @param channel the channel to look at, or 0 for all of them - the first look, which is what
     *                tells the module which channels its network is on
     * @return false when a look is already going on or the radio refused to start one
     */
    virtual bool startNodeScan(uint8_t channel) = 0;

    /** @brief SCAN_RUNNING, SCAN_NONE, or how many access points the finished look found. */
    virtual int nodeScanState() = 0;

    /**
     * @brief Has the radio reported, since this was last asked, that the signal is now poor?
     *
     * The radio watches the average signal of the association and says so once when it falls
     * through the threshold, which is sooner than the next look would have been. Reading it clears
     * it: the answer is about the time since the previous question.
     */
    virtual bool signalWarningRaised() = 0;

    /**
     * @brief Take what the finished look found and let the radio release its copy.
     *
     * @param out where to write them; may be null with max 0, which only releases the copy
     * @param max how many fit
     * @return how many were written
     */
    virtual size_t takeNodeScan(WifiNode* out, size_t max) = 0;

    /**
     * @brief Associate with this access point of the network the station is already on.
     *
     * Not free on this stack: the association is dropped and made again, and the address is asked
     * for again with it, so every socket that was open is rebuilt. Blocks the calling task until
     * the station is associated again or the attempt times out, which is why the caller is the
     * network task and not the control loop.
     *
     * False at once when there is no association to move - a link that went down between the
     * decision and this call is the link-down path's business, not this one's.
     *
     * @return true when the station is associated with the requested access point afterwards
     */
    virtual bool roamTo(const uint8_t* bssid, uint8_t channel) = 0;
};

#endif // I_WIFI_ROAM_RADIO_H
