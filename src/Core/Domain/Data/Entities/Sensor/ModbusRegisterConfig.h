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
    // Range a working sensor answers inside. Read by the scan and, since GDV1-3826, by the
    // plausibility gate of the reading path — so a config nobody filled in has to read as
    // "nothing declared" (max <= min) instead of as uninitialised memory.
    float canConnectMinValue = 0.0f;
    float canConnectMaxValue = 0.0f;
};

#endif

