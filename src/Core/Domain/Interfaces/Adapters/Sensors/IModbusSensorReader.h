#ifndef I_MODBUS_SENSOR_READER_H
#define I_MODBUS_SENSOR_READER_H

#include <vector>
#include <string>
#include "Core/Domain/Data/Entities/Sensor/ModbusRegisterConfig.h"

class IModbusSensorReader
{
public:
    virtual ~IModbusSensorReader() = default;

    virtual void init(unsigned long baud = 9600) = 0;

    virtual bool readRegister(uint8_t deviceAddress, const ModbusRegisterConfig &config, float &outValue, int portIndex = 0) = 0;
};

#endif