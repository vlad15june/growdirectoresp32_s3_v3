#ifndef MODBUS_SENSOR_READER_H
#define MODBUS_SENSOR_READER_H

#include <vector>
#include <string>
#include "../../../../../Core/Domain/Data/Entities/Sensor/ModbusRegisterConfig.h"
#include "../../../../../Core/Domain/Interfaces/Adapters/Sensors/IModbusSensorReader.h"
#include "../../../Rs485/IModbusRS485.h"
#include "../../../../../Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "../../../../../Core/Domain/Interfaces/ILogger.h"
#include "../../../../../Core/Domain/Interfaces/IDebugSystem.h"

class ModbusSensorReader : public IModbusSensorReader
{
private:
    std::vector<IModbusRS485*> _modbusPorts;
    IArduinoAdapter &_arduino;
    ILogger* _logger;
    IDebugSystem* _debugSystem;

public:
    ModbusSensorReader(const std::vector<IModbusRS485*>& modbusPorts, IArduinoAdapter &arduino, ILogger* logger, IDebugSystem* debugSystem)
        : _modbusPorts(modbusPorts), _arduino(arduino), _logger(logger), _debugSystem(debugSystem) {}

    void init(unsigned long baud = 9600)
    {
        for (auto* modbus : _modbusPorts)
        {
            modbus->begin(baud);
        }
    }

    bool readRegister(uint8_t deviceAddress, const ModbusRegisterConfig &config, float &outValue, int portIndex = 0) override
    {
        if (portIndex < 0 || portIndex >= _modbusPorts.size())
        {
            log("ModbusSensorReader: Invalid port index %d", portIndex);
            return false;
        }

        IModbusRS485* modbus = _modbusPorts[portIndex];

        uint8_t request[8] = {
            deviceAddress,
            config.functionCode,
            (uint8_t)(config.startRegister >> 8),
            (uint8_t)(config.startRegister & 0xFF),
            (uint8_t)(config.numRegisters >> 8),
            (uint8_t)(config.numRegisters & 0xFF)
        };

        uint16_t crc = modbus->calculateCRC(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8);

        modbus->sendData(request, sizeof(request));

        _arduino.delay(300);

        uint8_t response[64];
        int bytesRead = modbus->readAvailable(response, sizeof(response));
        log("ModbusSensorReader: Read %d bytes from device %d on port %d", bytesRead, deviceAddress, portIndex);

        if (bytesRead >= 5 && response[1] == config.functionCode)
        {
            uint8_t byteCount = response[2];
            if (byteCount == config.numRegisters * 2 && bytesRead >= 3 + byteCount + 2)
            {
                uint16_t rawValue = 0;
                if (config.numRegisters == 1)
                {
                    rawValue = (response[3] << 8) | response[4];
                }
                else
                {
                    rawValue = (response[3] << 8) | response[4];
                }

                outValue = rawValue * config.multiplier + config.offset;
                log("ModbusSensorReader: Read value %.2f from device %d on port %d", outValue, deviceAddress, portIndex);
                return true;
            }
        }

        return false;
    }

private:
    void log(const char* format, ...)
    {
        if (_debugSystem && _logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::SETUP_SENSOR_DEBUG, buffer);
        }
    }
};

#endif
