#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "Core/Domain/Interfaces/IWifiManager.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Data/Entities/WifiNetworkEntry.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstdarg>

class WiFiManager : public IWifiManager {
private:
    IWifiAdapter* _wifiAdapter;
    ISettings* _settings;
    ILogger* logger;
    IDebugSystem* debugSystem;
    ISpiffsAdapter* _spiffsAdapter;
    IJsonConverter* _jsonConverter;

    static constexpr const char* WIFI_NETWORKS_FILE = "/wifiNetworks.json";

    void log(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* feature = debugSystem->getFeature(DebugFeatures::SETUP_DEBUG);
            if (feature)
            {
                logger->println(DebugFeatures::SETUP_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::SETUP_DEBUG, buffer);
            }
        }
    }

    int loadNetworksFromFile()
    {
        if (!_spiffsAdapter || !_jsonConverter)
            return 0;
        std::string content = _spiffsAdapter->readSPIFFSstr(WIFI_NETWORKS_FILE);
        if (content.empty())
            return 0;
        std::vector<WifiNetworkEntry> networks;
        _jsonConverter->wifiNetworksFromJson(content, networks);
        log("SETUP: Networks in file: %d", static_cast<int>(networks.size()));
        for (int i = 0; i < static_cast<int>(networks.size()); i++)
        {
            log("SETUP:   [%d] SSID: %s", i, networks[i].ssid.c_str());
            _wifiAdapter->addNetwork(networks[i].ssid.c_str(), networks[i].password.c_str());
        }
        return static_cast<int>(networks.size());
    }

    bool saveNetworkToFile(const char* ssid, const char* password)
    {
        if (!_spiffsAdapter || !_jsonConverter)
            return false;
        std::string content = _spiffsAdapter->readSPIFFSstr(WIFI_NETWORKS_FILE);
        std::vector<WifiNetworkEntry> networks;
        if (!content.empty())
            _jsonConverter->wifiNetworksFromJson(content, networks);
        for (const auto& entry : networks)
        {
            if (entry.ssid == ssid)
                return true; // already stored, deduplicate
        }
        networks.push_back({ssid, password});
        std::string json = _jsonConverter->wifiNetworksToJson(networks);
        _spiffsAdapter->RewriteToFile(WIFI_NETWORKS_FILE, json.c_str());
        return true;
    }

public:
    WiFiManager(IWifiAdapter* wifiAdapter, ISettings* settings, ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr, ISpiffsAdapter* spiffsAdapter = nullptr, IJsonConverter* jsonConverter = nullptr);
    virtual ~WiFiManager() = default;

    bool setupWifiConnection() override;
    bool addWifiNetwork(const char* ssid, const char* password) override;
};

WiFiManager::WiFiManager(IWifiAdapter* wifiAdapter, ISettings* settings, ILogger* logger, IDebugSystem* debugSystem, ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter)
    : _wifiAdapter(wifiAdapter), _settings(settings), logger(logger), debugSystem(debugSystem), _spiffsAdapter(spiffsAdapter), _jsonConverter(jsonConverter) {
}

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

    int loaded = loadNetworksFromFile();
    if (loaded == 0)
    {
        char ssid[BUFSIZE] = {0};
        char password[BUFSIZE] = {0};
        _settings->getSsid(ssid);
        _settings->getPassword(password);
        _wifiAdapter->addNetwork(ssid, password);
        log("SETUP: No networks file found, using EEPROM credentials");
    }
    else
    {
        log("SETUP: Loaded %d network(s) from file", loaded);
    }

    if (_wifiAdapter->connectToNetworks())
    {
        const char* activeSsid = _wifiAdapter->getSSID(0);
        log("SETUP: Connected to WiFi network: %s", activeSsid);
        return true;
    }
    else
    {
        log("SETUP: Failed to connect to WiFi network");
        return false;
    }
}

bool WiFiManager::addWifiNetwork(const char* ssid, const char* password) {
    // Before overwriting settings, ensure the existing EEPROM credentials are persisted to file
    if (_settings)
    {
        char oldSsid[BUFSIZE] = {0};
        char oldPassword[BUFSIZE] = {0};
        _settings->getSsid(oldSsid);
        _settings->getPassword(oldPassword);
        if (oldSsid[0] != '\0')
        {
            saveNetworkToFile(oldSsid, oldPassword);
            log("SETUP: Preserved existing EEPROM network to file: %s", oldSsid);
        }
    }
    _settings->setSsid(ssid);
    _settings->setPassword(password);
    saveNetworkToFile(ssid, password);
    bool result = _wifiAdapter->addNetwork(ssid, password);
    if (result)
    {
        log("SETUP: Added WiFi network: %s", ssid);
    }
    else
    {
        log("SETUP: Failed to add WiFi network: %s", ssid);
    }
    return result;
}

#endif // WIFI_MANAGER_H