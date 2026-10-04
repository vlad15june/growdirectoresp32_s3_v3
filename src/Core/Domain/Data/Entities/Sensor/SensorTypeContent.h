#pragma once

#include <cmath>
#include <cstddef>
#include <vector>
#include "SensorType.h"

/**
 * @brief Whether two sensor types, or two parts of them, hold the same thing.
 *
 * Field for field, over every field the catalog is read with (SensorTypeStreamParser) and stored
 * with (the converter that writes the stored catalog): two types that compare the same here are
 * stored the same, and a type that differs in any of those fields is a different type for the
 * module, whatever its timestamp says.
 *
 * Numbers are compared as the module stores them - see sameNumber().
 */
namespace SensorTypeContent
{

/**
 * Two numbers of a type are the same number when they differ by no more than storing one can make
 * them. The catalog and the registry keep a number as text written by ArduinoJson, which writes a
 * double between 1e-5 and 1 with nine places after the point (FloatParts): such a number can come
 * back from flash up to half a billionth from the one written, and once read into a float, half a
 * float step more - together under a billionth. Outside that range the text carries more digits than
 * a float has, and the number comes back as it was. Compared exactly, a number the server sends with
 * more digits than survive the text would make every type read back from flash differ from the one
 * the server sends, and be written again at every boot.
 */
inline bool sameNumber(float a, float b)
{
    return a == b || std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= 1e-9;
}

inline bool sameRegisterConfig(const ModbusRegisterConfig &a, const ModbusRegisterConfig &b)
{
    return a.functionCode == b.functionCode && a.startRegister == b.startRegister &&
           a.numRegisters == b.numRegisters && sameNumber(a.multiplier, b.multiplier) &&
           sameNumber(a.offset, b.offset) &&
           a.channelName == b.channelName && a.channel == b.channel && a.sensorType == b.sensorType &&
           sameNumber(a.canConnectMinValue, b.canConnectMinValue) &&
           sameNumber(a.canConnectMaxValue, b.canConnectMaxValue) &&
           a.dataType == b.dataType;
}

inline bool sameRegisterConfig(const I2CRegisterConfig &a, const I2CRegisterConfig &b)
{
    return a.registerAddr == b.registerAddr && a.length == b.length && sameNumber(a.multiplier, b.multiplier) &&
           sameNumber(a.offset, b.offset) && a.channelName == b.channelName && a.channel == b.channel &&
           a.sensorType == b.sensorType;
}

inline bool sameAnalogPinConfig(const AnalogPinConfig &a, const AnalogPinConfig &b)
{
    return a.pin == b.pin && sameNumber(a.multiplier, b.multiplier) && sameNumber(a.offset, b.offset) &&
           a.channelName == b.channelName && a.channel == b.channel && a.sensorType == b.sensorType;
}

inline bool sameSubSensorType(const SubSensorType &a, const SubSensorType &b)
{
    return a.name == b.name && a.displayName == b.displayName && a.isAnalog == b.isAnalog &&
           a.canCalibrate == b.canCalibrate &&
           a.validCalibrationPeriodDays == b.validCalibrationPeriodDays &&
           a.calibrationTimeInSeconds == b.calibrationTimeInSeconds && sameNumber(a.minValue, b.minValue) &&
           sameNumber(a.maxValue, b.maxValue) && sameNumber(a.limitMinValue, b.limitMinValue) &&
           sameNumber(a.limitMaxValue, b.limitMaxValue) && a.unit == b.unit && sameNumber(a.step, b.step) &&
           sameNumber(a.precisionPercent, b.precisionPercent) && sameNumber(a.precisionValue, b.precisionValue) &&
           a.createdAt == b.createdAt && a.updatedAt == b.updatedAt;
}

/** In order: the order of a sensor's channels is part of the model, and the sensor follows it. */
template <typename Config>
bool sameConfigs(const std::vector<Config> &a, const std::vector<Config> &b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++)
    {
        if (!sameRegisterConfig(a[i], b[i]))
        {
            return false;
        }
    }
    return true;
}

inline bool sameAnalogPinConfigs(const std::vector<AnalogPinConfig> &a, const std::vector<AnalogPinConfig> &b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++)
    {
        if (!sameAnalogPinConfig(a[i], b[i]))
        {
            return false;
        }
    }
    return true;
}

/**
 * Whatever the order: the sub-sensor types of a model are the types it is linked to, and the server
 * lists them out of that link, which keeps no order of its own. The same types listed in another
 * order are the same model. Each one is counted on both sides, so that a list holding one type
 * twice is not taken for a list holding it once beside another.
 */
inline bool sameSubSensorTypes(const std::vector<SubSensorType> &a, const std::vector<SubSensorType> &b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++)
    {
        size_t inA = 0;
        size_t inB = 0;
        for (size_t j = 0; j < a.size(); j++)
        {
            inA += sameSubSensorType(a[i], a[j]) ? 1 : 0;
            inB += sameSubSensorType(a[i], b[j]) ? 1 : 0;
        }
        if (inA != inB)
        {
            return false;
        }
    }
    return true;
}

inline bool sameSensorType(const SensorType &a, const SensorType &b)
{
    return a.uid == b.uid && a.name == b.name && a.displayName == b.displayName &&
           a.canScan == b.canScan && a.protocol == b.protocol && a.isSupported == b.isSupported &&
           a.createdAt == b.createdAt && a.updatedAt == b.updatedAt && a.i2cAddress == b.i2cAddress &&
           a.chipType == b.chipType && a.modbusDeviceAddress == b.modbusDeviceAddress &&
           a.baudRate == b.baudRate && a.modbusPingAddress == b.modbusPingAddress &&
           a.registerToStoreModbusDeviceAddress == b.registerToStoreModbusDeviceAddress &&
           a.writeRegistersCommand == b.writeRegistersCommand &&
           a.applyAddressAfterPowerCycle == b.applyAddressAfterPowerCycle &&
           a.priorityForScan == b.priorityForScan && a.id == b.id &&
           sameSubSensorTypes(a.sensorTypes, b.sensorTypes) &&
           sameConfigs(a.i2cRegisterConfig, b.i2cRegisterConfig) &&
           sameConfigs(a.modbusRegisterConfig, b.modbusRegisterConfig) &&
           sameAnalogPinConfigs(a.analogPinConfig, b.analogPinConfig);
}

} // namespace SensorTypeContent
