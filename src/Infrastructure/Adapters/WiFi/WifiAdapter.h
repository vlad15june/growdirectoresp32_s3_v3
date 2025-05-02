#ifndef WIFI_ADAPTER_H
#define WIFI_ADAPTER_H

#include <WiFi.h>
#include <WiFiMulti.h>
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"

class WifiAdapter : public IWifiAdapter {
public:
    WifiAdapter(WiFiMulti& wifiMulti);
    ~WifiAdapter() override;

    // Access Point (AP) functionality
    bool createAccessPoint(const char* ssid, const char* password, int channel = 1, bool hidden = false, int maxConnections = 4) override;
    bool configureAccessPoint(const char* localIP, const char* gateway, const char* subnet) override;
    bool stopAccessPoint() override;
    bool isAccessPointActive() override;
    
    // WiFi station functionality
    bool connect(const char* ssid, const char* password, unsigned long timeout = 20000) override;
    bool disconnect() override;
    bool isConnected() override;
    
    // Multi-network functionality
    bool addNetwork(const char* ssid, const char* password);
    bool connectToNetworks(unsigned long timeout = 20000);
    void clearNetworks();
    
    // Network information
    const char* getLocalIP() override;
    const char* getMACAddress() override;
    int getRSSI() override;
    
    // WiFi scanning
    int scanNetworks() override;
    const char* getSSID(int networkIndex) override;
    int getSignalStrength(int networkIndex) override;
    
    // WiFi mode control
    void setStationMode() override;
    void setAccessPointMode() override;
    void setStationAndAccessPointMode() override;

private:
    char ipBuffer[16];        // Buffer to store IP address string
    char macBuffer[18];       // Buffer to store MAC address string
    char ssidBuffer[33];      // Buffer to store SSID string (max 32 chars + null terminator)
    bool apActive;            // Flag to track if AP is active
    WiFiMulti& wifiMulti;     // Reference to WiFiMulti instance
};

#endif // WIFI_ADAPTER_H
