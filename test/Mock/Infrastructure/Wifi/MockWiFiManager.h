#ifndef MOCK_WIFI_MANAGER_H
#define MOCK_WIFI_MANAGER_H

#include "Core/Domain/Interfaces/IWifiManager.h"
#include <string>

class MockWiFiManager : public IWifiManager {
public:
    // Mock state variables
    bool setupWifiConnectionCalled = false;
    bool addWifiNetworkCalled = false;
    
    // Parameters captured from last call
    std::string lastSsid;
    std::string lastPassword;
    
    // Return values for mock methods
    bool setupWifiConnectionSuccess = true;
    bool addWifiNetworkSuccess = true;
    
    // Reset all state variables
    void reset() {
        setupWifiConnectionCalled = false;
        addWifiNetworkCalled = false;
        lastSsid = "";
        lastPassword = "";
        setupWifiConnectionSuccess = true;
        addWifiNetworkSuccess = true;
    }

    // IWifiManager implementation
    bool setupWifiConnection() override {
        setupWifiConnectionCalled = true;
        return setupWifiConnectionSuccess;
    }
    
    bool addWifiNetwork(const char* ssid, const char* password) override {
        addWifiNetworkCalled = true;
        lastSsid = ssid ? ssid : "";
        lastPassword = password ? password : "";
        return addWifiNetworkSuccess;
    }
};

#endif // MOCK_WIFI_MANAGER_H
