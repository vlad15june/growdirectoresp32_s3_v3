#ifndef MODBUS_SENSOR_READER_H
#define MODBUS_SENSOR_READER_H

#include <vector>
#include <string>
#include <cstring>
#include <cstdarg>
#include <cstdio>
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

    static constexpr uint8_t kWildcardSlave = 0xFF;
    static constexpr uint8_t kWs12sQueryFc = 0x03;
    static constexpr uint16_t kWs12sQueryReg = 0x0100;
    static constexpr uint8_t kWs12sWriteFc = 0x06;
    static constexpr uint16_t kWs12sWriteReg = 0x0100;
    static constexpr uint8_t kCwywQueryFc = 0x6E;
    static constexpr uint16_t kCwywQueryReg = 0x0012;
    static constexpr uint8_t kCwywWriteFc = 0x10;
    static constexpr uint16_t kCwywWriteReg = 0x0018;

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

    bool pingAddress(uint8_t deviceAddress, const ModbusRegisterConfig& config, int portIndex = 0) override
    {
        if (portIndex < 0 || portIndex >= _modbusPorts.size())
        {
            log("ModbusSensorReader: Invalid port index %d for ping", portIndex);
            return false;
        }

        IModbusRS485* modbus = _modbusPorts[portIndex];

        for (int attempt = 1; attempt <= 3; attempt++)
        {
            log("ModbusSensorReader: Ping attempt %d/3 for device %d on port %d", attempt, deviceAddress, portIndex);

            uint8_t request[8] = {
                deviceAddress,
                config.functionCode,
                (uint8_t)(config.startRegister >> 8),
                (uint8_t)(config.startRegister & 0xFF),
                0x00, 0x01   // Number of registers to read (1)
            };

            uint16_t crc = modbus->calculateCRC(request, 6);
            request[6] = crc & 0xFF;
            request[7] = (crc >> 8);

            char sendHexBuffer[256] = "";
            for (int i = 0; i < (int)sizeof(request); i++) {
                char temp[4];
                snprintf(temp, sizeof(temp), "%02X ", request[i]);
                strcat(sendHexBuffer, temp);
            }
            log("ModbusSensorReader: Ping sending bytes: %s", sendHexBuffer);

            modbus->sendData(request, sizeof(request));

            unsigned long responseDelay = 200 + (attempt - 1) * 500;
            log("ModbusSensorReader: Waiting %lu ms for response (attempt %d)", responseDelay, attempt);
            _arduino.delay(responseDelay);

            uint8_t response[64];
            int bytesRead = modbus->readAvailable(response, sizeof(response));
            
            log("ModbusSensorReader: Ping device %d on port %d - received %d bytes", deviceAddress, portIndex, bytesRead);
            
            if (bytesRead > 0) {
                char hexBuffer[256] = "";
                for (int i = 0; i < bytesRead && i < 32; i++) {
                    char temp[4];
                    snprintf(temp, sizeof(temp), "%02X ", response[i]);
                    strcat(hexBuffer, temp);
                }
                log("ModbusSensorReader: Ping response bytes: %s", hexBuffer);
            }
            
            if (bytesRead >= 5 && response[0] == deviceAddress && response[1] == config.functionCode)
            {
                log("ModbusSensorReader: Device %d responded to ping on port %d (attempt %d)", deviceAddress, portIndex, attempt);
                return true;
            }
            else if (bytesRead > 0 && response[1] >= 0x80)
            {
                log("ModbusSensorReader: Device %d sent exception response (0x%02X) but is alive on port %d (attempt %d)", 
                    deviceAddress, response[1], portIndex, attempt);
                return true;
            }

            if (attempt < 3)
            {
                _arduino.delay(10);
            }
        }

        log("ModbusSensorReader: Device %d did not respond to ping on port %d after 3 attempts", deviceAddress, portIndex);
        return false;
    }

    bool writeSensorAddress(int currentAddress, int newAddress, int portIndex,
                            uint16_t storeRegister, uint8_t writeCommand, uint16_t pingRegister) override
    {
        if (portIndex < 0 || portIndex >= _modbusPorts.size())
        {
            log("ModbusSensorReader: Invalid port index %d for address write", portIndex);
            return false;
        }

        IModbusRS485* modbus = _modbusPorts[portIndex];

        bool targetJustTurnedOn = modbus->powerOnPort();

        for (int i = 0; i < (int)_modbusPorts.size(); i++)
        {
            if (i != portIndex)
            {
                _modbusPorts[i]->powerOffPort();
            }
        }

        if (targetJustTurnedOn)
        {
            log("ModbusSensorReader: Target port %d was off, warmup 10s", portIndex);
            _arduino.delay(10000);
        }
        else
        {
            log("ModbusSensorReader: Target port %d was already on, skipping warmup delay", portIndex);
        }

        ModbusRegisterConfig probe;
        probe.functionCode = 0x03;
        probe.startRegister = pingRegister;
        probe.numRegisters = 1;

        if (writeCommand == 16 || writeCommand == 6)
        {
            log("ModbusSensorReader: Primary write - FC%d to register 0x%04X on current address %d (new address %d) on port %d",
                writeCommand, storeRegister, currentAddress, newAddress, portIndex);

            if (writeCommand == 16)
            {
                modbus->writeRegister(currentAddress, storeRegister, (uint16_t)newAddress);
            }
            else
            {
                modbus->writeSingleRegister(currentAddress, storeRegister, (uint16_t)newAddress);
            }

            _arduino.delay(300);

            log("ModbusSensorReader: Verifying primary write - probing new address %d (ping register 0x%04X) on port %d",
                newAddress, pingRegister, portIndex);
            if (pingAddress((uint8_t)newAddress, probe, portIndex))
            {
                log("ModbusSensorReader: Primary write CONFIRMED - device responds at new address %d on port %d", newAddress, portIndex);
                return true;
            }

            log("ModbusSensorReader: Primary write at known address %d not confirmed, retrying FC%d to register 0x%04X via broadcast on port %d",
                currentAddress, writeCommand, storeRegister, portIndex);

            if (writeCommand == 16)
            {
                modbus->writeRegister(0x00, storeRegister, (uint16_t)newAddress);
            }
            else
            {
                modbus->writeSingleRegister(0x00, storeRegister, (uint16_t)newAddress);
            }

            _arduino.delay(300);

            log("ModbusSensorReader: Verifying primary broadcast write - probing new address %d (ping register 0x%04X) on port %d",
                newAddress, pingRegister, portIndex);
            if (pingAddress((uint8_t)newAddress, probe, portIndex))
            {
                log("ModbusSensorReader: Primary broadcast write CONFIRMED - device responds at new address %d on port %d", newAddress, portIndex);
                return true;
            }

            log("ModbusSensorReader: Primary write not confirmed at new address %d on port %d, falling back to legacy broadcast sequence",
                newAddress, portIndex);
        }
        else
        {
            log("ModbusSensorReader: writeRegistersCommand %d is not a write code (6/16), skipping primary write and using legacy broadcast sequence",
                writeCommand);
        }

        {
            uint8_t discoveredAddr = 0;

            log("ModbusSensorReader: FF_FALLBACK ws12s query slave=0xFF fc=0x%02X reg=0x%04X on port %d",
                kWs12sQueryFc, kWs12sQueryReg, portIndex);
            if (queryAddressViaWildcard(kWs12sQueryFc, kWs12sQueryReg, portIndex, discoveredAddr))
            {
                log("ModbusSensorReader: FF_FALLBACK ws12s discovered addr=%d on port %d, writing FC%d reg=0x%04X value=%d",
                    discoveredAddr, portIndex, kWs12sWriteFc, kWs12sWriteReg, newAddress);
                modbus->writeSingleRegister(discoveredAddr, kWs12sWriteReg, (uint16_t)newAddress);
                _arduino.delay(300);
                if (pingAddress((uint8_t)newAddress, probe, portIndex))
                {
                    log("ModbusSensorReader: FF_FALLBACK ws12s CONFIRMED - device responds at new address %d on port %d",
                        newAddress, portIndex);
                    return true;
                }
                log("ModbusSensorReader: FF_FALLBACK ws12s write_unconfirmed discoveredAddr=%d newAddr=%d port=%d",
                    discoveredAddr, newAddress, portIndex);
            }

            log("ModbusSensorReader: FF_FALLBACK cwyw query slave=0xFF fc=0x%02X reg=0x%04X on port %d",
                kCwywQueryFc, kCwywQueryReg, portIndex);
            if (queryAddressViaWildcard(kCwywQueryFc, kCwywQueryReg, portIndex, discoveredAddr))
            {
                log("ModbusSensorReader: FF_FALLBACK cwyw discovered addr=%d on port %d, writing FC%d reg=0x%04X value=%d",
                    discoveredAddr, portIndex, kCwywWriteFc, kCwywWriteReg, newAddress);
                modbus->writeRegister(discoveredAddr, kCwywWriteReg, (uint16_t)newAddress);
                _arduino.delay(300);
                if (pingAddress((uint8_t)newAddress, probe, portIndex))
                {
                    log("ModbusSensorReader: FF_FALLBACK cwyw CONFIRMED - device responds at new address %d on port %d",
                        newAddress, portIndex);
                    return true;
                }
                log("ModbusSensorReader: FF_FALLBACK cwyw write_unconfirmed discoveredAddr=%d newAddr=%d port=%d",
                    discoveredAddr, newAddress, portIndex);
            }

            log("ModbusSensorReader: FF_FALLBACK both_stages_failed port=%d, entering legacy broadcast loop",
                portIndex);
        }

        for (int attempt = 1; attempt <= 3; attempt++)
        {
            log("ModbusSensorReader: Broadcast FC06 write attempt %d/3 for new address %d on port %d",
                attempt, newAddress, portIndex);
            modbus->writeSingleRegister(0x00, 0x0000, (uint16_t)newAddress);

            _arduino.delay(300);

            log("ModbusSensorReader: Verifying address change after FC06 - probing new address %d on port %d (attempt %d/3)",
                newAddress, portIndex, attempt);
            if (pingAddress((uint8_t)newAddress, probe, portIndex))
            {
                log("ModbusSensorReader: Address change CONFIRMED via FC06 on attempt %d/3 - device responds at new address %d on port %d",
                    attempt, newAddress, portIndex);
                return true;
            }

            log("ModbusSensorReader: Broadcast FC16 write attempt %d/3 for new address %d on port %d",
                attempt, newAddress, portIndex);
            modbus->writeRegister(0x00, 0x0001, (uint16_t)newAddress);

            _arduino.delay(300);

            log("ModbusSensorReader: Verifying address change after FC16 - probing new address %d on port %d (attempt %d/3)",
                newAddress, portIndex, attempt);
            if (pingAddress((uint8_t)newAddress, probe, portIndex))
            {
                log("ModbusSensorReader: Address change CONFIRMED via FC16 on attempt %d/3 - device responds at new address %d on port %d",
                    attempt, newAddress, portIndex);
                return true;
            }

            if (attempt < 3)
            {
                _arduino.delay(500);
            }
        }

        log("ModbusSensorReader: New address %d silent on port %d after all write attempts, probing old address %d",
            newAddress, portIndex, currentAddress);
        if (pingAddress((uint8_t)currentAddress, probe, portIndex))
        {
            log("ModbusSensorReader: Address change FAILED - device still answers at old address %d on port %d (wanted %d), repository will NOT be updated",
                currentAddress, portIndex, newAddress);
            return false;
        }

        log("ModbusSensorReader: !!!!! CRITICAL !!!!! Device on port %d is UNREACHABLE at both old address %d and new address %d. Sensor likely stuck on an unknown address - MANUAL RECOVERY REQUIRED (power cycle / re-scan / factory reset). Returning false.",
            portIndex, currentAddress, newAddress);
        return false;
    }

    bool powerOnPort(int portIndex) override
    {
        if (portIndex >= 0 && portIndex < _modbusPorts.size())
        {
            bool changed = _modbusPorts[portIndex]->powerOnPort();
            if (changed)
            {
                log("ModbusSensorReader: Powered ON port %d", portIndex);
            }
            return changed;
        }
        else
        {
            log("ModbusSensorReader: Invalid port index %d for power ON", portIndex);
            return false;
        }
    }

    bool powerOffPort(int portIndex) override
    {
        if (portIndex >= 0 && portIndex < _modbusPorts.size())
        {
            bool changed = _modbusPorts[portIndex]->powerOffPort();
            if (changed)
            {
                log("ModbusSensorReader: Powered OFF port %d", portIndex);
            }
            return changed;
        }
        else
        {
            log("ModbusSensorReader: Invalid port index %d for power OFF", portIndex);
            return false;
        }
    }

private:
    bool queryAddressViaWildcard(uint8_t fc, uint16_t reg, int portIndex, uint8_t& outAddress)
    {
        if (portIndex < 0 || portIndex >= (int)_modbusPorts.size())
        {
            return false;
        }
        IModbusRS485* modbus = _modbusPorts[portIndex];

        uint8_t req[8];
        req[0] = kWildcardSlave;
        req[1] = fc;
        req[2] = (uint8_t)(reg >> 8);
        req[3] = (uint8_t)(reg & 0xFF);
        req[4] = 0x00;
        req[5] = 0x01;
        uint16_t crc = modbus->calculateCRC(req, 6);
        req[6] = (uint8_t)(crc & 0xFF);
        req[7] = (uint8_t)((crc >> 8) & 0xFF);

        modbus->sendData(req, 8);

        uint8_t buf[16];
        int n = 0;
        for (int attempt = 1; attempt <= 3; attempt++)
        {
            _arduino.delay(20);
            n = modbus->readAvailable(buf, sizeof(buf));
            if (n >= 5)
            {
                break;
            }
        }

        if (n < 5)
        {
            log("ModbusSensorReader: FF_FALLBACK query timeout slave=0xFF fc=0x%02X reg=0x%04X port=%d",
                fc, reg, portIndex);
            return false;
        }

        if (buf[1] != fc)
        {
            log("ModbusSensorReader: FF_FALLBACK query fc_mismatch expected=0x%02X got=0x%02X port=%d",
                fc, buf[1], portIndex);
            return false;
        }

        uint16_t computedCrc = modbus->calculateCRC(buf, n - 2);
        uint16_t receivedCrc = (uint16_t)buf[n - 2] | ((uint16_t)buf[n - 1] << 8);
        if (computedCrc != receivedCrc)
        {
            log("ModbusSensorReader: FF_FALLBACK query crc_mismatch fc=0x%02X reg=0x%04X port=%d",
                fc, reg, portIndex);
            return false;
        }

        if (buf[0] == 0x00 || buf[0] == 0xFF)
        {
            log("ModbusSensorReader: FF_FALLBACK query bad_slave=0x%02X fc=0x%02X reg=0x%04X port=%d",
                buf[0], fc, reg, portIndex);
            return false;
        }

        outAddress = buf[0];
        return true;
    }

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

            IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::SETUP_SENSOR_DEBUG);
            if (feature)
            {
                _logger->println(DebugFeatures::SETUP_SENSOR_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                _logger->println(DebugFeatures::SETUP_SENSOR_DEBUG, buffer);
            }
        }
    }
};

#endif
