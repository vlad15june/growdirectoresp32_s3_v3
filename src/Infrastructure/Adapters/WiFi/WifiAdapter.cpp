#include "Infrastructure/Adapters/WiFi/WifiAdapter.h"
#include <cstring>
#include <cstdarg>

WifiAdapter::WifiAdapter(WiFiMulti &wifiMulti, ILogger* logger, IDebugSystem* debugSystem) 
    : apActive(false), wifiMulti(wifiMulti), logger(logger), debugSystem(debugSystem)
{
    memset(ipBuffer, 0, sizeof(ipBuffer));
    memset(macBuffer, 0, sizeof(macBuffer));
    memset(ssidBuffer, 0, sizeof(ssidBuffer));
    WiFi.mode(WIFI_OFF);
    log("WiFi adapter initialized");
}

WifiAdapter::~WifiAdapter()
{
    WiFi.mode(WIFI_OFF);
}

bool WifiAdapter::createAccessPoint(const char *ssid, const char *password, int channel, bool hidden, int maxConnections)
{
    log("Creating access point: SSID=%s, Channel=%d, Hidden=%s, MaxConnections=%d", 
        ssid, channel, hidden ? "true" : "false", maxConnections);
    
    WiFi.disconnect();

    if (WiFi.getMode() == WIFI_STA)
    {
        WiFi.mode(WIFI_AP_STA);
    }
    else
    {
        WiFi.mode(WIFI_AP);
    }

    delay(250);

    apActive = WiFi.softAP(ssid, password, channel, hidden, maxConnections);
    
    if (apActive) {
        log("Access point created successfully: %s", ssid);
    } else {
        log("Failed to create access point: %s", ssid);
    }
    
    return apActive;
}

bool WifiAdapter::configureAccessPoint(const char *localIP, const char *gateway, const char *subnet)
{
    log("Configuring access point: IP=%s, Gateway=%s, Subnet=%s", localIP, gateway, subnet);
    
    IPAddress local_IP;
    IPAddress gateway_IP;
    IPAddress subnet_mask;

    if (!local_IP.fromString(localIP) || !gateway_IP.fromString(gateway) || !subnet_mask.fromString(subnet))
    {
        log("Failed to parse IP configuration");
        return false;
    }

    bool result = WiFi.softAPConfig(local_IP, gateway_IP, subnet_mask);
    
    if (result) {
        log("Access point configured successfully");
    } else {
        log("Failed to configure access point");
    }
    
    return result;
}

bool WifiAdapter::stopAccessPoint()
{
    log("Stopping access point");
    
    bool result = WiFi.softAPdisconnect(true);

    if (result)
    {
        apActive = false;
        log("Access point stopped successfully");

        if (WiFi.getMode() == WIFI_AP_STA)
        {
            WiFi.mode(WIFI_STA);
        }
        else
        {
            WiFi.mode(WIFI_OFF);
        }
    }
    else
    {
        log("Failed to stop access point");
    }

    return result;
}

bool WifiAdapter::isAccessPointActive()
{
    return apActive;
}

bool WifiAdapter::connect(const char *ssid, const char *password, unsigned long timeout)
{
    log("Connecting to WiFi network: %s (timeout: %lu ms)", ssid, timeout);
    
    if (WiFi.getMode() == WIFI_AP)
    {
        WiFi.mode(WIFI_AP_STA);
    }
    else
    {
        WiFi.mode(WIFI_STA);
    }

    WiFi.begin(ssid, password);

    unsigned long startTime = millis();    
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout)
    {
        delay(100);
    }
    log("WiFi connection finished in (%lu ms)", millis() - startTime);

    bool connected = WiFi.status() == WL_CONNECTED;
    
    if (connected) {
        log("Successfully connected to WiFi: %s, IP: %s", ssid, getLocalIP());
    } else {
        log("Failed to connect to WiFi: %s", ssid);
    }

    return connected;
}

bool WifiAdapter::addNetwork(const char *ssid, const char *password)
{
    if (ssid == nullptr || password == nullptr)
    {
        log("Failed to add network: null SSID or password");
        return false;
    }

    log("Adding WiFi network: %s", ssid);
    wifiMulti.addAP(ssid, password);

    bool alreadyKnown = false;
    for (const auto& known : knownSsids)
    {
        if (known == ssid)
        {
            alreadyKnown = true;
            break;
        }
    }
    if (!alreadyKnown)
    {
        knownSsids.push_back(ssid);
    }

    return true;
}

bool WifiAdapter::connectToNetworks(unsigned long timeout)
{
    log("Connecting to available networks (timeout: %lu ms)", timeout);
    
    if (WiFi.getMode() == WIFI_AP)
    {
        WiFi.mode(WIFI_AP_STA);
    }
    else
    {
        WiFi.mode(WIFI_STA);
    }

    unsigned long startTime = millis();
    while (wifiMulti.run() != WL_CONNECTED && millis() - startTime < timeout)
    {
        delay(100);
    }

    bool connected = WiFi.status() == WL_CONNECTED;
    
    if (connected) {
        log("Successfully connected to network: %s, IP: %s", WiFi.SSID().c_str(), getLocalIP());
    } else {
        log("Failed to connect to any available networks");
    }

    return connected;
}

void WifiAdapter::clearNetworks()
{
    log("Clearing WiFi networks");
    // The WiFiMulti object is now managed externally, so we can't reset it here.
    // The caller would need to create a new WiFiMulti object and provide it to a new WifiAdapter.
}

bool WifiAdapter::disconnect()
{
    log("Disconnecting from WiFi");
    
    bool result = WiFi.disconnect(true);

    if (result && !apActive)
    {
        WiFi.mode(WIFI_OFF);
        log("WiFi disconnected successfully");
    }
    else if (result)
    {
        log("WiFi disconnected (AP still active)");
    }
    else
    {
        log("Failed to disconnect WiFi");
    }

    return result;
}

bool WifiAdapter::reconnect()
{
    log("WIFI: Reconnect requested, restarting radio");

    if (apActive)
    {
        WiFi.disconnect(false);
        WiFi.mode(WIFI_AP_STA);
    }
    else
    {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.mode(WIFI_STA);
    }
    delay(100);

    const unsigned long timeout = 60000;
    unsigned long startTime = millis();
    while (wifiMulti.run() != WL_CONNECTED && millis() - startTime < timeout)
    {
        delay(100);
    }

    bool connected = WiFi.status() == WL_CONNECTED;

    if (connected)
    {
        log("WIFI: Reconnect succeeded: %s, IP: %s, rssi=%d", WiFi.SSID().c_str(), getLocalIP(), WiFi.RSSI());
    }
    else
    {
        log("WIFI: Reconnect failed to connect to any network");
        logKnownNetworks();
        logNearbyNetworks();
    }

    return connected;
}

void WifiAdapter::logKnownNetworks()
{
    if (knownSsids.empty())
    {
        log("WIFI: No configured networks to connect to");
        return;
    }
    for (size_t i = 0; i < knownSsids.size(); i++)
    {
        log("WIFI: Configured target SSID: %s", knownSsids[i].c_str());
    }
}

void WifiAdapter::logNearbyNetworks()
{
    int found = WiFi.scanNetworks();
    if (found <= 0)
    {
        log("WIFI: Scan found no networks in range");
        WiFi.scanDelete();
        return;
    }

    log("WIFI: Scan found %d networks in range:", found);
    for (int i = 0; i < found; i++)
    {
        log("WIFI:   [%d] %s rssi=%d ch=%d", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
    }
    WiFi.scanDelete();
}

bool WifiAdapter::isConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

const char *WifiAdapter::getLocalIP()
{
    IPAddress ip;

    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    {
        ip = WiFi.softAPIP();
    }
    else if (WiFi.status() == WL_CONNECTED)
    {
        ip = WiFi.localIP();
    }
    else
    {
        strcpy(ipBuffer, "0.0.0.0");
        return ipBuffer;
    }

    snprintf(ipBuffer, sizeof(ipBuffer), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return ipBuffer;
}

const char *WifiAdapter::getMACAddress()
{
    uint8_t mac[6];

    WiFi.macAddress(mac);    

    snprintf(macBuffer, sizeof(macBuffer), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return macBuffer;
}

int WifiAdapter::getRSSI()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return 0;
    }
    return WiFi.RSSI();
}

int WifiAdapter::getStatus()
{
    return WiFi.status();
}

int WifiAdapter::scanNetworks()
{
    log("Scanning for WiFi networks");
    int networkCount = WiFi.scanNetworks();
    log("Found %d WiFi networks", networkCount);
    return networkCount;
}

const char *WifiAdapter::getSSID(int networkIndex)
{
    strncpy(ssidBuffer, WiFi.SSID().c_str(), sizeof(ssidBuffer) - 1);
    ssidBuffer[sizeof(ssidBuffer) - 1] = '\0'; // Ensure null termination
    return ssidBuffer;
}

int WifiAdapter::getSignalStrength(int networkIndex)
{
    if (networkIndex < 0 || networkIndex >= WiFi.scanComplete())
    {
        return 0;
    }
    return WiFi.RSSI(networkIndex);
}

void WifiAdapter::setStationMode()
{
    log("Setting WiFi mode to Station");
    apActive = false;
    WiFi.mode(WIFI_STA);
}

void WifiAdapter::setAccessPointMode()
{
    log("Setting WiFi mode to Access Point");
    WiFi.mode(WIFI_AP);
}

void WifiAdapter::setStationAndAccessPointMode()
{
    log("Setting WiFi mode to Station + Access Point");
    WiFi.mode(WIFI_AP_STA);
}

void WifiAdapter::log(const char* format, ...)
{
    if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::WIFI_DEBUG))
    {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        logger->println(DebugFeatures::WIFI_DEBUG, buffer);
    }
}

ILogger* WifiAdapter::apEventLogger = nullptr;

void WifiAdapter::apEventCallback(WiFiEvent_t event, WiFiEventInfo_t info)
{
    if (apEventLogger == nullptr) return;
    char buffer[128];
    switch (event)
    {
        case ARDUINO_EVENT_WIFI_AP_START:
            apEventLogger->println(DebugFeatures::WIFI_DEBUG, "AP_EVENT: AP started");
            break;
        case ARDUINO_EVENT_WIFI_AP_STOP:
            apEventLogger->println(DebugFeatures::WIFI_DEBUG, "AP_EVENT: AP stopped");
            break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            snprintf(buffer, sizeof(buffer),
                     "AP_EVENT: STA connected mac=%02X:%02X:%02X:%02X:%02X:%02X aid=%d",
                     info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                     info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                     info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5],
                     info.wifi_ap_staconnected.aid);
            apEventLogger->println(DebugFeatures::WIFI_DEBUG, buffer);
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            snprintf(buffer, sizeof(buffer),
                     "AP_EVENT: STA disconnected mac=%02X:%02X:%02X:%02X:%02X:%02X aid=%d",
                     info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                     info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                     info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5],
                     info.wifi_ap_stadisconnected.aid);
            apEventLogger->println(DebugFeatures::WIFI_DEBUG, buffer);
            break;
        case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
            snprintf(buffer, sizeof(buffer), "AP_EVENT: probe request rssi=%d",
                     info.wifi_ap_probereqrecved.rssi);
            apEventLogger->println(DebugFeatures::WIFI_DEBUG, buffer);
            break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            apEventLogger->println(DebugFeatures::WIFI_DEBUG, "AP_EVENT: STA IP assigned");
            break;
        default:
            break;
    }
}

void WifiAdapter::enableApEventLogging(ILogger* loggerInstance)
{
    apEventLogger = loggerInstance;
    WiFi.onEvent(WifiAdapter::apEventCallback);
    log("AP event logging enabled");
}

int WifiAdapter::getConnectedClientCount()
{
    return static_cast<int>(WiFi.softAPgetStationNum());
}
