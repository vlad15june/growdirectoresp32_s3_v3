#ifndef I_SENSOR_STRUCTURE_USE_CASE_H
#define I_SENSOR_STRUCTURE_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"

/**
 * @brief Sensor records of the module structure: one sensor entity -> the Sensor records of a Node.
 *
 * Two flows need exactly the same records - the full structure fill at setup and the cheap
 * republish after the sensor type catalog moved a channel - so both are given this interface
 * instead of owning the code (review of PR #17, 2026-08-13).
 */
class ISensorStructureUseCase
{
public:
    virtual ~ISensorStructureUseCase() = default;

    /** @brief Upsert the records of one sensor (protocol and channel layout decide which) */
    virtual void addSensor(Node &node, const SensorEntity &sensorEntity) = 0;

    virtual void addMultiChannelModbusSensor(Node &node, const SensorEntity &sensorEntity) = 0;
    virtual void addMultiChannelI2CSensor(Node &node, const SensorEntity &sensorEntity) = 0;
    virtual void addSingleChannelSensor(Node &node, const SensorEntity &sensorEntity) = 0;

    /** @brief Records of the computed channels of this sensor (they have no register config) */
    virtual void addVirtualChannelSensors(Node &node, const SensorEntity &sensorEntity) = 0;
};

#endif // I_SENSOR_STRUCTURE_USE_CASE_H
