#ifndef DEBUG_SYSTEM_H
#define DEBUG_SYSTEM_H

#include <Arduino.h>
#include <map>
#include <vector>
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"

class DebugFeature : public IDebugFeature
{
private:
    const char *_name;
    const char *_mqttTopic;
    bool _enabled;
    MqttLoggerMode _logChannel;

public:
    DebugFeature(const char *name, const char *mqttTopic = nullptr, bool initialState = false, MqttLoggerMode channel = MqttLoggerMode::MqttAndSerial)
        : _name(name), _mqttTopic(mqttTopic ? mqttTopic : name), _enabled(initialState), _logChannel(channel) {}

    const char *getName() const override { return _name; }
    const char *getMqttTopic() const override { return _mqttTopic; }
    bool isEnabled() const override { return _enabled; }
    void setEnabled(bool enabled) override { _enabled = enabled; }
    
    // Logging channel configuration
    MqttLoggerMode getLogChannel() const override { return _logChannel; }
    void setLogChannel(MqttLoggerMode channel) override { _logChannel = channel; }
};

class DebugSystem : public IDebugSystem
{
private:
    std::map<String, DebugFeature *> _features;

    void initializeFeatures()
    {
        // Register all debug features with their default values
        registerFeature(DebugFeatures::IP_CHANGE_DEBUG, "ip_local_change", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::RULE_SENSOR_OFFLINE_DEBUG, "rule_sensor_offline", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG, "remote_sensor_receive", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::UDP_RECEIVE_DEBUG, "udp_receive", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::UDP_SEND_DEBUG, "udp_send", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::MODULE_UDP_COMMAND_DEBUG, "module_udp_command", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::WIFI_DEBUG, "wifi", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::HISTORY_DEBUG, "history", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::TIME_DEBUG, "time", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::TIME_SYNC_DEBUG, "time_sync", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::NET_DEBUG, "net", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::CONTROL_DEBUG, "control", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::RELAY_DRIVE_DEBUG, "relay_drive", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::RULE_DEBUG, "rule", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::RULE_SYNC_DEBUG, "rule_sync", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::RULE_REPOSITORY_DEBUG, "rule_repository", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::RULE_ON_DEBUG, "rule_on", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::RULE_SENSOR_VALUE_DEBUG, "rule_sensor_value", false, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::RULE_SUNRISE_DEBUG, "rule_sunrise", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG, "module_command", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::MODULE_MODE_DEBUG, "module_mode", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::DEVICE_COMMAND_DEBUG, "device_command", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::ANALOG_INPUT_DEVICE_COMMAND_DEBUG, "analog_input_device_command", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::RAW_MODBUS_DEBUG, "raw_modbus", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::SETUP_SENSOR_DEBUG, "setup_sensor", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::SENSOR_ERROR_DEBUG, "sensor_error", false, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::SENSOR_DATA_DEBUG, "sensor_data", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::DATA_DEBUG, "data", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::SETTINGS_DEBUG, "settings", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::SETUP_DEBUG, "setup", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::TEST_DEBUG, "test", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::LOG_DEBUG, "log", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::FILE_LOG_DEBUG, "file_log", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::FILE_DEBUG, "file", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::DIAGNOSTIC_DEBUG, "diagnostic", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::MQTT_DEBUG, "mqtt", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::MQTT_COMMAND_DEBUG, "mqtt_command", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::MQTT_CALLBACK_DEBUG, "mqtt_callback", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::BUTTON_DEBUG, "button", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::LED_DEBUG, "led", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::OFFLINE_INDICATION_DEBUG, "offline_indication", true, MqttLoggerMode::MqttAndSerial);
        registerFeature(DebugFeatures::SPIFFS_DEBUG, "spiffs", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::UPDATE_FIRMWARE_DEBUG, "update_firmware", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::LOOP_SERVICE_DEBUG, "loop_service", false, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::CONFIG_WEB_SERVER, "config_web_server", true, MqttLoggerMode::SerialOnly);
        registerFeature(DebugFeatures::REPOSITORY_DEBUG, "config_web_server", true, MqttLoggerMode::SerialOnly);        
    }

public:
    DebugSystem()
    {
        // Initialize the debug features
        initializeFeatures();
    }

    ~DebugSystem()
    {
        // Clean up all registered features
        for (auto &pair : _features)
        {
            delete pair.second;
        }
        _features.clear();
    }

    // Register a new debug feature
    IDebugFeature *registerFeature(const char *name, const char *mqttTopic = nullptr, bool initialState = false, MqttLoggerMode channel = MqttLoggerMode::MqttAndSerial) override
    {
        String nameStr(name);

        // Check if the feature already exists
        if (_features.find(nameStr) != _features.end())
        {
            return _features[nameStr];
        }

        // Create a new feature and add it to the map
        DebugFeature *feature = new DebugFeature(name, mqttTopic, initialState, channel);
        _features[nameStr] = feature;
        return feature;
    }

    // Get a debug feature by name
    IDebugFeature *getFeature(const char *name) override
    {
        String nameStr(name);

        if (_features.find(nameStr) != _features.end())
        {
            return _features[nameStr];
        }

        return nullptr;
    }

    // Enable or disable a feature - compatible with old SetDebugLevel method
    void setDebugFeatureState(const char *feature, bool isEnabled) override
    {
        IDebugFeature *featurePtr = getFeature(feature);
        if (featurePtr)
        {
            featurePtr->setEnabled(isEnabled);
        }
    }
    
    // Set logging channel for an existing debug feature
    void setFeatureLogChannel(const char *featureName, MqttLoggerMode channel) override
    {
        IDebugFeature *feature = getFeature(featureName);
        if (feature)
        {
            feature->setLogChannel(channel);
        }
    }

    // Check if a feature is enabled (for use in if statements)
    bool isFeatureEnabled(const char *featureName) override
    {
        IDebugFeature *feature = getFeature(featureName);
        return feature && feature->isEnabled();
    }
};

#endif // DEBUG_SYSTEM_H