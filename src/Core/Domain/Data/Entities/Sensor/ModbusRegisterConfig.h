#ifndef MODBUS_REGISTER_CONFIG_H
#define MODBUS_REGISTER_CONFIG_H

#include <cstdint>
#include <string>

// Data type of a Modbus register channel.
// The default is U16 to preserve existing single-register behaviour.
// Wider types (u32/s32/f32 with word order) are added by the RK900-11 ticket.
enum class ModbusDataType : uint8_t
{
    U16 = 0,
    S16 = 1
};

inline const char *modbusDataTypeToString(ModbusDataType type)
{
    switch (type)
    {
        case ModbusDataType::S16:
            return "s16";
        case ModbusDataType::U16:
        default:
            return "u16";
    }
}

inline ModbusDataType modbusDataTypeFromString(const std::string &value)
{
    if (value == "s16")
        return ModbusDataType::S16;
    return ModbusDataType::U16;
}

// Register config
//
// The numbers default to the ones a stored record without the field is read with - one holding
// register read through an identity scaling. A field left at whatever the stack held is a value
// the module scales a channel by, and, once the record is written back, text on flash: a float
// holding garbage serializes as "-nan", which is neither a number to read nor valid JSON.
struct ModbusRegisterConfig
{
    uint8_t functionCode = 3;
    uint16_t startRegister = 0;
    uint16_t numRegisters = 1;
    float multiplier = 1.0f;
    float offset = 0.0f;
    std::string channelName;
    uint8_t channel = 0;
    std::string sensorType;
    float canConnectMinValue = 0.0f;
    float canConnectMaxValue = 0.0f;
    ModbusDataType dataType = ModbusDataType::U16;
};

#endif
