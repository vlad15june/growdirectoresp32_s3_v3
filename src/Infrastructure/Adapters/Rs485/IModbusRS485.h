#ifndef IMODBUS_RS485_H
#define IMODBUS_RS485_H

#include <cstdint>
#include <cstddef>

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
};

#endif
