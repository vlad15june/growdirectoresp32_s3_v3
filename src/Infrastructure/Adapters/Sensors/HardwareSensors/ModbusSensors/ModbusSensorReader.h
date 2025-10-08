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
        
        #ifdef DEBUG_LOGGING_CODE_ON
        if (bytesRead > 0 && _debugSystem && _logger) {
            char hexBuffer[256] = "";
            for (int i = 0; i < bytesRead && i < 32; i++) {
                char temp[4];
                snprintf(temp, sizeof(temp), "%02X ", response[i]);
                strcat(hexBuffer, temp);
            }
            log("ModbusSensorReader: Raw bytes: %s", hexBuffer);
        }
        #endif

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

    bool pingAddress(uint8_t deviceAddress, int portIndex = 0) override
    {
        if (portIndex < 0 || portIndex >= _modbusPorts.size())
        {
            log("ModbusSensorReader: Invalid port index %d for ping", portIndex);
            return false;
        }

        IModbusRS485* modbus = _modbusPorts[portIndex];

        // Create a simple read holding registers request for register 0x0000
        uint8_t request[8] = {
            deviceAddress,
            0x03,  // Function code: Read Holding Registers
            0x00, 0x00,  // Starting register address (0x0000)
            0x00, 0x01   // Number of registers to read (1)
        };

        uint16_t crc = modbus->calculateCRC(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8);

        modbus->sendData(request, sizeof(request));

        _arduino.delay(300);

        uint8_t response[64];
        int bytesRead = modbus->readAvailable(response, sizeof(response));
        
        log("ModbusSensorReader: Ping device %d on port %d - received %d bytes", deviceAddress, portIndex, bytesRead);
        
        // Check if we got a valid response (at least 5 bytes and correct function code)
        if (bytesRead >= 5 && response[0] == deviceAddress && response[1] == 0x03)
        {
            log("ModbusSensorReader: Device %d responded to ping on port %d", deviceAddress, portIndex);
            return true;
        }
        else if (bytesRead > 0 && response[1] >= 0x80)
        {
            // Exception response (function code + 0x80), but device is responding
            log("ModbusSensorReader: Device %d sent exception response (0x%02X) but is alive on port %d", 
                deviceAddress, response[1], portIndex);
            return true;
        }

        log("ModbusSensorReader: Device %d did not respond to ping on port %d", deviceAddress, portIndex);
        return false;
    }

    bool writeSensorAddress(int currentAddress, int newAddress, int portIndex) override
    {
        return false;
        if (portIndex < 0 || portIndex >= _modbusPorts.size())
        {
            log("ModbusSensorReader: Invalid port index %d for address write", portIndex);
            return false;
        }

        IModbusRS485* modbus = _modbusPorts[portIndex];

        // Power off all ports first to avoid interference
        powerOffAllPorts();
        _arduino.delay(500);

        // Power on only the target port
        modbus->powerOnPort();
        _arduino.delay(1000); // Wait for sensor to stabilize

        // Write new address to register 0x0012 using function code 0x10
        bool success = modbus->writeRegister(currentAddress, 0x0012, (uint16_t)newAddress);

        if (success)
        {
            log("ModbusSensorReader: Successfully wrote new address %d to device %d on port %d", 
                newAddress, currentAddress, portIndex);
        }
        else
        {
            log("ModbusSensorReader: Failed to write new address %d to device %d on port %d", 
                newAddress, currentAddress, portIndex);
        }

        return success;
    }

    void powerOnPort(int portIndex) override
    {
        if (portIndex >= 0 && portIndex < _modbusPorts.size())
        {
            _modbusPorts[portIndex]->powerOnPort();
            log("ModbusSensorReader: Powered ON port %d", portIndex);
        }
        else
        {
            log("ModbusSensorReader: Invalid port index %d for power ON", portIndex);
        }
    }

    void powerOffPort(int portIndex) override
    {
        if (portIndex >= 0 && portIndex < _modbusPorts.size())
        {
            _modbusPorts[portIndex]->powerOffPort();
            log("ModbusSensorReader: Powered OFF port %d", portIndex);
        }
        else
        {
            log("ModbusSensorReader: Invalid port index %d for power OFF", portIndex);
        }
    }

private:
    void powerOffAllPorts()
    {
        for (int i = 0; i < _modbusPorts.size(); i++)
        {
            powerOffPort(i);
        }
        log("ModbusSensorReader: All ports powered OFF");
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

            _logger->println(DebugFeatures::SETUP_SENSOR_DEBUG, buffer);
        }
    }
};

#endif
