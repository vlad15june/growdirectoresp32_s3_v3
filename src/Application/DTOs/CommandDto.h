#pragma once

#include <string>

/**
 * @brief Data Transfer Object for command notifications
 */
class CommandDto
{
public:
    std::string growDirectorUid; // Grow director unique identifier
    std::string createdAt;       // Command creation timestamp
    std::string commandUid;      // Command unique identifier
    std::string type;            // Command type (DeviceCommandNotification, RuleCommandNotification, etc.)

    // DeviceCommandNotification fields
    std::string deviceUid;      // Device unique identifier
    int commandId;              // Command ID
    std::string requestedState; // Requested state (on/off)
    bool needToSwitchBack;
    int secondsToSwitchBack;
    int analogControlPinValue;
    std::string switchType;
    std::string createdDate;
    std::string status;

    // RuleCommandNotification fields
    std::string ruleUid;      // Rule unique identifier
    std::string rulesPayload; // JSON payload with rules

    // ModuleCommandNotification fields
    std::string command; // Command name (UpdateFirmware, AddSensor, ScanSensor, RemoveSensor)
    std::string version; // Firmware version
    std::string url;     // Download URL

    // SensorTypeCommandNotification fields
    std::string sensorUid;  // Sensor unique identifier
    std::string sensorType; // Sensor type (soil_moisture, ph, air_temp, etc.)
    std::string model;      // Model identifier (atlas_ph_v1, modbus_soil_sensor_v1, etc.)
    std::string options;    // JSON string containing sensor configuration options

    CommandDto() : commandId(0), needToSwitchBack(false), secondsToSwitchBack(0), analogControlPinValue(0) {}

    // Helper methods to determine command type
    bool isDeviceCommand() const { return type == "DeviceCommandNotification"; }
    bool isRuleCommand() const { return type == "RuleCommandNotification"; }
    bool isModuleCommand() const { return type == "ModuleCommandNotification"; }
    bool isSensorTypeCommand() const { return type == "SensorTypeCommandNotification"; } //TODO: Rename to SensorCommandNotification or delete
    bool isDeleteModuleCommand() const { return type == "DeleteGdCommandNotification"; }
};
