#ifndef MODBUS_REGISTER_CONFIG_H
#define MODBUS_REGISTER_CONFIG_H

#include <cstdint>
#include <string>

// Data type of a Modbus register channel.
// The default is UInt16 to preserve existing single-register behaviour.
enum class ModbusDataType : uint8_t
{
    UInt16 = 0,
    Int16 = 1,
    Float32 = 2
};

inline const char *modbusDataTypeToString(ModbusDataType type)
{
    switch (type)
    {
        case ModbusDataType::Int16:
            return "int16";
        case ModbusDataType::Float32:
            return "float32";
        case ModbusDataType::UInt16:
        default:
            return "uint16";
    }
}

inline ModbusDataType modbusDataTypeFromString(const std::string &value)
{
    if (value == "int16")
        return ModbusDataType::Int16;
    if (value == "float32")
        return ModbusDataType::Float32;
    return ModbusDataType::UInt16;
}

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
    float canConnectMinValue;
    float canConnectMaxValue;
    ModbusDataType dataType = ModbusDataType::UInt16;
};

#endif

