#ifndef MOCK_WIFI_MANAGER_H
#define MOCK_WIFI_MANAGER_H

#include "Core/Domain/Interfaces/IWifiManager.h"
#include <functional>
#include <string>

class MockWiFiManager : public IWifiManager {
public:
    // Mock state variables
    bool registerNetworksCalled = false;
    bool addWifiNetworkCalled = false;
    
    // Parameters captured from last call
    std::string lastSsid;
    std::string lastPassword;
    
    // Return values for mock methods
    int registerNetworksResult = 1;
    bool addWifiNetworkSuccess = true;
    
    /** Run at the moment the manager is asked to hand the radio its networks, to see what the
     *  caller did first. */
    std::function<void()> onRegisterNetworks;

    // Reset all state variables
    void reset() {
        registerNetworksCalled = false;
        addWifiNetworkCalled = false;
        lastSsid = "";
        lastPassword = "";
        registerNetworksResult = 1;
        addWifiNetworkSuccess = true;
        onRegisterNetworks = nullptr;
    }

    // IWifiManager implementation
    int registerNetworks() override {
        registerNetworksCalled = true;
        if (onRegisterNetworks) {
            onRegisterNetworks();
        }
        return registerNetworksResult;
    }
    
    bool addWifiNetwork(const char* ssid, const char* password) override {
        addWifiNetworkCalled = true;
        lastSsid = ssid ? ssid : "";
        lastPassword = password ? password : "";
        return addWifiNetworkSuccess;
    }
};

#endif // MOCK_WIFI_MANAGER_H
