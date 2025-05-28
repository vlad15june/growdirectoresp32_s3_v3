#pragma once

#include <string>

/**
 * @brief Data Transfer Object for command notifications
 */
class CommandDto
{
public:
    std::string growDirectorUid; // Grow director unique identifier
    std::string type;            // Command type (DeviceCommandNotification, RuleCommandNotification, etc.)

    // DeviceCommandNotification fields
    std::string deviceUid;      // Device unique identifier
    int commandId;              // Command ID
    std::string requestedState; // Requested state (on/off)

    // RuleCommandNotification fields
    std::string ruleUid;      // Rule unique identifier
    std::string rulesPayload; // JSON payload with rules

    // ModuleCommandNotification fields
    std::string command; // Command name (UpdateFirmware)
    std::string version; // Firmware version
    std::string url;     // Download URL

    // SensorTypeCommandNotification fields
    std::string sensorUid;  // Sensor unique identifier
    std::string sensorType; // Sensor type (soil_moisture, etc.)

    CommandDto() : commandId(0) {}

    // Helper methods to determine command type
    bool isDeviceCommand() const { return type == "DeviceCommandNotification"; }
    bool isRuleCommand() const { return type == "RuleCommandNotification"; }
    bool isModuleCommand() const { return type == "ModuleCommandNotification"; }
    bool isSensorTypeCommand() const { return type == "SensorTypeCommandNotification"; }
};
