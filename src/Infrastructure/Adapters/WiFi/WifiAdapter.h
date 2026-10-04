#ifndef WIFI_ADAPTER_H
#define WIFI_ADAPTER_H

#include <WiFi.h>
#include <WiFiMulti.h>
#include <atomic>
#include <vector>
#include <string>
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiRoamRadio.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/UseCases/Network/WifiLinkLogUseCase.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#ifdef GD_HEAP_FORENSICS
#include "Core/Domain/Diagnostics/RxFrameClassCounter.h"
#endif

class WifiAdapter : public IWifiAdapter, public IWifiRoamRadio {
public:
    WifiAdapter(WiFiMulti& wifiMulti, ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr);
    ~WifiAdapter() override;

    // Access Point (AP) functionality
    bool createAccessPoint(const char* ssid, const char* password, int channel = 1, bool hidden = false, int maxConnections = 4) override;
    bool configureAccessPoint(const char* localIP, const char* gateway, const char* subnet) override;
    bool stopAccessPoint() override;
    bool isAccessPointActive() override;
    
    // WiFi station functionality
    bool connect(const char* ssid, const char* password, unsigned long timeout = 20000) override;
    bool disconnect() override;
    bool reconnect() override;
    bool isConnected() override;
    
    // Multi-network functionality
    bool addNetwork(const char* ssid, const char* password) override;
    void clearNetworks() override;
    
    // Network information
    const char* getLocalIP() override;
    const char* getMACAddress() override;
    int getRSSI() override;
    int getStatus() override;  // Add this method declaration
    
    // WiFi scanning
    int scanNetworks() override;
    const char* getSSID(int networkIndex) override;
    int getSignalStrength(int networkIndex) override;
    
    // WiFi mode control
    void setStationMode() override;
    void setAccessPointMode() override;
    void setStationAndAccessPointMode() override;

    // AP diagnostics (test mode verbose logging)
    void enableApEventLogging(ILogger* logger) override;
    int getConnectedClientCount() override;

    // Station link events (connect, drop with the radio's reason code)
    void reportLinkEventsTo(WifiLinkLogUseCase* linkLog) override;

    // IWifiRoamRadio: which access point of the configured network the station stands on, what
    // else of that network is in range, and the move from one to the other.
    bool associatedNode(WifiNode& out) override;
    bool startNodeScan(uint8_t channel) override;
    int nodeScanState() override;
    size_t takeNodeScan(WifiNode* out, size_t max) override;
    bool roamTo(const uint8_t* bssid, uint8_t channel) override;
    bool signalWarningRaised() override;

    /**
     * @brief Have the radio report when the signal of the current access point falls too low.
     *
     * Called on both paths that start the station: everything it does needs a radio. The radio's
     * warning is one-shot, so the threshold is armed again on every event and on every address the
     * station is given.
     */
    void watchSignalStrength();

    /** @brief Where the radio restart records whether the station could be started at all; optional. */
    void setStatusBoard(ModuleStatusBoard* board) { statusBoard = board; }

#ifdef GD_HEAP_FORENSICS
    // Heap-forensics builds only: count every frame the station hands to the network stack, by the class of
    // its destination, into the given counter. Armed once; it follows every station session.
    void classifyReceivedFramesInto(RxFrameClassCounter* counter);
#endif

private:
    char ipBuffer[16];        // Buffer to store IP address string
    char macBuffer[18];       // Buffer to store MAC address string
    char ssidBuffer[33];      // Buffer to store SSID string (max 32 chars + null terminator)
    bool apActive;            // Flag to track if AP is active
    WiFiMulti& wifiMulti;     // Reference to WiFiMulti instance
    std::vector<std::string> knownSsids;
    ILogger* logger;
    IDebugSystem* debugSystem;
    ModuleStatusBoard* statusBoard = nullptr;
    static ILogger* apEventLogger;
    // WiFi.onEvent() takes a callback, not an object, so what the station callback works on is
    // static: the log it reports to, the name of the network the association event named (the
    // event that carries the address does not repeat it), and whether the callback is registered.
    static WifiLinkLogUseCase* stationLinkLog;
    static char associatedSsid[33];
    static bool stationEventsRegistered;
    // Set on the radio's event task when the driver reports the signal has fallen through the
    // threshold, read and cleared by whoever asks. One bit, two tasks, no other state.
    static std::atomic<bool> signalWarning;
    static bool signalWatchArmed;

    void log(const char* format, ...);
    void disableModemSleep();
    /** @brief Wait for the station to hold an address again, feeding the watchdog while it waits. */
    bool waitForAssociation(unsigned long timeoutMs);
    static esp_err_t armSignalWatch();
    static void signalLowEventHandler(void* arg, const char* base, int32_t id, void* data);
    static void signalWatchRearmCallback(WiFiEvent_t event, WiFiEventInfo_t info);
#ifdef GD_WIFI_STATIC_BUFFERS
    void logDriverBufferBudget();
#endif
    void logKnownNetworks();
    void logNearbyNetworks();
    static void apEventCallback(WiFiEvent_t event, WiFiEventInfo_t info);
    static void stationEventCallback(WiFiEvent_t event, WiFiEventInfo_t info);
#ifdef GD_HEAP_FORENSICS
    static void rxClassifierEventCallback(WiFiEvent_t event, WiFiEventInfo_t info);
    void sayTheRxClassifierIsArmed();
#endif
};

#endif // WIFI_ADAPTER_H
