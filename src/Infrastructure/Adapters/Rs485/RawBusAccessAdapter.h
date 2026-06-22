#ifndef RAW_BUS_ACCESS_ADAPTER_H
#define RAW_BUS_ACCESS_ADAPTER_H

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include "IModbusRS485.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRawBusAccess.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class RawBusAccessAdapter : public IRawBusAccess
{
private:
    std::vector<IModbusRS485*> _ports;
    IArduinoAdapter &_arduino;
    ILogger* _logger;
    IDebugSystem* _debugSystem;

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

    void log(const char* format, ...)
    {
        if (_debugSystem && _logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::RAW_MODBUS_DEBUG);
            if (feature)
            {
                _logger->println(DebugFeatures::RAW_MODBUS_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                _logger->println(DebugFeatures::RAW_MODBUS_DEBUG, buffer);
            }
        }
    }

    void logTxFrame(int channel, const uint8_t* frame, size_t n)
    {
        char hex[3 * 256 + 1];
        size_t pos = 0;
        for (size_t i = 0; i < n && pos + 3 < sizeof(hex); i++)
        {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", frame[i]);
        }
        log("RawBusAccessAdapter: TX ch%d (%d bytes): %s", channel, (int)n, hex);
    }

public:
    RawBusAccessAdapter(const std::vector<IModbusRS485*> &ports, IArduinoAdapter &arduino, ILogger* logger, IDebugSystem* debugSystem)
        : _ports(ports), _arduino(arduino), _logger(logger), _debugSystem(debugSystem) {}

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

        logTxFrame(channel, frame, len + 2);
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

        logTxFrame(channel, bytes, len);
        port->sendData(const_cast<uint8_t *>(bytes), (uint8_t)len);

        if (!expectResponse)
        {
            return true;
        }

        return readWithTimeout(port, timeoutMs, out);
    }
};

#endif
