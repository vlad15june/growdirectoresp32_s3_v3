// The Modbus read path: wait for the answer instead of sleeping through it, and check the answer
// that arrives.
//
// Three properties of ModbusSensorReader::readRegister() are under test:
//
//   the fixed sleep   there is none: a `_arduino.delay(300)` after every request would be an
//                     unconditional sleep, paid per channel and not per sensor, because SensorReader
//                     loops over modbusRegisterConfigs. Eight sensors at two or three channels
//                     each would sleep five to seven seconds in every poll cycle.
//   the blind accept  a frame is not accepted on its function code, byte count and length
//                     alone: its CRC and the address of the responder are checked as well, so
//                     the frame is known to be intact and to come from the device that was
//                     asked - as the CRC check in the discovery path of the same file already
//                     does.
//   the dirty line    the receive buffer is drained and an inter-frame gap is kept, so a
//                     frame from the previous transaction is never read as this sensor's answer.
//
// What is under test is the production reader. The only things replaced are the line (SimRs485Bus:
// devices with a turnaround, a byte rate, and the ability to be silent, to corrupt their own CRC,
// to answer under someone else's address) and time itself (VirtualClock via SimArduino).
//
// Every claim below except the two marked as guards fails on a reader without them. Measured on
// such a reader:
//   healthy sensor costs tens of ms .......... 300 ms       (assert <= 100)
//   broken CRC is not a measurement .......... accepted, 253.40 returned
//   foreign responder is not a measurement ... accepted, 253.40 returned
//   late frame is not the next sensor's ...... accepted, device 1's value returned for device 2
//   stale frame is not a fresh measurement ... accepted, a frame that predates the request
//   channel poll cost ........................ 900 ms for three channels (assert <= 200)
//   verify-read cost ......................... 300 ms       (assert <= 100)
//   verify-read rejects a broken CRC ......... accepted

#include <unity.h>

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "Infrastructure/Adapters/Rs485/IModbusRS485.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"

#include "Sim/SimArduino.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

namespace {

// The wait budget a read must never exceed: what the unconditional sleep would cost.
const unsigned long BUDGET_MS = 300;

// "Tens of milliseconds, not three hundred." A device answering in 30 ms plus the inter-frame gap
// plus one poll step lands near 40; the limit is set well clear of that and well clear of 300.
const unsigned long HEALTHY_LIMIT_MS = 100;

ModbusRegisterConfig makeConfig(uint8_t channel = 1, const char* name = "temperature")
{
    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = 0x0000 + channel;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channelName = name;
    config.channel = channel;
    config.sensorType = "temperature";
    config.canConnectMinValue = -100.0f;
    config.canConnectMaxValue = 1000.0f;
    return config;
}

hostsim::SimRs485Bus::Device makeDevice(uint8_t address, uint16_t value, uint32_t turnaroundUs)
{
    hostsim::SimRs485Bus::Device device;
    device.address = address;
    device.turnaroundUs = turnaroundUs;
    device.registerValues.push_back(value);
    device.defaultRegisterValue = value;
    return device;
}

// One healthy sensor on one port, answering in 30 ms - the ordinary case in the field.
struct Line {
    hostsim::VirtualClock clock;
    hostsim::SimArduino arduino;
    hostsim::SimRs485Bus bus;
    std::vector<IModbusRS485*> ports;
    ModbusSensorReader reader;

    Line() : arduino(clock), bus(clock), ports(1, &bus), reader(ports, arduino, nullptr, nullptr)
    {
        bus.begin(9600);
    }

    uint64_t nowMs() const { return clock.nowMs(); }
};

} // namespace

// --- Claim 1: a healthy sensor costs what its answer costs -----------------------------------

void test_a_healthy_sensor_costs_tens_of_milliseconds_not_three_hundred(void)
{
    Line line;
    line.bus.addDevice(makeDevice(1, 2534, 30000));
    ModbusRegisterConfig config = makeConfig();

    float value = 0.0f;
    uint64_t startedMs = line.nowMs();
    bool ok = line.reader.readRegister(1, config, value, 0);
    uint64_t elapsedMs = line.nowMs() - startedMs;

    TEST_ASSERT_TRUE_MESSAGE(ok, "a healthy sensor must still be read");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 253.4f, value);

    char message[128];
    snprintf(message, sizeof(message),
             "one channel of a sensor answering in 30 ms cost %lu ms",
             (unsigned long)elapsedMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(HEALTHY_LIMIT_MS, (unsigned int)elapsedMs, message);
}

void test_a_channel_poll_costs_far_less_than_it_used_to(void)
{
    // What the field actually pays: SensorReader loops over modbusRegisterConfigs, so a
    // three-channel sensor pays the wait three times.
    Line line;
    line.bus.addDevice(makeDevice(1, 2534, 30000));

    uint64_t startedMs = line.nowMs();
    for (uint8_t channel = 1; channel <= 3; channel++)
    {
        ModbusRegisterConfig config = makeConfig(channel);
        float value = 0.0f;
        TEST_ASSERT_TRUE(line.reader.readRegister(1, config, value, 0));
    }
    uint64_t elapsedMs = line.nowMs() - startedMs;

    char message[128];
    snprintf(message, sizeof(message), "three channels of one healthy sensor cost %lu ms",
             (unsigned long)elapsedMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(200, (unsigned int)elapsedMs, message);
}

// --- Claim 4 (guard): a silent sensor must not become more expensive --------------------------

void test_a_silent_sensor_costs_no_more_than_it_did_before(void)
{
    // Guard, not a red: this passes on a reader with the fixed sleep too, and that is the point. The whole saving
    // is meant to come out of healthy sensors; a broken one must not start costing more.
    Line line;
    hostsim::SimRs485Bus::Device device = makeDevice(1, 2534, 30000);
    device.silent = true;
    line.bus.addDevice(device);
    ModbusRegisterConfig config = makeConfig();

    float value = -1.0f;
    uint64_t startedMs = line.nowMs();
    bool ok = line.reader.readRegister(1, config, value, 0);
    uint64_t elapsedMs = line.nowMs() - startedMs;

    TEST_ASSERT_FALSE_MESSAGE(ok, "a silent sensor must not produce a reading");

    char message[128];
    snprintf(message, sizeof(message), "a silent sensor cost %lu ms", (unsigned long)elapsedMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(BUDGET_MS, (unsigned int)elapsedMs, message);
}

// --- Claim 2: a frame that fails its own checksum is not a measurement ------------------------

void test_a_frame_with_a_broken_crc_is_not_read_as_a_measurement(void)
{
    Line line;
    hostsim::SimRs485Bus::Device device = makeDevice(1, 2534, 30000);
    device.corruptCrc = true;
    line.bus.addDevice(device);
    ModbusRegisterConfig config = makeConfig();

    float value = -1.0f;
    bool ok = line.reader.readRegister(1, config, value, 0);

    TEST_ASSERT_FALSE_MESSAGE(ok, "a frame whose CRC does not match its payload is not a reading");
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, value);
}

// --- Claim 3: an answer from another device is not this device's measurement -------------------

void test_a_frame_from_another_device_is_not_read_as_a_measurement(void)
{
    Line line;
    hostsim::SimRs485Bus::Device device = makeDevice(1, 2534, 30000);
    device.answerAsAddress = 7; // well-formed frame, correct CRC, wrong sender
    line.bus.addDevice(device);
    ModbusRegisterConfig config = makeConfig();

    float value = -1.0f;
    bool ok = line.reader.readRegister(1, config, value, 0);

    TEST_ASSERT_FALSE_MESSAGE(ok, "device 7 answering must not be read as device 1's value");
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, value);
}

void test_address_255_is_checked_like_any_other_address(void)
{
    // 255 is not a discovery-only value:
    // main.cpp gives the RY-WS301 sensor type
    // modbusDeviceAddress = 255; StartupSensorScanUseCase copies that into the sensor it is
    // about to detect, and the sensor is stored at that address once
    // detection passes. From then on the ordinary poll reads at 255 -
    // SensorReader::readMultiChannelSensor() calls readRegister() directly, with no ping in front
    // of it. Exempting 255 from the responder check therefore switched the check off for good,
    // for a whole sensor type, on the one path this file exists to protect.
    Line line;
    hostsim::SimRs485Bus::Device other = makeDevice(3, 770, 30000);
    other.answerAsAddress = 200;
    line.bus.addDevice(other);

    ModbusRegisterConfig config = makeConfig();

    float value = -1.0f;
    bool ok = line.reader.readRegister(0xFF, config, value, 0);

    TEST_ASSERT_FALSE_MESSAGE(ok, "device 200 answering must not be read as a reading of 255");
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, value);
}

void test_a_device_that_answers_as_255_is_read_at_255(void)
{
    // The other half of the same claim: dropping the exemption must not stop a sensor that really
    // does answer under 255 from being read. That is what pingAddress() - untouched, and the gate
    // in front of detection - already demands of it, and what the fake in
    // test_xf700a_rainfall_window.cpp does: it answers with the address it was addressed at.
    Line line;
    line.bus.addDevice(makeDevice(0xFF, 2534, 30000));

    ModbusRegisterConfig config = makeConfig();

    float value = -1.0f;
    bool ok = line.reader.readRegister(0xFF, config, value, 0);

    TEST_ASSERT_TRUE_MESSAGE(ok, "a sensor answering as 255 must still be read at 255");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 253.4f, value);
}

void test_a_frame_longer_than_the_buffer_is_not_reported_as_a_broken_crc(void)
{
    // A response that cannot fit the receive buffer is truncated, so the checksum computed over
    // the stump never matches and the refusal must not be filed as crc_error. Those are different
    // site visits: "the line is corrupting frames" sends an engineer after cabling and
    // termination, while the truth here is that the device is answering with more registers than
    // this reader can hold.
    Line line;
    line.bus.addDevice(makeDevice(1, 2534, 30000));

    ModbusRegisterConfig config = makeConfig();
    config.numRegisters = 30; // byte count 60, frame 65 bytes, buffer 64

    float value = -1.0f;
    bool ok = line.reader.readRegister(1, config, value, 0);

    TEST_ASSERT_FALSE(ok);

    const ModbusSensorReader::ReadStats& stats = line.reader.getReadStats();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.crcError,
                                     "a frame too long for the buffer is not a checksum failure");
    TEST_ASSERT_EQUAL_UINT32(1, stats.oversizeFrame);
}

// --- Claim 4: a late frame belongs to the transaction that asked for it -------------------------

void test_a_late_frame_is_not_attributed_to_the_next_sensor(void)
{
    // Device 1 answers 350 ms after being asked - past the wait budget, so its read fails. Device
    // 2 is silent. Device 1's frame lands on the line while device 2 is being waited for.
    Line line;
    line.bus.addDevice(makeDevice(1, 2534, 350000));
    hostsim::SimRs485Bus::Device silent = makeDevice(2, 0, 30000);
    silent.silent = true;
    line.bus.addDevice(silent);

    ModbusRegisterConfig config = makeConfig();

    float first = -1.0f;
    TEST_ASSERT_FALSE_MESSAGE(line.reader.readRegister(1, config, first, 0),
                              "premise: device 1 answers too late to be read");

    float second = -1.0f;
    bool ok = line.reader.readRegister(2, config, second, 0);

    TEST_ASSERT_FALSE_MESSAGE(ok, "device 1's late frame must not become device 2's reading");
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, second);
}

void test_a_stale_frame_left_on_the_line_is_not_read_as_a_fresh_measurement(void)
{
    // The same sensor, the retry loop in SensorReader: attempt 1 gives up at the budget, the
    // device answers just afterwards, attempt 2 finds that frame already sitting in the buffer.
    // The address check cannot catch this one - the address is right. Only draining the line
    // before the request can.
    Line line;
    line.bus.addDevice(makeDevice(1, 2534, 350000));
    ModbusRegisterConfig config = makeConfig();

    float first = -1.0f;
    TEST_ASSERT_FALSE_MESSAGE(line.reader.readRegister(1, config, first, 0),
                              "premise: the first attempt gives up before the answer arrives");

    // The answer lands on the line while nobody is listening, and the sensor then goes quiet.
    line.clock.advanceMs(100);
    TEST_ASSERT_TRUE(line.bus.available() > 0);
    line.bus.device(1)->silent = true;

    float second = -1.0f;
    bool ok = line.reader.readRegister(1, config, second, 0);

    TEST_ASSERT_FALSE_MESSAGE(ok, "a frame that predates the request is not an answer to it");
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, second);
}

// --- The same treatment on the verify read (readHoldingRegister) --------------------------------

void test_the_verify_read_costs_what_the_answer_costs(void)
{
    Line line;
    line.bus.addDevice(makeDevice(9, 5, 30000));

    uint16_t raw = 0;
    uint64_t startedMs = line.nowMs();
    bool ok = line.reader.readHoldingRegister(9, 0x0107, raw, 0);
    uint64_t elapsedMs = line.nowMs() - startedMs;

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(5, raw);

    char message[128];
    snprintf(message, sizeof(message), "the verify read cost %lu ms", (unsigned long)elapsedMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(HEALTHY_LIMIT_MS, (unsigned int)elapsedMs, message);
}

void test_the_verify_read_rejects_a_frame_with_a_broken_crc(void)
{
    Line line;
    hostsim::SimRs485Bus::Device device = makeDevice(9, 5, 30000);
    device.corruptCrc = true;
    line.bus.addDevice(device);

    uint16_t raw = 0xFFFF;
    bool ok = line.reader.readHoldingRegister(9, 0x0107, raw, 0);

    TEST_ASSERT_FALSE_MESSAGE(ok, "the write verification must not confirm on a corrupt frame");
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, raw);
}

void test_the_verify_read_rejects_an_answer_from_another_device(void)
{
    Line line;
    hostsim::SimRs485Bus::Device device = makeDevice(9, 5, 30000);
    device.answerAsAddress = 3;
    line.bus.addDevice(device);

    uint16_t raw = 0xFFFF;
    bool ok = line.reader.readHoldingRegister(9, 0x0107, raw, 0);

    TEST_ASSERT_FALSE_MESSAGE(ok, "the write verification must not confirm from a foreign device");
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, raw);
}

void test_the_verify_read_costs_no_more_than_the_budget_when_silent(void)
{
    Line line;
    hostsim::SimRs485Bus::Device device = makeDevice(9, 5, 30000);
    device.silent = true;
    line.bus.addDevice(device);

    uint16_t raw = 0;
    uint64_t startedMs = line.nowMs();
    bool ok = line.reader.readHoldingRegister(9, 0x0107, raw, 0);
    uint64_t elapsedMs = line.nowMs() - startedMs;

    TEST_ASSERT_FALSE(ok);

    char message[128];
    snprintf(message, sizeof(message), "a silent verify read cost %lu ms",
             (unsigned long)elapsedMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(BUDGET_MS, (unsigned int)elapsedMs, message);
}

// --- Observability: the causes of refusal must be told apart -----------------------------------

void test_every_rejection_is_counted_under_its_own_cause(void)
{
    // This one has no red on a reader without counters: it cannot fail on code that
    // does not have them. It is here because the firmware refuses frames it would otherwise
    // accept, and "the sensor errors out" is not an answer anyone can act on. A bus with a
    // failing checksum, a sensor answering on someone else's address and a sensor that has gone
    // quiet are three different site visits.
    Line line;
    ModbusRegisterConfig config = makeConfig();
    float value = 0.0f;

    line.bus.addDevice(makeDevice(1, 2534, 30000));
    TEST_ASSERT_TRUE(line.reader.readRegister(1, config, value, 0));

    hostsim::SimRs485Bus::Device brokenCrc = makeDevice(2, 2534, 30000);
    brokenCrc.corruptCrc = true;
    line.bus.addDevice(brokenCrc);
    TEST_ASSERT_FALSE(line.reader.readRegister(2, config, value, 0));

    hostsim::SimRs485Bus::Device foreign = makeDevice(3, 2534, 30000);
    foreign.answerAsAddress = 77;
    line.bus.addDevice(foreign);
    TEST_ASSERT_FALSE(line.reader.readRegister(3, config, value, 0));

    hostsim::SimRs485Bus::Device silent = makeDevice(4, 0, 30000);
    silent.silent = true;
    line.bus.addDevice(silent);
    TEST_ASSERT_FALSE(line.reader.readRegister(4, config, value, 0));

    hostsim::SimRs485Bus::Device wrongFunction = makeDevice(5, 2534, 30000);
    wrongFunction.answerAsFunctionCode = 0x04;
    line.bus.addDevice(wrongFunction);
    TEST_ASSERT_FALSE(line.reader.readRegister(5, config, value, 0));

    // Two bytes and then nothing: never enough to know how long the frame was meant to be.
    hostsim::SimRs485Bus::Device truncated = makeDevice(6, 0, 30000);
    truncated.silent = true;
    line.bus.addDevice(truncated);
    std::vector<uint8_t> twoBytes;
    twoBytes.push_back(6);
    twoBytes.push_back(0x03);
    line.bus.injectFrameAt(twoBytes, line.clock.nowUs() + 20000);
    TEST_ASSERT_FALSE(line.reader.readRegister(6, config, value, 0));

    // A well-formed frame carrying two registers when one was asked for.
    hostsim::SimRs485Bus::Device overlong = makeDevice(7, 0, 30000);
    overlong.silent = true;
    line.bus.addDevice(overlong);
    std::vector<uint16_t> twoRegisters;
    twoRegisters.push_back(1);
    twoRegisters.push_back(2);
    line.bus.injectFrameAt(hostsim::SimRs485Bus::makeReadResponse(7, 0x03, twoRegisters),
                           line.clock.nowUs() + 20000);
    TEST_ASSERT_FALSE(line.reader.readRegister(7, config, value, 0));

    // A device answering with more registers than the receive buffer can hold.
    line.bus.addDevice(makeDevice(8, 2534, 30000));
    ModbusRegisterConfig tooMany = makeConfig();
    tooMany.numRegisters = 30;
    TEST_ASSERT_FALSE(line.reader.readRegister(8, tooMany, value, 0));

    const ModbusSensorReader::ReadStats& stats = line.reader.getReadStats();
    TEST_ASSERT_EQUAL_UINT32(1, stats.accepted);
    TEST_ASSERT_EQUAL_UINT32(1, stats.crcError);
    TEST_ASSERT_EQUAL_UINT32(1, stats.addressMismatch);
    TEST_ASSERT_EQUAL_UINT32(1, stats.timeout);
    TEST_ASSERT_EQUAL_UINT32(1, stats.functionMismatch);
    TEST_ASSERT_EQUAL_UINT32(1, stats.shortFrame);
    TEST_ASSERT_EQUAL_UINT32(1, stats.payloadMismatch);
    TEST_ASSERT_EQUAL_UINT32(1, stats.oversizeFrame);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, stats.rejected(),
                                     "seven refusals, each under a cause of its own");
}

void test_bytes_left_on_the_line_are_dropped_and_counted(void)
{
    Line line;
    line.bus.addDevice(makeDevice(1, 2534, 350000));
    ModbusRegisterConfig config = makeConfig();

    float value = 0.0f;
    TEST_ASSERT_FALSE(line.reader.readRegister(1, config, value, 0));

    line.clock.advanceMs(100); // the answer arrives with nobody listening
    line.bus.device(1)->silent = true;

    TEST_ASSERT_FALSE(line.reader.readRegister(1, config, value, 0));

    const ModbusSensorReader::ReadStats& stats = line.reader.getReadStats();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, stats.staleBytesDropped,
                                     "the whole stale frame is dropped before the next request");
    TEST_ASSERT_EQUAL_UINT32(0, stats.accepted);
}

extern "C" void run_modbus_read_path_tests(void)
{
    printf("\n----- Running Modbus Read Path Tests -----\n");

    RUN_TEST(test_a_healthy_sensor_costs_tens_of_milliseconds_not_three_hundred);
    RUN_TEST(test_a_channel_poll_costs_far_less_than_it_used_to);
    RUN_TEST(test_a_silent_sensor_costs_no_more_than_it_did_before);
    RUN_TEST(test_a_frame_with_a_broken_crc_is_not_read_as_a_measurement);
    RUN_TEST(test_a_frame_from_another_device_is_not_read_as_a_measurement);
    RUN_TEST(test_address_255_is_checked_like_any_other_address);
    RUN_TEST(test_a_device_that_answers_as_255_is_read_at_255);
    RUN_TEST(test_a_frame_longer_than_the_buffer_is_not_reported_as_a_broken_crc);
    RUN_TEST(test_a_late_frame_is_not_attributed_to_the_next_sensor);
    RUN_TEST(test_a_stale_frame_left_on_the_line_is_not_read_as_a_fresh_measurement);
    RUN_TEST(test_the_verify_read_costs_what_the_answer_costs);
    RUN_TEST(test_the_verify_read_rejects_a_frame_with_a_broken_crc);
    RUN_TEST(test_the_verify_read_rejects_an_answer_from_another_device);
    RUN_TEST(test_the_verify_read_costs_no_more_than_the_budget_when_silent);
    RUN_TEST(test_every_rejection_is_counted_under_its_own_cause);
    RUN_TEST(test_bytes_left_on_the_line_are_dropped_and_counted);

    printf("\n----- Modbus Read Path Tests Complete -----\n");
}
