#pragma once

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Models/SensorCommandResult.h"

class ISensorCommandUseCase
{
public:
    virtual ~ISensorCommandUseCase() = default;

    virtual SensorCommandResult execute(const SensorEntity& sensor) = 0;
};
