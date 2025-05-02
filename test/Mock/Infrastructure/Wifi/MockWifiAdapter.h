#ifndef MOCK_WIFI_ADAPTER_H
#define MOCK_WIFI_ADAPTER_H

#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"

class MockWifiAdapter : public IWifiAdapter {
public:
    bool createAPCalled = false;
    bool stopAPCalled = false;
    bool isAPActiveCalled = false;
    bool connectCalled = false;
    bool disconnectCalled = false;
    bool isConnectedCalled = false;
    bool scanNetworksCalled = false;
    bool setStationModeCalled = false;
    bool setAPModeCalled = false;
    bool setStationAndAPModeCalled = false;
    
    const char* lastSSID = nullptr;
    const char* lastPassword = nullptr;
    int lastChannel = 0;
    bool lastHidden = false;
    int lastMaxConnections = 0;
    bool apActive = false;
    bool wifiConnected = false;
    
    void reset() {
        createAPCalled = false;
        stopAPCalled = false;
        isAPActiveCalled = false;
        connectCalled = false;
        disconnectCalled = false;
        isConnectedCalled = false;
        scanNetworksCalled = false;
        setStationModeCalled = false;
        setAPModeCalled = false;
        setStationAndAPModeCalled = false;
        
        lastSSID = nullptr;
        lastPassword = nullptr;
        lastChannel = 0;
        lastHidden = false;
        lastMaxConnections = 0;
        apActive = false;
        wifiConnected = false;
    }
    
    bool createAccessPoint(const char* ssid, const char* password, int channel = 1, bool hidden = false, int maxConnections = 4) override {
        createAPCalled = true;
        lastSSID = ssid;
        lastPassword = password;
        lastChannel = channel;
        lastHidden = hidden;
        lastMaxConnections = maxConnections;
        apActive = true;
        return true;
    }
    
    bool stopAccessPoint() override {
        stopAPCalled = true;
        apActive = false;
        return true;
    }
    
    bool isAccessPointActive() override {
        isAPActiveCalled = true;
        return apActive;
    }
    
    bool connect(const char* ssid, const char* password, unsigned long timeout = 20000) override {
        connectCalled = true;
        lastSSID = ssid;
        lastPassword = password;
        wifiConnected = true;
        return true;
    }
    
    bool disconnect() override {
        disconnectCalled = true;
        wifiConnected = false;
        return true;
    }
    
    bool isConnected() override {
        isConnectedCalled = true;
        return wifiConnected;
    }
    
    const char* getLocalIP() override { return "192.168.4.1"; }
    const char* getMACAddress() override { return "AA:BB:CC:DD:EE:FF"; }
    int getRSSI() override { return -50; }
    
    int scanNetworks() override {
        scanNetworksCalled = true;
        return 5; // Mock 5 networks found
    }
    
    const char* getSSID(int networkIndex) override { return "TestNetwork"; }
    int getSignalStrength(int networkIndex) override { return -60; }
    
    void setStationMode() override { setStationModeCalled = true; }
    void setAccessPointMode() override { setAPModeCalled = true; }
    void setStationAndAccessPointMode() override { setStationAndAPModeCalled = true; }
};

#endif // MOCK_WIFI_ADAPTER_H
