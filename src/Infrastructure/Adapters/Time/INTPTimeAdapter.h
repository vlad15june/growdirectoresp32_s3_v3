#ifndef I_NTP_TIME_ADAPTER_H
#define I_NTP_TIME_ADAPTER_H

#include <Arduino.h>
#include <time.h>

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
    virtual String getFormattedTime() = 0;
    
    // Print current time to Serial (for debugging)
    virtual void printTime() = 0;
    
    // Check if command time is within the allowed time window
    virtual bool isCommandTimeValid(String createdDate, unsigned long maxAgeSeconds) = 0;
    
    // Save current time to persistent storage
    virtual void saveTimeToStorage() = 0;
    
    // Restore time from persistent storage
    virtual void restoreTimeFromStorage() = 0;
};

// Default implementation for the destructor
inline INTPTimeAdapter::~INTPTimeAdapter() {}

#endif // I_NTP_TIME_ADAPTER_H