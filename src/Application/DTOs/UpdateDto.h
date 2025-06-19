#pragma once

#include <string>
#include <vector>
#include "DeviceDto.h"
#include "SensorDto.h"

/**
 * @brief Data Transfer Object for system updates containing device information
 */
class UpdateDto {
public:
    std::string growdirectorUid;    // Unique identifier of the Growdirector (MAC address)
    std::string email;              // User email
    std::string accountUid;         // Account unique identifier
    std::string time;               // Current system time in ISO format
    unsigned long millis;           // System uptime in milliseconds
    std::vector<DeviceDto> devices; // Array of device information
    std::vector<SensorDto> sensors; // Array of sensor information
    
    UpdateDto() : millis(0) {}
};
