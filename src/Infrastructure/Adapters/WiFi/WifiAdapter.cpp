#include "Infrastructure/Adapters/WiFi/WifiAdapter.h"
#include <cstring>

WifiAdapter::WifiAdapter(WiFiMulti &wifiMulti) : apActive(false), wifiMulti(wifiMulti)
{
    memset(ipBuffer, 0, sizeof(ipBuffer));
    memset(macBuffer, 0, sizeof(macBuffer));
    memset(ssidBuffer, 0, sizeof(ssidBuffer));
    WiFi.mode(WIFI_OFF);
}

WifiAdapter::~WifiAdapter()
{
    WiFi.mode(WIFI_OFF);
}

bool WifiAdapter::createAccessPoint(const char *ssid, const char *password, int channel, bool hidden, int maxConnections)
{
    WiFi.disconnect();

    if (WiFi.getMode() == WIFI_STA)
    {
        WiFi.mode(WIFI_AP_STA);
    }
    else
    {
        WiFi.mode(WIFI_AP);
    }

    apActive = WiFi.softAP(ssid, password, channel, hidden, maxConnections);
    return apActive;
}

bool WifiAdapter::configureAccessPoint(const char *localIP, const char *gateway, const char *subnet)
{
    IPAddress local_IP;
    IPAddress gateway_IP;
    IPAddress subnet_mask;

    if (!local_IP.fromString(localIP) || !gateway_IP.fromString(gateway) || !subnet_mask.fromString(subnet))
    {
        return false;
    }

    return WiFi.softAPConfig(local_IP, gateway_IP, subnet_mask);
}

bool WifiAdapter::stopAccessPoint()
{
    bool result = WiFi.softAPdisconnect(true);

    if (result)
    {
        apActive = false;

        if (WiFi.getMode() == WIFI_AP_STA)
        {
            WiFi.mode(WIFI_STA);
        }
        else
        {
            WiFi.mode(WIFI_OFF);
        }
    }

    return result;
}

bool WifiAdapter::isAccessPointActive()
{
    return apActive;
}

bool WifiAdapter::connect(const char *ssid, const char *password, unsigned long timeout)
{
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

    return WiFi.status() == WL_CONNECTED;
}

bool WifiAdapter::addNetwork(const char *ssid, const char *password)
{
    if (ssid == nullptr || password == nullptr)
    {
        return false;
    }

    wifiMulti.addAP(ssid, password);
    return true;
}

bool WifiAdapter::connectToNetworks(unsigned long timeout)
{
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

    return WiFi.status() == WL_CONNECTED;
}

void WifiAdapter::clearNetworks()
{
    // The WiFiMulti object is now managed externally, so we can't reset it here.
    // The caller would need to create a new WiFiMulti object and provide it to a new WifiAdapter.
}

bool WifiAdapter::disconnect()
{
    bool result = WiFi.disconnect(true);

    if (result && !apActive)
    {
        WiFi.mode(WIFI_OFF);
    }

    return result;
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

    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    {
        WiFi.softAPmacAddress(mac);
    }
    else
    {
        WiFi.macAddress(mac);
    }

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

int WifiAdapter::scanNetworks()
{
    return WiFi.scanNetworks();
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
    apActive = false;
    WiFi.mode(WIFI_STA);
}

void WifiAdapter::setAccessPointMode()
{
    WiFi.mode(WIFI_AP);
}

void WifiAdapter::setStationAndAccessPointMode()
{
    WiFi.mode(WIFI_AP_STA);
}
