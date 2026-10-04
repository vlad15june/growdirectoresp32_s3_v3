#pragma once

#include <cstdint>
#include <cstring>

/**
 * @brief One access point of a network, as a scan saw it or as the station's association reports it.
 *
 * Several access points share one SSID in a mesh, so the SSID does not identify the one the module
 * is talking to - the BSSID, the radio address of that access point, is what does. The channel
 * travels with it because an association aimed at a particular access point has to name the channel
 * it is on, and because a scan that already knows the channels can look only at those.
 */
struct WifiNode
{
    uint8_t bssid[6];
    uint8_t channel;
    int rssi;
};

/** @brief Whether two radio addresses are the same one. Neither may be null. */
inline bool sameBssid(const uint8_t* left, const uint8_t* right)
{
    return memcmp(left, right, 6) == 0;
}
