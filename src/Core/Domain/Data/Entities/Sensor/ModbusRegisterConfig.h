#ifndef MODBUS_REGISTER_CONFIG_H
#define MODBUS_REGISTER_CONFIG_H

#include <string>

// Register config
struct ModbusRegisterConfig
{
    uint8_t functionCode;
    uint16_t startRegister;
    uint16_t numRegisters;
    float multiplier;
    float offset;
    std::string channelName;
    uint8_t channel;
    std::string sensorType;
};

#endif

