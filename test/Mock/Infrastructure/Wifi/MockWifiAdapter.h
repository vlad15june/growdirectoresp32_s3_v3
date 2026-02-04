#ifndef MOCK_WIFI_ADAPTER_H
#define MOCK_WIFI_ADAPTER_H

#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include <cstring>
#include <vector>
#include <string>

class MockWifiAdapter : public IWifiAdapter {
public:
    // Mock state variables
    bool createAccessPointCalled = false;
    bool configureAccessPointCalled = false;
    bool stopAccessPointCalled = false;
    bool isAccessPointActiveCalled = false;
    bool connectCalled = false;
    bool disconnectCalled = false;
    bool isConnectedCalled = false;
    bool getLocalIPCalled = false;
    bool getMACAddressCalled = false;
    bool addNetworkCalled = false;
    bool connectToNetworksCalled = false;
    bool clearNetworksCalled = false;

    // Parameters captured from last call
    char lastSSID[64] = {0};
    char lastPassword[64] = {0};
    int lastChannel = 0;
    bool lastHidden = false;
    int lastMaxConnections = 0;
    
    char lastLocalIP[32] = {0};
    char lastGateway[32] = {0};
    char lastSubnet[32] = {0};
    
    // Return values for mock methods
    bool accessPointActive = false;
    bool connected = false;
    bool configureAccessPointSuccess = true;
    bool addNetworkSuccess = true;
    bool connectToNetworksSuccess = true;
    
    char localIP[32] = "192.168.0.1";
    char macAddress[32] = "00:11:22:33:44:55";
    int rssi = -70;
    
    // Multi-network functionality storage
    struct NetworkCredentials {
        std::string ssid;
        std::string password;
    };
    std::vector<NetworkCredentials> savedNetworks;
    
    // Reset all state variables
    void reset() {
        createAccessPointCalled = false;
        configureAccessPointCalled = false;
        stopAccessPointCalled = false;
        isAccessPointActiveCalled = false;
        connectCalled = false;
        disconnectCalled = false;
        isConnectedCalled = false;
        getLocalIPCalled = false;
        getMACAddressCalled = false;
        addNetworkCalled = false;
        connectToNetworksCalled = false;
        clearNetworksCalled = false;
        
        lastSSID[0] = '\0';
        lastPassword[0] = '\0';
        lastChannel = 0;
        lastHidden = false;
        lastMaxConnections = 0;
        
        lastLocalIP[0] = '\0';
        lastGateway[0] = '\0';
        lastSubnet[0] = '\0';
        
        accessPointActive = false;
        connected = false;
        configureAccessPointSuccess = true;
        addNetworkSuccess = true;
        connectToNetworksSuccess = true;
        
        strcpy(localIP, "192.168.0.1");
        strcpy(macAddress, "00:11:22:33:44:55");
        rssi = -70;
        
        savedNetworks.clear();
    }
    
    // Helper methods for tests
    void setAccessPointActive(bool active) {
        accessPointActive = active;
    }
    
    void setConnected(bool isConnected) {
        connected = isConnected;
    }
    
    void setLocalIP(const char* ip) {
        strncpy(localIP, ip, sizeof(localIP));
    }
    
    void setMACAddress(const char* mac) {
        strncpy(macAddress, mac, sizeof(macAddress));
    }
    
    // Interface implementation
    bool createAccessPoint(const char* ssid, const char* password, int channel = 1, bool hidden = false, int maxConnections = 4) override {
        createAccessPointCalled = true;
        strncpy(lastSSID, ssid, sizeof(lastSSID));
        strncpy(lastPassword, password, sizeof(lastPassword));
        lastChannel = channel;
        lastHidden = hidden;
        lastMaxConnections = maxConnections;
        accessPointActive = true;
        return true;
    }
    
    bool configureAccessPoint(const char* localIP, const char* gateway, const char* subnet) override {
        configureAccessPointCalled = true;
        strncpy(lastLocalIP, localIP, sizeof(lastLocalIP));
        strncpy(lastGateway, gateway, sizeof(lastGateway));
        strncpy(lastSubnet, subnet, sizeof(lastSubnet));
        strncpy(this->localIP, localIP, sizeof(this->localIP));
        return configureAccessPointSuccess;
    }
    
    bool stopAccessPoint() override {
        stopAccessPointCalled = true;
        accessPointActive = false;
        return true;
    }
    
    bool isAccessPointActive() override {
        isAccessPointActiveCalled = true;
        return accessPointActive;
    }
    
    bool connect(const char* ssid, const char* password, unsigned long timeout = 20000) override {
        connectCalled = true;
        strncpy(lastSSID, ssid, sizeof(lastSSID));
        strncpy(lastPassword, password, sizeof(lastPassword));
        connected = true;
        return true;
    }
    
    bool disconnect() override {
        disconnectCalled = true;
        connected = false;
        return true;
    }
    
    bool isConnected() override {
        isConnectedCalled = true;
        return connected;
    }
    
    const char* getLocalIP() override {
        getLocalIPCalled = true;
        return localIP;
    }
    
    const char* getMACAddress() override {
        getMACAddressCalled = true;
        return macAddress;
    }
    
    int getRSSI() override {
        return rssi;
    }
    
    int getStatus() override {
        return connected ? 3 : 0; // WL_CONNECTED = 3, WL_IDLE_STATUS = 0
    }
    
    int scanNetworks() override {
        return 0;
    }
    
    const char* getSSID(int networkIndex) override {
        return "TestSSID";
    }
    
    int getSignalStrength(int networkIndex) override {
        return -65;
    }
    
    void setStationMode() override {
        // Not implemented for mock
    }
    
    void setAccessPointMode() override {
        // Not implemented for mock
    }
    
    void setStationAndAccessPointMode() override {
        // Not implemented for mock
    }
    
    bool addNetwork(const char* ssid, const char* password) override {
        addNetworkCalled = true;
        if (addNetworkSuccess) {
            savedNetworks.push_back({ssid, password});
        }
        return addNetworkSuccess;
    }
    
    bool connectToNetworks(unsigned long timeout = 20000) override {
        connectToNetworksCalled = true;
        if (connectToNetworksSuccess && !savedNetworks.empty()) {
            connected = true;
        }
        return connectToNetworksSuccess;
    }
    
    void clearNetworks() override {
        clearNetworksCalled = true;
        savedNetworks.clear();
    }
};

#endif // MOCK_WIFI_ADAPTER_H
