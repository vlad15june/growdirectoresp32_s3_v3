#ifndef MODBUS_SENSOR_READER_H
#define MODBUS_SENSOR_READER_H

#include <vector>
#include <string>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
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
    static constexpr int kWs12sAddressByteOffset = 4;
    static constexpr uint8_t kCwywQueryFc = 0x6E;
    static constexpr uint16_t kCwywQueryReg = 0x0012;
    static constexpr uint8_t kCwywWriteFc = 0x10;
    static constexpr uint16_t kCwywWriteReg = 0x0018;
    static constexpr int kCwywAddressByteOffset = 4;

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
        logVerbose("ModbusSensorReader: Read %d bytes from device %d on port %d", bytesRead, deviceAddress, portIndex);

        #ifdef DEBUG_LOGGING_CODE_ON
        if (bytesRead > 0 && _debugSystem && _logger) {
            char hexBuffer[256] = "";
            for (int i = 0; i < bytesRead && i < 32; i++) {
                char temp[4];
                snprintf(temp, sizeof(temp), "%02X ", response[i]);
                strcat(hexBuffer, temp);
            }
            log("ModbusSensorReader: Raw bytes ch=%s(%d) dev=%d port=%d: %s", config.channelName.c_str(), config.channel, deviceAddress, portIndex, hexBuffer);
        }
        #endif

        if (bytesRead >= 5 && response[1] == config.functionCode)
        {
            uint8_t byteCount = response[2];
            if (byteCount == config.numRegisters * 2 && bytesRead >= 3 + byteCount + 2)
            {
                float rawValue = 0.0f;
                if (config.numRegisters == 1)
                {
                    rawValue = static_cast<float>((response[3] << 8) | response[4]);
                }
                else if (config.numRegisters == 2)
                {
                    // 32-bit big-endian value (two consecutive 16-bit registers)
                    int32_t raw32 = static_cast<int32_t>(
                        (static_cast<uint32_t>(response[3]) << 24) |
                        (static_cast<uint32_t>(response[4]) << 16) |
                        (static_cast<uint32_t>(response[5]) << 8) |
                        static_cast<uint32_t>(response[6]));
                    rawValue = static_cast<float>(raw32);
                }
                else
                {
                    // Fallback for unsupported lengths - use the first register
                    rawValue = static_cast<float>((response[3] << 8) | response[4]);
                }

                outValue = rawValue * config.multiplier + config.offset;
                logVerbose("ModbusSensorReader: Read value %.2f from device %d on port %d", outValue, deviceAddress, portIndex);
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

        isolatePortForAddressWrite(portIndex);

        ModbusRegisterConfig probe;
        probe.functionCode = 0x03;
        probe.startRegister = pingRegister;
        probe.numRegisters = 1;

        if (tryPrimaryWrite(currentAddress, newAddress, portIndex, storeRegister, writeCommand, probe))
        {
            return true;
        }

        if (tryFfWildcardFallback(newAddress, portIndex, probe))
        {
            return true;
        }

        if (tryLegacyBroadcastLoop(newAddress, portIndex, probe))
        {
            return true;
        }

        return reportAddressWriteFailure(currentAddress, newAddress, portIndex, probe);
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

    bool writeHoldingRegister(uint8_t address, uint16_t reg, uint16_t value, int portIndex) override
    {
        if (portIndex < 0 || portIndex >= (int)_modbusPorts.size())
        {
            log("ModbusSensorReader: XF700A_RAIN port_invalid port=%d", portIndex);
            return false;
        }

        bool ok = _modbusPorts[portIndex]->writeSingleRegister(address, reg, value);
        log("ModbusSensorReader: XF700A_RAIN write addr=%d reg=0x%04X value=%u port=%d result=%d",
            address, reg, value, portIndex, ok ? 1 : 0);
        return ok;
    }

    bool readHoldingRegister(uint8_t address, uint16_t reg, uint16_t &outValue, int portIndex) override
    {
        if (portIndex < 0 || portIndex >= (int)_modbusPorts.size())
        {
            log("ModbusSensorReader: XF700A_RAIN port_invalid port=%d", portIndex);
            return false;
        }

        IModbusRS485* modbus = _modbusPorts[portIndex];

        uint8_t request[8] = {
            address,
            0x03,
            (uint8_t)(reg >> 8),
            (uint8_t)(reg & 0xFF),
            0x00,
            0x01
        };

        uint16_t crc = modbus->calculateCRC(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8);

        modbus->sendData(request, sizeof(request));
        _arduino.delay(300);

        uint8_t response[64];
        int bytesRead = modbus->readAvailable(response, sizeof(response));

        if (bytesRead >= 5 && response[1] == 0x03 && response[2] == 0x02)
        {
            outValue = (response[3] << 8) | response[4];
            log("ModbusSensorReader: XF700A_RAIN verify addr=%d reg=0x%04X raw=%u port=%d",
                address, reg, outValue, portIndex);
            return true;
        }

        log("ModbusSensorReader: XF700A_RAIN verify_fail addr=%d reg=0x%04X port=%d bytes=%d",
            address, reg, portIndex, bytesRead);
        return false;
    }

private:
    void isolatePortForAddressWrite(int portIndex)
    {
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
    }

    bool tryPrimaryWrite(int currentAddress, int newAddress, int portIndex,
                         uint16_t storeRegister, uint8_t writeCommand,
                         const ModbusRegisterConfig& probe)
    {
        if (writeCommand != 16 && writeCommand != 6)
        {
            log("ModbusSensorReader: writeRegistersCommand %d is not a write code (6/16), skipping primary write and using legacy broadcast sequence",
                writeCommand);
            return false;
        }

        IModbusRS485* modbus = _modbusPorts[portIndex];

        log("ModbusSensorReader: Primary write - FC%d to register 0x%04X on current address %d (new address %d) on port %d",
            writeCommand, storeRegister, currentAddress, newAddress, portIndex);

        issuePrimaryWrite(modbus, writeCommand, currentAddress, storeRegister, newAddress);
        _arduino.delay(300);

        log("ModbusSensorReader: Verifying primary write - probing new address %d (ping register 0x%04X) on port %d",
            newAddress, probe.startRegister, portIndex);
        if (pingAddress((uint8_t)newAddress, probe, portIndex))
        {
            log("ModbusSensorReader: Primary write CONFIRMED - device responds at new address %d on port %d", newAddress, portIndex);
            return true;
        }

        log("ModbusSensorReader: Primary write at known address %d not confirmed, retrying FC%d to register 0x%04X via broadcast on port %d",
            currentAddress, writeCommand, storeRegister, portIndex);

        issuePrimaryWrite(modbus, writeCommand, 0x00, storeRegister, newAddress);
        _arduino.delay(300);

        log("ModbusSensorReader: Verifying primary broadcast write - probing new address %d (ping register 0x%04X) on port %d",
            newAddress, probe.startRegister, portIndex);
        if (pingAddress((uint8_t)newAddress, probe, portIndex))
        {
            log("ModbusSensorReader: Primary broadcast write CONFIRMED - device responds at new address %d on port %d", newAddress, portIndex);
            return true;
        }

        log("ModbusSensorReader: Primary write not confirmed at new address %d on port %d, falling back to legacy broadcast sequence",
            newAddress, portIndex);
        return false;
    }

    void issuePrimaryWrite(IModbusRS485* modbus, uint8_t writeCommand, int slave,
                           uint16_t storeRegister, int newAddress)
    {
        if (writeCommand == 16)
        {
            modbus->writeRegister(slave, storeRegister, (uint16_t)newAddress);
        }
        else
        {
            modbus->writeSingleRegister(slave, storeRegister, (uint16_t)newAddress);
        }
    }

    bool tryFfWildcardFallback(int newAddress, int portIndex, const ModbusRegisterConfig& probe)
    {
        IModbusRS485* modbus = _modbusPorts[portIndex];

        if (runFfWildcardStage("ws12s", kWs12sQueryFc, kWs12sQueryReg,
                               kWs12sWriteFc, kWs12sWriteReg, kWs12sAddressByteOffset,
                               newAddress, portIndex, modbus, probe))
        {
            return true;
        }

        if (runFfWildcardStage("cwyw", kCwywQueryFc, kCwywQueryReg,
                               kCwywWriteFc, kCwywWriteReg, kCwywAddressByteOffset,
                               newAddress, portIndex, modbus, probe))
        {
            return true;
        }

        log("ModbusSensorReader: FF_FALLBACK both_stages_failed port=%d, entering legacy broadcast loop",
            portIndex);
        return false;
    }

    bool runFfWildcardStage(const char* tag,
                            uint8_t queryFc, uint16_t queryReg,
                            uint8_t writeFc, uint16_t writeReg, int addressByteOffset,
                            int newAddress, int portIndex,
                            IModbusRS485* modbus, const ModbusRegisterConfig& probe)
    {
        log("ModbusSensorReader: FF_FALLBACK %s query slave=0xFF fc=0x%02X reg=0x%04X on port %d",
            tag, queryFc, queryReg, portIndex);

        uint8_t discoveredAddr = 0;
        if (!queryAddressViaWildcard(queryFc, queryReg, portIndex, addressByteOffset, discoveredAddr))
        {
            return false;
        }

        log("ModbusSensorReader: FF_FALLBACK %s discovered addr=%d on port %d, writing FC%d reg=0x%04X value=%d",
            tag, discoveredAddr, portIndex, writeFc, writeReg, newAddress);

        if (writeFc == 0x10)
        {
            modbus->writeRegister(discoveredAddr, writeReg, (uint16_t)newAddress);
        }
        else
        {
            modbus->writeSingleRegister(discoveredAddr, writeReg, (uint16_t)newAddress);
        }

        _arduino.delay(300);

        if (pingAddress((uint8_t)newAddress, probe, portIndex))
        {
            log("ModbusSensorReader: FF_FALLBACK %s CONFIRMED - device responds at new address %d on port %d",
                tag, newAddress, portIndex);
            return true;
        }

        log("ModbusSensorReader: FF_FALLBACK %s write_unconfirmed discoveredAddr=%d newAddr=%d port=%d",
            tag, discoveredAddr, newAddress, portIndex);
        return false;
    }

    bool tryLegacyBroadcastLoop(int newAddress, int portIndex, const ModbusRegisterConfig& probe)
    {
        IModbusRS485* modbus = _modbusPorts[portIndex];

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

        return false;
    }

    bool reportAddressWriteFailure(int currentAddress, int newAddress, int portIndex,
                                   const ModbusRegisterConfig& probe)
    {
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

    bool queryAddressViaWildcard(uint8_t fc, uint16_t reg, int portIndex,
                                  int addressByteOffset, uint8_t& outAddress)
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

        // The device can trickle the response out over ~200 ms, and every
        // readAvailable() call consumes what it returns, so partial frames
        // must be accumulated here instead of being discarded between polls.
        uint8_t buf[16];
        int n = 0;
        for (int attempt = 1; attempt <= 30; attempt++)
        {
            _arduino.delay(10);
            int got = modbus->readAvailable(buf + n, sizeof(buf) - n);
            if (got > 0)
            {
                n += got;
            }
            if (n >= 3)
            {
                // addr + fc + byteCount + data(byteCount) + crc(2)
                int expected = buf[2] + 5;
                if (expected > (int)sizeof(buf))
                {
                    expected = sizeof(buf);
                }
                if (n >= expected)
                {
                    n = expected;
                    break;
                }
            }
        }

        if (n < 5)
        {
            log("ModbusSensorReader: FF_FALLBACK query timeout slave=0xFF fc=0x%02X reg=0x%04X port=%d",
                fc, reg, portIndex);
            return false;
        }

        char hexBuffer[256] = "";
        for (int i = 0; i < n && i < 32; i++)
        {
            char temp[4];
            snprintf(temp, sizeof(temp), "%02X ", buf[i]);
            strcat(hexBuffer, temp);
        }
        log("ModbusSensorReader: FF_FALLBACK query response (%d bytes) fc=0x%02X reg=0x%04X port=%d: %s",
            n, fc, reg, portIndex, hexBuffer);

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
            log("ModbusSensorReader: FF_FALLBACK query crc_mismatch fc=0x%02X reg=0x%04X port=%d computed=0x%04X received=0x%04X",
                fc, reg, portIndex, computedCrc, receivedCrc);
            return false;
        }

        if (addressByteOffset < 0 || addressByteOffset >= n - 2)
        {
            log("ModbusSensorReader: FF_FALLBACK query bad_offset=%d (n=%d) fc=0x%02X reg=0x%04X port=%d",
                addressByteOffset, n, fc, reg, portIndex);
            return false;
        }

        uint8_t candidate = buf[addressByteOffset];
        if (candidate == 0x00 || candidate > 247)
        {
            log("ModbusSensorReader: FF_FALLBACK query bad_address=0x%02X (offset=%d) fc=0x%02X reg=0x%04X port=%d",
                candidate, addressByteOffset, fc, reg, portIndex);
            return false;
        }

        outAddress = candidate;
        log("ModbusSensorReader: FF_FALLBACK query accepted address=%d (slave-byte=0x%02X, offset=%d) fc=0x%02X reg=0x%04X port=%d",
            candidate, buf[0], addressByteOffset, fc, reg, portIndex);
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

    void logVerbose(const char* format, ...)
    {
        if (_debugSystem && _logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::SENSOR_VERBOSE_DEBUG);
            if (feature)
            {
                _logger->println(DebugFeatures::SENSOR_VERBOSE_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                _logger->println(DebugFeatures::SENSOR_VERBOSE_DEBUG, buffer);
            }
        }
    }
};

#endif
