#include <HardwareSerial.h>
#include "IModbusRS485.h"
#include "../../../Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#ifndef MODBUS_RS485_H
#define MODBUS_RS485_H
class ModbusRS485 : public IModbusRS485
{
private:
    HardwareSerial &_port; // HardwareSerial object for RS-485 port
    int _controlPin;       // Control pin for receive/transmit switching
    const char *_portName; // Port name for debugging
    IArduinoAdapter &_arduino;
    uint32_t _config;      // Serial configuration
    int _rxPin;           // RX pin
    int _txPin;           // TX pin
    int _sensorPowerPin;  // Sensor power pin
    bool _isPoweredOn;    // Track power state

public:
    ModbusRS485(HardwareSerial &port, int controlPin, const char *portName, IArduinoAdapter &arduino, uint32_t config, int rxPin, int txPin, int sensorPowerPin)
        : _port(port), _controlPin(controlPin), _portName(portName), _arduino(arduino), _config(config), _rxPin(rxPin), _txPin(txPin), _sensorPowerPin(sensorPowerPin), _isPoweredOn(false)
    {
        _arduino.pinMode(_controlPin, _arduino.getPinModeOutput());
        _arduino.digitalWrite(_controlPin, _arduino.getDigitalLow());
        _arduino.pinMode(_sensorPowerPin, _arduino.getPinModeOutput());
        _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalLow());
    }

    // Initialize RS-485 port
    void begin(unsigned long baud) override
    {
        _port.begin(baud, _config, _rxPin, _txPin);
        Serial.printf("[%s] Initialized at %lu baud\n", _portName, baud);
    }

    uint16_t calculateCRC(uint8_t *data, uint8_t length) override
    {
        uint16_t crc = 0xFFFF;
        for (uint8_t i = 0; i < length; i++)
        {
            crc ^= data[i];
            for (uint8_t j = 0; j < 8; j++)
            {
                if (crc & 0x0001)
                {
                    crc >>= 1;
                    crc ^= 0xA001;
                }
                else
                {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    void sendData(uint8_t *data, uint8_t length) override
    {
        _arduino.digitalWrite(_controlPin, _arduino.getDigitalHigh());
        _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalHigh());
        _arduino.delay(500);

        Serial.printf("[%s] Sending request: ", _portName);
        for (int i = 0; i < length; i++)
        {
            Serial.printf("0x%02X ", data[i]);
            _port.write(data[i]);
        }
        _port.flush();
        Serial.println(" | Request sent.");

        _arduino.digitalWrite(_controlPin, _arduino.getDigitalLow());
        // _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalLow());
    }

    int readAvailable(uint8_t *buffer, size_t maxLength) override
    {
        int bytesRead = 0;

        while (_port.available() > 0 && bytesRead < maxLength)
        {
            buffer[bytesRead++] = _port.read();
        }

        return bytesRead;
    }

    int available() override
    {
        return _port.available();
    }

    const char *getPortName() const override
    {
        return _portName;
    }

    bool writeRegister(int slaveAddress, int registerAddress, uint16_t value) override
    {
        // Modbus function code 0x10 (Preset Multiple Registers) for writing single register
        uint8_t request[11] = {
            (uint8_t)slaveAddress,
            0x10,  // Function code: Preset Multiple Registers
            (uint8_t)(registerAddress >> 8),    // Register address high byte
            (uint8_t)(registerAddress & 0xFF),  // Register address low byte
            0x00, 0x01,  // Number of registers (1)
            0x02,        // Byte count (2 bytes for 1 register)
            (uint8_t)(value >> 8),      // Value high byte
            (uint8_t)(value & 0xFF)     // Value low byte
        };

        uint16_t crc = calculateCRC(request, 9);
        request[9] = crc & 0xFF;
        request[10] = (crc >> 8);

        sendData(request, sizeof(request));

        _arduino.delay(300);

        uint8_t response[8];
        int bytesRead = readAvailable(response, sizeof(response));
        
        Serial.printf("[%s] Write register response: %d bytes received\n", _portName, bytesRead);
        
        if (bytesRead >= 6 && response[0] == slaveAddress && response[1] == 0x10)
        {
            Serial.printf("[%s] Successfully wrote register 0x%04X = 0x%04X to device %d\n", 
                         _portName, registerAddress, value, slaveAddress);
            return true;
        }
        else if (bytesRead > 0 && response[1] >= 0x80)
        {
            Serial.printf("[%s] Exception response (0x%02X) when writing register to device %d\n", 
                         _portName, response[1], slaveAddress);
            return false;
        }

        Serial.printf("[%s] Failed to write register to device %d\n", _portName, slaveAddress);
        return false;
    }

    bool powerOnPort() override
    {
        if (_isPoweredOn)
        {
            return false;
        }
        _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalHigh());
        _arduino.delay(100);
        _isPoweredOn = true;
        Serial.printf("[%s] Port powered ON\n", _portName);
        return true;
    }

    bool powerOffPort() override
    {
        if (!_isPoweredOn)
        {
            return false;
        }
        _arduino.digitalWrite(_sensorPowerPin, _arduino.getDigitalLow());
        _arduino.delay(100);
        _isPoweredOn = false;
        Serial.printf("[%s] Port powered OFF\n", _portName);
        return true;
    }
};
#endif