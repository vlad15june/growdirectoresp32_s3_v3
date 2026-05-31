#ifndef RAW_BUS_ACCESS_ADAPTER_H
#define RAW_BUS_ACCESS_ADAPTER_H

#include <vector>
#include <cstdint>
#include <cstddef>
#include "IModbusRS485.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRawBusAccess.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

class RawBusAccessAdapter : public IRawBusAccess
{
private:
    std::vector<IModbusRS485*> _ports;
    IArduinoAdapter &_arduino;

    bool readWithTimeout(IModbusRS485 *port, unsigned long timeoutMs, std::vector<uint8_t> &out)
    {
        unsigned long start = _arduino.millis();
        while (_arduino.millis() - start < timeoutMs)
        {
            if (port->available() > 0)
            {
                _arduino.delay(20);
                uint8_t buffer[256];
                int bytesRead = port->readAvailable(buffer, sizeof(buffer));
                if (bytesRead > 0)
                {
                    out.assign(buffer, buffer + bytesRead);
                    return true;
                }
            }
            _arduino.delay(5);
        }
        return false;
    }

public:
    RawBusAccessAdapter(const std::vector<IModbusRS485*> &ports, IArduinoAdapter &arduino)
        : _ports(ports), _arduino(arduino) {}

    bool modbusRequest(int channel, const uint8_t *pduNoCrc, size_t len, bool expectResponse, unsigned long timeoutMs, std::vector<uint8_t> &out) override
    {
        if (channel < 0 || channel >= (int)_ports.size())
        {
            return false;
        }
        if (pduNoCrc == nullptr || len == 0 || len > 253)
        {
            return false;
        }

        IModbusRS485 *port = _ports[channel];

        uint8_t frame[256];
        for (size_t i = 0; i < len; i++)
        {
            frame[i] = pduNoCrc[i];
        }

        uint16_t crc = port->calculateCRC(frame, (uint8_t)len);
        frame[len] = crc & 0xFF;
        frame[len + 1] = (crc >> 8) & 0xFF;

        port->sendData(frame, (uint8_t)(len + 2));

        if (!expectResponse)
        {
            return true;
        }

        return readWithTimeout(port, timeoutMs, out);
    }

    bool serialWrite(int channel, const uint8_t *bytes, size_t len, bool expectResponse, unsigned long timeoutMs, std::vector<uint8_t> &out) override
    {
        if (channel < 0 || channel >= (int)_ports.size())
        {
            return false;
        }
        if (bytes == nullptr || len == 0 || len > 255)
        {
            return false;
        }

        IModbusRS485 *port = _ports[channel];

        port->sendData(const_cast<uint8_t *>(bytes), (uint8_t)len);

        if (!expectResponse)
        {
            return true;
        }

        return readWithTimeout(port, timeoutMs, out);
    }
};

#endif
