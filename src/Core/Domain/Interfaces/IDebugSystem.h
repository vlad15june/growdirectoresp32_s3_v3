#ifndef I_DEBUG_SYSTEM_H
#define I_DEBUG_SYSTEM_H

#include <map>
#include <vector>

// Debug feature constants
namespace DebugFeatures {
    // System debug features
    constexpr const char* IP_CHANGE_DEBUG = "IP_CHANGE";
    constexpr const char* NET_DEBUG = "NET";
    constexpr const char* TIME_DEBUG = "TIME";
    constexpr const char* TIME_SYNC_DEBUG = "TIME_SYNC";
    constexpr const char* SETUP_DEBUG = "SETUP";
    constexpr const char* TEST_DEBUG = "TEST";
    
    // Logging debug features
    constexpr const char* LOG_DEBUG = "LOG";
    constexpr const char* FILE_LOG_DEBUG = "FILE_LOG";
    constexpr const char* FILE_DEBUG = "FILE";
    constexpr const char* DIAGNOSTIC_DEBUG = "DIAGNOSTIC";
    
    // Loop service debug feature
    constexpr const char* LOOP_SERVICE_DEBUG = "LOOP_SERVICE";
    
    // Web server debug feature
    constexpr const char* CONFIG_WEB_SERVER = "CONFIG_WEB_SERVER";
    
    // Rule debug features
    constexpr const char* RULE_DEBUG = "RULE";
    constexpr const char* RULE_SYNC_DEBUG = "RULE_SYNC";
    constexpr const char* RULE_ON_DEBUG = "RULE_ON";
    constexpr const char* RULE_SENSOR_VALUE_DEBUG = "RULE_SENSOR_VALUE";
    constexpr const char* RULE_SENSOR_OFFLINE_DEBUG = "RULE_SENSOR_OFFLINE";
    constexpr const char* RULE_SUNRISE_DEBUG = "RULE_SUNRISE";
    
    // Command debug features
    constexpr const char* MODULE_COMMAND_DEBUG = "MODULE_COMMAND";
    constexpr const char* MODULE_MODE_DEBUG = "MODULE_MODE";
    constexpr const char* DEVICE_COMMAND_DEBUG = "DEVICE_COMMAND";
    constexpr const char* ANALOG_INPUT_DEVICE_COMMAND_DEBUG = "ANALOG_INPUT_DEVICE_COMMAND";
    
    // Sensor debug features
    constexpr const char* SETUP_SENSOR_DEBUG = "SETUP_SENSOR";
    constexpr const char* SENSOR_ERROR_DEBUG = "SENSOR_ERROR";
    constexpr const char* REMOTE_SENSOR_RECEIVE_DEBUG = "REMOTE_SENSOR_RECEIVE";
    
    // Network communication debug features
    constexpr const char* UDP_RECEIVE_DEBUG = "UDP_RECEIVE";
    constexpr const char* UDP_SEND_DEBUG = "UDP_SEND";
    
    // MQTT debug features
    constexpr const char* MQTT_DEBUG = "MQTT";
    constexpr const char* MQTT_COMMAND_DEBUG = "MQTT_COMMAND";
    constexpr const char* MQTT_CALLBACK_DEBUG = "MQTT_CALLBACK";
    
    // Other debug features
    constexpr const char* HISTORY_DEBUG = "HISTORY";
    constexpr const char* CONTROL_DEBUG = "CONTROL";
    constexpr const char* DATA_DEBUG = "DATA";
    constexpr const char* SETTINGS_DEBUG = "SETTINGS";
    constexpr const char* BUTTON_DEBUG = "BUTTON";
    constexpr const char* LED_DEBUG = "LED";
    constexpr const char* UPDATE_FIRMWARE_DEBUG = "UPDATE_FIRMWARE";
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