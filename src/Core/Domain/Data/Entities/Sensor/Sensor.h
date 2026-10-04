#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "ModbusRegisterConfig.h"
#include "I2CRegisterConfig.h"
#include "SensorType.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"
#include "constants.h"

namespace SensorLimits {

/**
 * @brief Sensor ports the module has.
 *
 * The board's four RS-485 / analog ports. The number is the board's, and the board configuration
 * (ModuleConfig::SENSOR_PIN_COUNT) is an Infrastructure header this layer may not include, so it is
 * repeated here with its source named rather than reached for.
 */
static const size_t SENSOR_PORTS = 4;

/**
 * @brief Devices one port can carry, from the Modbus address range of a sensor line.
 *
 * An analog or I2C port carries one sensor, so this is the wider of the two and the one that
 * bounds a port.
 */
static const size_t DEVICES_PER_PORT = MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1;

/**
 * @brief How many sensors one module keeps, at most.
 *
 * A declared capacity: sensors are read out of the registry one at a time, so nothing in the
 * parsing decides how many of them there can be, and something has to.
 *
 * The number is read off the product - every port of the module carrying as many devices as its
 * address range holds. It is a ceiling above every narrower reading of the same model rather than a
 * target: the address allocator draws from that range module-wide rather than per port, which makes
 * the sensors it can address by itself a quarter of this; a sensor that arrives with an address of
 * its own is not bound by the allocator, which is why the ceiling is the wider figure. Virtual
 * channels take no slot here at all - they are computed channels of a stored sensor, published in
 * the module structure, never rows of the registry.
 *
 * A registry that runs past it is read up to it, and the sensors past it are refused and counted:
 * the alternative - reading a longer registry into a module that cannot address it - is a set of
 * sensors no port can answer for.
 */
static const size_t MAX_SENSORS_PER_MODULE = SENSOR_PORTS * DEVICES_PER_PORT;

} // namespace SensorLimits

/**
 * @brief Base sensor entity class
 */
class SensorEntity {
public:
    std::string uid;
    std::string sensorType;
    std::string model;
    std::string protocol;
    std::string createdAt;
    int pin;
    bool isActive;
    bool isError;
    SensorStatusEnum status;
    
    int modbusDeviceAddress;
    int baudRate;
    uint16_t modbusPingAddress = 0;
    uint16_t registerToStoreModbusDeviceAddress = 0;
    uint8_t writeRegistersCommand = 0;
    // copied from the sensor model: true when the device applies a newly written
    // Modbus address only after a power cycle (e.g. RK520-02)
    bool applyAddressAfterPowerCycle = false;
    std::vector<ModbusRegisterConfig> modbusRegisterConfigs;
    
    std::string i2cAddress;
    std::string chipType;
    std::vector<I2CRegisterConfig> i2cRegisterConfigs;
    
    float currentValue;
    uint32_t lastReadTime;
    
    // Analog sensor calibration values
    float multiplier = 1.0f;
    float offset = 0.0f;

    int sensorTypeId = 0;
    std::string sensorTypeUid;

    SensorEntity() : isActive(true), isError(false), modbusDeviceAddress(1), baudRate(9600), 
               currentValue(0.0f), lastReadTime(0), multiplier(1.0f), offset(0.0f), sensorTypeId(0), 
               status(SENSOR_STATUS_NEW) {}

    bool isModbusSensor() const { return protocol == "ModBus"; }
    bool isI2CSensor() const { return protocol == "I2C"; }
    bool isAtlasSensor() const { return model.find("atlas_") == 0; }
    bool isMultiChannel() const { return !modbusRegisterConfigs.empty() || !i2cRegisterConfigs.empty(); }

    /**
     * @brief The registers this sensor is read by, as one number.
     *
     * Covers what decides the value a channel is kept with and the channel it is kept under: where
     * each register is read, how its value is scaled, and its channel number. Names, types and the
     * ranges the discovery matches by are left out - they do not change what a register reads. The
     * types do decide which computed channels a sensor has (VpdChannelUseCase pairs air_temp with
     * humidity); those are worked out from the sensor as it is when a reading is applied, and
     * compared on their own where a change of them matters (SyncSensorTypesUseCase). Two sensors
     * read by the same registers have the same number; FNV-1a over the fields, in order.
     */
    uint32_t registerFingerprint() const
    {
        uint32_t hash = 2166136261u;
        const uint32_t modbusCount = static_cast<uint32_t>(modbusRegisterConfigs.size());
        mixIntoFingerprint(hash, &modbusCount, sizeof(modbusCount));
        for (size_t i = 0; i < modbusRegisterConfigs.size(); i++)
        {
            const ModbusRegisterConfig& config = modbusRegisterConfigs[i];
            const uint8_t dataType = static_cast<uint8_t>(config.dataType);
            mixIntoFingerprint(hash, &config.functionCode, sizeof(config.functionCode));
            mixIntoFingerprint(hash, &config.startRegister, sizeof(config.startRegister));
            mixIntoFingerprint(hash, &config.numRegisters, sizeof(config.numRegisters));
            mixIntoFingerprint(hash, &dataType, sizeof(dataType));
            mixIntoFingerprint(hash, &config.multiplier, sizeof(config.multiplier));
            mixIntoFingerprint(hash, &config.offset, sizeof(config.offset));
            mixIntoFingerprint(hash, &config.channel, sizeof(config.channel));
        }
        const uint32_t i2cCount = static_cast<uint32_t>(i2cRegisterConfigs.size());
        mixIntoFingerprint(hash, &i2cCount, sizeof(i2cCount));
        for (size_t i = 0; i < i2cRegisterConfigs.size(); i++)
        {
            const I2CRegisterConfig& config = i2cRegisterConfigs[i];
            mixIntoFingerprint(hash, &config.registerAddr, sizeof(config.registerAddr));
            mixIntoFingerprint(hash, &config.length, sizeof(config.length));
            mixIntoFingerprint(hash, &config.multiplier, sizeof(config.multiplier));
            mixIntoFingerprint(hash, &config.offset, sizeof(config.offset));
            mixIntoFingerprint(hash, &config.channel, sizeof(config.channel));
        }
        return hash;
    }

private:
    static void mixIntoFingerprint(uint32_t& hash, const void* data, size_t size)
    {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; i++)
        {
            hash ^= bytes[i];
            hash *= 16777619u;
        }
    }
};
