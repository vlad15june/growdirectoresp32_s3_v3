#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <deque>
#include <map>
#include <vector>

#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Infrastructure/Adapters/Rs485/IModbusRS485.h"
#include "Sim/HostAllocationCounter.h"
#include "Sim/VirtualClock.h"

namespace hostsim {

/**
 * @brief A simulated RS-485 line with devices on it, on top of the virtual clock.
 *
 * The existing MockModbusRS485 in the discovery tests answers "was a frame sent, and here is a
 * canned reply"; it has no notion of time, so it cannot express the questions these tests ask:
 * how long a healthy sensor costs, what happens when a
 * frame is late, what happens when the bytes of one answer straddle two polls.
 *
 * What is modelled:
 *  - turnaround: a device starts answering `turnaroundUs` after the request was written;
 *  - byte rate: the answer becomes readable one byte at a time at the configured baud (8N1,
 *    10 bit times per byte), so a reader that assembles a frame across polls is exercised for
 *    real rather than handed the whole frame at once;
 *  - silence: a device that never answers;
 *  - corruption: a device whose CRC does not match its payload;
 *  - a foreign responder: a device that answers under someone else's address;
 *  - a late frame: bytes put on the line at an absolute moment, independent of any request -
 *    the tail of a transaction the master already gave up on;
 *  - a device that needs its port's power for a while before it answers anything (wakeUpMs);
 *  - two ports on one wire: see shareTheLineWith();
 *  - two devices answering one address at once: see pickResponder();
 *  - a device that does not answer the wildcard (answersTheWildcard);
 *  - the register writes as the driver makes them, when a stand asks: see setWritesLikeTheBoard();
 *  - a UART nobody has begun: see begin().
 *
 * Not modelled (out of scope here, cheap to add later): line noise, and the RS-485 driver-enable
 * turnaround of the master itself.
 */
class SimRs485Bus : public IModbusRS485 {
public:
    struct Device {
        uint8_t address = 1;

        /** Time from the request being written to the first response byte being on the line. */
        uint32_t turnaroundUs = 20000;

        /** A device that is present but never answers. */
        bool silent = false;

        /** Answer with a CRC that does not match the payload. */
        bool corruptCrc = false;

        /** Answer under a different address than the one addressed (0 = answer as `address`). */
        uint8_t answerAsAddress = 0;

        /** Answer with a function code other than the one requested (0 = echo the request). */
        uint8_t answerAsFunctionCode = 0;

        /** Register contents; registers past the end read back as `defaultRegisterValue`. */
        std::vector<uint16_t> registerValues;
        uint16_t defaultRegisterValue = 0;

        /**
         * @brief Does this device take a new ModBus address, and in which holding register.
         *
         * A device that takes one moves the moment the master writes that register - FC6 or FC16,
         * addressed to it or broadcast to 0x00 - which is what the address write does
         * (`ModbusSensorReader::writeSensorAddress()`: isolate the port, write the register, ping
         * the new address). From then on it answers at the new address and nowhere else, which is
         * what makes the ping that follows the write confirm anything.
         *
         * Off by default, and that is not only for the tests written before this: a model whose
         * address the module cannot rewrite - and the catalog has no field that says which models
         * those are - is exactly a device that ignores the write and goes on answering where it
         * was. Both halves of that are worth a test.
         */
        bool addressIsWritable = false;
        uint16_t addressStoreRegister = 0;

        /**
         * @brief How long the device needs its port's power before it answers anything.
         *
         * Zero - the default - is a device that answers the moment it is addressed, which is what
         * every test written before this one asks for. The firmware's own belief about a real one
         * is ten seconds: that is what SensorReader::scanModbusSensor() and
         * ModbusSensorReader::isolatePortForAddressWrite() wait after they change a port's power,
         * and it is the number a test that asks "would the board find this device" has to use.
         */
        uint32_t wakeUpMs = 0;

        /**
         * @brief Does the device answer a request to 0xFF, the slave address of the
         *        address-discovery frames, as though it had been addressed?
         *
         * Only the models those frames were written for do (ModbusSensorReader: ws12s and cwyw).
         * A device of another model answers its own address and nothing else - an RY-WS301 stays
         * silent to both wildcard frames and answers at the address the module gave it. On by
         * default, which is what every stand written before this assumed.
         */
        bool answersTheWildcard = true;

        /**
         * @brief Does a read start at the register it names?
         *
         * Off by default: a read of any register answers registerValues from the first, which is
         * what every stand written before this was calibrated on. On, a read of register N answers
         * registerValues[N] onwards, and registers past the end read as defaultRegisterValue -
         * which is what decides, on a catalog of several models, which model's channels a device
         * satisfies.
         */
        bool registersAreAddressed = false;

        /**
         * @brief Is a read that starts at a register this device does not have answered at all?
         *
         * On by default, and with registersAreAddressed such a read answers defaultRegisterValue -
         * a device that answers whatever it is asked. Off, the device does not answer it, which is
         * what a real RY-WS301 does: asked register 9 at its address with FC03, it sends
         * nothing within the reader's budget, and asked register 0 a moment later, it answers.
         * What decides it here is only the register an FC03 read starts at; other function codes
         * and writes are not affected.
         */
        bool answersRegistersItDoesNotHave = true;
    };

    explicit SimRs485Bus(VirtualClock& clock, const char* portName = "sim-rs485")
        : _clock(clock), _portName(portName)
    {
        _line->ports.push_back(this);
    }

    /**
     * @brief Wire this port and another to one UART, the way the board wires them.
     *
     * Ports 0 and 1 of the module share HardwareSerial(1) and ports 2 and 3 share
     * HardwareSerial(2) (globals.h): one pair of wires, one receive FIFO, and two separate
     * SENSOR_POWER pins. So a request written to one of them is heard by whatever is plugged into
     * the other, unless that other port's power is off - which is the only thing that separates
     * them - and the answer arrives in the FIFO both of them read.
     *
     * A port nobody wires this way keeps a line of its own, which is what the tests that predate
     * this get: four independent buses.
     */
    void shareTheLineWith(SimRs485Bus& other)
    {
        if (other._line == _line)
        {
            return;
        }

        Line* absorbed = other._line;
        for (size_t i = 0; i < absorbed->ports.size(); i++)
        {
            absorbed->ports[i]->_line = _line;
            _line->ports.push_back(absorbed->ports[i]);
        }
        absorbed->ports.clear();
        if (absorbed->everBegun && !_line->everBegun)
        {
            _line->everBegun = true;
            _line->begunAtUs = absorbed->begunAtUs;
        }
        _line->begun = _line->begun || absorbed->begun;
        _line->driverInstalled = _line->driverInstalled || absorbed->driverInstalled;
    }

    /**
     * @brief Charge a delay before every request the master puts on the line.
     *
     * ModbusRS485::sendData() sleeps its 100 ms only when it has to raise the port's power first:
     * the settling time is owed to the power-up, not to the request (ModbusRS485::sendData()). With
     * setPortPowerLikeTheBoard(true) this bus charges it the same way - putOnTheWire() raises a dark
     * port through powerOnPort(), which spends the settling time once; without it, the power costs
     * nothing here. A request on a port that already has its power goes out at once. So the firmware
     * has no delay of this kind: one set here makes every request on this port cost more than the
     * firmware's does, for a test that wants the line slower than the reader's own timings. Left at 0
     * by default.
     */
    void setMasterSendDelayMs(unsigned long ms) { _masterSendDelayMs = ms; }

    /**
     * @brief Spend the master's own waits the way the firmware spends them.
     *
     * ModbusRS485 waits in `_arduino.delay()`, which on the device is a vTaskDelay and lets other
     * tasks run: 100 ms for the sensor to settle at every power transition (ModbusRS485::sendData(), powerOnPort(),
     * powerOffPort()) and 300 ms between a register write and the reading of its answer (writeRegister(), writeSingleRegister()). This bus
     * spends its own waits through the adapter too - the settling time with
     * setPortPowerLikeTheBoard(true), the write's wait with setWritesLikeTheBoard(true), the autobaud
     * timeout of a zero rate, and a delay set with setMasterSendDelayMs(). Without an adapter they are
     * charged straight to the clock, which is right for a test that measures one reader in isolation
     * and wrong for one that asks what the rest of the module was doing meanwhile. Left null by
     * default, so the tests written against this bus before the acquisition task are unaffected.
     */
    void setMasterArduino(IArduinoAdapter* arduino) { _masterArduino = arduino; }

    /**
     * @brief Switch the power pin on and off the way ModbusRS485 does, and charge what it costs.
     *
     * Two things the board does and this model left out: sendData() raises the port's power pin
     * when it is down (ModbusRS485::sendData()) - so a request to a port nobody powered arrives anyway,
     * and the port stays up afterwards - and every power transition sleeps 100 ms for the sensor
     * to settle (ModbusRS485::sendData(), powerOnPort(), powerOffPort()).
     *
     * Opt-in per stand rather than always on: the scenario tests written before it are calibrated
     * without those milliseconds and prove things that have nothing to do with port power, and
     * moving all their figures would buy them nothing. A stand that measures what a pass costs or
     * what state it leaves the ports in wants it on.
     */
    void setPortPowerLikeTheBoard(bool like) { _boardPower = like; }

    /**
     * @brief Make the two register writes the way ModbusRS485 makes them, and charge what it costs.
     *
     * ModbusRS485::writeRegister() (FC16) and writeSingleRegister() (FC06) build the request, put
     * it on the wire through sendData() - raising the power pin when it is down - sleep 300 ms and
     * read back whatever answered. Without this a write here is a note in `writes` and nothing on
     * the wire: it costs no time, it is not among `sentFrames`, and nobody on the line hears a
     * frame. That is enough for a stand that asks whether a device moved, and not for one that
     * asks what the line looked like while the module wrote an address.
     *
     * Off by default, for the same reason setPortPowerLikeTheBoard() is: the stands calibrated
     * before it would move without learning anything.
     */
    void setWritesLikeTheBoard(bool like) { _boardWrites = like; }

    /**
     * @brief Keep the moment each frame of `sentFrames` went out, in `sentAtUs`, and the port's power
     *        at that moment, in `sentAfterPowerForMs`. Off by default.
     */
    void recordSendTimes(bool record) { _recordSendTimes = record; }

    /** @brief Keep the moment of each power-off transition, in `powerOffAtUs`. Off by default. */
    void recordPowerOffTimes(bool record) { _recordPowerOffTimes = record; }

    /** @brief Keep the moment each power-on transition had settled, in `powerOnAtUs`. Off by default. */
    void recordPowerOnTimes(bool record) { _recordPowerOnTimes = record; }

    // --- Populating the line ----------------------------------------------------------------

    /** @brief Plug a device into this port, now: a wake-up is counted from this moment too. */
    void addDevice(const Device& device)
    {
        Occupant occupant;
        occupant.device = device;
        occupant.pluggedInAtUs = _clock.nowUs();
        _devices[device.address] = occupant;
    }

    /** @brief Unplug what is at this address, now: nothing on the line answers for it any more. */
    void removeDevice(uint8_t address) { _devices.erase(address); }

    Device* device(uint8_t address)
    {
        std::map<uint8_t, Occupant>::iterator it = _devices.find(address);
        return (it == _devices.end()) ? nullptr : &it->second.device;
    }

    /**
     * @brief Put a ready-made frame on the line, readable from `atUs` onwards.
     *
     * This is how a late frame is expressed: the answer to transaction N arriving while
     * transaction N+1 is waiting for its own.
     */
    void injectFrameAt(const std::vector<uint8_t>& frame, uint64_t atUs)
    {
        scheduleFrame(frame, atUs);
    }

    /** @brief Build the frame a healthy device would send, for use with injectFrameAt(). */
    static std::vector<uint8_t> makeReadResponse(uint8_t address, uint8_t functionCode,
                                                 const std::vector<uint16_t>& registers)
    {
        std::vector<uint8_t> frame;
        frame.push_back(address);
        frame.push_back(functionCode);
        frame.push_back(static_cast<uint8_t>(registers.size() * 2));
        for (size_t i = 0; i < registers.size(); i++)
        {
            frame.push_back(static_cast<uint8_t>(registers[i] >> 8));
            frame.push_back(static_cast<uint8_t>(registers[i] & 0xFF));
        }
        uint16_t crc = crc16(&frame[0], static_cast<uint8_t>(frame.size()));
        frame.push_back(static_cast<uint8_t>(crc & 0xFF));
        frame.push_back(static_cast<uint8_t>(crc >> 8));
        return frame;
    }

    // --- IModbusRS485 -----------------------------------------------------------------------
    //
    // Every entry point below opens with a bench tag for the allocation census. What this class
    // allocates is the line, not the module: the frames the devices answer with, the queue they
    // arrive in byte by byte, and the recording of what was sent. On a board none of that is
    // heap - it is the UART and the wire - so leaving it in the caller's row would make the
    // sensor path look several times more expensive than it is. The module's own Modbus buffers
    // live in ModbusSensorReader, above this seam, and stay where they belong.

    /**
     * @brief Begin the UART of this port's line - and so of every port wired to it.
     *
     * Until then the line carries nothing, the way the board's does: a ModbusRS485 whose
     * HardwareSerial was never begun has no UART driver behind it, and what it is asked to send
     * never reaches the pins (arduino-esp32 2.0.17, esp32-hal-uart.c: uartWrite() and uartFlush()
     * return at once on a NULL uart, uartAvailable() answers 0). What such a send still does here
     * is what ModbusRS485::sendData() does before its first byte - the send delay and the power
     * pin - and it is counted in framesDroppedBeforeBegin, because nobody on the wire hears it and
     * nothing answers it. A register write is the same: no frame, nothing moves, no acknowledgement.
     *
     * Ports 0 and 1 of the board share one HardwareSerial, so beginning either begins both; a port
     * that shares nothing is a line of its own. What reached the receive FIFO before the driver
     * existed is not kept.
     *
     * A zero rate fails the way the core fails it. HardwareSerial::begin(0) installs the driver at
     * 9600, then waits up to timeout_ms - ModbusRS485 passes none, so the default 20 s - for 30
     * edges on RX to measure the rate from, and when they do not come it leaves the object with
     * `_uart` NULL: no driver as far as every later call is concerned, whatever was begun before
     * (HardwareSerial::begin(), uartBaudrateDetect()). Nothing talks
     * to a master that has not asked anything, so here the detection always fails: the wait is
     * spent and the line is left carrying nothing until a begin() with a rate. The driver the core
     * installed stays, so what arrives meanwhile is still there for that begin().
     */
    void begin(unsigned long baud) override
    {
        beginCalls++;
        if (!_line->driverInstalled)
        {
            // What reached the pins before any driver existed is not kept.
            _line->driverInstalled = true;
            _line->rx.clear();
            const uint64_t nowUs = _clock.nowUs();
            size_t kept = 0;
            for (size_t i = 0; i < _line->pending.size(); i++)
            {
                if (_line->pending[i].atUs > nowUs)
                {
                    _line->pending[kept++] = _line->pending[i];
                }
            }
            _line->pending.resize(kept);
        }

        if (baud == 0)
        {
            spend(AUTOBAUD_TIMEOUT_MS);
            _line->begun = false;
            return;
        }

        _baud = baud;
        if (!_line->everBegun)
        {
            _line->everBegun = true;
            _line->begunAtUs = _clock.nowUs();
        }
        _line->begun = true;
    }

    /** @brief Has anything begun the UART this port is wired to? */
    bool lineIsBegun() const { return _line->begun; }

    /** @brief When the UART of this port's line first had a driver; meaningful once it has had one. */
    uint64_t lineBegunAtUs() const { return _line->begunAtUs; }

    void sendData(uint8_t* data, uint8_t length) override
    {
        HostAllocationCensus::Scope owner(HostAllocationCensus::kBench);
        if (!putOnTheWire(data, length))
        {
            return;
        }

        if (length < 6)
        {
            return;
        }

        // Every port of the line is asked, not only this one: the request is on the wire, and what
        // answers it is whatever is powered and awake anywhere on that wire.
        bool collided = false;
        Device* dev = pickResponder(data[0], collided, data);
        if (dev == nullptr || dev->silent)
        {
            return;
        }

        answer(*dev, buildResponse(*dev, data), collided);
    }

    int readAvailable(uint8_t* buffer, size_t maxLength) override
    {
        HostAllocationCensus::Scope owner(HostAllocationCensus::kBench);
        readAvailableCalls++;
        if (!_line->begun)
        {
            return 0;
        }
        materialize();
        int n = 0;
        while (static_cast<size_t>(n) < maxLength && !_line->rx.empty())
        {
            buffer[n++] = _line->rx.front();
            _line->rx.pop_front();
        }
        return n;
    }

    int available() override
    {
        HostAllocationCensus::Scope owner(HostAllocationCensus::kBench);
        if (!_line->begun)
        {
            return 0;
        }
        materialize();
        return static_cast<int>(_line->rx.size());
    }

    const char* getPortName() const override { return _portName; }

    uint16_t calculateCRC(uint8_t* data, uint8_t length) override { return crc16(data, length); }

    bool writeRegister(int slaveAddress, int registerAddress, uint16_t value) override
    {
        HostAllocationCensus::Scope owner(HostAllocationCensus::kBench);
        if (_boardWrites)
        {
            return writeLikeTheBoard(0x10, slaveAddress, registerAddress, value);
        }
        if (!_line->begun)
        {
            framesDroppedBeforeBegin++;
            return false;
        }
        writes.push_back(WriteCall{true, slaveAddress, registerAddress, value});
        moveWhateverThatWriteAddressed(slaveAddress, registerAddress, value);
        return true;
    }

    bool writeSingleRegister(int slaveAddress, int registerAddress, uint16_t value) override
    {
        HostAllocationCensus::Scope owner(HostAllocationCensus::kBench);
        if (_boardWrites)
        {
            return writeLikeTheBoard(0x06, slaveAddress, registerAddress, value);
        }
        if (!_line->begun)
        {
            framesDroppedBeforeBegin++;
            return false;
        }
        writes.push_back(WriteCall{false, slaveAddress, registerAddress, value});
        moveWhateverThatWriteAddressed(slaveAddress, registerAddress, value);
        return true;
    }

    bool powerOnPort() override
    {
        powerOnCalls++;
        if (_powered) return false;
        spendTheSettlingTime();
        _powered = true;
        _poweredSinceUs = _clock.nowUs();
        // Rounded up to the millisecond: the record may not say the power came earlier than the
        // moment this model's devices count their wake-up from.
        _record.wentOn(static_cast<uint32_t>((_poweredSinceUs + 999) / 1000));
        powerOnTransitions++;
        if (_recordPowerOnTimes)
        {
            powerOnAtUs.push_back(_poweredSinceUs);
        }
        return true;
    }

    bool powerOffPort() override
    {
        powerOffCalls++;
        if (!_powered) return false;
        // The moment the pin goes low, before the settling time, as ModbusRS485 drives it.
        const uint64_t pinLowAtUs = _clock.nowUs();
        _record.wentOff(static_cast<uint32_t>(pinLowAtUs / 1000));
        spendTheSettlingTime();
        closeThePowerStretch();
        _powered = false;
        powerOffTransitions++;
        if (_recordPowerOffTimes)
        {
            powerOffAtUs.push_back(pinLowAtUs);
        }
        // What is in flight stops with the power, and so does what has arrived: the model has
        // always cleared both. On the board the master's receive FIFO keeps whatever already
        // reached it.
        _line->pending.clear();
        _line->rx.clear();
        return true;
    }

    bool isPowered() const { return _powered; }

    /**
     * @brief The same record ModbusRS485 keeps, stamped at the same moments: up once the settling
     *        is over, down the moment the pin goes low. A port up without a transition - every
     *        stand not started unpowered - is up for good.
     */
    PortPower powerRecord() const override
    {
        PortPower record = _record;
        record.on = _powered;
        return record;
    }

    /** @brief The line this port is wired to: the same for two ports shareTheLineWith() joined. */
    const void* wire() const override { return _line; }

    /**
     * @brief Start with the power pin down, the way the board starts every port.
     *
     * ModbusRS485's constructor drives SENSOR_POWER_n low, so nothing plugged into a module is
     * alive until something raises it. Not the default here: the buses of the tests written
     * before this one are simply "there", and nothing in them ever asks for power.
     */
    void startUnpowered() { _powered = false; }

    /** @brief Milliseconds this port's power has been up in total, counting the stretch in progress. */
    uint64_t poweredMs() const
    {
        uint64_t total = _poweredTotalUs;
        if (_powered)
        {
            total += _clock.nowUs() - _poweredSinceUs;
        }
        return total / 1000ULL;
    }

    /** @brief How long the port has had its power without a break; 0 while it is dark. */
    uint64_t currentPowerStretchMs() const
    {
        return _powered ? (_clock.nowUs() - _poweredSinceUs) / 1000ULL : 0;
    }

    /** @brief The longest single stretch of power, counting the one in progress. */
    uint64_t longestPoweredStretchMs() const
    {
        uint64_t longest = _longestPoweredStretchUs;
        if (_powered && (_clock.nowUs() - _poweredSinceUs) > longest)
        {
            longest = _clock.nowUs() - _poweredSinceUs;
        }
        return longest / 1000ULL;
    }

    // --- Observable state -------------------------------------------------------------------

    struct WriteCall { bool multi; int slave; int reg; uint16_t value; };

    std::vector<std::vector<uint8_t> > sentFrames;
    /** When each frame of `sentFrames` went out; kept only on a port asked to (recordSendTimes). */
    std::vector<uint64_t> sentAtUs;
    /**
     * How long the port had had its power when each of those frames went out - 0 for a frame that
     * found it down and raised it, as ModbusRS485::sendData() does. Kept with `sentAtUs`.
     */
    std::vector<uint64_t> sentAfterPowerForMs;
    std::vector<WriteCall> writes;
    /** Frames and register writes offered to this port while its line was not begun. */
    uint32_t framesDroppedBeforeBegin = 0;
    int beginCalls = 0;
    int readAvailableCalls = 0;
    int powerOnCalls = 0;
    int powerOffCalls = 0;
    int powerOnTransitions = 0;
    int powerOffTransitions = 0;
    /** When the power pin went low for each of those transitions; kept only when asked to. */
    std::vector<uint64_t> powerOffAtUs;
    /** When the power was up and settled for each power-on transition; kept only when asked to. */
    std::vector<uint64_t> powerOnAtUs;

    /** @brief The Modbus RTU CRC16, byte for byte the one ModbusRS485 computes on the device. */
    static uint16_t crc16(const uint8_t* data, uint8_t length)
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

private:
    struct PendingByte { uint8_t value; uint64_t atUs; };

    /** @brief A device and the moment it was plugged in, which is when its wake-up starts. */
    struct Occupant
    {
        Device device;
        uint64_t pluggedInAtUs = 0;
    };

    /**
     * @brief One UART and the ports wired to it: the bytes in flight and the receive FIFO.
     *
     * A port that shares nothing owns its own, so nothing changes for a stand of four independent
     * buses.
     */
    struct Line
    {
        std::vector<SimRs485Bus*> ports;
        std::vector<PendingByte> pending;
        std::deque<uint8_t> rx;
        /** The UART has a driver behind it; until then nothing is sent or received. See begin(). */
        bool begun = false;
        /** Whether, and when, it first had one. */
        bool everBegun = false;
        uint64_t begunAtUs = 0;
        /** The core's driver exists - installed by the first begin(), whatever its rate. */
        bool driverInstalled = false;
    };

    /**
     * @brief Which device on this wire answers a request to `addressed`, if any.
     *
     * @param collided Set when more than one powered, awake device holds that address. They all
     *        answer at once - two drivers on one pair - and what the master reads is not the frame
     *        either of them sent: modelled as the first one's frame with a checksum that does not
     *        match it, which every reader of this firmware rejects as it would reject the garbage
     *        of a real collision. The header bytes stay intact, because two devices answering
     *        one address put the same address and function code on the wire.
     * @param request The request, when it is a read: a device that does not answer the register it
     *        starts at is not on the wire for it (answersRegistersItDoesNotHave). Null for a write.
     */
    Device* pickResponder(uint8_t addressed, bool& collided, const uint8_t* request = nullptr)
    {
        Device* exact = nullptr;
        size_t exactCount = 0;
        Device* theOnlyWildcardAnswerer = nullptr;
        size_t wildcardAnswerers = 0;

        for (size_t p = 0; p < _line->ports.size(); p++)
        {
            SimRs485Bus* port = _line->ports[p];
            if (!port->_powered)
            {
                // Its power pin is down: whatever is plugged into it cannot drive the line.
                continue;
            }

            for (std::map<uint8_t, Occupant>::iterator it = port->_devices.begin();
                 it != port->_devices.end(); ++it)
            {
                if (!port->occupantIsAwake(it->second))
                {
                    continue;
                }
                if (request != nullptr && !answersTheRegister(it->second.device, request))
                {
                    continue;
                }
                if (it->second.device.answersTheWildcard)
                {
                    wildcardAnswerers++;
                    theOnlyWildcardAnswerer = &it->second.device;
                }
                if (it->first == addressed)
                {
                    if (exact == nullptr)
                    {
                        exact = &it->second.device;
                    }
                    exactCount++;
                }
            }
        }

        collided = exactCount > 1;
        if (exact != nullptr)
        {
            return exact;
        }
        // 0xFF is the wildcard the discovery frames use: any single device on the line
        // that answers it does. 0x00 is the Modbus broadcast address - nobody answers it.
        //
        // Two devices awake on one line both answer a wildcard at once, which on the wire is a
        // collision too. Here it stays what it has always been in this model - no answer - rather
        // than a corrupted frame: the presence poll counts bytes, not checksums, and the stands
        // that exercise it were calibrated on this.
        if (addressed == 0xFF && wildcardAnswerers == 1)
        {
            return theOnlyWildcardAnswerer;
        }
        return nullptr;
    }

    /**
     * @brief What sendData() does before the first byte: the send delay, the power, the record.
     * @return false when the line was never begun: the frame is counted and goes nowhere
     */
    bool putOnTheWire(const uint8_t* data, uint8_t length)
    {
        if (_masterSendDelayMs > 0)
        {
            spend(_masterSendDelayMs);
        }
        if (_boardPower && !_powered)
        {
            // What ModbusRS485::sendData() does before it writes a byte: the port whose power pin
            // is down gets it raised, and pays the settling time once.
            powerOnPort();
        }

        if (!_line->begun)
        {
            framesDroppedBeforeBegin++;
            return false;
        }

        sentFrames.push_back(std::vector<uint8_t>(data, data + length));
        if (_recordSendTimes)
        {
            sentAtUs.push_back(_clock.nowUs());
            sentAfterPowerForMs.push_back(currentPowerStretchMs());
        }
        return true;
    }

    /** @brief Put a device's answer on the line, garbled when another device answered with it. */
    void answer(const Device& dev, std::vector<uint8_t> frame, bool collided)
    {
        if (collided && frame.size() >= 2)
        {
            frame[frame.size() - 2] ^= 0x01;
        }
        scheduleFrame(frame, _clock.nowUs() + dev.turnaroundUs);
    }

    /**
     * @brief ModbusRS485::writeRegister() / writeSingleRegister(), frame for frame.
     *
     * The request as the driver builds it, sent through the same wire; the acknowledgement a
     * Modbus device gives a write - the first six bytes of the request back, from the address it
     * held when the frame arrived - and none for a broadcast to 0x00; the driver's 300 ms; and the
     * driver's reading of the answer, which is also what it returns.
     */
    bool writeLikeTheBoard(uint8_t functionCode, int slaveAddress, int registerAddress, uint16_t value)
    {
        std::vector<uint8_t> request;
        request.push_back(static_cast<uint8_t>(slaveAddress));
        request.push_back(functionCode);
        request.push_back(static_cast<uint8_t>(registerAddress >> 8));
        request.push_back(static_cast<uint8_t>(registerAddress & 0xFF));
        if (functionCode == 0x10)
        {
            request.push_back(0x00);
            request.push_back(0x01);
            request.push_back(0x02);
        }
        request.push_back(static_cast<uint8_t>(value >> 8));
        request.push_back(static_cast<uint8_t>(value & 0xFF));
        const uint16_t crc = crc16(&request[0], static_cast<uint8_t>(request.size()));
        request.push_back(static_cast<uint8_t>(crc & 0xFF));
        request.push_back(static_cast<uint8_t>(crc >> 8));

        if (!putOnTheWire(&request[0], static_cast<uint8_t>(request.size())))
        {
            // Nothing went out, so nothing answers and nothing moves; the driver still waits.
            spend(WRITE_ANSWER_WAIT_MS);
            return false;
        }
        writes.push_back(WriteCall{functionCode == 0x10, slaveAddress, registerAddress, value});

        if (slaveAddress != 0x00)
        {
            bool collided = false;
            Device* dev = pickResponder(static_cast<uint8_t>(slaveAddress), collided);
            if (dev != nullptr && !dev->silent)
            {
                std::vector<uint8_t> acknowledgement(request.begin(), request.begin() + 6);
                const uint16_t ackCrc = crc16(&acknowledgement[0], 6);
                acknowledgement.push_back(static_cast<uint8_t>(ackCrc & 0xFF));
                acknowledgement.push_back(static_cast<uint8_t>(ackCrc >> 8));
                answer(*dev, acknowledgement, collided);
            }
        }
        moveWhateverThatWriteAddressed(slaveAddress, registerAddress, value);

        spend(WRITE_ANSWER_WAIT_MS);

        uint8_t response[8];
        const int bytesRead = readAvailable(response, sizeof(response));
        return bytesRead >= 6 && response[0] == static_cast<uint8_t>(slaveAddress) &&
               response[1] == functionCode;
    }

    /** @brief Time the master spends: yielded through its adapter when it has one, as on the board. */
    void spend(unsigned long ms)
    {
        if (_masterArduino != nullptr)
        {
            _masterArduino->delay(ms);
        }
        else
        {
            _clock.advanceMs(ms);
        }
    }

    /**
     * @brief Move whatever on this wire the master's register write has just re-addressed.
     *
     * The write is a frame on the wire, so every port of it hears the frame - which is precisely
     * why the firmware takes the other ports' power down before it writes an address - and a
     * device hears it if it is powered and awake, if the frame is addressed to it or broadcast to
     * 0x00, and if the register written is the one it keeps its address in. From then on it
     * answers at the new address and at no other.
     */
    void moveWhateverThatWriteAddressed(int slaveAddress, int registerAddress, uint16_t value)
    {
        if (value > 0xFF)
        {
            return;
        }

        for (size_t p = 0; p < _line->ports.size(); p++)
        {
            SimRs485Bus* port = _line->ports[p];
            if (!port->_powered)
            {
                continue;
            }

            std::vector<uint8_t> moved;
            for (std::map<uint8_t, Occupant>::iterator it = port->_devices.begin();
                 it != port->_devices.end(); ++it)
            {
                if (!it->second.device.addressIsWritable ||
                    it->second.device.addressStoreRegister != (uint16_t)registerAddress)
                {
                    continue;
                }
                if (slaveAddress != 0x00 && slaveAddress != static_cast<int>(it->first))
                {
                    continue;
                }
                if (!port->occupantIsAwake(it->second))
                {
                    continue;
                }
                moved.push_back(it->first);
            }

            for (size_t i = 0; i < moved.size(); i++)
            {
                if (moved[i] == static_cast<uint8_t>(value))
                {
                    continue;
                }
                Occupant occupant = port->_devices[moved[i]];
                occupant.device.address = static_cast<uint8_t>(value);
                port->_devices.erase(moved[i]);
                // Its power never went down, so it is awake at the new address the moment it
                // answers to it: pluggedInAtUs travels with it.
                port->_devices[static_cast<uint8_t>(value)] = occupant;
            }
        }
    }

    /** @brief Does this device answer a read (FC03) that starts where this request starts? */
    static bool answersTheRegister(const Device& device, const uint8_t* request)
    {
        if (device.answersRegistersItDoesNotHave || !device.registersAreAddressed || request[1] != 0x03)
        {
            return true;
        }
        const size_t first = static_cast<size_t>((request[2] << 8) | request[3]);
        return first < device.registerValues.size();
    }

    /** @brief Has this occupant had power for as long as it says it needs? */
    bool occupantIsAwake(const Occupant& occupant) const
    {
        if (occupant.device.wakeUpMs == 0)
        {
            return true;
        }

        const uint64_t poweredAndPresentUs =
            (occupant.pluggedInAtUs > _poweredSinceUs) ? occupant.pluggedInAtUs : _poweredSinceUs;
        return _clock.nowUs() >=
               poweredAndPresentUs + static_cast<uint64_t>(occupant.device.wakeUpMs) * 1000ULL;
    }

    /** @brief The 100 ms every power transition of ModbusRS485 sleeps for the sensor to settle. */
    void spendTheSettlingTime()
    {
        if (!_boardPower)
        {
            return;
        }
        if (_masterArduino != nullptr)
        {
            _masterArduino->delay(POWER_SETTLE_MS);
        }
        else
        {
            _clock.advanceMs(POWER_SETTLE_MS);
        }
    }

    void closeThePowerStretch()
    {
        const uint64_t stretch = _clock.nowUs() - _poweredSinceUs;
        _poweredTotalUs += stretch;
        if (stretch > _longestPoweredStretchUs)
        {
            _longestPoweredStretchUs = stretch;
        }
    }

    std::vector<uint8_t> buildResponse(const Device& dev, const uint8_t* request) const
    {
        uint8_t functionCode = (dev.answerAsFunctionCode != 0) ? dev.answerAsFunctionCode
                                                               : request[1];
        uint8_t responder = (dev.answerAsAddress != 0) ? dev.answerAsAddress : dev.address;
        int numRegisters = (static_cast<int>(request[4]) << 8) | request[5];
        if (numRegisters < 1) numRegisters = 1;
        const size_t first = dev.registersAreAddressed
                                 ? static_cast<size_t>((request[2] << 8) | request[3])
                                 : 0;

        std::vector<uint16_t> values;
        for (int i = 0; i < numRegisters; i++)
        {
            const size_t reg = first + static_cast<size_t>(i);
            values.push_back(reg < dev.registerValues.size() ? dev.registerValues[reg]
                                                             : dev.defaultRegisterValue);
        }

        std::vector<uint8_t> frame = makeReadResponse(responder, functionCode, values);
        if (dev.corruptCrc)
        {
            // One flipped bit in the CRC: a frame whose contents are intact and whose checksum
            // says otherwise, which is what line noise on the checksum itself looks like.
            frame[frame.size() - 2] ^= 0x01;
        }
        return frame;
    }

    void scheduleFrame(const std::vector<uint8_t>& frame, uint64_t firstByteAtUs)
    {
        uint64_t perByteUs = 10ULL * 1000000ULL / static_cast<uint64_t>(_baud);
        for (size_t i = 0; i < frame.size(); i++)
        {
            PendingByte b;
            b.value = frame[i];
            b.atUs = firstByteAtUs + perByteUs * static_cast<uint64_t>(i);
            _line->pending.push_back(b);
        }
        std::stable_sort(_line->pending.begin(), _line->pending.end(), earlierFirst);
    }

    static bool earlierFirst(const PendingByte& a, const PendingByte& b) { return a.atUs < b.atUs; }

    void materialize()
    {
        uint64_t now = _clock.nowUs();
        size_t i = 0;
        while (i < _line->pending.size() && _line->pending[i].atUs <= now)
        {
            _line->rx.push_back(_line->pending[i].value);
            i++;
        }
        if (i > 0)
        {
            _line->pending.erase(_line->pending.begin(), _line->pending.begin() + i);
        }
    }

    VirtualClock& _clock;
    const char* _portName;
    unsigned long _baud = 9600;
    unsigned long _masterSendDelayMs = 0;
    IArduinoAdapter* _masterArduino = nullptr;
    /** ModbusRS485 sleeps this long after it moves a power pin, in either direction. */
    static const unsigned long POWER_SETTLE_MS = 100;
    /** ModbusRS485's two register writes sleep this long between the request and the reading. */
    static const unsigned long WRITE_ANSWER_WAIT_MS = 300;
    /** HardwareSerial::begin()'s default timeout_ms, which a zero rate spends detecting one. */
    static const unsigned long AUTOBAUD_TIMEOUT_MS = 20000;
    bool _boardPower = false;
    bool _boardWrites = false;
    bool _recordSendTimes = false;
    bool _recordPowerOffTimes = false;
    bool _recordPowerOnTimes = false;
    PortPower _record;
    bool _powered = true;
    /** When the power of this port last came up, and what it has cost in total. */
    uint64_t _poweredSinceUs = 0;
    uint64_t _poweredTotalUs = 0;
    uint64_t _longestPoweredStretchUs = 0;
    std::map<uint8_t, Occupant> _devices;
    Line _ownLine;
    Line* _line = &_ownLine;
};

} // namespace hostsim
