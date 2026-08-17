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
     * @param powerCycleToApply true for models that apply the new address only after
     *        a power cycle (e.g. RK520-02): on a failed ping the port is power-cycled
     *        and the ping retried. false keeps the previous behaviour (no power cycle).
     * @return true if address was written successfully, false otherwise
     */
    virtual bool writeSensorAddress(int currentAddress, int newAddress, int portIndex,
                                    uint16_t storeRegister, uint8_t writeCommand, uint16_t pingRegister,
                                    bool powerCycleToApply = false) = 0;

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

    /**
     * @brief Write a single holding register (FC06)
     * @param address Device address
     * @param reg Register address
     * @param value Value to write
     * @param portIndex Port index for communication
     * @return true if the write was issued successfully, false otherwise
     */
    virtual bool writeHoldingRegister(uint8_t address, uint16_t reg, uint16_t value, int portIndex) = 0;

    /**
     * @brief Read a single register (FC03), returning the raw 16-bit word with no scaling
     * @param address Device address
     * @param reg Register address
     * @param outValue Raw register value on success
     * @param portIndex Port index for communication
     * @return true if a valid response was parsed, false otherwise
     */
    virtual bool readHoldingRegister(uint8_t address, uint16_t reg, uint16_t &outValue, int portIndex) = 0;
};

#endif