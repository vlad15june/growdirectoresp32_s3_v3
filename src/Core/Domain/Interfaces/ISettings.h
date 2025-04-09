#ifndef ISETTINGS_H
#define ISETTINGS_H

#include <time.h>

// Buffer size for string operations
#define BUFSIZE 40

// Sensor type constants
#define NOT_CONNECTED 0


// Interface for Settings
class ISettings {
public:
    // Pure virtual destructor to make the class abstract
    virtual ~ISettings() = 0;
    
    // Initialization
    virtual bool begin() = 0;
    
    // Configuration status methods
    virtual uint8_t getIfConfigured() = 0;
    virtual void setConfigured(uint8_t isConfigured) = 0;
    
    // Update status methods
    virtual uint8_t getNeedUpdate() = 0;
    virtual void setNeedUpdate(uint8_t needUpdate) = 0;
    virtual uint8_t getUpdateStatus() = 0;
    virtual void setUpdateStatus(uint8_t updateStatus) = 0;
    
    // Boot type methods
    virtual uint8_t getBootType() = 0;
    virtual void setBootType(uint8_t bootType) = 0;
    
    // Time and location methods
    virtual struct tm getLocalTime() = 0;
    virtual void setLocalTime(const struct tm& localTime) = 0;
    virtual float getLatitude() = 0;
    virtual void setLatitude(float latitude) = 0;
    virtual float getLongitude() = 0;
    virtual void setLongitude(float longitude) = 0;
    
    // Network configuration methods
    virtual void getSsid(char* buffer, int bufferSize = BUFSIZE) = 0;
    virtual void setSsid(const char* ssid) = 0;
    virtual void getPassword(char* buffer, int bufferSize = BUFSIZE) = 0;
    virtual void setPassword(const char* password) = 0;
    virtual void getUser(char* buffer, int bufferSize = BUFSIZE) = 0;
    virtual void setUser(const char* user) = 0;
    virtual void getToken(char* buffer, int bufferSize = BUFSIZE) = 0;
    virtual void setToken(const char* token) = 0;
    virtual void getUpdateLink(char* buffer, int bufferSize = BUFSIZE) = 0;
    virtual void setUpdateLink(const char* link) = 0;
    virtual void getModuleCommandUid(char* buffer, int bufferSize = BUFSIZE) = 0;
    virtual void setModuleCommandUid(const char* uid) = 0;
    virtual void getAccountUid(char* buffer, int bufferSize = BUFSIZE) = 0;
    virtual void setAccountUid(const char* uid) = 0;
    virtual void getServerUrl(char* buffer, int bufferSize = BUFSIZE) = 0;
    virtual void setServerUrl(const char* url) = 0;
    
    // Rule and sensor configuration methods
    virtual uint8_t getRuleState() = 0;
    virtual void setRuleState(uint8_t ruleState) = 0;
    virtual uint8_t getSensorTypeDoOrp() = 0;
    virtual void setSensorTypeDoOrp(uint8_t sensorType) = 0;
    virtual uint8_t getSensorType(uint8_t channel) = 0;
    virtual void setSensorType(uint8_t sensorType, uint8_t channel) = 0;
    virtual uint8_t getKeyTestMode() = 0;
    virtual void setKeyTestMode(uint8_t isApTestKey) = 0;
    virtual uint8_t getTempCoefVersion() = 0;
    virtual void setTempCoefVersion(uint8_t versionNumber) = 0;    
};

// Default implementation of destructor
inline ISettings::~ISettings() {}

#endif // ISETTINGS_H