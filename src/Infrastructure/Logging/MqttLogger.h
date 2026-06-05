// MqttLoggerInfrastructure.h
#ifndef MQTT_LOGGER_H
#define MQTT_LOGGER_H

#include "Core/Domain/Interfaces/ILogger.h"
#include <AsyncMqttClient.h>
#include <vector>
#include <mutex>
#include <stdarg.h>

// Concrete implementation of MQTT Logger
class MqttLogger : public ILogger {
private:
    AsyncMqttClient& _mqttClient;
    std::recursive_mutex* _clientMutex;
    String _defaultTopic;
    MqttLoggerMode _mode;
    String _deviceId;
    String _userEmail;
    String _accountUid;
    String _moduleUid;
    
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
    
    // Helper methods with channel override
    bool shouldLogToMqtt(MqttLoggerMode mode) const {
        return (mode == MqttLoggerMode::MqttOnly || mode == MqttLoggerMode::MqttAndSerial) && 
               _mqttClient.connected();
    }
    
    bool shouldLogToSerial(MqttLoggerMode mode) const {
        return (mode == MqttLoggerMode::SerialOnly || mode == MqttLoggerMode::MqttAndSerial);
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

    // Helper method to get the topic with module UID if available
    String getTopicWithModuleUid(const String& baseTopic) const {
        if (_moduleUid.length() > 0) {
            return baseTopic + "/" + _moduleUid;
        }
        return baseTopic;
    }

    void publishMqtt(const char* topic, const char* message) {
        if (_clientMutex) {
            std::lock_guard<std::recursive_mutex> lock(*_clientMutex);
            _mqttClient.publish(topic, 0, false, message);
        } else {
            _mqttClient.publish(topic, 0, false, message);
        }
    }

public:
    // Constructor
    MqttLogger(AsyncMqttClient& mqttClient, const char* defaultTopic, MqttLoggerMode mode = MqttLoggerMode::MqttAndSerial, std::recursive_mutex* clientMutex = nullptr)
        : _mqttClient(mqttClient),
          _clientMutex(clientMutex),
          _defaultTopic(defaultTopic),
          _mode(mode),
          _deviceId(""),
          _userEmail(""),
          _accountUid(""),
          _moduleUid("") {
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
    
    // Set module UID
    void setModuleUid(const char* moduleUid) override {
        if (moduleUid && strlen(moduleUid) > 0) {
            _moduleUid = moduleUid;
        }
    }
    
    // Set logger mode
    void setMode(MqttLoggerMode mode) {
        _mode = mode;
    }
    
    // Base printing methods for default topic
    void print(const char* message) override {
        if (shouldLogToMqtt()) {
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), message);
        }
        
        if (shouldLogToSerial()) {
            Serial.print(message);
        }
    }
    
    void println(const char* message) override {
        if (shouldLogToMqtt()) {
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), message);
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
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.print(_buffer);
        }
    }
    
    // New overloaded methods with channel override for default topic
    void print(const char* message, MqttLoggerMode channelOverride) override {
        if (shouldLogToMqtt(channelOverride)) {
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), message);
        }
        
        if (shouldLogToSerial(channelOverride)) {
            Serial.print(message);
        }
    }
    
    void println(const char* message, MqttLoggerMode channelOverride) override {
        if (shouldLogToMqtt(channelOverride)) {
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), message);
        }
        
        if (shouldLogToSerial(channelOverride)) {
            Serial.println(message);
        }
    }
    
    void printf(const char* format, MqttLoggerMode channelOverride, ...) override {
        va_list args;
        va_start(args, channelOverride);
        vsnprintf(_buffer, sizeof(_buffer), format, args);
        va_end(args);
        
        if (shouldLogToMqtt(channelOverride)) {
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial(channelOverride)) {
            Serial.print(_buffer);
        }
    }
    
    // Methods for specific topic
    void print(const char* topic, const char* message) override {
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        if (shouldLogToMqtt()) {
            publishMqtt(fullTopic.c_str(), message);
        }
        
        if (shouldLogToSerial()) {
            Serial.print(message);
        }
    }
    
    void println(const char* topic, const char* message) override {
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        if (shouldLogToMqtt()) {
            publishMqtt(fullTopic.c_str(), message);
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
        
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        if (shouldLogToMqtt()) {
            publishMqtt(fullTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.print(_buffer);
        }
    }
    
    // New overloaded methods with channel override for specific topic
    void print(const char* topic, const char* message, MqttLoggerMode channelOverride) override {
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        if (shouldLogToMqtt(channelOverride)) {
            publishMqtt(fullTopic.c_str(), message);
        }
        
        if (shouldLogToSerial(channelOverride)) {
            Serial.print(message);
        }
    }
    
    void println(const char* topic, const char* message, MqttLoggerMode channelOverride) override {
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        if (shouldLogToMqtt(channelOverride)) {
            publishMqtt(fullTopic.c_str(), message);
        }
        
        if (shouldLogToSerial(channelOverride)) {
            Serial.println(message);
        }
    }
    
    void printf(const char* topic, const char* format, MqttLoggerMode channelOverride, ...) override {
        va_list args;
        va_start(args, channelOverride);
        vsnprintf(_buffer, sizeof(_buffer), format, args);
        va_end(args);
        
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        if (shouldLogToMqtt(channelOverride)) {
            publishMqtt(fullTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial(channelOverride)) {
            Serial.print(_buffer);
        }
    }
    
    // Methods for specific topic with custom mode
    void print(const char* topic, MqttLoggerMode mode, const char* message) override {
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        bool logToMqtt = (mode == MqttLoggerMode::MqttOnly || mode == MqttLoggerMode::MqttAndSerial) && _mqttClient.connected();
        bool logToSerial = (mode == MqttLoggerMode::SerialOnly || mode == MqttLoggerMode::MqttAndSerial);
        
        if (logToMqtt) {
            publishMqtt(fullTopic.c_str(), message);
        }
        
        if (logToSerial) {
            Serial.print(message);
        }
    }
    
    void println(const char* topic, MqttLoggerMode mode, const char* message) override {
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        bool logToMqtt = (mode == MqttLoggerMode::MqttOnly || mode == MqttLoggerMode::MqttAndSerial) && _mqttClient.connected();
        bool logToSerial = (mode == MqttLoggerMode::SerialOnly || mode == MqttLoggerMode::MqttAndSerial);
        
        if (logToMqtt) {
            publishMqtt(fullTopic.c_str(), message);
        }
        
        if (logToSerial) {
            Serial.println(message);
        }
    }
    
    void printf(const char* topic, MqttLoggerMode mode, const char* format, ...) override {
        va_list args;
        va_start(args, format);
        vsnprintf(_buffer, sizeof(_buffer), format, args);
        va_end(args);
        
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        bool logToMqtt = (mode == MqttLoggerMode::MqttOnly || mode == MqttLoggerMode::MqttAndSerial) && _mqttClient.connected();
        bool logToSerial = (mode == MqttLoggerMode::SerialOnly || mode == MqttLoggerMode::MqttAndSerial);
        
        if (logToMqtt) {
            publishMqtt(fullTopic.c_str(), _buffer);
        }
        
        if (logToSerial) {
            Serial.print(_buffer);
        }
    }
    
    // Logging with levels
    void log(LogLevel level, const char* message) override {
        snprintf(_buffer, sizeof(_buffer), "[%s] %s", logLevelToString(level), message);
        
        if (shouldLogToMqtt()) {
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
    
    void log(LogLevel level, const char* topic, const char* message) override {
        snprintf(_buffer, sizeof(_buffer), "[%s] %s", logLevelToString(level), message);
        
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        if (shouldLogToMqtt()) {
            publishMqtt(fullTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
    
    // Structured JSON logging
    void logJson(const char* feature, const char* action, const char* details) override {
        snprintf(_buffer, sizeof(_buffer), 
                 "{\"log_level\":\"info\",\"uid\":\"%s\",\"email\":\"%s\",\"accountUid\":\"%s\",\"moduleUid\":\"%s\",\"feature\":\"%s\",\"action\":\"%s\",\"details\":%s}",
                 _deviceId.c_str(), _userEmail.c_str(), _accountUid.c_str(), _moduleUid.c_str(), feature, action, details);
        
        if (shouldLogToMqtt()) {
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
    
    void logJson(const char* topic, const char* feature, const char* action, const char* details) override {
        snprintf(_buffer, sizeof(_buffer), 
                 "{\"log_level\":\"info\",\"uid\":\"%s\",\"email\":\"%s\",\"accountUid\":\"%s\",\"moduleUid\":\"%s\",\"feature\":\"%s\",\"action\":\"%s\",\"details\":%s}",
                 _deviceId.c_str(), _userEmail.c_str(), _accountUid.c_str(), _moduleUid.c_str(), feature, action, details);
        
        String baseTopic = _defaultTopic + "/" + topic;
        String fullTopic = getTopicWithModuleUid(baseTopic);
        
        if (shouldLogToMqtt()) {
            publishMqtt(fullTopic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
    
    // Additional method for JSON logging with log level
    void logJson(LogLevel level, const char* feature, const char* action, const char* details) {
        snprintf(_buffer, sizeof(_buffer), 
                 "{\"log_level\":\"%s\",\"uid\":\"%s\",\"email\":\"%s\",\"accountUid\":\"%s\",\"moduleUid\":\"%s\",\"feature\":\"%s\",\"action\":\"%s\",\"details\":%s}",
                 logLevelToString(level), _deviceId.c_str(), _userEmail.c_str(), _accountUid.c_str(), _moduleUid.c_str(), feature, action, details);
        
        if (shouldLogToMqtt()) {
            String topic = getTopicWithModuleUid(_defaultTopic);
            publishMqtt(topic.c_str(), _buffer);
        }
        
        if (shouldLogToSerial()) {
            Serial.println(_buffer);
        }
    }
};

#endif // MQTT_LOGGER_H