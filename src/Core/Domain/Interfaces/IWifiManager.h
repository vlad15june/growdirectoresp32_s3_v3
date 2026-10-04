#ifndef IWIFI_MANAGER_H
#define IWIFI_MANAGER_H

class IWifiManager {
public:
    virtual ~IWifiManager() = default;
    
    // Hand the radio the networks it may associate with: the stored list, or the credentials in
    // the settings when there is none. Associates with nothing - the station is associated by the
    // link's supervision, on the network task. Returns how many networks were handed over.
    virtual int registerNetworks() = 0;
    
    // Add a WiFi network to the list of available networks
    virtual bool addWifiNetwork(const char* ssid, const char* password) = 0;
};

#endif // IWIFI_MANAGER_H
