#pragma once

#include <string>

/**
 * @brief Data Transfer Object for sensor information
 */
class SensorDto {
public:
    std::string sensorUid;      // Sensor unique identifier
    std::string sensorType;     // Sensor type (ph, soil_moisture, air_temp, etc.)
    std::string model;          // Model identifier
    std::string protocol;       // Communication protocol
    std::string createdAt;      // Creation timestamp
    bool isActive;              // Sensor status
    bool isError;               // Error status
    float currentValue;         // Current sensor reading
    uint32_t lastReadTime;      // Last reading timestamp
    std::string options;        // JSON string with protocol-specific options
    
    SensorDto() : isActive(true), isError(false), currentValue(0.0f), lastReadTime(0) {}
    
    // Helper methods
    bool isModbusSensor() const { return protocol == "RS485"; }
    bool isI2CSensor() const { return protocol == "I2C"; }
};
