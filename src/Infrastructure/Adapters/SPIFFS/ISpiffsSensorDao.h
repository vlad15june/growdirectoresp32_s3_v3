#pragma once

#include "ISpiffsDao.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"

/**
 * @brief Sensor-specific DAO interface extending base DAO functionality
 */
class ISpiffsSensorDao : public ISpiffsDao<SensorEntity> {
public:
    virtual ~ISpiffsSensorDao() = default;

    /**
     * @brief Save a sensor using its UID as identifier
     * @param sensor The sensor to save
     * @return true if the operation was successful, false otherwise
     */
    virtual bool saveSensor(const SensorEntity& sensor) = 0;

    /**
     * @brief Get a sensor by its UID
     * @param uid The UID of the sensor to retrieve
     * @param sensor Reference to a sensor object to populate
     * @return true if the sensor was found, false otherwise
     */
    virtual bool getSensor(const std::string& uid, SensorEntity& sensor) = 0;

    /**
     * @brief Delete a sensor by its UID
     * @param uid The UID of the sensor to delete
     * @return true if the sensor was deleted, false otherwise
     */
    virtual bool deleteSensor(const std::string& uid) = 0;

    /**
     * @brief Get all sensors
     * @param sensors Reference to a vector to populate with sensors
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getSensors(std::vector<SensorEntity>& sensors) = 0;

    /**
     * @brief Save multiple sensors
     * @param sensors Vector of sensors to save
     * @return true if the operation was successful, false otherwise
     */
    virtual bool saveSensors(const std::vector<SensorEntity>& sensors) = 0;

    /**
     * @brief Get sensors by protocol type
     * @param protocol The protocol type to filter by
     * @param sensors Reference to a vector to populate with sensors
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getSensorsByProtocol(const std::string& protocol, std::vector<SensorEntity>& sensors) = 0;

    /**
     * @brief Get sensors by sensor type
     * @param sensorType The sensor type to filter by
     * @param sensors Reference to a vector to populate with sensors
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getSensorsByType(const std::string& sensorType, std::vector<SensorEntity>& sensors) = 0;

    /**
     * @brief Get sensors by model
     * @param model The model to filter by
     * @param sensors Reference to a vector to populate with sensors
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getSensorsByModel(const std::string& model, std::vector<SensorEntity>& sensors) = 0;

    /**
     * @brief Get active sensors
     * @param sensors Reference to a vector to populate with sensors
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getActiveSensors(std::vector<SensorEntity>& sensors) = 0;

    /**
     * @brief Get sensors with errors
     * @param sensors Reference to a vector to populate with sensors
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getSensorsWithErrors(std::vector<SensorEntity>& sensors) = 0;
};
