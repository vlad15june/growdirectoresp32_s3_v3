#ifndef NTP_TIME_ADAPTER_H
#define NTP_TIME_ADAPTER_H

#include "INTPTimeAdapter.h"
#include <WiFiUdp.h>
#include "time.h"
#include "esp_sntp.h"
#include <ESP32Time.h>
#include <cstdarg>
#include <cstdio>

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h" // Add ISettings include

// Forward declaration for the MqttLogger class
class MqttLogger;

class NTPTimeAdapter : public INTPTimeAdapter
{
private:
    // NTP server configuration
    const char *_ntpServer;
    long _utcOffsetSeconds;
    int _daylightOffsetSeconds;

    // Real time seconds counter (for command time validation)
    unsigned long _realTimeSeconds;

    ESP32Time _rtc;

    ILogger *_logger;
    IDebugSystem *_debugSystem;
    ISettings *_settings; // Add ISettings member

    // Utility buffer for logging
    char _logBuffer[256];

    // Buffer to store formatted time string
    char _timeBuffer[150];

    // Time synchronization callback
    static void timeSyncCallback(struct timeval *tv);

    // Pointer to the active instance for the callback
    static NTPTimeAdapter *_instance;

    // EEPROM operations for time storage
    void putTimeToEEPROM(struct tm timeinfo);
    struct tm getTimeFromEEPROM();

    // Calculate total seconds from hours, minutes, seconds
    unsigned long calculateTotalSeconds(int hours, int minutes, int seconds);

    // Update real time seconds counter
    void updateRealTimeSeconds();

    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr && _debugSystem->isFeatureEnabled(DebugFeatures::TIME_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::TIME_DEBUG, buffer);
        }
    }

public:
    // Constructor
    NTPTimeAdapter(ILogger *logger,
                   IDebugSystem *debugSystemInstance = nullptr,
                   ISettings *settings = nullptr, // Add settings parameter
                   const char *ntpServer = "pool.ntp.org",
                   long utcOffsetSeconds = 0,
                   int daylightOffsetSeconds = 0);

    // Destructor
    ~NTPTimeAdapter() override;

    // Interface implementations
    void setup() override;
    unsigned long getEpochTime() override;
    const char* getFormattedTime() override;
    void printTime() override;
    bool isCommandTimeValid(const char* createdDate, unsigned long maxAgeSeconds) override;
    void saveTimeToStorage() override;
    void restoreTimeFromStorage() override;
    void forceSyncTime() override;
    bool isTimeSynchronized() override;

    // Callback handler that will be called by the static callback
    void onTimeSynchronized();
};

// Initialize static member
NTPTimeAdapter *NTPTimeAdapter::_instance = nullptr;

NTPTimeAdapter::NTPTimeAdapter(ILogger *logger,
                               IDebugSystem *debugSystemInstance,
                               ISettings *settings, // Add settings parameter
                               const char *ntpServer,
                               long utcOffsetSeconds,
                               int daylightOffsetSeconds)
    : _logger(logger),
      _debugSystem(debugSystemInstance),
      _settings(settings), // Initialize settings
      _ntpServer(ntpServer),
      _utcOffsetSeconds(utcOffsetSeconds),
      _daylightOffsetSeconds(daylightOffsetSeconds),
      _realTimeSeconds(0)
{
    // Store instance pointer for callback
    _instance = this;
}

NTPTimeAdapter::~NTPTimeAdapter()
{
    // Reset static instance if this instance is being destroyed
    if (_instance == this)
    {
        _instance = nullptr;
    }
}

void NTPTimeAdapter::setup()
{
    // Try to restore time from storage first
    log("TIME: Attempting to restore time from storage during setup\n");
    restoreTimeFromStorage();
    
    // Set more aggressive time synchronization interval for faster sync (5 minutes initially)
    sntp_set_sync_interval(5 * 60 * 1000UL);

    // Register callback for time synchronization
    sntp_set_time_sync_notification_cb(timeSyncCallback);

    // Configure time server with multiple servers for better reliability
    configTime(_utcOffsetSeconds, _daylightOffsetSeconds, _ntpServer, "time.nist.gov", "time.google.com");

    log("TIME: NTP time service initialized with server: %s\n", _ntpServer);
    
    // Force an immediate NTP sync attempt
    sntp_restart();
    log("TIME: Forced NTP sync restart\n");
}

unsigned long NTPTimeAdapter::getEpochTime()
{
    time_t now;
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        log("TIME: Failed to obtain time\n");
        
        return 0;
    }

    time(&now);
    return static_cast<unsigned long>(now);
}

const char* NTPTimeAdapter::getFormattedTime()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        log("TIME: NTP time not available, attempting to restore from storage\n");
        
        // Try to restore time from storage
        timeinfo = getTimeFromEEPROM();
        
        // Check if we got valid time from storage
        if (timeinfo.tm_year > 0) // tm_year is years since 1900, so > 0 means after 1900
        {
            log("TIME: Using time from storage\n");
            // Use the restored time
        }
        else
        {
            // If no valid time in storage either, return a message
            snprintf(_timeBuffer, sizeof(_timeBuffer), "No time available (waiting for NTP sync)");
            log("TIME: No time available from NTP or storage\n");
            return _timeBuffer;
        }
    }
    else
    {
        log("TIME: Got time from NTP successfully\n");
    }

    // Format time as ISO 8601 (YYYY-MM-DDThh:mm:ss.SSSZ)
    strftime(_timeBuffer, sizeof(_timeBuffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);

    // Add milliseconds
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned long milliseconds = tv.tv_usec / 1000;
    snprintf(_timeBuffer + strlen(_timeBuffer), sizeof(_timeBuffer) - strlen(_timeBuffer), ".%03luZ", milliseconds % 1000);

    return _timeBuffer;
}

void NTPTimeAdapter::printTime()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        log("TIME: No time available (yet)\n");
        return;
    }

    // Update real time seconds
    updateRealTimeSeconds();

    // Format and print time
    char buffer[150];
    strftime(buffer, sizeof(buffer), "%A, %B %d %Y %H:%M:%S", &timeinfo);

    snprintf(_logBuffer, sizeof(_logBuffer), "Current time: %s (Total seconds: %lu)",
             buffer, _realTimeSeconds);
    log("TIME: %s\n", _logBuffer);
}

bool NTPTimeAdapter::isCommandTimeValid(const char* createdDate, unsigned long maxAgeSeconds)
{
    // Update real time seconds if needed
    updateRealTimeSeconds();

    // Parse the ISO timestamp
    String createdDateStr(createdDate);
    int splitT = createdDateStr.indexOf("T");
    int splitPoint = createdDateStr.indexOf(".");

    // Extract time portion
    String timeStamp = createdDateStr.substring(splitT + 1, splitPoint);

    // Parse hours, minutes, seconds
    String hoursStr = timeStamp.substring(0, 2);
    String minutesStr = timeStamp.substring(3, 5);
    String secondsStr = timeStamp.substring(6, 8);

    int hoursInt = hoursStr.toInt();
    int minutesInt = minutesStr.toInt();
    int secondsInt = secondsStr.toInt();

    // Calculate command time in seconds
    unsigned long commandTimeSeconds = calculateTotalSeconds(hoursInt, minutesInt, secondsInt);

    // Calculate time difference
    long timeDifference = _realTimeSeconds - commandTimeSeconds;

    if (timeDifference < 0)
    {
        timeDifference = 0;
        log("TIME: Command time difference was negative, reset to 0\n");
    }

    snprintf(_logBuffer, sizeof(_logBuffer),
             "Command time: %s, diff: %ld seconds, max allowed: %lu seconds",
             timeStamp.c_str(), timeDifference, maxAgeSeconds);
    log("TIME: %s\n", _logBuffer);

    // Check if within allowed time window
    return (static_cast<unsigned long>(abs(timeDifference)) <= maxAgeSeconds);
}

void NTPTimeAdapter::saveTimeToStorage()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        log("TIME: Failed to get local time for storage\n");
        return;
    }

    putTimeToEEPROM(timeinfo);

    char buffer[150];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    snprintf(_logBuffer, sizeof(_logBuffer), "Time stored to EEPROM: %s", buffer);
    log("TIME_SYNC: %s\n", _logBuffer);
}

void NTPTimeAdapter::restoreTimeFromStorage()
{
    if (isTimeSynchronized())
    {
        log("TIME_SYNC: System clock already valid, skipping EEPROM restore\n");
        return;
    }

    struct tm timeinfo = getTimeFromEEPROM();

    char buffer[150];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    snprintf(_logBuffer, sizeof(_logBuffer), "Time restored from EEPROM: %s", buffer);
    log("TIME_SYNC: %s\n", _logBuffer);

    if (timeinfo.tm_year + 1900 > 2020)
    {
        _rtc.setTimeStruct(timeinfo);
        log("TIME_SYNC: System clock seeded from EEPROM: %s\n", buffer);
    }
    else
    {
        log("TIME_SYNC: Skipped clock seed, stored time invalid: %s\n", buffer);
    }
}

// Static callback function
void NTPTimeAdapter::timeSyncCallback(struct timeval *tv)
{
    // Call the non-static member function through the instance pointer
    if (_instance)
    {
        _instance->onTimeSynchronized();
    }
}

// Non-static callback handler
void NTPTimeAdapter::onTimeSynchronized()
{
    saveTimeToStorage();
}

// Private helper methods
void NTPTimeAdapter::updateRealTimeSeconds()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        return;
    }

    _realTimeSeconds = calculateTotalSeconds(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

unsigned long NTPTimeAdapter::calculateTotalSeconds(int hours, int minutes, int seconds)
{
    return hours * 3600 + minutes * 60 + seconds;
}

// EEPROM operations - Modified to use ISettings
void NTPTimeAdapter::putTimeToEEPROM(struct tm timeinfo)
{
    if (_settings == nullptr) {
        log("TIME: No settings instance provided\n");
        return;
    }
    
    _settings->setLocalTime(timeinfo);
    log("TIME: Time stored via settings interface\n");
}

struct tm NTPTimeAdapter::getTimeFromEEPROM()
{
    struct tm timeinfo = {0};
    
    if (_settings == nullptr) {
        log("TIME: No settings instance provided\n");
        return timeinfo;
    }
    
    timeinfo = _settings->getLocalTime();
    log("TIME: Time retrieved via settings interface\n");
    
    return timeinfo;
}

void NTPTimeAdapter::forceSyncTime()
{
    log("TIME: Forcing NTP synchronization\n");
    
    // Stop SNTP service
    sntp_stop();
    
    // Restart SNTP service with aggressive settings
    sntp_set_sync_interval(30 * 1000UL); // 30 seconds for immediate sync
    sntp_set_time_sync_notification_cb(timeSyncCallback);
    
    // Reconfigure time servers
    configTime(_utcOffsetSeconds, _daylightOffsetSeconds, _ntpServer, "time.nist.gov", "time.google.com");
    
    // Restart SNTP
    sntp_restart();
    
    log("TIME: NTP sync forced, waiting for synchronization\n");
}

bool NTPTimeAdapter::isTimeSynchronized()
{
    struct tm timeinfo;
    bool hasNtpTime = getLocalTime(&timeinfo);
    
    if (hasNtpTime) {
        // Check if the year is reasonable (after 2020)
        if (timeinfo.tm_year + 1900 > 2020) {
            log("TIME: NTP time is synchronized\n");
            return true;
        }
    }
    
    log("TIME: NTP time is not synchronized\n");
    return false;
}

#endif // NTP_TIME_ADAPTER_H
