#pragma once

#include "ISpiffsDao.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Enums/StorageWriteErrorEnum.h"
#include <cstddef>
#include <vector>

class ISpiffsSensorTypeDao : public ISpiffsDao<SensorType> {
public:
    virtual ~ISpiffsSensorTypeDao() = default;

    virtual bool getSensorTypesByProtocol(SensorProtocol protocol, std::vector<SensorType>& sensorTypes) = 0;
    virtual bool getCalibrableSensorTypes(std::vector<SensorType>& sensorTypes) = 0;
    virtual bool getSensorTypeByName(const std::string& name, SensorType& sensorType) = 0;

    /**
     * @brief Why the last saveAll() did not store the data (GDV1-3827)
     */
    virtual StorageWriteErrorEnum getLastSaveError() const = 0;

    /**
     * @brief How many bytes the last saveAll() handed to storage (0 if it never got that far)
     */
    virtual size_t getLastSaveBytes() const = 0;
};
