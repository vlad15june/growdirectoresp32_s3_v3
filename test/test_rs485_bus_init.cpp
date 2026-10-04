// The RS-485 UARTs are begun once at boot, before any frame can be offered to them.
//
// What the board does with a frame before HardwareSerial::begin() - arduino-esp32 2.0.17, the core
// this firmware pins: the object is built with `_uart` NULL, and write() -> uartWrite(), flush() ->
// uartFlushTxOnly(), available() -> uartAvailable() and setMode() -> uartSetMode() all return at
// once on a NULL uart. write() still reports one byte written. Nothing reaches the TX pin, nothing
// is received, the RS-485 half-duplex mode is not taken. `operator bool()` says so:
// uartIsDriverInstalled(_uart).
//
// Two groups:
//  - the two stand-ins of the line hold to that: test/Mock/native/HardwareSerial.h under the real
//    ModbusRS485, and test/Sim/SimRs485Bus.h in its place. A stand-in that carries a frame the
//    board would not lets a module that cannot hear its sensors pass every stand;
//  - setup begins the line on a sensor module in every mode, before the mode is handled, and on
//    no other module type.
//
// What ModbusRS485 does with a frame for a UART nobody began is test_modbus_rs485_frame_drop.cpp;
// what a whole module does in the first minutes after such a boot is scenario S18, section 19.
//
// To see the red of the second group: take the setupSensorBus() call out of
// ModuleSetupService::setupModule() - the line is then never begun, and every mode fails with the
// order the boot actually took. Move it after handleModuleMode() and the order fails instead.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include <HardwareSerial.h>

#include "Application/Services/ModuleSetupService.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"

#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"

#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

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

SimRs485Bus::Device deviceAt0x70()
{
    SimRs485Bus::Device device;
    device.address = 0x70;
    device.registerValues.push_back(6500);
    device.addressIsWritable = true;
    device.addressStoreRegister = 0x0001;
    return device;
}

} // namespace

// --------------------------------------------------------------------------------------------
// The stand-ins behave as the board does before begin()
// --------------------------------------------------------------------------------------------

static void test_a_uart_nobody_began_sends_nothing_and_hears_nothing(void)
{
    HardwareSerial uart(1);
    std::vector<std::vector<uint8_t> > flushed;
    uart.onFlush = [&flushed](const std::vector<uint8_t>& bytes) { flushed.push_back(bytes); };

    TEST_ASSERT_FALSE_MESSAGE((bool)uart, "a HardwareSerial nobody began says it has a driver");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)uart.write(0x70),
                                     "write() before begin() does not report what the board "
                                     "reports - one byte written");
    uart.flush();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)flushed.size(),
                                     "a byte written before begin() reached the line");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, uart.available(), "a UART with no driver has something to read");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, uart.read(), "a UART with no driver returned a byte");
    TEST_ASSERT_FALSE_MESSAGE(uart.setMode(UART_MODE_RS485_HALF_DUPLEX),
                              "the RS-485 mode was taken by a UART with no driver");
    TEST_ASSERT_EQUAL_INT(UART_MODE_UART, uart.mode);

    uart.begin(9600, SERIAL_8N1, 18, 17);
    TEST_ASSERT_TRUE_MESSAGE((bool)uart, "begin() did not give the UART a driver");
    TEST_ASSERT_TRUE(uart.setMode(UART_MODE_RS485_HALF_DUPLEX));
    uart.write(0x70);
    uart.write(0x03);
    uart.flush();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)flushed.size(), "a begun UART sent nothing");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)flushed[0].size(),
                                     "the byte written before begin() was held and sent later");
}

static void test_the_simulated_line_carries_nothing_until_its_uart_is_begun(void)
{
    VirtualClock clock;
    SimRs485Bus port0(clock, "port-0");
    SimRs485Bus port1(clock, "port-1");
    SimRs485Bus port2(clock, "port-2");
    port0.shareTheLineWith(port1);
    port0.addDevice(deviceAt0x70());

    std::vector<uint8_t> read = readOfRegisterZeroAt0x70();
    port0.sendData(&read[0], static_cast<uint8_t>(read.size()));
    clock.advanceMs(100);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)port0.sentFrames.size(),
                                     "a frame reached a line nobody began");
    TEST_ASSERT_EQUAL_UINT32(1, port0.framesDroppedBeforeBegin);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, port0.available(), "a line nobody began has something to read");
    TEST_ASSERT_FALSE_MESSAGE(port0.writeRegister(0x70, 0x0001, 0x71),
                              "a register write on a line nobody began was acknowledged");
    TEST_ASSERT_NOT_NULL_MESSAGE(port0.device(0x70),
                                 "a register write on a line nobody began moved the device");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)port0.writes.size());

    // Ports 0 and 1 are one HardwareSerial on the board: beginning either begins both.
    port1.begin(9600);
    TEST_ASSERT_TRUE_MESSAGE(port0.lineIsBegun(), "beginning port 1 did not begin port 0's UART");
    TEST_ASSERT_FALSE_MESSAGE(port2.lineIsBegun(), "beginning one UART began the other");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, port0.available(),
                                  "the answer to a frame that never went out arrived after begin()");

    port0.sendData(&read[0], static_cast<uint8_t>(read.size()));
    clock.advanceMs(100);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)port0.sentFrames.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, port0.available(), "the device did not answer on a begun line");

    port2.sendData(&read[0], static_cast<uint8_t>(read.size()));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)port2.sentFrames.size(),
                                     "the other UART carried a frame without being begun");
    TEST_ASSERT_EQUAL_UINT32(1, port2.framesDroppedBeforeBegin);
}

// begin(0) is the core's "detect the rate": it installs the driver at 9600, waits up to the default
// timeout_ms of 20 s for 30 edges on RX and, when none come - nothing talks to a master that has not
// asked - leaves the object with `_uart` NULL (HardwareSerial::begin(), uartBaudrateDetect()).
// That kills a UART begun before too, until a begin() with a rate.
static void test_a_zero_rate_leaves_the_uart_without_a_driver_as_on_the_board(void)
{
    HardwareSerial uart(1);
    std::vector<std::vector<uint8_t> > flushed;
    uart.onFlush = [&flushed](const std::vector<uint8_t>& bytes) { flushed.push_back(bytes); };

    uart.begin(9600, SERIAL_8N1, 18, 17);
    TEST_ASSERT_TRUE(uart);
    uart.begin(0, SERIAL_8N1, 18, 17);
    TEST_ASSERT_FALSE_MESSAGE((bool)uart, "begin(0) left a driver behind the UART");
    uart.write(0x70);
    uart.flush();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)flushed.size(),
                                     "a UART begun at a zero rate sent a byte");
    uart.begin(9600, SERIAL_8N1, 18, 17);
    TEST_ASSERT_TRUE_MESSAGE((bool)uart, "a begin() with a rate did not bring the UART back");

    VirtualClock clock;
    SimRs485Bus port0(clock, "port-0");
    SimRs485Bus port1(clock, "port-1");
    port0.shareTheLineWith(port1);
    port0.addDevice(deviceAt0x70());
    port0.begin(9600);
    TEST_ASSERT_TRUE(port1.lineIsBegun());

    const uint64_t beforeMs = clock.nowMs();
    port1.begin(0);
    char message[160];
    snprintf(message, sizeof(message), "begin(0) took %lu ms of the line; the core waits its 20000 ms "
             "timeout for a rate that never comes", (unsigned long)(clock.nowMs() - beforeMs));
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(20000, clock.nowMs() - beforeMs, message);
    TEST_ASSERT_FALSE_MESSAGE(port0.lineIsBegun(),
                              "begin(0) on one port left the UART both ports share with a driver");

    std::vector<uint8_t> read = readOfRegisterZeroAt0x70();
    port0.sendData(&read[0], static_cast<uint8_t>(read.size()));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)port0.sentFrames.size(),
                                     "a line begun at a zero rate carried a frame");
    TEST_ASSERT_EQUAL_UINT32(1, port0.framesDroppedBeforeBegin);

    port0.begin(9600);
    port0.sendData(&read[0], static_cast<uint8_t>(read.size()));
    clock.advanceMs(100);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)port0.sentFrames.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, port0.available(),
                                  "the line did not come back with a begin() at a rate");
}

// --------------------------------------------------------------------------------------------
// Setup begins the line before the mode is handled, in every mode, on a sensor module only
// --------------------------------------------------------------------------------------------

namespace {

const char* const LINE_BEGUN = "line begun";
const char* const MODE_HANDLED = "mode handled";
const char* const BOARD_TEST_SET_UP = "board test set up";
const char* const DISCOVERY_ARMED = "discovery armed";
const char* const PREPARATION_ARMED = "preparation armed";

class LineBeginRecorder : public MockSensorReader {
public:
    explicit LineBeginRecorder(std::vector<std::string>& order) : _order(order) {}

    void init(unsigned long modbusBaud = 9600) override
    {
        MockSensorReader::init(modbusBaud);
        initCalls++;
        _order.push_back(LINE_BEGUN);
    }

    int initCalls = 0;

private:
    std::vector<std::string>& _order;
};

/** The first thing handleModuleMode() does, in every mode, is ask the button. */
class ModeHandlingRecorder : public MockButtonUseCases {
public:
    explicit ModeHandlingRecorder(std::vector<std::string>& order) : _order(order) {}

    bool checkButtonPressedWhenOn() override
    {
        _order.push_back(MODE_HANDLED);
        return MockButtonUseCases::checkButtonPressedWhenOn();
    }

private:
    std::vector<std::string>& _order;
};

class BoardTestRecorder : public IBoardTest {
public:
    explicit BoardTestRecorder(std::vector<std::string>& order) : _order(order) {}

    void processTestCommands() override {}
    void setupDevicesForTest(IModuleConfig*) override { _order.push_back(BOARD_TEST_SET_UP); }
    bool hasTestCommandsAvailable() override { return false; }

private:
    std::vector<std::string>& _order;
};

class DiscoveryRecorder : public IStartupSensorScan {
public:
    explicit DiscoveryRecorder(std::vector<std::string>& order) : _order(order) {}

    void describeStoredSensors(Node&) override {}
    void arm() override { _order.push_back(DISCOVERY_ARMED); }
    void rescan(int) override {}
    void armPreparationOnly() override { _order.push_back(PREPARATION_ARMED); }
    bool isRunning() const override { return false; }
    bool isScanEntryRunning() const override { return false; }
    bool takeNextStep(StartupScanStep&) override { return false; }
    void returnStep() override {}
    StartupScanOutcome runStepOnBus(const StartupScanStep&) override { return StartupScanOutcome(); }
    void applyStepOutcome(const StartupScanOutcome&) override {}
    bool holdsAWindowOpen() const override { return false; }
    bool keepsThePortDark(int) const override { return false; }
    void reviewSensorLiveness() override {}
    void onDescribedStructureSettled(bool) override {}

private:
    std::vector<std::string>& _order;
};

/** Setup of a module whose every collaborator on the way to the mode's work records its turn. */
struct SetupStand {
    std::vector<std::string> order;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    ModeHandlingRecorder buttons;
    LineBeginRecorder sensorReader;
    BoardTestRecorder boardTest;
    DiscoveryRecorder discovery;
    MockSensorRepository sensorRepository;
    ModuleStructure moduleStructure;
    ModuleSetupService service;

    SetupStand(int moduleType, uint8_t mode, bool noScanSensors)
        : moduleConfig(&logger, &debugSystem, &settings),
          buttons(order),
          sensorReader(order),
          boardTest(order),
          discovery(order),
          service(&logger, &debugSystem, &moduleConfig,
                  nullptr, nullptr, nullptr, &buttons,
                  nullptr, nullptr, nullptr, nullptr, nullptr,
                  &moduleStructure, nullptr, nullptr,
                  &sensorRepository, nullptr, &sensorReader,
                  nullptr, nullptr, &boardTest)
    {
        debugSystem.featureEnabled = false;
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleType(moduleType);
        settings.setConfigured(mode);
        settings.setNoScanSensors(noScanSensors ? 1 : 0);
        if (noScanSensors)
        {
            // The flag skips the discovery around the sensors a module holds; a module that holds
            // none is discovered whatever the flag says.
            SensorEntity stored;
            stored.uid = "rs485-module.00";
            stored.pin = 0;
            sensorRepository.sensors.push_back(stored);
        }
        service.setStartupSensorScan(&discovery);
    }

    int indexOf(const char* event) const
    {
        for (size_t i = 0; i < order.size(); i++)
        {
            if (order[i] == event)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::string describeOrder() const
    {
        std::string all;
        for (size_t i = 0; i < order.size(); i++)
        {
            all += (i == 0 ? "" : " -> ") + order[i];
        }
        return all.empty() ? std::string("nothing") : all;
    }
};

struct ModeCase {
    const char* name;
    uint8_t mode;
    bool noScanSensors;
    /** What the mode goes on to do on or near the line, which must come after the line is begun. */
    const char* laterWork;
};

} // namespace

static void test_a_sensor_module_begins_its_line_before_the_mode_is_handled_in_every_mode(void)
{
    const ModeCase cases[] = {
        {"configured", ModuleModeEnum::CONFIGURED_DONE, false, DISCOVERY_ARMED},
        {"configured with noScanSensors", ModuleModeEnum::CONFIGURED_DONE, true, PREPARATION_ARMED},
        {"board test (JIG)", ModuleModeEnum::AP_TEST_MODE, false, BOARD_TEST_SET_UP},
        {"configuration", ModuleModeEnum::USER_CAN_CONFIGURE, false, nullptr},
        {"reconfiguration", ModuleModeEnum::USER_CAN_RE_CONFIGURE, false, nullptr},
        {"configured, restart pending", ModuleModeEnum::CONFIGURED_NEED_RESTART, false, nullptr},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const ModeCase& c = cases[i];
        SetupStand stand(MODULE_TYPE_SENSOR, c.mode, c.noScanSensors);
        stand.service.setupModule();

        const std::string order = stand.describeOrder();
        printf("  rs485: %s mode: %s\n", c.name, order.c_str());

        char message[320];
        snprintf(message, sizeof(message), "%s mode: the line was begun %d time(s) - the boot went %s",
                 c.name, stand.sensorReader.initCalls, order.c_str());
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, stand.sensorReader.initCalls, message);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(9600, (uint32_t)stand.sensorReader.lastInitBaud, message);

        snprintf(message, sizeof(message), "%s mode: the mode was handled before the line was begun - %s",
                 c.name, order.c_str());
        TEST_ASSERT_TRUE_MESSAGE(stand.indexOf(MODE_HANDLED) > stand.indexOf(LINE_BEGUN), message);

        if (c.laterWork != nullptr)
        {
            snprintf(message, sizeof(message), "%s mode: '%s' did not come after the line was begun - %s",
                     c.name, c.laterWork, order.c_str());
            TEST_ASSERT_TRUE_MESSAGE(stand.indexOf(c.laterWork) > stand.indexOf(LINE_BEGUN), message);
        }
    }
}

namespace {

struct TypeCase {
    const char* name;
    int type;
    int subType;
    /** Why a UART begun on this module would be wrong, as ModuleConfig.h and setup wire it. */
    const char* why;
};

} // namespace

// Only a sensor module begins the line in setup. On a hydro and a relay module the UART pins drive
// something else (ModuleConfig.h): a hydro module's setupRelayOutputs() makes 16, 17 and 18 its pump
// outputs - 17 and 18 are TX and RX of the first UART - and its green LED is on 14, TX of the
// second; a relay module's red LEDs are on 15 and 14. A socket and a dimmer module wire none of
// those pins, but nothing either of them does on its own talks to the line, so it is not begun
// there either.
static void test_only_a_sensor_module_begins_the_line_in_setup(void)
{
    const TypeCase cases[] = {
        {"hydro", MODULE_TYPE_HYDRO, MODULE_SUBTYPE_NONE,
         "17 and 18 are its pump outputs and 14 its green LED"},
        {"relay", MODULE_TYPE_DEVICE, MODULE_SUBTYPE_RELAY, "15 and 14 are its red LEDs"},
        {"socket", MODULE_TYPE_DEVICE, MODULE_SUBTYPE_SOKET,
         "it is not a sensor module and does not use the line on its own"},
        {"dimmer", MODULE_TYPE_ANALOG_INPUT_DEVICE, MODULE_SUBTYPE_DIMMER,
         "it is not a sensor module and does not use the line on its own"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const TypeCase& c = cases[i];
        SetupStand stand(c.type, ModuleModeEnum::CONFIGURED_DONE, false);
        stand.moduleConfig.setModuleSubType(c.subType);
        stand.service.setupModule();

        char message[200];
        snprintf(message, sizeof(message), "%s module: setup began the RS-485 UARTs, but %s", c.name,
                 c.why);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, stand.sensorReader.initCalls, message);
    }
}

extern "C" int run_rs485_bus_init_tests(void)
{
    RUN_TEST(test_a_uart_nobody_began_sends_nothing_and_hears_nothing);
    RUN_TEST(test_the_simulated_line_carries_nothing_until_its_uart_is_begun);
    RUN_TEST(test_a_zero_rate_leaves_the_uart_without_a_driver_as_on_the_board);
    RUN_TEST(test_a_sensor_module_begins_its_line_before_the_mode_is_handled_in_every_mode);
    RUN_TEST(test_only_a_sensor_module_begins_the_line_in_setup);
    return 0;
}
