#ifndef MODULE_COMMAND_ENUM_H
#define MODULE_COMMAND_ENUM_H

#include <string>

enum class ModuleCommandEnum {
    UNKNOWN,
    ADD_WIFI,
    UPDATE_FIRMWARE,
    BLINKING_START,
    BLINKING_STOP,
    REBOOT,
    PING_PIN,
    SET_SENSOR_MODEL,
    DELETE_SENSORS,
    SCAN_SENSORS
};

// Utility function to get ModuleCommandEnum from string
inline ModuleCommandEnum getModuleCommandEnum(const std::string& commandStr) {
    if (commandStr == "AddWifi") {
        return ModuleCommandEnum::ADD_WIFI;
    } else if (commandStr == "UpdateFirmware") {
        return ModuleCommandEnum::UPDATE_FIRMWARE;
    } else if (commandStr == "BlinkingStart") {
        return ModuleCommandEnum::BLINKING_START;
    } else if (commandStr == "BlinkingStop") {
        return ModuleCommandEnum::BLINKING_STOP;
    } else if (commandStr == "Reboot") {
        return ModuleCommandEnum::REBOOT;
    } else if (commandStr == "PingPin") {
        return ModuleCommandEnum::PING_PIN;
    } else if (commandStr == "SetSensorModel") {
        return ModuleCommandEnum::SET_SENSOR_MODEL;
    } else if (commandStr == "deleteSensors") {
        return ModuleCommandEnum::DELETE_SENSORS;
    } else if (commandStr == "scanSensors") {
        return ModuleCommandEnum::SCAN_SENSORS;
    }
    return ModuleCommandEnum::UNKNOWN;
}

// Utility function to get string representation of ModuleCommandEnum
inline const char* getModuleCommandName(ModuleCommandEnum command) {
    switch (command) {
        case ModuleCommandEnum::ADD_WIFI:
            return "AddWifi";
        case ModuleCommandEnum::UPDATE_FIRMWARE:
            return "UpdateFirmware";
        case ModuleCommandEnum::BLINKING_START:
            return "BlinkingStart";
        case ModuleCommandEnum::BLINKING_STOP:
            return "BlinkingStop";
        case ModuleCommandEnum::REBOOT:
            return "Reboot";
        case ModuleCommandEnum::PING_PIN:
            return "PingPin";
        case ModuleCommandEnum::SET_SENSOR_MODEL:
            return "SetSensorModel";
        case ModuleCommandEnum::DELETE_SENSORS:
            return "deleteSensors";
        case ModuleCommandEnum::SCAN_SENSORS:
            return "scanSensors";
        case ModuleCommandEnum::UNKNOWN:
        default:
            return "Unknown";
    }
}

#endif // MODULE_COMMAND_ENUM_H
