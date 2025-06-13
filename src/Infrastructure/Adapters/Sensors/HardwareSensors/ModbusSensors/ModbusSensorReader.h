#ifndef MODBUS_SENSOR_READER_H
#define MODBUS_SENSOR_READER_H

#include <vector>
#include <string>
#include "../../../../../Core/Domain/Data/Entities/Sensor/ModbusRegisterConfig.h"
#include "../../../../../Core/Domain/Interfaces/Adapters/Sensors/IModbusSensorReader.h"
#include "../../../Rs485/IModbusRS485.h"
#include "../../../../../Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

class ModbusSensorReader : public IModbusSensorReader
{
private:
    IModbusRS485 &_modbus;
    IArduinoAdapter &_arduino;

public:
    ModbusSensorReader(IModbusRS485 &modbus, IArduinoAdapter &arduino)
        : _modbus(modbus), _arduino(arduino) {}

    bool readRegister(uint8_t deviceAddress, const ModbusRegisterConfig &config, float &outValue) override
    {
        uint8_t request[8] = {
            deviceAddress,
            config.functionCode,
            (uint8_t)(config.startRegister >> 8),
            (uint8_t)(config.startRegister & 0xFF),
            (uint8_t)(config.numRegisters >> 8),
            (uint8_t)(config.numRegisters & 0xFF)
        };

        uint16_t crc = _modbus.calculateCRC(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8);

        _modbus.sendData(request, sizeof(request));

        _arduino.delay(300);

        uint8_t response[64];
        int bytesRead = _modbus.readAvailable(response, sizeof(response));

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
                return true;
            }
        }

        return false;
    }
};

#endif
