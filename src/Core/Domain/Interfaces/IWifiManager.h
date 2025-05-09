#ifndef IWIFI_MANAGER_H
#define IWIFI_MANAGER_H

class IWifiManager {
public:
    virtual ~IWifiManager() = default;
    
    // Setup WiFi connection using configured settings
    virtual bool setupWifiConnection() = 0;
    
    // Add a WiFi network to the list of available networks
    virtual bool addWifiNetwork(const char* ssid, const char* password) = 0;
};

#endif // IWIFI_MANAGER_H
