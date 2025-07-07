#ifndef I_SENSOR_REPOSITORY_H
#define I_SENSOR_REPOSITORY_H

#include "IRepository.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"
#include <vector>
#include <string>

class ISensorRepository : public IRepository<SensorEntity> {
public:
    virtual ~ISensorRepository() = default;

    /**
     * @brief Get sensors by protocol type
     * @param protocol Protocol type (e.g., "ModBus", "I2C")
     * @return Vector of sensors using the specified protocol
     */
    virtual std::vector<SensorEntity> getSensorsByProtocol(const char* protocol) = 0;

    /**
     * @brief Get sensors by sensor type
     * @param sensorType Type of sensor (e.g., "ph", "ec", "temperature")
     * @return Vector of sensors of the specified type
     */
    virtual std::vector<SensorEntity> getSensorsByType(const char* sensorType) = 0;

    /**
     * @brief Get sensors by model
     * @param model Sensor model
     * @return Vector of sensors of the specified model
     */
    virtual std::vector<SensorEntity> getSensorsByModel(const char* model) = 0;

    /**
     * @brief Get only active sensors (where isActive is true)
     * @return Vector of active sensors
     */
    virtual std::vector<SensorEntity> getActiveSensors() = 0;

    /**
     * @brief Get sensors with errors (where isError is true)
     * @return Vector of sensors that have errors
     */
    virtual std::vector<SensorEntity> getSensorsWithErrors() = 0;

    /**
     * @brief Get Modbus sensors
     * @return Vector of sensors using Modbus protocol
     */
    virtual std::vector<SensorEntity> getModbusSensors() = 0;

    /**
     * @brief Get I2C sensors
     * @return Vector of sensors using I2C protocol
     */
    virtual std::vector<SensorEntity> getI2CSensors() = 0;

    /**
     * @brief Get Atlas sensors (sensors with model starting with "atlas_")
     * @return Vector of Atlas sensors
     */
    virtual std::vector<SensorEntity> getAtlasSensors() = 0;

    /**
     * @brief Get multi-channel sensors
     * @return Vector of sensors that have multiple channels
     */
    virtual std::vector<SensorEntity> getMultiChannelSensors() = 0;

    /**
     * @brief Update sensor status
     * @param uid UID of the sensor to update
     * @param isActive New active status
     * @param isError New error status
     * @return true if operation was successful, false otherwise
     */
    virtual bool updateSensorStatus(const char* uid, bool isActive, bool isError) = 0;

    /**
     * @brief Create or update sensor with status handling
     * @param sensor Sensor entity to save
     * @return true if operation was successful, false otherwise
     */
    virtual bool createOrUpdateSensor(const SensorEntity& sensor) = 0;

    /**
     * @brief Update sensor status by UID using SensorStatusEnum
     * @param uid UID of the sensor to update
     * @param status New sensor status
     * @return true if operation was successful, false otherwise
     */
    virtual bool updateSensorStatusEnum(const char* uid, SensorStatusEnum status) = 0;

    /**
     * @brief Get sensors by status
     * @param status Sensor status to filter by
     * @return Vector of sensors with the specified status
     */
    virtual std::vector<SensorEntity> getSensorsByStatus(SensorStatusEnum status) = 0;
};

#endif // I_SENSOR_REPOSITORY_H
