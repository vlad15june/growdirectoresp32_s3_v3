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
#include "../../../../../Core/Domain/Interfaces/Adapters/Sensors/IBusLock.h"
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

    // --- Timing of the read path ---------------------------------------------------------------
    // The budget below is exactly what the unconditional `delay(300)` it replaces cost, split
    // between the inter-frame gap and the response window. A sensor that never answers therefore
    // costs no more than it did before; the whole saving comes out of sensors that do answer, and
    // they answer in tens of milliseconds at 9600 baud.
    static constexpr unsigned long kReadBudgetMs = 300;
    // Longer than the 3.5 character times (3.65 ms at 9600 baud) that delimit an RTU frame.
    static constexpr unsigned long kInterFrameGapMs = 5;
    static constexpr unsigned long kResponseWindowMs = kReadBudgetMs - kInterFrameGapMs;
    static constexpr unsigned long kPollStepMs = 5;
    static constexpr int kMaxResponsePolls = (int)(kResponseWindowMs / kPollStepMs);
    // Address + function + one data byte + CRC: nothing shorter can be a frame.
    static constexpr int kMinFrameLength = 5;

    /** @brief Why a response was not accepted as a measurement. */
    enum class ReadRejection
    {
        None,
        Timeout,          // nothing at all came back within the budget
        ShortFrame,       // bytes came back, but never completed into a frame
        OversizeFrame,    // the frame announced a length this reader's buffer cannot hold
        CrcError,         // frame complete, checksum does not match its own payload
        AddressMismatch,  // frame complete and intact, another device sent it
        FunctionMismatch, // frame complete and intact, wrong function code (exceptions land here)
        PayloadMismatch   // frame complete and intact, not the register count that was asked for
    };

public:
    /**
     * @brief Read outcomes counted separately per cause.
     *
     * Strict frame checks turn some sensors that appeared to "work" into sensors that report an
     * error - intended, but it has to be presentable as such: "the CRC on this line is failing" and
     * "this sensor stopped answering" are different problems with different fixes, and a single
     * error flag cannot tell them apart.
     *
     * The JSON the module publishes is a contract with the backend and is not touched here, so
     * the breakdown is reachable two ways: through this getter, and through the log - each
     * rejection is logged with a `reason=` token on the SETUP_SENSOR channel, which
     * DebugSystem registers enabled and routed to MQTT and serial.
     */
    struct ReadStats
    {
        uint32_t accepted = 0;
        uint32_t timeout = 0;
        uint32_t shortFrame = 0;
        uint32_t oversizeFrame = 0;
        uint32_t crcError = 0;
        uint32_t addressMismatch = 0;
        uint32_t functionMismatch = 0;
        uint32_t payloadMismatch = 0;
        /** Bytes found on the line before a request was sent - the tail of a previous transaction. */
        uint32_t staleBytesDropped = 0;

        uint32_t rejected() const
        {
            return timeout + shortFrame + oversizeFrame + crcError + addressMismatch +
                   functionMismatch + payloadMismatch;
        }
    };

    ModbusSensorReader(const std::vector<IModbusRS485*>& modbusPorts, IArduinoAdapter &arduino, ILogger* logger, IDebugSystem* debugSystem)
        : _modbusPorts(modbusPorts), _arduino(arduino), _logger(logger), _debugSystem(debugSystem) {}

    const ReadStats& getReadStats() const { return _readStats; }
    void resetReadStats() { _readStats = ReadStats(); }

    /**
     * @brief Guard every transaction this reader makes with the shared RS-485 lock.
     *
     * Without one the class behaves exactly as it did, which is what the tests that predate the
     * acquisition task exercise. With one, the acquisition task and the command paths take turns
     * on the line instead of interleaving a request with somebody else's response.
     *
     * The lock is taken **per transaction**, not per channel, per sensor or per sweep. That
     * granularity is the whole answer to how long a command coming in on the control loop can be
     * made to wait: one request and its response window, not a walk of every port. The retry loop
     * in `SensorReader::readMultiChannelSensor()` sits above this method, so the line is free
     * again between two attempts on the same channel.
     *
     * `writeSensorAddress()` is the exception and takes the lock for its whole sequence, because
     * a wildcard address write with somebody else's poll in the middle writes the wrong device.
     * Its nested `pingAddress()`/`powerOffPort()` calls rely on IBusLock being reentrant.
     */
    void setBusLock(IBusLock* busLock) { _busLock = busLock; }

private:
    ReadStats _readStats;
    IBusLock* _busLock = nullptr;

public:

    void init(unsigned long baud = 9600)
    {
        BusLockGuard busGuard(_busLock);
        for (auto* modbus : _modbusPorts)
        {
            modbus->begin(baud);
        }
    }

    bool readRegister(uint8_t deviceAddress, const ModbusRegisterConfig &config, float &outValue, int portIndex = 0) override
    {
        BusLockGuard busGuard(_busLock);

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

        quiesceBus(modbus, portIndex);
        modbus->sendData(request, sizeof(request));

        uint8_t response[64];
        bool oversize = false;
        int bytesRead = awaitFrame(modbus, response, (int)sizeof(response), oversize);
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

        ReadRejection rejection = validateFrame(modbus, response, bytesRead, oversize,
                                               deviceAddress, config.functionCode,
                                               (int)config.numRegisters * 2);
        if (rejection != ReadRejection::None)
        {
            recordRejection(rejection, deviceAddress, portIndex, config.channelName.c_str(),
                            response, bytesRead);
            return false;
        }

        float rawValue = 0.0f;
        if (config.numRegisters == 1)
        {
            rawValue = decodeSingleRegister(response, config.dataType);
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
            rawValue = decodeSingleRegister(response, config.dataType);
        }

        outValue = rawValue * config.multiplier + config.offset;
        _readStats.accepted++;
        logVerbose("ModbusSensorReader: Read value %.2f from device %d on port %d", outValue, deviceAddress, portIndex);
        return true;
    }

    bool pingAddress(uint8_t deviceAddress, const ModbusRegisterConfig& config, int portIndex = 0) override
    {
        BusLockGuard busGuard(_busLock);

        // One of the two bounded steps this class spends real time in, the other being a port
        // warmup. Sequences here are built out of them - an address write runs a dozen back to
        // back - so the caller vouches for itself before each step and the gap the task watchdog
        // measures stays one step (WDT_SENSOR_ACQ_LEGAL_PAUSE_S) rather than the whole sequence.
        feedCallerWatchdog();

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
                            uint16_t storeRegister, uint8_t writeCommand, uint16_t pingRegister,
                            bool powerCycleToApply = false) override
    {
        // The one place the lock covers a sequence rather than a single transaction: isolating the
        // port, writing the address and verifying it have to happen with nobody else on the line.
        BusLockGuard busGuard(_busLock);

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

        if (tryPrimaryWrite(currentAddress, newAddress, portIndex, storeRegister, writeCommand, probe,
                            powerCycleToApply))
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

    bool writeAddressByBroadcast(int newAddress, int portIndex, uint16_t storeRegister,
                                 uint8_t writeCommand, uint16_t pingRegister,
                                 bool powerCycleToApply = false) override
    {
        // One sequence on the line, like writeSensorAddress(): a broadcast write with somebody
        // else's frame in the middle of its verification confirms the wrong device.
        BusLockGuard busGuard(_busLock);

        if (portIndex < 0 || portIndex >= (int)_modbusPorts.size())
        {
            log("ModbusSensorReader: Invalid port index %d for broadcast address write", portIndex);
            return false;
        }

        ModbusRegisterConfig probe;
        probe.functionCode = 0x03;
        probe.startRegister = pingRegister;
        probe.numRegisters = 1;

        if (writeCommand == 16 || writeCommand == 6)
        {
            log("ModbusSensorReader: Broadcast address write - FC%d to register 0x%04X for new address %d on port %d",
                writeCommand, storeRegister, newAddress, portIndex);
            if (tryPrimaryBroadcastWrite(newAddress, portIndex, storeRegister, writeCommand, probe,
                                         powerCycleToApply))
            {
                return true;
            }
        }
        else
        {
            log("ModbusSensorReader: writeRegistersCommand %d is not a write code (6/16), skipping the model's broadcast write",
                writeCommand);
        }

        return tryLegacyBroadcastLoop(newAddress, portIndex, probe);
    }

    bool powerOnPort(int portIndex) override
    {
        BusLockGuard busGuard(_busLock);

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
        BusLockGuard busGuard(_busLock);

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

    /**
     * Read off the port itself, without the lock: the record changes only when a pin moves, and the
     * pins are moved on the task that asks.
     */
    PortPower portPower(int portIndex) override
    {
        if (portIndex < 0 || portIndex >= (int)_modbusPorts.size())
        {
            return PortPower();
        }
        return _modbusPorts[portIndex]->powerRecord();
    }

    bool sharesAWire(int portIndex, int otherPortIndex) const override
    {
        const int count = (int)_modbusPorts.size();
        if (portIndex < 0 || otherPortIndex < 0 || portIndex >= count || otherPortIndex >= count)
        {
            return false;
        }
        return _modbusPorts[portIndex]->wire() == _modbusPorts[otherPortIndex]->wire();
    }

    /**
     * @brief Which line each port is on, one entry per port: the lowest index of a port sharing its
     *        wire. Read off the ports themselves (IModbusRS485::wire()), so what it says is how the
     *        module is built rather than a second account of it.
     */
    std::vector<int> portLines() const
    {
        std::vector<int> lines;
        lines.reserve(_modbusPorts.size());
        for (int i = 0; i < (int)_modbusPorts.size(); i++)
        {
            // A port shares a wire with itself, so this stops at i at the latest.
            int first = 0;
            while (!sharesAWire(i, first))
            {
                first++;
            }
            lines.push_back(first);
        }
        return lines;
    }

    bool writeHoldingRegister(uint8_t address, uint16_t reg, uint16_t value, int portIndex) override
    {
        BusLockGuard busGuard(_busLock);

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
        BusLockGuard busGuard(_busLock);

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

        // Same treatment as readRegister(): this is the read-back that confirms a write, so
        // sleeping through the answer costs the same, and accepting an unchecked frame here means
        // confirming a write that may never have happened.
        quiesceBus(modbus, portIndex);
        modbus->sendData(request, sizeof(request));

        uint8_t response[64];
        bool oversize = false;
        int bytesRead = awaitFrame(modbus, response, (int)sizeof(response), oversize);

        ReadRejection rejection = validateFrame(modbus, response, bytesRead, oversize,
                                               address, 0x03, 2);
        if (rejection == ReadRejection::None)
        {
            outValue = (response[3] << 8) | response[4];
            _readStats.accepted++;
            log("ModbusSensorReader: XF700A_RAIN verify addr=%d reg=0x%04X raw=%u port=%d",
                address, reg, outValue, portIndex);
            return true;
        }

        recordRejection(rejection, address, portIndex, "XF700A_RAIN", response, bytesRead);
        log("ModbusSensorReader: XF700A_RAIN verify_fail addr=%d reg=0x%04X port=%d bytes=%d",
            address, reg, portIndex, bytesRead);
        return false;
    }

private:
    // --- The read path: wait, assemble, check ---------------------------------------------------

    /**
     * @brief Value of the first data register of a validated response, in the type the channel declares
     *
     * U16 keeps the whole 0..65535 range; S16 reads the same two bytes as two's complement, which is
     * what a probe reporting a sub-zero soil temperature puts on the line.
     *
     * @param frame Validated response frame; data starts at byte 3
     * @param dataType Data type declared by the channel of the sensor model
     */
    float decodeSingleRegister(const uint8_t* frame, ModbusDataType dataType) const
    {
        uint16_t raw = static_cast<uint16_t>((frame[3] << 8) | frame[4]);
        if (dataType == ModbusDataType::S16)
        {
            return static_cast<float>(static_cast<int16_t>(raw));
        }
        return static_cast<float>(raw);
    }

    /**
     * @brief Drop whatever is on the line, then let it fall quiet, before a request goes out.
     *
     * Bytes present before a request cannot be an answer to it - they are the tail of a
     * transaction that already timed out. Left in place they are read as this sensor's answer, and
     * when the stale frame carries the same address as the sensor being polled (the retry loop in
     * SensorReader polls the same sensor three times) no downstream check can tell the difference:
     * the address is right, the CRC is right, the value is minutes old.
     *
     * Note the residual: ModbusRS485::sendData() sleeps 100 ms of its own before writing the
     * request, and bytes arriving during that sleep are not covered by this drain. Those are
     * caught by the responder-address check instead, which leaves only a stale frame from the very
     * same address arriving inside that 100 ms window. Closing it means touching sendData(), which
     * is not this class's to change.
     */
    void quiesceBus(IModbusRS485* modbus, int portIndex)
    {
        uint8_t scrap[64];
        int dropped = modbus->readAvailable(scrap, sizeof(scrap));
        _arduino.delay(kInterFrameGapMs);
        dropped += modbus->readAvailable(scrap, sizeof(scrap));

        if (dropped > 0)
        {
            _readStats.staleBytesDropped += (uint32_t)dropped;
            log("ModbusSensorReader: stale_bytes_dropped count=%d port=%d before request",
                dropped, portIndex);
        }
    }

    /**
     * @brief Length of the frame whose first three bytes are in `header`.
     *
     * The length of a Modbus RTU response is carried in the response itself, which is what makes
     * an early exit possible at all: three bytes are enough to know how many more to wait for.
     * Both function codes this reader issues - 0x03 and the discovery code 0x6E - are byte-count framed,
     * the same assumption the discovery path in this file already makes.
     */
    static int frameLengthFromHeader(const uint8_t* header)
    {
        // Exception response: address + function|0x80 + exception code + CRC.
        if ((header[1] & 0x80) != 0)
        {
            return 5;
        }
        // Normal response: address + function + byte count + data + CRC.
        return 3 + (int)header[2] + 2;
    }

    /**
     * @brief Wait for one response frame, returning the moment it is complete.
     *
     * This replaces `_arduino.delay(300)`, which was not a timeout but an unconditional sleep paid
     * on every channel of every sensor. The wait is bounded twice on purpose: by the clock, which
     * is the honest bound, and by the number of polls, which keeps the loop finite under an
     * IArduinoAdapter whose millis() does not move. Whichever fires first ends the wait, and the
     * delays inside it add up to exactly the budget a fixed sleep would spend.
     *
     * @param outOversize Set when the frame announced a length this buffer cannot hold. Without
     *        it that case reaches validateFrame() as a truncated frame, whose checksum can never
     *        match, and gets filed as a CRC error - which sends a field engineer after cabling
     *        and termination when the truth is that the device answered with more registers than
     *        this reader can hold.
     * @return Number of bytes in `buffer`: the frame length when a frame was assembled, otherwise
     *         however much arrived (0 when nothing did).
     */
    int awaitFrame(IModbusRS485* modbus, uint8_t* buffer, int capacity, bool& outOversize)
    {
        const unsigned long startedMs = _arduino.millis();
        int received = 0;
        int expected = -1;

        outOversize = false;

        for (int poll = 0;; poll++)
        {
            int got = modbus->readAvailable(buffer + received, (size_t)(capacity - received));
            if (got > 0)
            {
                received += got;
            }

            if (expected < 0 && received >= 3)
            {
                expected = frameLengthFromHeader(buffer);
                if (expected > capacity)
                {
                    // A byte count this buffer cannot hold is not a frame we will ever assemble.
                    outOversize = true;
                    return received;
                }
            }

            if (expected > 0 && received >= expected)
            {
                return expected;
            }

            if (received >= capacity || poll >= kMaxResponsePolls)
            {
                return received;
            }

            // Unsigned arithmetic, so this stays correct across the millis() wrap.
            if (_arduino.millis() - startedMs >= kResponseWindowMs)
            {
                return received;
            }

            _arduino.delay(kPollStepMs);
        }
    }

    /**
     * @brief Decide whether an assembled frame may be read as a measurement.
     *
     * The function code, the byte count and the length say the frame is well-formed; only the
     * **CRC and the address of the responder** say it is *this sensor's* answer. Without those
     * two checks any well-formed-looking frame from any device on the line would be read as this
     * sensor's measurement.
     */
    ReadRejection validateFrame(IModbusRS485* modbus, uint8_t* frame, int length, bool oversize,
                                uint8_t expectedAddress, uint8_t expectedFunctionCode,
                                int expectedByteCount)
    {
        if (oversize)
        {
            return ReadRejection::OversizeFrame;
        }
        if (length <= 0)
        {
            return ReadRejection::Timeout;
        }
        if (length < kMinFrameLength)
        {
            return ReadRejection::ShortFrame;
        }

        uint16_t computedCrc = modbus->calculateCRC(frame, (uint8_t)(length - 2));
        uint16_t receivedCrc = (uint16_t)frame[length - 2] | ((uint16_t)frame[length - 1] << 8);
        if (computedCrc != receivedCrc)
        {
            return ReadRejection::CrcError;
        }

        // Every address is checked, 0xFF included. Exempting 0xFF as "the discovery wildcard"
        // would be wrong here: 255 is the configured address of a real sensor type (main.cpp, the
        // RY-WS301 type), the module stores sensors at it, and the ordinary poll then reads
        // at 255 with no ping in front - an exemption would switch this check off permanently
        // for that whole sensor type. A device that genuinely answers as 255 still passes,
        // which is all pingAddress() ever demanded of it.
        if (frame[0] != expectedAddress)
        {
            return ReadRejection::AddressMismatch;
        }

        if (frame[1] != expectedFunctionCode)
        {
            return ReadRejection::FunctionMismatch;
        }

        if ((int)frame[2] != expectedByteCount || length < 3 + (int)frame[2] + 2)
        {
            return ReadRejection::PayloadMismatch;
        }

        return ReadRejection::None;
    }

    /**
     * @brief Count a rejection under its own cause and say so in the log.
     *
     * A timeout is the ordinary "the sensor is not there" case and is already visible as the
     * sensor's error state, so it goes to the verbose channel. The frame-level rejections are
     * what turns a sensor that appeared to "work" into a sensor that reports an error, and they
     * go to the ordinary channel, each with its own reason token.
     */
    void recordRejection(ReadRejection reason, uint8_t deviceAddress, int portIndex,
                         const char* channelName, const uint8_t* frame, int length)
    {
        const char* token = "unknown";
        bool ordinary = false;

        switch (reason)
        {
            case ReadRejection::Timeout:
                _readStats.timeout++;
                token = "timeout";
                break;
            case ReadRejection::ShortFrame:
                _readStats.shortFrame++;
                token = "short_frame";
                ordinary = true;
                break;
            case ReadRejection::OversizeFrame:
                _readStats.oversizeFrame++;
                token = "oversize_frame";
                ordinary = true;
                break;
            case ReadRejection::CrcError:
                _readStats.crcError++;
                token = "crc_error";
                ordinary = true;
                break;
            case ReadRejection::AddressMismatch:
                _readStats.addressMismatch++;
                token = "address_mismatch";
                ordinary = true;
                break;
            case ReadRejection::FunctionMismatch:
                _readStats.functionMismatch++;
                token = "function_mismatch";
                ordinary = true;
                break;
            case ReadRejection::PayloadMismatch:
                _readStats.payloadMismatch++;
                token = "payload_mismatch";
                ordinary = true;
                break;
            default:
                return;
        }

        int responder = (length > 0) ? (int)frame[0] : -1;
        int functionCode = (length > 1) ? (int)frame[1] : -1;

        if (ordinary)
        {
            log("ModbusSensorReader: read_rejected reason=%s dev=%d port=%d ch=%s bytes=%d responder=%d fc=0x%02X",
                token, deviceAddress, portIndex, channelName, length, responder, functionCode);
        }
        else
        {
            logVerbose("ModbusSensorReader: read_rejected reason=%s dev=%d port=%d ch=%s bytes=%d",
                       token, deviceAddress, portIndex, channelName, length);
        }
    }

    /**
     * The target port up and the other ports of its wire down: a write to 0x00 or to the wildcard
     * is taken by every device that hears it, and a frame on this wire is heard by what is plugged
     * into any of its ports. A port on another UART is not on this wire, and keeps its power.
     */
    void isolatePortForAddressWrite(int portIndex)
    {
        IModbusRS485* modbus = _modbusPorts[portIndex];
        bool targetJustTurnedOn = modbus->powerOnPort();

        for (int i = 0; i < (int)_modbusPorts.size(); i++)
        {
            if (i != portIndex && sharesAWire(portIndex, i))
            {
                _modbusPorts[i]->powerOffPort();
            }
        }

        if (targetJustTurnedOn)
        {
            log("ModbusSensorReader: Target port %d was off, warmup 10s", portIndex);
            feedCallerWatchdog();
            _arduino.delay(10000);
            return;
        }

        // Up already, but whoever switched it on - the sweep, the preparation, a raw command - may
        // have done it moments ago, and a write before what is on it has come up is a write nobody
        // takes.
        const uint32_t owedMs =
            modbus->powerRecord().wakeUpOwedMs(static_cast<uint32_t>(_arduino.millis()));
        if (owedMs > 0)
        {
            log("ModbusSensorReader: Target port %d came up %lu ms ago, warmup %lu ms", portIndex,
                (unsigned long)(PortPower::WAKE_UP_MS - owedMs), (unsigned long)owedMs);
            feedCallerWatchdog();
            _arduino.delay(owedMs);
            return;
        }
        log("ModbusSensorReader: Target port %d was already on, skipping warmup delay", portIndex);
    }

    bool confirmAddress(int newAddress, int portIndex,
                        const ModbusRegisterConfig& probe, bool powerCycleToApply)
    {
        if (pingAddress((uint8_t)newAddress, probe, portIndex))
        {
            return true;
        }

        if (!powerCycleToApply)
        {
            return false;
        }

        // Some sensors (e.g. RK520-02) apply a new Modbus address only after a
        // power cycle. Reboot the port and try again before giving up. Gated by
        // the model flag: the cycle costs ~10 s per failed write, so it runs only
        // for models that actually need it.
        log("ModbusSensorReader: Ping failed after address write, power-cycling port %d to apply new address",
            portIndex);
        powerCyclePort(portIndex);
        return pingAddress((uint8_t)newAddress, probe, portIndex);
    }

    void powerCyclePort(int portIndex)
    {
        powerOffPort(portIndex);
        _arduino.delay(100);
        powerOnPort(portIndex);
        log("ModbusSensorReader: Port %d power-cycled, warmup 10s", portIndex);
        feedCallerWatchdog();
        _arduino.delay(10000);
    }

    /**
     * @brief Vouch for the calling task before a bounded step of a line operation.
     *
     * The task watchdog measures the gap between two feeds of a subscribed task, and the tasks
     * that reach this class - the acquisition task in steady state, the setup task at boot - are
     * both subscribed. A single step here is a port warmup or a three-attempt ping; a sequence of
     * them is an address write, whose fallback chain is a dozen steps long and, for a model that
     * applies its address only after a power cycle, carries two more warmups. Feeding between the
     * steps, not inside one, is what keeps the budget honest: a step that overruns on its own is
     * still caught.
     */
    void feedCallerWatchdog()
    {
        _arduino.feedTaskWatchdog();
    }

    bool tryPrimaryWrite(int currentAddress, int newAddress, int portIndex,
                         uint16_t storeRegister, uint8_t writeCommand,
                         const ModbusRegisterConfig& probe, bool powerCycleToApply)
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
        if (confirmAddress(newAddress, portIndex, probe, powerCycleToApply))
        {
            log("ModbusSensorReader: Primary write CONFIRMED - device responds at new address %d on port %d", newAddress, portIndex);
            return true;
        }

        log("ModbusSensorReader: Primary write at known address %d not confirmed, retrying FC%d to register 0x%04X via broadcast on port %d",
            currentAddress, writeCommand, storeRegister, portIndex);

        if (tryPrimaryBroadcastWrite(newAddress, portIndex, storeRegister, writeCommand, probe,
                                     powerCycleToApply))
        {
            return true;
        }

        log("ModbusSensorReader: Primary write not confirmed at new address %d on port %d, falling back to legacy broadcast sequence",
            newAddress, portIndex);
        return false;
    }

    /** @brief The model's own address write, addressed to 0x00, and its verification. */
    bool tryPrimaryBroadcastWrite(int newAddress, int portIndex, uint16_t storeRegister,
                                  uint8_t writeCommand, const ModbusRegisterConfig& probe,
                                  bool powerCycleToApply)
    {
        issuePrimaryWrite(_modbusPorts[portIndex], writeCommand, 0x00, storeRegister, newAddress);
        _arduino.delay(300);

        log("ModbusSensorReader: Verifying primary broadcast write - probing new address %d (ping register 0x%04X) on port %d",
            newAddress, probe.startRegister, portIndex);
        if (confirmAddress(newAddress, portIndex, probe, powerCycleToApply))
        {
            log("ModbusSensorReader: Primary broadcast write CONFIRMED - device responds at new address %d on port %d", newAddress, portIndex);
            return true;
        }
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
