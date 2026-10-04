#ifndef IMODBUS_RS485_H
#define IMODBUS_RS485_H

#include "../../../Core/Domain/Interfaces/Adapters/Sensors/PortPower.h"

// Interface for Modbus RS-485 communication
class IModbusRS485
{
public:
    virtual ~IModbusRS485() = default;
    
    // Initialize RS-485 port
    virtual void begin(unsigned long baud) = 0;
    
    // Send data via RS-485
    virtual void sendData(uint8_t *data, uint8_t length) = 0;
    
    // Read available data
    virtual int readAvailable(uint8_t *buffer, size_t maxLength) = 0;
    
    // Check for available data
    virtual int available() = 0;
    
    // Get port name
    virtual const char *getPortName() const = 0;
    
    // Calculate CRC16 (Modbus)
    virtual uint16_t calculateCRC(uint8_t *data, uint8_t length) = 0;

    // Write register using Modbus function code 0x10 (Preset Multiple Registers)
    virtual bool writeRegister(int slaveAddress, int registerAddress, uint16_t value) = 0;

    // Write single register using Modbus function code 0x06 (Write Single Register)
    virtual bool writeSingleRegister(int slaveAddress, int registerAddress, uint16_t value) = 0;

    // Power control methods
    // Returns true if state changed, false if already in that state
    virtual bool powerOnPort() = 0;
    virtual bool powerOffPort() = 0;

    // What this port's power pin has done, and when - including a pin sendData() raised itself
    virtual PortPower powerRecord() const = 0;

    // The UART this port's frames go out on, as an identity and nothing more. Two ports that
    // answer the same are one electrical line: a frame put on either is heard by whatever is
    // plugged into both, and only a port's power keeps what is behind it off that line.
    virtual const void *wire() const = 0;
};

#endif
