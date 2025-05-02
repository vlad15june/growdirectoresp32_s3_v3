#ifndef DEBUG_SYSTEM_H
#define DEBUG_SYSTEM_H

#include <Arduino.h>
#include <map>
#include <vector>
#include "Core/Domain/Interfaces/IDebugSystem.h"

class DebugFeature : public IDebugFeature
{
private:
    const char *_name;
    const char *_mqttTopic;
    bool _enabled;

public:
    DebugFeature(const char *name, const char *mqttTopic = nullptr, bool initialState = false)
        : _name(name), _mqttTopic(mqttTopic ? mqttTopic : name), _enabled(initialState) {}

    const char *getName() const override { return _name; }
    const char *getMqttTopic() const override { return _mqttTopic; }
    bool isEnabled() const override { return _enabled; }
    void setEnabled(bool enabled) override { _enabled = enabled; }
};

class DebugSystem : public IDebugSystem
{
private:
    std::map<String, DebugFeature *> _features;

    void initializeFeatures()
    {
        // Register all debug features with their default values
        registerFeature(DebugFeatures::IP_CHNG_DEBUG, "ip_change", true);
        registerFeature(DebugFeatures::RULE_SENSOR_OFFLINE_DEBUG, "rule_sensor_offline", true);
        registerFeature(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG, "remote_sensor_receive", false);
        registerFeature(DebugFeatures::UDP_RECEIVE_DEBUG, "udp_receive", false);
        registerFeature(DebugFeatures::UDP_SEND_DEBUG, "udp_send", false);
        registerFeature(DebugFeatures::HISTORY_DEBUG, "history", false);
        registerFeature(DebugFeatures::TIME_DEBUG, "time", false);
        registerFeature(DebugFeatures::TIME_SYNC_DEBUG, "time_sync", false);
        registerFeature(DebugFeatures::NET_DEBUG, "net", false);
        registerFeature(DebugFeatures::CONTROL_DEBUG, "control", false);
        registerFeature(DebugFeatures::RULE_DEBUG, "rule", false);
        registerFeature(DebugFeatures::RULE_SYNC_DEBUG, "rule_sync", true);
        registerFeature(DebugFeatures::RULE_ON_DEBUG, "rule_on", true);
        registerFeature(DebugFeatures::RULE_SENSOR_VALUE_DEBUG, "rule_sensor_value", false);
        registerFeature(DebugFeatures::RULE_SUNRISE_DEBUG, "rule_sunrise", true);
        registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG, "module_command", true);
        registerFeature(DebugFeatures::DEVICE_COMMAND_DEBUG, "device_command", true);
        registerFeature(DebugFeatures::ANALOG_INPUT_DEVICE_COMMAND_DEBUG, "analog_input_device_command", true);
        registerFeature(DebugFeatures::SETUP_SENSOR_DEBUG, "setup_sensor", true);
        registerFeature(DebugFeatures::SENSOR_ERROR_DEBUG, "sensor_error", false);
        registerFeature(DebugFeatures::DATA_DEBUG, "data", false);
        registerFeature(DebugFeatures::SETTINGS_DEBUG, "settings", false);
        registerFeature(DebugFeatures::SETUP_DEBUG, "setup", true);
        registerFeature(DebugFeatures::TEST_DEBUG, "test", false);
        registerFeature(DebugFeatures::LOG_DEBUG, "log", false);
        registerFeature(DebugFeatures::FILE_LOG_DEBUG, "file_log", false);
        registerFeature(DebugFeatures::FILE_DEBUG, "file", false);
        registerFeature(DebugFeatures::DIAGNOSTIC_DEBUG, "diagnostic", false);
        registerFeature(DebugFeatures::MQTT_DEBUG, "mqtt", false);
        registerFeature(DebugFeatures::MQTT_COMMAND_DEBUG, "mqtt_command", true);
        registerFeature(DebugFeatures::MQTT_CALLBACK_DEBUG, "mqtt_callback", false);
        registerFeature(DebugFeatures::BUTTON_DEBUG, "button", false);
        registerFeature(DebugFeatures::LED_DEBUG, "led", false);
        registerFeature(DebugFeatures::UPDATE_FIRMWARE_DEBUG, "update_firmware", true);
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
    IDebugFeature *registerFeature(const char *name, const char *mqttTopic = nullptr, bool initialState = false) override
    {
        String nameStr(name);

        // Check if the feature already exists
        if (_features.find(nameStr) != _features.end())
        {
            return _features[nameStr];
        }

        // Create a new feature and add it to the map
        DebugFeature *feature = new DebugFeature(name, mqttTopic, initialState);
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

    // Check if a feature is enabled (for use in if statements)
    bool isFeatureEnabled(const char *featureName) override
    {
        IDebugFeature *feature = getFeature(featureName);
        return feature && feature->isEnabled();
    }
};

#endif // DEBUG_SYSTEM_H