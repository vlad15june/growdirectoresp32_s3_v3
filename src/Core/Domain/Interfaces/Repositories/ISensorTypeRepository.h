#pragma once

#include "IRepository.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Enums/StorageWriteErrorEnum.h"
#include <cstddef>
#include <vector>
#include <string>

class ISensorTypeRepository : public IRepository<SensorType> {
public:
    virtual ~ISensorTypeRepository() = default;

    virtual std::vector<SensorType> getSensorTypesByProtocol(SensorProtocol protocol) = 0;
    virtual std::vector<SensorType> getAnalogSensorTypes() = 0;
    virtual std::vector<SensorType> getI2CSensorTypes() = 0;
    virtual std::vector<SensorType> getModbusSensorTypes() = 0;
    virtual std::vector<SensorType> getCalibrableSensorTypes() = 0;
    virtual std::vector<SensorType> getCanScan() = 0;
    virtual SensorType* getSensorTypeByName(const std::string& name) = 0;

    /**
     * @brief Why the last saveAll() stored nothing (GDV1-3827)
     *
     * saveAll() returns a count, which cannot tell "the catalog was empty" from "the write was
     * refused". The reason belongs in the log of whoever asked for the write.
     */
    virtual StorageWriteErrorEnum getLastSaveError() const = 0;

    /**
     * @brief How many bytes the last saveAll() handed to storage
     */
    virtual size_t getLastSaveBytes() const = 0;
};
