#ifndef I_WIFI_ADAPTER_H
#define I_WIFI_ADAPTER_H

class IWifiAdapter {
public:
    virtual ~IWifiAdapter() = default;

    // Access Point (AP) functionality
    virtual bool createAccessPoint(const char* ssid, const char* password, int channel = 1, bool hidden = false, int maxConnections = 4) = 0;
    virtual bool configureAccessPoint(const char* localIP, const char* gateway, const char* subnet) = 0;
    virtual bool stopAccessPoint() = 0;
    virtual bool isAccessPointActive() = 0;
    
    // WiFi station functionality
    virtual bool connect(const char* ssid, const char* password, unsigned long timeout = 20000) = 0;
    virtual bool disconnect() = 0;
    virtual bool isConnected() = 0;
    
    // Multi-network functionality
    virtual bool addNetwork(const char* ssid, const char* password) = 0;
    virtual bool connectToNetworks(unsigned long timeout = 20000) = 0;
    virtual void clearNetworks() = 0;
    
    // Network information
    virtual const char* getLocalIP() = 0;
    virtual const char* getMACAddress() = 0;
    virtual int getRSSI() = 0;
    virtual int getStatus() = 0;  // New method for WiFi status
    
    // WiFi scanning
    virtual int scanNetworks() = 0;
    virtual const char* getSSID(int networkIndex) = 0;
    virtual int getSignalStrength(int networkIndex) = 0;
    
    // WiFi mode control
    virtual void setStationMode() = 0;
    virtual void setAccessPointMode() = 0;
    virtual void setStationAndAccessPointMode() = 0;
};

#endif // I_WIFI_ADAPTER_H
