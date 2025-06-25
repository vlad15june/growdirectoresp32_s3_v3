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

public:
    ModbusRS485(HardwareSerial &port, int controlPin, const char *portName, IArduinoAdapter &arduino, uint32_t config, int rxPin, int txPin)
        : _port(port), _controlPin(controlPin), _portName(portName), _arduino(arduino), _config(config), _rxPin(rxPin), _txPin(txPin)
    {
        _arduino.pinMode(_controlPin, _arduino.getPinModeOutput());
        _arduino.digitalWrite(_controlPin, _arduino.getDigitalLow());
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
        _arduino.delay(100);

        Serial.printf("[%s] Sending request: ", _portName);
        for (int i = 0; i < length; i++)
        {
            Serial.printf("0x%02X ", data[i]);
            _port.write(data[i]);
        }
        _port.flush();
        Serial.println(" | Request sent.");

        _arduino.digitalWrite(_controlPin, _arduino.getDigitalLow());
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
};
#endif