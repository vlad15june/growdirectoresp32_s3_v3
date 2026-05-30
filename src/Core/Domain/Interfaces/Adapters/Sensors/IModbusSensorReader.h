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
    
    virtual bool pingAddress(uint8_t deviceAddress, const ModbusRegisterConfig& config, int portIndex = 0) = 0;

    /**
     * @brief Write new address to Modbus sensor
     * @param currentAddress Current address of the sensor
     * @param newAddress New address to set
     * @param portIndex Port index for communication
     * @param storeRegister Per-model register the device address is written to (primary path)
     * @param writeCommand Per-model Modbus function code used for the primary write
     * @param pingRegister Per-model register read to verify a device responds
     * @return true if address was written successfully, false otherwise
     */
    virtual bool writeSensorAddress(int currentAddress, int newAddress, int portIndex,
                                    uint16_t storeRegister, uint8_t writeCommand, uint16_t pingRegister) = 0;

    /**
     * @brief Power on specific port
     * @param portIndex Port index to power on
     * @return true if state changed, false if already on
     */
    virtual bool powerOnPort(int portIndex) = 0;

    /**
     * @brief Power off specific port
     * @param portIndex Port index to power off
     * @return true if state changed, false if already off
     */
    virtual bool powerOffPort(int portIndex) = 0;
};

#endif