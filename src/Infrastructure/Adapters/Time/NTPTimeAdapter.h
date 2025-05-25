#ifndef NTP_TIME_ADAPTER_H
#define NTP_TIME_ADAPTER_H

#include "INTPTimeAdapter.h"
#include <WiFiUdp.h>
#include "time.h"
#include "esp_sntp.h"

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
    // Set time synchronization interval (1 hour by default)
    sntp_set_sync_interval(1 * 60 * 60 * 1000UL);

    // Register callback for time synchronization
    sntp_set_time_sync_notification_cb(timeSyncCallback);

    // Configure time server
    configTime(_utcOffsetSeconds, _daylightOffsetSeconds, _ntpServer);

    log("TIME: NTP time service initialized\n");
    
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
        snprintf(_timeBuffer, sizeof(_timeBuffer), "No time available (yet)");
        log("TIME: No time available (yet)\n");
        return _timeBuffer;
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
    struct tm timeinfo = getTimeFromEEPROM();

    char buffer[150];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    snprintf(_logBuffer, sizeof(_logBuffer), "Time restored from EEPROM: %s", buffer);
    log("TIME_SYNC: %s\n", _logBuffer);

    // Set RTC time (assumes ESP32 RTC is available)
    // Note: You might need to adjust this to match your specific RTC implementation
    // rtc.setTimeStruct(timeinfo);
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

#endif // NTP_TIME_ADAPTER_H