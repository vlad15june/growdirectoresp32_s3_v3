#pragma once

#include "IRepository.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
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
    virtual SensorType* getSensorTypeByName(const std::string& name) = 0;
};
