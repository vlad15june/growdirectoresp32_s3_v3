#include "Infrastructure/Adapters/WiFi/WifiAdapter.h"
#include "Infrastructure/Adapters/WiFi/WifiDriverBufferProfile.h"
#include "Core/Domain/UseCases/Network/WifiRoamPolicy.h"
#include "esp_event.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include <cstring>
#include <cstdarg>

WifiAdapter::WifiAdapter(WiFiMulti &wifiMulti, ILogger* logger, IDebugSystem* debugSystem) 
    : apActive(false), wifiMulti(wifiMulti), logger(logger), debugSystem(debugSystem)
{
    memset(ipBuffer, 0, sizeof(ipBuffer));
    memset(macBuffer, 0, sizeof(macBuffer));
    memset(ssidBuffer, 0, sizeof(ssidBuffer));
    prepareRadioBeforeFirstStart(WiFi, WIFI_OFF, kWifiBufferCountsComeFromTheBuild);
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
        // From WIFI_OFF the core initialises the driver again (esp_wifi_init) and starts it
        // (esp_wifi_start); either refusing leaves a radio that cannot associate until the module
        // restarts, which is what the status says.
        const bool started = WiFi.mode(WIFI_STA);
        if (!started)
        {
            log("WIFI: Reconnect could not start the station (driver init or start refused)");
        }
        if (statusBoard != nullptr)
        {
            statusBoard->noteRadioStart(started);
        }
    }
    disableModemSleep();
    watchSignalStrength();
#ifdef GD_WIFI_STATIC_BUFFERS
    logDriverBufferBudget();
#endif
    delay(100);

    const unsigned long timeout = 60000;
    unsigned long startTime = millis();
    while (wifiMulti.run() != WL_CONNECTED && millis() - startTime < timeout)
    {
        // This waits on the caller's task - the control loop - for up to a minute, on purpose:
        // the radio is being restarted to recover the network stack. The caller vouches for
        // itself through the wait; the unfeedable unit shrinks to one wifiMulti.run() attempt.
        esp_task_wdt_reset();
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

/**
 * @brief Keep the station radio awake; the core enables modem sleep by default.
 *
 * A sleeping station wakes only for DTIM beacons, and the access point holds the unicast frames
 * addressed to it in between and delivers them in one burst - dozens of frames landing on the
 * heap at once. The module is mains-powered, so the radio stays on and unicast arrives as it is
 * sent. Group frames - multicast and broadcast - the access point holds until the DTIM beacon
 * for as long as any station of the network sleeps; about those this changes nothing.
 *
 * Called once the radio is up in station mode, by reconnect(), which starts it. The core re-applies
 * the sleep type it holds at every station start, and the constructor set that before the first
 * start; the direct call is what takes effect on this task, now. Its return says only that the
 * driver was initialised, so the line reports the driver's own reading of the mode afterwards.
 *
 * The driver refuses to run without modem sleep while a Bluetooth controller is enabled - with
 * abort(), not an error code. This firmware starts no Bluetooth controller.
 */
void WifiAdapter::disableModemSleep()
{
    WiFi.setSleep(false);
    wifi_ps_type_t applied = WIFI_PS_MIN_MODEM;
    esp_err_t rc = esp_wifi_set_ps(WIFI_PS_NONE);
    if (rc == ESP_OK)
    {
        rc = esp_wifi_get_ps(&applied);
    }
    if (rc == ESP_OK)
    {
        log("WIFI: modem sleep off (ps=%d)", (int)applied);
    }
    else
    {
        log("WIFI: modem sleep off failed (rc=%d)", (int)rc);
    }

#ifdef GD_HEAP_FORENSICS
    sayTheRxClassifierIsArmed();
#endif
}

#ifdef GD_WIFI_STATIC_BUFFERS
/**
 * @brief Say what buffer counts the driver of this image was initialised with.
 *
 * The numbers are the fields of the initialisation structure, so the line reads as what the radio
 * got - and it reads that way only because the core was asked to keep them; an image that does not
 * ask has no line here, and the counts it runs on are the core's own. Which of the two transmit
 * counts is in force depends on the transmit buffer type, and both are printed: the static one is
 * pinned for the lifetime of the driver, the dynamic one is a ceiling.
 */
void WifiAdapter::logDriverBufferBudget()
{
    log("WIFI: driver buffers static_rx=%d dyn_rx=%d static_tx=%d dyn_tx=%d cache_tx=%d",
        (int)CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM, (int)CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM,
        (int)WIFI_STATIC_TX_BUFFER_NUM, (int)WIFI_DYNAMIC_TX_BUFFER_NUM,
        (int)WIFI_CACHE_TX_BUFFER_NUM);
}
#endif

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

WifiLinkLogUseCase* WifiAdapter::stationLinkLog = nullptr;
char WifiAdapter::associatedSsid[33] = {0};
bool WifiAdapter::stationEventsRegistered = false;

/**
 * @brief The three station events a field log needs, translated into what happened to the link.
 *
 * Runs on the radio's event task. It reads what the event brought with it and hands it on; the
 * decisions - what is a change, what is a repetition, what is worth a line - are the link log's.
 */
void WifiAdapter::stationEventCallback(WiFiEvent_t event, WiFiEventInfo_t info)
{
    if (stationLinkLog == nullptr)
    {
        return;
    }

    switch (event)
    {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        {
            // The only event that names the network; the address arrives one event later.
            size_t length = info.wifi_sta_connected.ssid_len;
            if (length > sizeof(associatedSsid) - 1)
            {
                length = sizeof(associatedSsid) - 1;
            }
            memcpy(associatedSsid, info.wifi_sta_connected.ssid, length);
            associatedSsid[length] = '\0';
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        {
            // The link is usable from here and not from the association: everything above it needs
            // the address. Formatted from the event rather than through IPAddress, which builds a
            // String, because this is the event task.
            const uint32_t address = info.got_ip.ip_info.ip.addr;
            char ipText[16];
            snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u",
                     (unsigned)(address & 0xFF), (unsigned)((address >> 8) & 0xFF),
                     (unsigned)((address >> 16) & 0xFF), (unsigned)((address >> 24) & 0xFF));
            stationLinkLog->linkUp(associatedSsid, WiFi.RSSI(), ipText);
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            stationLinkLog->linkDown((int)info.wifi_sta_disconnected.reason);
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------------------------------------------
// Which access point of the network the station stands on, and moving it to another one
// ------------------------------------------------------------------------------------------------

std::atomic<bool> WifiAdapter::signalWarning(false);
bool WifiAdapter::signalWatchArmed = false;

/** How long one channel of a look is allowed to take. Active scan: probe, then listen. */
static const uint32_t kScanMsPerChannel = 120;

/** How long a move waits for the station to be associated and hold an address again. */
static const unsigned long kRoamAssociationTimeoutMs = 10000;

bool WifiAdapter::associatedNode(WifiNode& out)
{
    // Not while the setup access point is up: the station shares the radio with it, and a module
    // somebody is configuring is not a module to move.
    if (apActive || WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    wifi_ap_record_t record;
    if (esp_wifi_sta_get_ap_info(&record) != ESP_OK)
    {
        return false;
    }

    memcpy(out.bssid, record.bssid, sizeof(out.bssid));
    out.channel = record.primary;
    out.rssi = record.rssi;
    return true;
}

/**
 * @brief Look for the access points of the station's own network, narrowly.
 *
 * Narrow twice over: the driver is given this network's SSID, so it reports nothing else, and one
 * channel, so the radio is off its own channel for about kScanMsPerChannel rather than for the
 * band. The name comes from the association itself rather than from WiFi.SSID(), which builds a
 * String; this runs on the control loop.
 *
 * The result is left with the core, which collects it when the driver reports the scan done; it is
 * read - and released - by takeNodeScan().
 */
bool WifiAdapter::startNodeScan(uint8_t channel)
{
    if (apActive || WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    wifi_ap_record_t record;
    if (esp_wifi_sta_get_ap_info(&record) != ESP_OK || record.ssid[0] == '\0')
    {
        return false;
    }

    const int16_t started = WiFi.scanNetworks(true, false, false, kScanMsPerChannel, channel,
                                              (const char*)record.ssid);
    return started == WIFI_SCAN_RUNNING;
}

int WifiAdapter::nodeScanState()
{
    const int16_t state = WiFi.scanComplete();
    if (state == WIFI_SCAN_RUNNING)
    {
        return IWifiRoamRadio::SCAN_RUNNING;
    }
    if (state < 0)
    {
        return IWifiRoamRadio::SCAN_NONE;
    }
    return (int)state;
}

/**
 * @brief Take what the look found, and give the core's copy of it back.
 *
 * The records are read where the core put them, without going through WiFi.SSID(i) and its String.
 * The name is checked here as well as in the scan request: the core keeps one scan result for the
 * whole image, and a radio restart - which scans for every configured network, unfiltered - can
 * leave a different list behind the same reading. Nothing of another network may ever become a
 * candidate to associate with.
 */
size_t WifiAdapter::takeNodeScan(WifiNode* out, size_t max)
{
    size_t written = 0;
    const int16_t found = WiFi.scanComplete();

    wifi_ap_record_t current;
    const bool ours = esp_wifi_sta_get_ap_info(&current) == ESP_OK;

    for (int16_t index = 0; ours && out != nullptr && written < max && index < found; index++)
    {
        const wifi_ap_record_t* record = (const wifi_ap_record_t*)WiFi.getScanInfoByIndex(index);
        if (record == nullptr ||
            strncmp((const char*)record->ssid, (const char*)current.ssid, sizeof(current.ssid)) != 0)
        {
            continue;
        }
        memcpy(out[written].bssid, record->bssid, sizeof(out[written].bssid));
        out[written].channel = record->primary;
        out[written].rssi = record->rssi;
        written++;
    }

    WiFi.scanDelete();
    return written;
}

/**
 * @brief Move the station to a named access point of the network it is already on.
 *
 * What this costs, in the order it happens: the association is dropped, the new one is made, and
 * the address is asked for again - WiFiSTAClass::begin() does the same three things whenever the
 * access point it is given differs from the current one. Every socket open at that moment is
 * rebuilt afterwards, the TLS session and the broker session among them. There is no way on this
 * stack to change access point without paying that: fast transition is an 802.11r feature the
 * driver of this SDK does not offer.
 *
 * The credentials are not passed in and not stored anywhere for this: the configuration the driver
 * already holds - the one the connect put there - is read back, its access point replaced, and
 * written again. Pinning the configuration to one BSSID is what every association this firmware
 * makes does already; WiFiMulti::run() connects with the BSSID it picked, and the core's
 * wifi_sta_config() sets the same flag.
 *
 * On failure the previous access point is put back, so that a module whose move did not take is
 * left aimed at the one it came from rather than at one it could not reach.
 */
bool WifiAdapter::roamTo(const uint8_t* bssid, uint8_t channel)
{
    if (bssid == nullptr)
    {
        return false;
    }

    wifi_ap_record_t before;
    if (esp_wifi_sta_get_ap_info(&before) != ESP_OK)
    {
        log("WIFI: roam not made, the station is not associated");
        return false;
    }

    wifi_config_t config;
    if (esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK)
    {
        log("WIFI: roam not made, the driver would not say what it is configured with");
        return false;
    }

    wifi_config_t previous = config;

    config.sta.bssid_set = true;
    memcpy(config.sta.bssid, bssid, 6);
    config.sta.channel = channel;

    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK)
    {
        log("WIFI: roam not made, the driver refused the access point");
        return false;
    }

    esp_wifi_disconnect();
    esp_wifi_connect();

    if (waitForAssociation(kRoamAssociationTimeoutMs))
    {
        wifi_ap_record_t after;
        if (esp_wifi_sta_get_ap_info(&after) == ESP_OK && memcmp(after.bssid, bssid, 6) == 0)
        {
            return true;
        }
    }

    // Back where it came from. The configuration is restored first so that the driver's own
    // reconnect, which retries whatever the configuration holds, retries an access point that was
    // answering a minute ago.
    esp_wifi_set_config(WIFI_IF_STA, &previous);
    esp_wifi_disconnect();
    esp_wifi_connect();
    waitForAssociation(kRoamAssociationTimeoutMs);
    return false;
}

/**
 * @brief Wait for the station to be associated and hold an address, or for the timeout.
 *
 * WL_CONNECTED is set when the address arrives, not at the association, which is the state the
 * caller cares about: everything above the link needs the address. Runs on the network task and
 * feeds the watchdog while it waits, as the radio restart does.
 */
bool WifiAdapter::waitForAssociation(unsigned long timeoutMs)
{
    const unsigned long startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs)
    {
        esp_task_wdt_reset();
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}

bool WifiAdapter::signalWarningRaised()
{
    return signalWarning.exchange(false);
}

/**
 * @brief Ask the driver to say when the average signal falls through the threshold.
 *
 * The threshold is the one the roaming policy calls weak, so the warning means exactly "the policy
 * would now look around". The driver reports it once and disarms itself, and a new association
 * does not carry it over, which is why this is called again from every place that can.
 */
esp_err_t WifiAdapter::armSignalWatch()
{
    return esp_wifi_set_rssi_threshold(WifiRoamPolicy::WEAK_DBM);
}

/** Runs on the radio's event task: one bit, and the warning armed again for the next fall. */
void WifiAdapter::signalLowEventHandler(void* arg, const char* base, int32_t id, void* data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    signalWarning.store(true);
    armSignalWatch();
}

void WifiAdapter::signalWatchRearmCallback(WiFiEvent_t event, WiFiEventInfo_t info)
{
    (void)event;
    (void)info;
    armSignalWatch();
}

/**
 * @brief Ask the radio to warn about a falling signal, from a station that is already up.
 *
 * Called by reconnect(), which starts the station, and not at wiring time, because everything here
 * needs a radio: the event loop this registers with is created by the core when the station first
 * starts, and the threshold is a property of the driver, which does not exist before that. Asking
 * earlier fails - with a code, quietly, into a serial port that is not open yet.
 *
 * The registration is done once and the threshold every time: the driver reports the crossing once
 * and disarms itself, and a fresh association does not carry the threshold over.
 */
void WifiAdapter::watchSignalStrength()
{
    if (!signalWatchArmed)
    {
        // The driver's own event, which the core does not translate into an Arduino one - so this
        // is registered with the event loop the core created, next to the core's own handlers.
        const esp_err_t registered = esp_event_handler_register(
            WIFI_EVENT, WIFI_EVENT_STA_BSS_RSSI_LOW, &WifiAdapter::signalLowEventHandler, nullptr);
        if (registered != ESP_OK)
        {
            log("WIFI: signal watch not armed (rc=%d)", (int)registered);
            return;
        }

        // WiFi.onEvent() appends, so registering twice would arm on every address twice.
        WiFi.onEvent(WifiAdapter::signalWatchRearmCallback, ARDUINO_EVENT_WIFI_STA_GOT_IP);
        signalWatchArmed = true;
    }

    const esp_err_t armed = armSignalWatch();
    if (armed == ESP_OK)
    {
        log("WIFI: signal watch armed at %d dBm", (int)WifiRoamPolicy::WEAK_DBM);
    }
    else
    {
        // Not fatal: the threshold is asked for again on every address the station is given, and
        // without it the module still looks around on its own interval.
        log("WIFI: signal watch threshold refused (rc=%d)", (int)armed);
    }
}

void WifiAdapter::reportLinkEventsTo(WifiLinkLogUseCase* linkLog)
{
    stationLinkLog = linkLog;

    if (stationEventsRegistered || linkLog == nullptr)
    {
        return;
    }

    // WiFi.onEvent() appends, so registering twice would report every event twice.
    WiFi.onEvent(WifiAdapter::stationEventCallback);
    stationEventsRegistered = true;
    log("WIFI: station link events armed");
}

int WifiAdapter::getConnectedClientCount()
{
    return static_cast<int>(WiFi.softAPgetStationNum());
}

#ifdef GD_HEAP_FORENSICS

#include <atomic>
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"

// The counter the station's frames are classified into, and the stack's own input function the
// classifier hands every frame on to. Static for the same reason as the event callbacks: what
// lwIP calls through the netif is a function, not an object. The input behind the classifier
// starts out as the function the network interface layer gives every netif, so that a frame
// arriving between the classifier's publication and the first read of this variable still has
// somewhere to go.
static RxFrameClassCounter* stationRxFrameClasses = nullptr;
static netif_input_fn stationInputBehindClassifier = tcpip_input;
static bool rxClassifierArmed = false;
static bool rxClassifierSaid = false;

/**
 * @brief The station netif's input, with the frame counted on its way through.
 *
 * Runs on the radio's task: the driver hands each frame to lwIP through the netif's input pointer
 * while the frame still sits in the driver's buffer - the moment the heap ledger sees it. The
 * payload is an Ethernet frame, so its first byte is the first byte of the destination address.
 */
static err_t classifyStationInput(struct pbuf* p, struct netif* inp)
{
    if (stationRxFrameClasses != nullptr && p != nullptr)
    {
        stationRxFrameClasses->count((const uint8_t*)p->payload, p->len);
    }
    return stationInputBehindClassifier(p, inp);
}

/**
 * @brief Put the classifier in front of the station netif's input, unless it is there already.
 *
 * The network interface layer adds the lwIP netif afresh at every station start, with the
 * stack's plain input function, so this is done when the station has its address: by then the
 * netif of this session exists, and nothing replaces its input until the next start. Two pointer
 * writes, no memory.
 *
 * This runs on the event task while the radio's task, on the other core, may be delivering a
 * frame through the very pointer being replaced. The function behind the classifier is written
 * first and made visible - the fence is a memory barrier on this core - before the classifier is
 * published, so the radio's task never calls through the classifier to a function it has not
 * seen yet.
 */
void WifiAdapter::rxClassifierEventCallback(WiFiEvent_t event, WiFiEventInfo_t info)
{
    (void)info;
    if (event != ARDUINO_EVENT_WIFI_STA_GOT_IP)
    {
        return;
    }

    esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    struct netif* stationNetif =
        station != nullptr ? (struct netif*)esp_netif_get_netif_impl(station) : nullptr;
    if (stationNetif == nullptr || stationNetif->input == &classifyStationInput)
    {
        return;
    }

    stationInputBehindClassifier = stationNetif->input;
    std::atomic_thread_fence(std::memory_order_release);
    stationNetif->input = &classifyStationInput;
}

void WifiAdapter::classifyReceivedFramesInto(RxFrameClassCounter* counter)
{
    stationRxFrameClasses = counter;
    if (rxClassifierArmed)
    {
        return;
    }

    // WiFi.onEvent() appends, so registering twice would arm on every address twice.
    WiFi.onEvent(WifiAdapter::rxClassifierEventCallback, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    rxClassifierArmed = true;
}

/**
 * @brief Say once that the classifier is in place, on the first start of the station.
 *
 * The arming itself happens while the dependency graph is built, which is before Serial.begin():
 * a line written there goes to a port that is not open yet and is never seen. The station start
 * is where the rest of the radio's state is reported, and by then the log carries.
 */
void WifiAdapter::sayTheRxClassifierIsArmed()
{
    if (!rxClassifierArmed || rxClassifierSaid)
    {
        return;
    }

    rxClassifierSaid = true;
    log("WIFI: rx frame classifier armed");
}

#endif // GD_HEAP_FORENSICS
