#ifndef SENSOR_STATUS_ENUM_H
#define SENSOR_STATUS_ENUM_H

#include <string>

enum SensorStatusEnum {
    SENSOR_STATUS_NEW,
    SENSOR_STATUS_ADDED,
    SENSOR_STATUS_ERROR,
    SENSOR_STATUS_DELETED,
    SENSOR_STATUS_DISCONNECTED
};

// Utility function to get string representation of sensor status
inline const char* getSensorStatusName(SensorStatusEnum sensorStatus) {
    switch (sensorStatus) {
        case SENSOR_STATUS_NEW:
            return "new";
        case SENSOR_STATUS_ADDED:
            return "added";
        case SENSOR_STATUS_ERROR:
            return "error";
        case SENSOR_STATUS_DELETED:
            return "deleted";
        case SENSOR_STATUS_DISCONNECTED:
            return "disconnected";
        default:
            return "unknown";
    }
}

// Utility function to get enum from string
inline SensorStatusEnum getSensorStatusFromString(const std::string& statusStr) {
    if (statusStr == "new") return SENSOR_STATUS_NEW;
    if (statusStr == "added") return SENSOR_STATUS_ADDED;
    if (statusStr == "error") return SENSOR_STATUS_ERROR;
    if (statusStr == "deleted") return SENSOR_STATUS_DELETED;
    if (statusStr == "disconnected") return SENSOR_STATUS_DISCONNECTED;
    return SENSOR_STATUS_NEW; // default
}

#endif // SENSOR_STATUS_ENUM_H
