#pragma once

#include "ISpiffsDao.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include <vector>

class ISpiffsSensorTypeDao : public ISpiffsDao<SensorType> {
public:
    virtual ~ISpiffsSensorTypeDao() = default;
    
    virtual bool getSensorTypesByProtocol(SensorProtocol protocol, std::vector<SensorType>& sensorTypes) = 0;
    virtual bool getCalibrableSensorTypes(std::vector<SensorType>& sensorTypes) = 0;
    virtual bool getSensorTypeByName(const std::string& name, SensorType& sensorType) = 0;
};
