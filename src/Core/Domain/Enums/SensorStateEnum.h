#ifndef SENSOR_STATE_ENUM_H
#define SENSOR_STATE_ENUM_H

#include <string>

enum SensorStateEnum {
    SENSOR_STATE_ONLINE,
    SENSOR_STATE_DISCONNECTED
};

// Utility function to get string representation of sensor state
inline const char* getSensorStateName(SensorStateEnum state) {
    switch (state) {
        case SENSOR_STATE_ONLINE:
            return "online";
        case SENSOR_STATE_DISCONNECTED:
            return "disconnected";
        default:
            return "online";
    }
}

// Utility function to get enum from string
inline SensorStateEnum getSensorStateFromString(const std::string& stateStr) {
    if (stateStr == "online") return SENSOR_STATE_ONLINE;
    if (stateStr == "disconnected") return SENSOR_STATE_DISCONNECTED;
    return SENSOR_STATE_ONLINE; // default
}

#endif // SENSOR_STATE_ENUM_H
