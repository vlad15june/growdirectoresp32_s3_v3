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
    //TODO: add creds from file
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
    
    //TODO: store the new WiFi credentials in a file

    
    bool result = _wifiAdapter->addNetwork(ssid, password);
    if (result) {
        log("SETUP: Added WiFi network: %s", ssid);
    } else {
        log("SETUP: Failed to add WiFi network: %s", ssid);
    }
    return result;
}

#endif // WIFI_MANAGER_H



/*


void addWifiToFile(const String &ssid, const String &password)
{
    wifiNetworksFile = SPIFFS.open(wifiNetworksFilePath, "r");
    DeserializationError error = deserializeJson(wifiNetworks, wifiNetworksFile);
    wifiNetworksFile.close();
    if (error)
    {
        if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
            mqttLogger.printf("SETUP: deserializeJson() failed: %s\n", error.c_str());

        return;
    }

    bool isNewNet = true;
    JsonArray wifiNetworksArray = wifiNetworks.as<JsonArray>();
    for (JsonObject wifiNetwork : wifiNetworksArray)
    {
        if (wifiNetwork.containsKey("ssid") && wifiNetwork.containsKey("password"))
        {
            if (wifiNetwork["ssid"].as<String>() == ssid && wifiNetwork["password"].as<String>() == password)
            {
                isNewNet = false;
                break;
            }
        }
    }

    if (!isNewNet)
    {
        if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
            mqttLogger.printf("SETUP: wifi allready exist: ssid: %s, password: %s\n", ssid.c_str(), password.c_str());
        return;
    }

    wifiMulti.addAP(ssid.c_str(), password.c_str());

    // Add new wifi network to wifiNetworks array
    JsonObject newNetwork = wifiNetworks.createNestedObject();
    newNetwork["ssid"] = ssid;
    newNetwork["password"] = password;

    // Open the file for writing
    wifiNetworksFile = SPIFFS.open(wifiNetworksFilePath, "w");
    if (!wifiNetworksFile)
    {
        if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
            mqttLogger.println("SETUP: Failed to open wifiNetworksFile for writing");
        return;
    }

    // Serialize the updated JSON data and save it to the file
    serializeJson(wifiNetworks, wifiNetworksFile);
    wifiNetworksFile.close();

    // Read back and log for debugging
    wifiNetworksFile = SPIFFS.open(wifiNetworksFilePath, "r");
    if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
    {
        mqttLogger.printf("SETUP: wifiNetworksFile: %s\n", wifiNetworksFile.readString().c_str());
    }
    wifiNetworksFile.close();
}

void addWifiNetwork(const String &ssid, const String &password)
{
    // Clear existing networks
    wifiNetworks.clear();

    // Add new network
    JsonObject newNetwork = wifiNetworks.createNestedObject();
    newNetwork["ssid"] = ssid;
    newNetwork["password"] = password;

    // Save to file
    wifiNetworksFile = SPIFFS.open(wifiNetworksFilePath, "w");
    serializeJson(wifiNetworks, wifiNetworksFile);
    wifiNetworksFile.close();

    // Read back and log for debugging
    wifiNetworksFile = SPIFFS.open(wifiNetworksFilePath, "r");
    if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
    {
        mqttLogger.printf("SETUP: wifiNetworksFile: %s\n", wifiNetworksFile.readString().c_str());
    }
    wifiNetworksFile.close();
}

void addWifiNetworks(const String &ssid, const String &password)
{
    if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
        mqttLogger.printf("SETUP: addWifiNetworks: ssid: %s, password: %s\n", ssid.c_str(), password.c_str());

    if (!SPIFFS.exists(wifiNetworksFilePath))
    {
        addWifiNetwork(ssid, password);
    }
    else
    {
        wifiNetworksFile = SPIFFS.open(wifiNetworksFilePath, "r");
        wifiNetworksStr = wifiNetworksFile.readString().c_str();
        wifiNetworksFile.close();

        if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
            mqttLogger.printf("SETUP: wifiNetworksFile: %s\n", wifiNetworksStr.c_str());

        DeserializationError error = deserializeJson(wifiNetworks, wifiNetworksStr);
        if (error)
        {
            if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
                mqttLogger.printf("SETUP: deserializeJson() failed: %s\n", error.c_str());

            addWifiNetwork(ssid, password);
        }
        else
        {
            JsonArray wifiNetworksArray = wifiNetworks.as<JsonArray>();
            for (JsonObject wifiNetwork : wifiNetworksArray)
            {
                if (wifiNetwork.containsKey("ssid") && wifiNetwork.containsKey("password"))
                {
                    if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
                        mqttLogger.printf("SETUP: wifiNetworks: ssid: %s, password: %s\n", wifiNetwork["ssid"].as<String>().c_str(), wifiNetwork["password"].as<String>().c_str());

                    if (!(wifiNetwork["ssid"].as<String>() == ssid && wifiNetwork["password"].as<String>() == password))
                    {
                        if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
                            mqttLogger.printf("SETUP: wifiNetworks was added: ssid: %s, password: %s\n", wifiNetwork["ssid"].as<String>().c_str(), wifiNetwork["password"].as<String>().c_str());

                        wifiMulti.addAP(wifiNetwork["ssid"].as<String>().c_str(), wifiNetwork["password"].as<String>().c_str());
                    }
                    else
                    {
                        if (debugLevel >= DebugLevel::TRACE && SETUP_DEBUG)
                            mqttLogger.printf("SETUP: wifiNetworks was not added: ssid: %s, password: %s\n", wifiNetwork["ssid"].as<String>().c_str(), wifiNetwork["password"].as<String>().c_str());
                    }
                }
            }
        }
    }
}






*/