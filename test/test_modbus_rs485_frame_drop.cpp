// A frame offered to a UART nobody began is refused where everyone can see it, rather than written
// into silence.
//
// On the board such a frame goes nowhere whatever ModbusRS485 does - the HardwareSerial has no
// driver behind it until begin() (see test_rs485_bus_init.cpp) - and write() says one byte was
// written all the same, so without a check here the log would say "Sending request ... Request
// sent." for a frame nobody could hear. ModbusRS485::sendData() asks the HardwareSerial, logs the
// first refusal of each port once and counts every refusal; the counters line carries the count as
// rs485_drop_uninit. The boot begins the line before any frame, so this is an artificial stand: a
// port nobody began, which on a sensor module only a defect can produce.
//
// To see the red: drop the `!_port` check in ModbusRS485::sendData() - the frames go into the
// stand-in's silence, uncounted and unlogged. Drop only the once-per-port flag and the log gets a
// line per frame.

#include <unity.h>

#include "Application/Services/CounterReport.h"
#include "Infrastructure/Adapters/Rs485/ModbusRS485.h"
#include "Infrastructure/Adapters/Rs485/Rs485Counters.h"

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

/** An FC03 read of one register at 0x70, CRC included, as the reader builds it. */
std::vector<uint8_t> readOfRegisterZeroAt0x70()
{
    std::vector<uint8_t> frame;
    frame.push_back(0x70);
    frame.push_back(0x03);
    frame.push_back(0x00);
    frame.push_back(0x00);
    frame.push_back(0x00);
    frame.push_back(0x01);
    const uint16_t crc = SimRs485Bus::crc16(&frame[0], 6);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

/** @brief The counters line the RS-485 column makes, registered the way globals.h does it. */
std::string countersLineOf(const std::vector<ModbusRS485*>& ports)
{
    CounterReport report;
    registerRs485Counters(report, ports);
    char line[CounterReport::LINE_BUFFER_CHARS];
    report.format(line, sizeof(line));
    return std::string(line);
}

} // namespace

static void test_a_frame_for_a_port_nobody_began_is_dropped_logged_once_and_counted(void)
{
    VirtualClock clock;
    SimArduino arduino(clock);
    HardwareSerial uart1(1);
    HardwareSerial uart2(2);
    std::vector<std::vector<uint8_t> > onTheLine;
    uart1.onFlush = [&onTheLine](const std::vector<uint8_t>& bytes) { onTheLine.push_back(bytes); };
    uart2.onFlush = [&onTheLine](const std::vector<uint8_t>& bytes) { onTheLine.push_back(bytes); };

    // Two ports of the board: the first one of each UART, as globals.h names and wires them.
    ModbusRS485 port0(uart1, 19, "ModbusRS485_1", arduino, SERIAL_8N1, 18, 17, 42);
    ModbusRS485 port2(uart2, 20, "ModbusRS485_1", arduino, SERIAL_8N1, 15, 14, 41);

    std::vector<uint8_t> read = readOfRegisterZeroAt0x70();
    const int printsBefore = Serial.printfCalls;
    for (int i = 0; i < 3; i++)
    {
        port0.sendData(&read[0], static_cast<uint8_t>(read.size()));
    }
    const std::string port0Line = Serial.lastText;
    const int port0Prints = Serial.printfCalls - printsBefore;
    port2.sendData(&read[0], static_cast<uint8_t>(read.size()));
    const std::string port2Line = Serial.lastText;
    const int allPrints = Serial.printfCalls - printsBefore;

    printf("  rs485: %s", port0Line.c_str());

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)onTheLine.size(),
                                     "a frame for a UART nobody began was put on the line");
    TEST_ASSERT_EQUAL_UINT32(3, port0.droppedFrameCount());
    TEST_ASSERT_EQUAL_UINT32(1, port2.droppedFrameCount());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, port0Prints,
                                  "three refusals on one port were not one line of the log");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, allPrints, "the second port's first refusal was not logged");
    TEST_ASSERT_TRUE_MESSAGE(
        port0Line.find("[ModbusRS485_1] RS485: frame dropped - port not initialized (DE pin 19)") == 0,
        "the refusal line does not say which port and why");
    TEST_ASSERT_TRUE_MESSAGE(port0Line.find("rs485_drop_uninit") != std::string::npos,
                             "the refusal line does not name the column the rest are counted in");
    // The two ports share a name on the board; the DE pin tells them apart.
    TEST_ASSERT_TRUE_MESSAGE(port2Line.find("(DE pin 20)") != std::string::npos,
                             "the second UART's refusal cannot be told from the first's");

    std::vector<ModbusRS485*> ports;
    ports.push_back(&port0);
    ports.push_back(&port2);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DIAG-COUNTERS: rs485_drop_uninit=4", countersLineOf(ports).c_str(),
                                     "the counters line does not carry every refusal");
}

// The check is the UART's, not the port's: two ports share one HardwareSerial, and whichever of
// them began it began it for both.
static void test_a_begun_uart_carries_the_frames_of_both_ports_wired_to_it(void)
{
    VirtualClock clock;
    SimArduino arduino(clock);
    HardwareSerial uart1(1);
    std::vector<std::vector<uint8_t> > onTheLine;
    uart1.onFlush = [&onTheLine](const std::vector<uint8_t>& bytes) { onTheLine.push_back(bytes); };

    ModbusRS485 port0(uart1, 19, "ModbusRS485_1", arduino, SERIAL_8N1, 18, 17, 42);
    ModbusRS485 port1(uart1, 19, "ModbusRS485_2", arduino, SERIAL_8N1, 18, 17, 40);
    port0.begin(9600);
    TEST_ASSERT_EQUAL_INT_MESSAGE(UART_MODE_RS485_HALF_DUPLEX, uart1.mode,
                                  "begin() did not put the UART into RS-485 half duplex");

    std::vector<uint8_t> read = readOfRegisterZeroAt0x70();
    port1.sendData(&read[0], static_cast<uint8_t>(read.size()));
    port0.sendData(&read[0], static_cast<uint8_t>(read.size()));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)onTheLine.size(),
                                     "a port of a begun UART did not put its frame on the line");
    TEST_ASSERT_TRUE_MESSAGE(onTheLine[0] == read, "the frame on the line is not the frame sent");
    TEST_ASSERT_EQUAL_UINT32(0, port0.droppedFrameCount());
    TEST_ASSERT_EQUAL_UINT32(0, port1.droppedFrameCount());

    std::vector<ModbusRS485*> ports;
    ports.push_back(&port0);
    ports.push_back(&port1);
    TEST_ASSERT_EQUAL_STRING("DIAG-COUNTERS: rs485_drop_uninit=0", countersLineOf(ports).c_str());
}

extern "C" int run_modbus_rs485_frame_drop_tests(void)
{
    RUN_TEST(test_a_frame_for_a_port_nobody_began_is_dropped_logged_once_and_counted);
    RUN_TEST(test_a_begun_uart_carries_the_frames_of_both_ports_wired_to_it);
    return 0;
}
