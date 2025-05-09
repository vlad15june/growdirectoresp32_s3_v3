#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "Core/Domain/Interfaces/IWifiManager.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class WiFiManager : public IWifiManager {
private:
    IWifiAdapter* _wifiAdapter;
    ISettings* _settings;
    ILogger* logger;
    IDebugSystem* debugSystem;

    void log(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(buffer);
        }
    }

public:
    WiFiManager(IWifiAdapter* wifiAdapter, ISettings* settings, ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr);
    virtual ~WiFiManager() = default;
    
    // IWifiManager implementation
    bool setupWifiConnection() override;
    bool addWifiNetwork(const char* ssid, const char* password) override;
};

// Constructor implementation
WiFiManager::WiFiManager(IWifiAdapter* wifiAdapter, ISettings* settings, ILogger* logger, IDebugSystem* debugSystem) 
    : _wifiAdapter(wifiAdapter), _settings(settings), logger(logger), debugSystem(debugSystem) {
}

// Setup WiFi connection using settings
bool WiFiManager::setupWifiConnection() {
    if (!_wifiAdapter)
    {
        log("SETUP: Error - WiFi adapter is missing");
        return false;
    }

    if (!_settings)
    {
        log("SETUP: Error - Settings not available for WiFi connection");
        return false;
    }

    char ssid[BUFSIZE] = {0};
    char password[BUFSIZE] = {0};
    
    _settings->getSsid(ssid);
    _settings->getPassword(password);
    
    _wifiAdapter->addNetwork(ssid, password);
    if (_wifiAdapter->connectToNetworks())
    {
        const char *activeSsid = _wifiAdapter->getSSID(0);
        log("SETUP: Connected to WiFi network: %s", activeSsid);
        return true;        
    }
    else
    {
        log("SETUP: Failed to connect to WiFi network");
        return false;
    }
}

// Add a new WiFi network
bool WiFiManager::addWifiNetwork(const char* ssid, const char* password) {
    // First store the new WiFi credentials in settings
    _settings->setSsid(ssid);
    _settings->setPassword(password);
    
    // Add the network to the WiFi adapter
    bool result = _wifiAdapter->addNetwork(ssid, password);
    if (result) {
        log("SETUP: Added WiFi network: %s", ssid);
    } else {
        log("SETUP: Failed to add WiFi network: %s", ssid);
    }
    return result;
}

#endif // WIFI_MANAGER_H
