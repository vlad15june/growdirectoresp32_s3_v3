// The hundred milliseconds of sensor power belong to the transition, not to the request.
//
// What is under test is the real ModbusRS485 - the class that owns the SENSOR_POWER_n pin and the
// only place that delay is spent. It is admitted to the host build the same way ArduinoJsonConverter
// was: through a stand-in for the device-only header it needs (test/Mock/native/HardwareSerial.h),
// not by copying its logic into a model of it.
//
// The defect: `sendData()` raised the power pin and slept 100 ms on *every* send, while
// `_isPoweredOn` - a flag this class has always had and has always maintained in powerOnPort() and
// powerOffPort() - was never consulted here. On a port powered since boot that is a wait for an
// event that already happened, paid once per channel per attempt: 100 ms of the ~145 ms a healthy
// channel costs, and 300 ms of the 1400 ms a silent sensor costs.
//
// To see the red: make the wait unconditional again - drop the `_isPoweredOn` check in front of
// it - and every cost assertion below fails with the 100 ms per send back in the figure. The
// numbers each assertion expects in that case are named in its message.

#include <unity.h>

#include "Infrastructure/Adapters/Rs485/ModbusRS485.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"

#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"

#include "Sim/SimArduino.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::SimArduino;
using hostsim::SimRs485Bus;
using hostsim::VirtualClock;

namespace {

const uint8_t POWER_PIN = 41;
const uint8_t CONTROL_PIN = 19;

// What a channel costs in the reader, from ModbusSensorReader's own constants: a 5 ms inter-frame
// gap plus a response window polled every 5 ms. A device answering in 30 ms is found on the eighth
// poll.
const uint32_t HEALTHY_ANSWER_TURNAROUND_MS = 30;

// Three attempts of the 300 ms budget with SensorReader's 100 ms between them - but this file
// exercises ModbusSensorReader directly, so it counts single reads and does its own arithmetic.
const uint32_t SILENT_READ_MS = 300;

const uint32_t POWER_SETTLE_MS = 100;

/**
 * @brief One device on the far end of the stub UART, answering in virtual time.
 *
 * Everything about *when* bytes appear lives here rather than in the header stand-in: a request is
 * answered `turnaroundMs` after it was flushed, one byte at a time at 9600 baud, exactly as
 * SimRs485Bus does it for the tests that replace ModbusRS485 instead of driving it.
 */
class LineWithOneDevice {
public:
    LineWithOneDevice(HardwareSerial& port, VirtualClock& clock)
        : _port(port), _clock(clock)
    {
        _port.onFlush = [this](const std::vector<uint8_t>& request) { answer(request); };
        _port.onPoll = [this]() { materialize(); };
    }

    void setSilent(bool silent) { _silent = silent; }
    void setAddress(uint8_t address) { _address = address; }
    void setRegisterValue(uint16_t value) { _registerValue = value; }

    uint32_t requestCount() const { return _requestCount; }

    /** When each request was flushed onto the line, in virtual milliseconds. */
    std::vector<uint64_t> requestAtMs;

private:
    void answer(const std::vector<uint8_t>& request)
    {
        _requestCount++;
        requestAtMs.push_back(_clock.nowMs());
        if (_silent || request.size() < 6)
        {
            return;
        }

        std::vector<uint16_t> registers;
        int count = (static_cast<int>(request[4]) << 8) | request[5];
        if (count < 1)
        {
            count = 1;
        }
        for (int i = 0; i < count; i++)
        {
            registers.push_back(_registerValue);
        }

        const std::vector<uint8_t> frame =
            SimRs485Bus::makeReadResponse(_address, request[1], registers);

        const uint64_t perByteUs = 10ULL * 1000000ULL / 9600ULL;
        const uint64_t firstByteAtUs =
            _clock.nowUs() + static_cast<uint64_t>(HEALTHY_ANSWER_TURNAROUND_MS) * 1000ULL;
        for (size_t i = 0; i < frame.size(); i++)
        {
            _pending.push_back(Pending{frame[i], firstByteAtUs + perByteUs * i});
        }
    }

    void materialize()
    {
        const uint64_t now = _clock.nowUs();
        size_t released = 0;
        while (released < _pending.size() && _pending[released].atUs <= now)
        {
            _port.push(_pending[released].value);
            released++;
        }
        if (released > 0)
        {
            _pending.erase(_pending.begin(), _pending.begin() + released);
        }
    }

    struct Pending { uint8_t value; uint64_t atUs; };

    HardwareSerial& _port;
    VirtualClock& _clock;
    std::vector<Pending> _pending;
    bool _silent = false;
    uint8_t _address = 1;
    uint16_t _registerValue = 250;
    uint32_t _requestCount = 0;
};

struct Fixture {
    VirtualClock clock;
    SimArduino arduino;
    HardwareSerial port;
    ModbusRS485 rs485;
    LineWithOneDevice line;
    MockLogger logger;
    MockDebugSystem debugSystem;
    std::vector<IModbusRS485*> ports;
    ModbusSensorReader reader;

    Fixture()
        : arduino(clock),
          rs485(port, CONTROL_PIN, "test-port", arduino, SERIAL_8N1, 18, 17, POWER_PIN),
          line(port, clock),
          ports{&rs485},
          reader(ports, arduino, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false;
        reader.init(9600);
    }

    static ModbusRegisterConfig channelConfig()
    {
        ModbusRegisterConfig config;
        config.functionCode = 0x03;
        config.startRegister = 0x0101;
        config.numRegisters = 1;
        config.multiplier = 0.1f;
        config.offset = 0.0f;
        config.channel = 1;
        config.channelName = "temperature";
        return config;
    }
};

uint8_t request[8] = {0x01, 0x03, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00};

} // namespace

// --------------------------------------------------------------------------------------------
// The delay itself
// --------------------------------------------------------------------------------------------

// A port that has never been powered pays the settle on its first send - the transition is real.
static void test_the_first_send_on_an_unpowered_port_still_pays_the_settling_time(void)
{
    Fixture f;

    const uint64_t startedAtMs = f.clock.nowMs();
    f.rs485.sendData(request, sizeof(request));
    const uint64_t costMs = f.clock.nowMs() - startedAtMs;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(POWER_SETTLE_MS, (uint32_t)costMs,
        "the first send after boot did not wait for the sensors to come up");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, f.arduino.digitalOutputs[POWER_PIN],
        "the sensor power pin was not raised");
}

// And every send after that pays nothing, because nothing transitions.
static void test_a_send_on_an_already_powered_port_costs_nothing(void)
{
    Fixture f;

    f.rs485.sendData(request, sizeof(request)); // the real transition
    const uint64_t startedAtMs = f.clock.nowMs();

    for (int i = 0; i < 10; i++)
    {
        f.rs485.sendData(request, sizeof(request));
    }

    const uint64_t costMs = f.clock.nowMs() - startedAtMs;

    char message[192];
    snprintf(message, sizeof(message),
             "ten sends on a port that was already powered cost %lu ms; they must cost 0, and "
             "with a settling time on every send they cost %lu - one settling time each",
             (unsigned long)costMs, (unsigned long)(10 * POWER_SETTLE_MS));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)costMs, message);
}

// powerOnPort() keeps its own settle - that transition is real too - and the sends after it are free.
static void test_the_settling_time_is_paid_by_power_on_not_by_the_send_after_it(void)
{
    Fixture f;

    const uint64_t powerOnStartedAtMs = f.clock.nowMs();
    TEST_ASSERT_TRUE_MESSAGE(f.rs485.powerOnPort(), "powerOnPort() did not report a state change");
    const uint64_t powerOnCostMs = f.clock.nowMs() - powerOnStartedAtMs;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(POWER_SETTLE_MS, (uint32_t)powerOnCostMs,
        "powerOnPort() no longer waits for the sensors to come up");

    const uint64_t sendStartedAtMs = f.clock.nowMs();
    f.rs485.sendData(request, sizeof(request));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)(f.clock.nowMs() - sendStartedAtMs),
        "the send after powerOnPort() paid the settling time a second time");
}

// A port whose power was cut and comes back is a transition again, and it is paid for. This is the
// case the task calls out: recovery through setModbusPortPower() must keep its settling time.
static void test_power_off_then_a_send_pays_the_settling_time_again(void)
{
    Fixture f;

    f.rs485.powerOnPort();
    TEST_ASSERT_TRUE_MESSAGE(f.rs485.powerOffPort(), "powerOffPort() did not report a state change");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, f.arduino.digitalOutputs[POWER_PIN],
        "the sensor power pin was not lowered");

    const uint64_t startedAtMs = f.clock.nowMs();
    f.rs485.sendData(request, sizeof(request));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(POWER_SETTLE_MS, (uint32_t)(f.clock.nowMs() - startedAtMs),
        "a send after the port lost power did not wait for the sensors to come back");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, f.arduino.digitalOutputs[POWER_PIN],
        "the sensor power pin was not raised again after the port was powered off");
}

// --------------------------------------------------------------------------------------------
// What it is worth: the cost of a poll, measured through the real reader over the real adapter
// --------------------------------------------------------------------------------------------

static void test_a_healthy_channel_no_longer_pays_the_power_delay_on_every_read(void)
{
    Fixture f;
    f.rs485.powerOnPort(); // as prepareSensorsForReading() does at startup

    float value = 0.0f;
    const uint64_t startedAtMs = f.clock.nowMs();
    const bool ok = f.reader.readRegister(1, Fixture::channelConfig(), value, 0);
    const uint64_t costMs = f.clock.nowMs() - startedAtMs;

    TEST_ASSERT_TRUE_MESSAGE(ok, "the healthy device was not read at all");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, f.line.requestCount(), "more than one request went out");

    char message[224];
    snprintf(message, sizeof(message),
             "one channel of a device answering in %lu ms cost %lu ms; the settling time on every "
             "send would add %lu ms of that",
             (unsigned long)HEALTHY_ANSWER_TURNAROUND_MS, (unsigned long)costMs,
             (unsigned long)POWER_SETTLE_MS);

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(60, (unsigned int)costMs, message);
}

static void test_a_silent_channel_costs_the_read_budget_and_not_a_millisecond_of_power(void)
{
    Fixture f;
    f.rs485.powerOnPort();
    f.line.setSilent(true);

    float value = 0.0f;
    const uint64_t startedAtMs = f.clock.nowMs();
    const bool ok = f.reader.readRegister(1, Fixture::channelConfig(), value, 0);
    const uint64_t costMs = f.clock.nowMs() - startedAtMs;

    TEST_ASSERT_FALSE_MESSAGE(ok, "a silent device was read as a measurement");

    char message[224];
    snprintf(message, sizeof(message),
             "one read of a silent device cost %lu ms; the read budget alone is %lu ms, and the "
             "settling time on every send would add %lu ms on top of it",
             (unsigned long)costMs, (unsigned long)SILENT_READ_MS,
             (unsigned long)POWER_SETTLE_MS);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SILENT_READ_MS, (uint32_t)costMs, message);
}

// --------------------------------------------------------------------------------------------
// When the power came: the port's record of it, and what waits on it
// --------------------------------------------------------------------------------------------

/** The time this firmware gives a sensor to come up, written out: what the tests below hold it to. */
const uint64_t WAKE_UP_MS = 10000;

// The port keeps what its pin did and when: nothing until something moves it; up once the settling
// is over, whether powerOnPort() or a send raised it; down the moment the pin goes low.
static void test_the_port_records_when_its_power_came_and_went(void)
{
    Fixture f;
    PortPower power = f.rs485.powerRecord();
    TEST_ASSERT_FALSE(power.on);
    TEST_ASSERT_EQUAL_UINT32(0, power.ons);
    TEST_ASSERT_EQUAL_UINT32(WAKE_UP_MS, power.wakeUpOwedMs((uint32_t)f.clock.nowMs()));

    const uint64_t firstUpMs = f.clock.nowMs() + 1000;
    f.arduino.delay(1000);
    f.rs485.powerOnPort();
    power = f.rs485.powerRecord();
    TEST_ASSERT_TRUE(power.on);
    TEST_ASSERT_EQUAL_UINT32(1, power.ons);
    TEST_ASSERT_EQUAL_UINT32(firstUpMs + POWER_SETTLE_MS, power.onAtMs);

    f.arduino.delay(5000);
    const uint64_t downMs = f.clock.nowMs();
    f.rs485.powerOffPort();
    power = f.rs485.powerRecord();
    TEST_ASSERT_FALSE(power.on);
    TEST_ASSERT_EQUAL_UINT32(1, power.offs);
    TEST_ASSERT_EQUAL_UINT32(downMs, power.offAtMs);

    f.arduino.delay(1000);
    const uint64_t raisedMs = f.clock.nowMs();
    f.rs485.sendData(request, sizeof(request));
    power = f.rs485.powerRecord();
    TEST_ASSERT_TRUE(power.on);
    TEST_ASSERT_EQUAL_UINT32(2, power.ons);
    TEST_ASSERT_EQUAL_UINT32(raisedMs + POWER_SETTLE_MS, power.onAtMs);
    TEST_ASSERT_EQUAL_UINT32(WAKE_UP_MS - 3000,
                             power.wakeUpOwedMs((uint32_t)(raisedMs + POWER_SETTLE_MS + 3000)));
}

/** @brief Somebody else - the sweep, the preparation - switches the port on; 3 s go by. */
static uint64_t switchedOnThreeSecondsAgo(Fixture& f)
{
    f.rs485.powerOnPort();
    const uint64_t upAtMs = f.clock.nowMs();
    f.arduino.delay(3000);
    return upAtMs;
}

static std::string firstRequestAfter(const Fixture& f, uint64_t upAtMs)
{
    char text[160];
    snprintf(text, sizeof(text), "the first frame went %lu ms after the port came up - before the %lu ms "
             "a sensor is given to come up",
             f.line.requestAtMs.empty() ? 0UL : (unsigned long)(f.line.requestAtMs[0] - upAtMs),
             (unsigned long)WAKE_UP_MS);
    return std::string(text);
}

// An address write into a port switched on 3 s ago waits for the other 7 before its first frame: a
// device that has not come up takes no write, and a write it did not take is a sensor left where it
// was.
static void test_an_address_write_waits_for_a_port_switched_on_moments_ago(void)
{
    Fixture f;
    f.line.setSilent(true);
    const uint64_t upAtMs = switchedOnThreeSecondsAgo(f);

    f.reader.writeSensorAddress(1, 0x70, 0, 0x0100, 0x06, 0x0101);

    TEST_ASSERT_TRUE_MESSAGE(!f.line.requestAtMs.empty(), "the address write put nothing on the line");
    TEST_ASSERT_TRUE_MESSAGE(f.line.requestAtMs[0] >= upAtMs + WAKE_UP_MS,
                             firstRequestAfter(f, upAtMs).c_str());
}

// The model scan the same: its ping waits for the rest of the wake-up of a port it found on.
static void test_a_model_scan_waits_for_a_port_switched_on_moments_ago(void)
{
    Fixture f;
    SensorReader sensorReader(nullptr, &f.reader, &f.arduino, nullptr, nullptr);
    const uint64_t upAtMs = switchedOnThreeSecondsAgo(f);

    SensorEntity sensor;
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.baudRate = 9600;
    sensor.modbusDeviceAddress = 1;
    sensor.modbusPingAddress = 0x0101;
    sensor.modbusRegisterConfigs.push_back(Fixture::channelConfig());
    sensorReader.scanSensor(sensor);

    TEST_ASSERT_TRUE_MESSAGE(!f.line.requestAtMs.empty(), "the scan put nothing on the line");
    TEST_ASSERT_TRUE_MESSAGE(f.line.requestAtMs[0] >= upAtMs + WAKE_UP_MS,
                             firstRequestAfter(f, upAtMs).c_str());
}

// The preparation writes the rainfall window into an XF700A: that write too waits for a port that
// came up moments ago, however the preparation found it.
static void test_the_preparation_writes_nothing_into_a_port_switched_on_moments_ago(void)
{
    Fixture f;
    SensorReader sensorReader(nullptr, &f.reader, &f.arduino, nullptr, nullptr);
    const uint64_t upAtMs = switchedOnThreeSecondsAgo(f);

    SensorEntity sensor;
    sensor.protocol = "ModBus";
    sensor.model = "XF700A";
    sensor.pin = 0;
    sensor.baudRate = 9600;
    sensor.modbusDeviceAddress = 1;
    sensorReader.prepareSensorsForReading(std::vector<SensorEntity>(1, sensor));

    TEST_ASSERT_TRUE_MESSAGE(!f.line.requestAtMs.empty(), "the preparation put nothing on the line");
    TEST_ASSERT_TRUE_MESSAGE(f.line.requestAtMs[0] >= upAtMs + WAKE_UP_MS,
                             firstRequestAfter(f, upAtMs).c_str());
}

// And a preparation with nothing to write holds the line for no port: the sweep does not read a port
// before it has come up, and waiting here would only keep it and every other port off the line.
static void test_the_preparation_waits_for_no_port_it_does_not_write_into(void)
{
    Fixture f;
    SensorReader sensorReader(nullptr, &f.reader, &f.arduino, nullptr, nullptr);
    switchedOnThreeSecondsAgo(f);

    SensorEntity sensor;
    sensor.protocol = "ModBus";
    sensor.model = "RY-WS301";
    sensor.pin = 0;
    sensor.baudRate = 9600;
    sensor.modbusDeviceAddress = 1;
    const uint64_t startedAtMs = f.clock.nowMs();
    sensorReader.prepareSensorsForReading(std::vector<SensorEntity>(1, sensor));

    char message[160];
    snprintf(message, sizeof(message),
             "the preparation of a sensor it writes nothing into held the line for %lu ms",
             (unsigned long)(f.clock.nowMs() - startedAtMs));
    TEST_ASSERT_TRUE_MESSAGE(f.clock.nowMs() - startedAtMs < 1000, message);
    TEST_ASSERT_TRUE_MESSAGE(f.line.requestAtMs.empty(), "the preparation put a frame on the line");
}

// A raw command from the backend: into a dark port - switched on, then waited for - and into a port
// switched on moments ago, the frame goes once what is on the port has come up.
static void test_a_raw_command_waits_for_the_port_to_come_up(void)
{
    const uint8_t pdu[6] = {0x01, 0x03, 0x01, 0x01, 0x00, 0x01};
    {
        Fixture f;
        RawBusAccessAdapter raw(f.ports, f.arduino, &f.logger, &f.debugSystem);
        const uint64_t calledAtMs = f.clock.nowMs();
        std::vector<uint8_t> out;
        raw.modbusRequest(0, pdu, sizeof(pdu), true, 1000, out);
        TEST_ASSERT_TRUE_MESSAGE(!f.line.requestAtMs.empty(), "the raw read put nothing on the line");
        TEST_ASSERT_TRUE_MESSAGE(f.line.requestAtMs[0] >= calledAtMs + POWER_SETTLE_MS + WAKE_UP_MS,
                                 firstRequestAfter(f, calledAtMs + POWER_SETTLE_MS).c_str());
        TEST_ASSERT_FALSE_MESSAGE(out.empty(), "the device did not answer the raw read");
    }
    {
        Fixture f;
        RawBusAccessAdapter raw(f.ports, f.arduino, &f.logger, &f.debugSystem);
        const uint64_t upAtMs = switchedOnThreeSecondsAgo(f);
        std::vector<uint8_t> out;
        raw.modbusRequest(0, pdu, sizeof(pdu), true, 1000, out);
        TEST_ASSERT_TRUE_MESSAGE(!f.line.requestAtMs.empty(), "the raw read put nothing on the line");
        TEST_ASSERT_TRUE_MESSAGE(f.line.requestAtMs[0] >= upAtMs + WAKE_UP_MS,
                                 firstRequestAfter(f, upAtMs).c_str());
    }
}

// --------------------------------------------------------------------------------------------
// Which ports are one wire
// --------------------------------------------------------------------------------------------

namespace {

/**
 * @brief The four ports as globals.h builds them: ports 0 and 1 on one HardwareSerial and 2 and 3
 *        on the other, each with its own SENSOR_POWER pin (42, 40, 41, 39 - ModuleConfig.h).
 */
struct BoardPorts {
    static const uint8_t POWER_PINS[4];

    VirtualClock clock;
    SimArduino arduino;
    HardwareSerial uart1;
    HardwareSerial uart2;
    ModbusRS485 port0;
    ModbusRS485 port1;
    ModbusRS485 port2;
    ModbusRS485 port3;
    MockLogger logger;
    MockDebugSystem debugSystem;
    std::vector<IModbusRS485*> ports;
    ModbusSensorReader reader;

    BoardPorts()
        : arduino(clock),
          uart1(1),
          uart2(2),
          port0(uart1, 19, "ModbusRS485_1", arduino, SERIAL_8N1, 18, 17, POWER_PINS[0]),
          port1(uart1, 19, "ModbusRS485_2", arduino, SERIAL_8N1, 18, 17, POWER_PINS[1]),
          port2(uart2, 20, "ModbusRS485_1", arduino, SERIAL_8N1, 15, 14, POWER_PINS[2]),
          port3(uart2, 20, "ModbusRS485_2", arduino, SERIAL_8N1, 15, 14, POWER_PINS[3]),
          ports{&port0, &port1, &port2, &port3},
          reader(ports, arduino, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false;
        reader.init(9600);
    }
};

const uint8_t BoardPorts::POWER_PINS[4] = {42, 40, 41, 39};

} // namespace

// Which ports share a wire is read off the ports themselves: two built on one HardwareSerial are one
// line, and a port on the other UART is not on it.
static void test_the_ports_built_on_one_uart_are_one_wire(void)
{
    BoardPorts b;

    const std::vector<int> lines = b.reader.portLines();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, (uint32_t)lines.size(), "not one line per port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(lines[0], lines[1], "ports 0 and 1 are on one UART and not on one line");
    TEST_ASSERT_EQUAL_INT_MESSAGE(lines[2], lines[3], "ports 2 and 3 are on one UART and not on one line");
    TEST_ASSERT_TRUE_MESSAGE(lines[0] != lines[2], "ports 0 and 2 are on two UARTs and on one line");

    TEST_ASSERT_TRUE(b.reader.sharesAWire(0, 1));
    TEST_ASSERT_TRUE(b.reader.sharesAWire(3, 2));
    TEST_ASSERT_TRUE(b.reader.sharesAWire(2, 2));
    TEST_ASSERT_FALSE(b.reader.sharesAWire(0, 2));
    TEST_ASSERT_FALSE(b.reader.sharesAWire(1, 3));
    TEST_ASSERT_FALSE(b.reader.sharesAWire(1, 2));
    TEST_ASSERT_FALSE_MESSAGE(b.reader.sharesAWire(0, 4), "a port the reader does not have shares a wire");
    TEST_ASSERT_FALSE_MESSAGE(b.reader.sharesAWire(-1, 0), "a port the reader does not have shares a wire");

    // The same four ports built the other way round - 0 and 3 on one UART, 1 and 2 on the other - are
    // paired the other way round: the answer follows the UART, not the port's number.
    std::vector<IModbusRS485*> crossed;
    crossed.push_back(&b.port0);
    crossed.push_back(&b.port2);
    crossed.push_back(&b.port3);
    crossed.push_back(&b.port1);
    ModbusSensorReader crossedReader(crossed, b.arduino, &b.logger, &b.debugSystem);
    const std::vector<int> crossedLines = crossedReader.portLines();
    TEST_ASSERT_EQUAL_INT_MESSAGE(crossedLines[0], crossedLines[3],
                                  "ports 0 and 3 are on one UART and not on one line");
    TEST_ASSERT_EQUAL_INT_MESSAGE(crossedLines[1], crossedLines[2],
                                  "ports 1 and 2 are on one UART and not on one line");
    TEST_ASSERT_TRUE_MESSAGE(crossedLines[0] != crossedLines[1], "ports 0 and 1 are on two UARTs and on one line");
    TEST_ASSERT_FALSE(crossedReader.sharesAWire(0, 1));
    TEST_ASSERT_TRUE(crossedReader.sharesAWire(0, 3));
}

namespace {

/** @brief The level a port's power pin was last driven to, and how many times it was driven. */
unsigned powerPinOf(BoardPorts& b, int port)
{
    return b.arduino.digitalOutputs[BoardPorts::POWER_PINS[port]];
}

int powerPinWritesOf(BoardPorts& b, int port)
{
    return b.arduino.digitalWriteCounts[BoardPorts::POWER_PINS[port]];
}

/** @brief Every port up, as on a module with a sensor on each; pin writes are counted from here. */
void powerEveryPort(BoardPorts& b, int writesBefore[4])
{
    for (int i = 0; i < 4; i++)
    {
        b.ports[static_cast<size_t>(i)]->powerOnPort();
    }
    for (int i = 0; i < 4; i++)
    {
        writesBefore[i] = powerPinWritesOf(b, i);
    }
}

/** @brief Say what the isolation of the target did to every port's power pin. */
std::string pinsAfter(BoardPorts& b, const int writesBefore[4], int target)
{
    char text[240];
    snprintf(text, sizeof(text),
             "isolating port %d left the power pins of ports 0/1/2/3 at %u/%u/%u/%u after "
             "%d/%d/%d/%d write(s)",
             target, powerPinOf(b, 0), powerPinOf(b, 1), powerPinOf(b, 2), powerPinOf(b, 3),
             powerPinWritesOf(b, 0) - writesBefore[0], powerPinWritesOf(b, 1) - writesBefore[1],
             powerPinWritesOf(b, 2) - writesBefore[2], powerPinWritesOf(b, 3) - writesBefore[3]);
    return std::string(text);
}

} // namespace

// A model scan of port 0 drives the power pin of port 1 - the other port of its UART - low, and does
// not touch the pins of ports 2 and 3: a frame on port 0's wire is not heard on theirs.
static void test_a_model_scan_switches_off_the_port_of_its_wire_and_no_other(void)
{
    BoardPorts b;
    int writesBefore[4];
    powerEveryPort(b, writesBefore);
    SensorReader sensorReader(nullptr, &b.reader, &b.arduino, nullptr, nullptr);

    SensorEntity sensor;
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.baudRate = 9600;
    sensor.modbusDeviceAddress = 1;
    sensor.modbusPingAddress = 0x0101;
    sensor.modbusRegisterConfigs.push_back(Fixture::channelConfig());
    sensorReader.scanSensor(sensor);

    const std::string message = pinsAfter(b, writesBefore, 0);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, powerPinOf(b, 0), message.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, powerPinOf(b, 1), message.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, powerPinOf(b, 2), message.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, powerPinOf(b, 3), message.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(writesBefore[2], powerPinWritesOf(b, 2), message.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(writesBefore[3], powerPinWritesOf(b, 3), message.c_str());
}

// The address write the same, on the other UART: a write to port 2 drives port 3's pin low and does
// not touch the pins of ports 0 and 1.
static void test_an_address_write_switches_off_the_port_of_its_wire_and_no_other(void)
{
    BoardPorts b;
    int writesBefore[4];
    powerEveryPort(b, writesBefore);

    b.reader.writeSensorAddress(1, 0x72, 2, 0x0100, 0x06, 0x0101);

    const std::string message = pinsAfter(b, writesBefore, 2);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, powerPinOf(b, 2), message.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, powerPinOf(b, 3), message.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, powerPinOf(b, 0), message.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, powerPinOf(b, 1), message.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(writesBefore[0], powerPinWritesOf(b, 0), message.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(writesBefore[1], powerPinWritesOf(b, 1), message.c_str());
}

extern "C" void run_modbus_port_power_tests(void)
{
    printf("\n----- Running Modbus Port Power Tests -----\n");

    RUN_TEST(test_the_first_send_on_an_unpowered_port_still_pays_the_settling_time);
    RUN_TEST(test_a_send_on_an_already_powered_port_costs_nothing);
    RUN_TEST(test_the_settling_time_is_paid_by_power_on_not_by_the_send_after_it);
    RUN_TEST(test_power_off_then_a_send_pays_the_settling_time_again);
    RUN_TEST(test_a_healthy_channel_no_longer_pays_the_power_delay_on_every_read);
    RUN_TEST(test_a_silent_channel_costs_the_read_budget_and_not_a_millisecond_of_power);
    RUN_TEST(test_the_port_records_when_its_power_came_and_went);
    RUN_TEST(test_an_address_write_waits_for_a_port_switched_on_moments_ago);
    RUN_TEST(test_a_model_scan_waits_for_a_port_switched_on_moments_ago);
    RUN_TEST(test_the_preparation_writes_nothing_into_a_port_switched_on_moments_ago);
    RUN_TEST(test_the_preparation_waits_for_no_port_it_does_not_write_into);
    RUN_TEST(test_a_raw_command_waits_for_the_port_to_come_up);
    RUN_TEST(test_the_ports_built_on_one_uart_are_one_wire);
    RUN_TEST(test_a_model_scan_switches_off_the_port_of_its_wire_and_no_other);
    RUN_TEST(test_an_address_write_switches_off_the_port_of_its_wire_and_no_other);

    printf("\n----- Modbus Port Power Tests Complete -----\n");
}
