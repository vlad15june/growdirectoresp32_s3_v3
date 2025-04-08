// MqttLoggerInfrastructure.h
#ifndef MQTT_LOGGER_H
#define MQTT_LOGGER_H

#include "Core/Domain/Interfaces/ILogger.h"
#include <PubSubClient.h>
#include <vector>
#include <stdarg.h>

// Concrete implementation of MQTT Logger
class MqttLogger : public ILogger {
private:
    PubSubClient& _mqttClient;
    String _defaultTopic;
    MqttLoggerMode _mode;
    String _deviceId;
    String _userEmail;
    String _accountUid;
    
    // Buffer for formatting messages
    char _buffer[512];
    
    // Helper method to determine if we should log to MQTT
    bool shouldLogToMqtt() const {
        return (_mode == MqttLoggerMode::MqttOnly || _mode == MqttLoggerMode::MqttAndSerial) && 
               _mqttClient.connected();
    }
    
    // Helper method to determine if we should log to Serial
    bool shouldLogToSerial() const {
        return (_mode == MqttLoggerMode::SerialOnly || _mode == MqttLoggerMode::MqttAndSerial);
    }
    
    // Convert LogLevel to string
    const char* logLevelToString(LogLevel level) const {
        switch (level) {
            case LogLevel::DEBUG: return "debug";
            case LogLevel::INFO: return "info";
            case LogLevel::WARNING: return "warning";
            case LogLevel::ERROR: return "error";
            case LogLevel::CRITICAL: return "critical";
            default: return "unknown";
        }
    }

public:
    // Constructor
    MqttLogger(PubSubClient& mqttClient, const char* defaultTopic, MqttLoggerMode mode = MqttLoggerMode::MqttAndSerial) 
        : _mqttClient(mqttClient), 
          _defaultTopic(defaultTopic), 
          _mode(mode),
          _deviceId(""),
          _userEmail(""),
          _accountUid("") {
    }
    
    // Set user information for structured logs
    void setUserInfo(const char* deviceId, const char* userEmail, const char* accountUid) {
        if (deviceId && strlen(deviceId) > 0) {
            _deviceId = deviceId;
        }
        
        if (userEmail && strlen(userEmail) > 0) {
            _userEmail = userEmail;
        }
        
        if (accountUid && strlen(accountUid) > 0) {
            _accountUid = accountUid;
        }
    }
    
    // Set logger mode
    void setMode(MqttLoggerMode mode) {
        _mode = mode;
    }
    
    // Base printing methods for default topic
    void print(const char* message) override {
        if (shouldLogToMqtt()) {
            _mqttClient.publish(_defaultTopic.c_str(), message);
        }
        
        if (shouldLogToSerial()) {
            Serial.print(message);
        }
    }
    
    void println(const char* message) override {
        if (shouldLogToMqtt()) {
            _mqttClient.publish(_defaultTopic.c_str(), message);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(message);
        }
    }
    
    void printf(const char* format, ...) override {
        va_list args;
        va_start(args, format);
        vsnprintf(_buffer, sizeof(_buffer), format, args);
        va_end(args);
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(_defaultTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.print(_buffer);
        }
    }
    
    // Methods for specific topic
    void print(const char* topic, const char* message) override {
        String fullTopic = _defaultTopic + "/" + topic;
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(fullTopic.c_str(), message);
        }
        
        if (shouldLogToSerial()) {
            Serial.print(message);
        }
    }
    
    void println(const char* topic, const char* message) override {
        String fullTopic = _defaultTopic + "/" + topic;
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(fullTopic.c_str(), message);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(message);
        }
    }
    
    void printf(const char* topic, const char* format, ...) override {
        va_list args;
        va_start(args, format);
        vsnprintf(_buffer, sizeof(_buffer), format, args);
        va_end(args);
        
        String fullTopic = _defaultTopic + "/" + topic;
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(fullTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.print(_buffer);
        }
    }
    
    // Logging with levels
    void log(LogLevel level, const char* message) override {
        snprintf(_buffer, sizeof(_buffer), "[%s] %s", logLevelToString(level), message);
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(_defaultTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
    
    void log(LogLevel level, const char* topic, const char* message) override {
        snprintf(_buffer, sizeof(_buffer), "[%s] %s", logLevelToString(level), message);
        String fullTopic = _defaultTopic + "/" + topic;
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(fullTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
    
    // Structured JSON logging
    void logJson(const char* feature, const char* action, const char* details) override {
        snprintf(_buffer, sizeof(_buffer), 
                 "{\"log_level\":\"info\",\"uid\":\"%s\",\"email\":\"%s\",\"accountUid\":\"%s\",\"feature\":\"%s\",\"action\":\"%s\",\"details\":%s}",
                 _deviceId.c_str(), _userEmail.c_str(), _accountUid.c_str(), feature, action, details);
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(_defaultTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
    
    void logJson(const char* topic, const char* feature, const char* action, const char* details) override {
        snprintf(_buffer, sizeof(_buffer), 
                 "{\"log_level\":\"info\",\"uid\":\"%s\",\"email\":\"%s\",\"accountUid\":\"%s\",\"feature\":\"%s\",\"action\":\"%s\",\"details\":%s}",
                 _deviceId.c_str(), _userEmail.c_str(), _accountUid.c_str(), feature, action, details);
        
        String fullTopic = _defaultTopic + "/" + topic;
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(fullTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
    
    // Additional method for JSON logging with log level
    void logJson(LogLevel level, const char* feature, const char* action, const char* details) {
        snprintf(_buffer, sizeof(_buffer), 
                 "{\"log_level\":\"%s\",\"uid\":\"%s\",\"email\":\"%s\",\"accountUid\":\"%s\",\"feature\":\"%s\",\"action\":\"%s\",\"details\":%s}",
                 logLevelToString(level), _deviceId.c_str(), _userEmail.c_str(), _accountUid.c_str(), feature, action, details);
        
        if (shouldLogToMqtt()) {
            _mqttClient.publish(_defaultTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
};

#endif // MQTT_LOGGER_H