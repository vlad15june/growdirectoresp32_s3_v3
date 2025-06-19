#pragma once

#include <vector>
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Application/DTOs/UpdateDto.h"

class ISensorService {
public:
    virtual ~ISensorService() = default;
    
    virtual bool init() = 0;
    virtual void loop() = 0;
    virtual bool readAllSensors() = 0;
    virtual bool readSensor(const char* uid) = 0;
};
