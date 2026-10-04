#pragma once

#include <string>

/**
 * @brief Data Transfer Object for system diagnostic information
 */
class DiagnosticDto {
public:
    int rssi;                       // WiFi signal strength
    std::string uid;                // Device unique identifier (MAC address)
    std::string email;              // User email
    std::string accountUid;         // Account unique identifier
    std::string timestamp;          // Current time
    unsigned long uptimeMs;         // System uptime in milliseconds
    int networkFails;               // Count of network connection failures
    int apiSendFails;               // Count of API send failures
    std::string apiStatus;          // API connection status ("connected" or "disconnected")
    std::string mqttStatus;         // MQTT connection status ("connected" or "disconnected")
    int mqttFails;                  // Count of MQTT connection failures
    uint32_t mqttOversizeDrops;     // Count of inbound MQTT messages dropped for exceeding the size limit
    float apiSendFailRatio;         // Ratio of API send failures
    std::string firmware;           // Firmware version
    uint32_t freeHeap;              // Current free heap memory
    uint32_t minFreeHeap;           // Minimum free heap memory
    uint32_t heapSize;              // Total heap size
    uint32_t maxAllocHeap;          // Maximum allocatable heap block
    uint32_t psramSize;             // Total PSRAM size
    uint32_t psramFree;             // Current free PSRAM
    unsigned long failsInHour;      // Number of failures in the last hour
    // The last outage the module came back from, carried from the first report after it on.
    // "router", "internet", "services" or "module"; empty while there has been none since the boot.
    std::string lastOutageCause;
    unsigned long lastOutageStartUptimeMs;   // When it began, on the uptime this report carries
    unsigned long lastOutageDurationMs;      // How long it lasted
    
    DiagnosticDto() : 
        rssi(0), 
        uptimeMs(0), 
        networkFails(0), 
        apiSendFails(0), 
        mqttFails(0),
        mqttOversizeDrops(0),
        apiSendFailRatio(0),
        freeHeap(0), 
        minFreeHeap(0), 
        heapSize(0),
        maxAllocHeap(0),
        psramSize(0),
        psramFree(0),
        failsInHour(0),
        lastOutageStartUptimeMs(0),
        lastOutageDurationMs(0) {}
};
