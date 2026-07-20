#ifndef I_NTP_TIME_ADAPTER_H
#define I_NTP_TIME_ADAPTER_H

// Base class (interface) for NTP time functionality
class INTPTimeAdapter {
public:
    // Pure virtual destructor to make the class abstract
    virtual ~INTPTimeAdapter() = 0;
    
    // Initialize and configure NTP service
    virtual void setup() = 0;
    
    // Get current epoch time (Unix timestamp)
    virtual unsigned long getEpochTime() = 0;
    
    // Get formatted current time
    virtual const char* getFormattedTime() = 0;
    
    // Print current time to Serial (for debugging)
    virtual void printTime() = 0;
    
    // Check if command time is within the allowed time window
    virtual bool isCommandTimeValid(const char* createdDate, unsigned long maxAgeSeconds) = 0;
    
    // Save current time to persistent storage
    virtual void saveTimeToStorage() = 0;
    
    // Restore time from persistent storage
    virtual void restoreTimeFromStorage() = 0;

    // Get current local time as struct tm
    virtual bool getLocalTime(struct tm* out) = 0;

    // Force immediate NTP synchronization
    virtual void forceSyncTime() = 0;
    
    // Check if time is currently synchronized
    virtual bool isTimeSynchronized() = 0;
};

// Default implementation for the destructor
inline INTPTimeAdapter::~INTPTimeAdapter() {}

#endif // I_NTP_TIME_ADAPTER_H