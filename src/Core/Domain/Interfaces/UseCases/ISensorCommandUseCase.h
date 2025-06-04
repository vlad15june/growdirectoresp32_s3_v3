#pragma once

#include <string>

/**
 * @brief Interface for sensor command execution use case
 */
class ISensorCommandUseCase
{
public:
    virtual ~ISensorCommandUseCase() = default;

    /**
     * @brief Execute a sensor type configuration command
     * @param sensorType Sensor type name (soil_moisture, lux, par, do, orp)
     * @param sensorUid Sensor unique identifier
     * @return true if command was executed successfully, false otherwise
     */
    virtual bool execute(const std::string &sensorType, const std::string &sensorUid) = 0;
};
