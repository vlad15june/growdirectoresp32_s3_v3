#ifndef SENSOR_TYPE_ENUM_H
#define SENSOR_TYPE_ENUM_H

enum SensorTypeEnum {
    ANALOG_DO_CONNECTED = 1,
    ANALOG_ORP_CONNECTED = 2,
    ANALOG_SOIL_MOISTURE = 3,
    ANALOG_LUX_CONNECTED = 4,
    ANALOG_PAR_CONNECTED = 5,
    ANALOG_TEMP_HUM_CONNECTED = 6
};

// Utility function to get string representation of sensor type
inline const char* getSensorTypeName(SensorTypeEnum sensorType) {
    switch (sensorType) {
        case ANALOG_DO_CONNECTED:
            return "do";
        case ANALOG_ORP_CONNECTED:
            return "orp";
        case ANALOG_SOIL_MOISTURE:
            return "soil_moisture";
        case ANALOG_LUX_CONNECTED:
            return "lux";
        case ANALOG_PAR_CONNECTED:
            return "par";
        case ANALOG_TEMP_HUM_CONNECTED:
            return "temp_hum";
        default:
            return "unknown";
    }
}

// Utility function to get sensor type from string
inline SensorTypeEnum getSensorTypeFromName(const char* name) {
    if (strcmp(name, "do") == 0)
        return ANALOG_DO_CONNECTED;
    if (strcmp(name, "orp") == 0)
        return ANALOG_ORP_CONNECTED;
    if (strcmp(name, "soil_moisture") == 0)
        return ANALOG_SOIL_MOISTURE;
    if (strcmp(name, "lux") == 0)
        return ANALOG_LUX_CONNECTED;
    if (strcmp(name, "par") == 0)
        return ANALOG_PAR_CONNECTED;
    if (strcmp(name, "temp_hum") == 0)
        return ANALOG_TEMP_HUM_CONNECTED;
    
    return ANALOG_DO_CONNECTED; // Default fallback
}

#endif // SENSOR_TYPE_ENUM_H
