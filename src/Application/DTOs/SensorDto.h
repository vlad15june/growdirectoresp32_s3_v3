#pragma once

#include <string>

/**
 * @brief Data Transfer Object for sensor information
 */
class SensorDto {
public:
    std::string uid;        // Sensor unique identifier (e.g., "84:F7:03:FE:88:2C.00.01")
    float value;            // Current sensor reading
    std::string state;      // Sensor state ("online", "offline", "error")
    
    SensorDto() : value(0.0f), state("offline") {}
};
