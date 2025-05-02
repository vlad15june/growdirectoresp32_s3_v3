#ifndef I_DEBUG_SYSTEM_H
#define I_DEBUG_SYSTEM_H

#include <map>
#include <vector>

// Debug feature constants
namespace DebugFeatures {
    // System debug features
    constexpr const char* IP_CHNG_DEBUG = "IP_CHNG_DEBUG";
    constexpr const char* NET_DEBUG = "NET_DEBUG";
    constexpr const char* TIME_DEBUG = "TIME_DEBUG";
    constexpr const char* TIME_SYNC_DEBUG = "TIME_SYNC_DEBUG";
    constexpr const char* SETUP_DEBUG = "SETUP_DEBUG";
    constexpr const char* TEST_DEBUG = "TEST_DEBUG";
    
    // Logging debug features
    constexpr const char* LOG_DEBUG = "LOG_DEBUG";
    constexpr const char* FILE_LOG_DEBUG = "FILE_LOG_DEBUG";
    constexpr const char* FILE_DEBUG = "FILE_DEBUG";
    constexpr const char* DIAGNOSTIC_DEBUG = "DIAGNOSTIC_DEBUG";
    
    // Rule debug features
    constexpr const char* RULE_DEBUG = "RULE_DEBUG";
    constexpr const char* RULE_SYNC_DEBUG = "RULE_SYNC_DEBUG";
    constexpr const char* RULE_ON_DEBUG = "RULE_ON_DEBUG";
    constexpr const char* RULE_SENSOR_VALUE_DEBUG = "RULE_SENSOR_VALUE_DEBUG";
    constexpr const char* RULE_SENSOR_OFFLINE_DEBUG = "RULE_SENSOR_OFFLINE_DEBUG";
    constexpr const char* RULE_SUNRISE_DEBUG = "RULE_SUNRISE_DEBUG";
    
    // Command debug features
    constexpr const char* MODULE_COMMAND_DEBUG = "MODULE_COMMAND_DEBUG";
    constexpr const char* DEVICE_COMMAND_DEBUG = "DEVICE_COMMAND_DEBUG";
    constexpr const char* ANALOG_INPUT_DEVICE_COMMAND_DEBUG = "ANALOG_INPUT_DEVICE_COMMAND_DEBUG";
    
    // Sensor debug features
    constexpr const char* SETUP_SENSOR_DEBUG = "SETUP_SENSOR_DEBUG";
    constexpr const char* SENSOR_ERROR_DEBUG = "SENSOR_ERROR_DEBUG";
    constexpr const char* REMOTE_SENSOR_RECEIVE_DEBUG = "REMOTE_SENSOR_RECEIVE_DEBUG";
    
    // Network communication debug features
    constexpr const char* UDP_RECEIVE_DEBUG = "UDP_RECEIVE_DEBUG";
    constexpr const char* UDP_SEND_DEBUG = "UDP_SEND_DEBUG";
    
    // MQTT debug features
    constexpr const char* MQTT_DEBUG = "MQTT_DEBUG";
    constexpr const char* MQTT_COMMAND_DEBUG = "MQTT_COMMAND_DEBUG";
    constexpr const char* MQTT_CALLBACK_DEBUG = "MQTT_CALLBACK_DEBUG";
    
    // Other debug features
    constexpr const char* HISTORY_DEBUG = "HISTORY_DEBUG";
    constexpr const char* CONTROL_DEBUG = "CONTROL_DEBUG";
    constexpr const char* DATA_DEBUG = "DATA_DEBUG";
    constexpr const char* SETTINGS_DEBUG = "SETTINGS_DEBUG";
    constexpr const char* BUTTON_DEBUG = "BUTTON_DEBUG";
    constexpr const char* LED_DEBUG = "LED_DEBUG";
    constexpr const char* UPDATE_FIRMWARE_DEBUG = "UPDATE_FIRMWARE_DEBUG";
}

// Debug feature interface
class IDebugFeature
{
public:
    virtual ~IDebugFeature() = default;

    // Feature properties
    virtual const char *getName() const = 0;
    virtual const char *getMqttTopic() const = 0;
    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
};

// Debug system interface
class IDebugSystem
{
public:
    virtual ~IDebugSystem() = default;

    // Feature management
    virtual IDebugFeature *registerFeature(const char *name, const char *mqttTopic = nullptr, bool initialState = false) = 0;
    virtual IDebugFeature *getFeature(const char *name) = 0;
    virtual void setDebugFeatureState(const char *feature, bool isEnabled) = 0;

    // Check if a feature is enabled (for use in if statements)
    virtual bool isFeatureEnabled(const char *featureName) = 0;
};

#endif // I_DEBUG_SYSTEM_H