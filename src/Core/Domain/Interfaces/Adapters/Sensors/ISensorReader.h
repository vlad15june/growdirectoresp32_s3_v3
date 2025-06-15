#ifndef I_SENSOR_READER_H
#define I_SENSOR_READER_H

#include "../../Data/Entities/Sensor/Sensor.h"
#include "../../Data/Models/SensorReadResult.h"

class ISensorReader
{
public:
    virtual ~ISensorReader() = default;

    /**
     * @brief Read sensor value based on sensor configuration
     * @param sensor Sensor entity containing configuration
     * @param outValue Output value read from sensor
     * @return true if read was successful, false otherwise
     */
    virtual bool readSensor(const SensorEntity& sensor, float& outValue) = 0;

    /**
     * @brief Scan/check if sensor is online and responding
     * @param sensor Sensor entity to scan
     * @return true if sensor is online and responding, false otherwise
     */
    virtual bool scanSensor(const SensorEntity& sensor) = 0;

    /**
     * @brief Read sensor with detailed result information
     * @param sensor Sensor entity containing configuration
     * @return SensorReadResult with value, status, and error information
     */
    virtual SensorReadResult readSensorDetailed(const SensorEntity& sensor) = 0;

    /**
     * @brief Read multiple channel sensor
     * @param sensor Multi-channel sensor entity
     * @param channelValues Output map of channel names to values
     * @return true if read was successful, false otherwise
     */
    virtual bool readMultiChannelSensor(const SensorEntity& sensor, std::map<std::string, float>& channelValues) = 0;
};

#endif // I_SENSOR_READER_H
