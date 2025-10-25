// MqttLoggerDomain.h
#ifndef I_LOGGER_H
#define I_LOGGER_H

// Enum to define logger mode
enum class MqttLoggerMode {
    MqttOnly,        // Log only to MQTT
    SerialOnly,      // Log only to Serial
    MqttAndSerial    // Log to both MQTT and Serial
};

// Enum for log levels
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

// Base class (interface) for MQTT logging
class ILogger {
public:
    // Pure virtual destructor to make the class abstract
    virtual ~ILogger() = 0;
    
    // Log to default topic
    virtual void print(const char* message) = 0;
    virtual void println(const char* message) = 0;
    virtual void printf(const char* format, ...) = 0;
    
    // Log to specific topic
    virtual void print(const char* topic, const char* message) = 0;
    virtual void println(const char* topic, const char* message) = 0;
    virtual void printf(const char* topic, const char* format, ...) = 0;
    
    // Log to specific topic with custom mode
    virtual void print(const char* topic, MqttLoggerMode mode, const char* message) = 0;
    virtual void println(const char* topic, MqttLoggerMode mode, const char* message) = 0;
    virtual void printf(const char* topic, MqttLoggerMode mode, const char* format, ...) = 0;
    
    // Log with specified level
    virtual void log(LogLevel level, const char* message) = 0;
    virtual void log(LogLevel level, const char* topic, const char* message) = 0;
    
    // Structured logging (JSON)
    virtual void logJson(const char* feature, const char* action, const char* details) = 0;
    virtual void logJson(const char* topic, const char* feature, const char* action, const char* details) = 0;
    
    // Set module UID
    virtual void setModuleUid(const char* moduleUid) = 0;
};

// Default implementation
inline ILogger::~ILogger() {}

#endif // I_LOGGER_H