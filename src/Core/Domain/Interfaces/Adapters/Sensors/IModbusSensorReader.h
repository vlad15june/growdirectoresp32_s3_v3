#ifndef I_MODBUS_SENSOR_READER_H
#define I_MODBUS_SENSOR_READER_H

#include <vector>
#include <string>
#include "Core/Domain/Data/Entities/Sensor/ModbusRegisterConfig.h"

class IModbusSensorReader
{
public:
    virtual ~IModbusSensorReader() = default;

    virtual bool readRegister(uint8_t deviceAddress, const ModbusRegisterConfig &config, float &outValue) = 0;
};

#endif