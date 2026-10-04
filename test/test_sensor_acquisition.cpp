// The cache the control loop reads, and the freshness stamp that keeps a rule honest about it.
//
// The acquisition task stores a value and the time it was obtained; the loop reads the cache
// and never waits. The cache is the SensorValueModel that was
// already there, and the freshness logic is the one that was already there
// (EvaluateRulesUseCase::ruleSensorFreshness(), SENSOR_FRESHNESS_WINDOW_MS = 300 s, covered by
// test_rule_phase8_freshness.cpp). Nothing here duplicates either. What is new is the join between
// them: the value is now produced on one task and applied on another, so the timestamp has to
// travel with the reading instead of being taken where it lands.
//
// **The switch to flip to see the red of the first two tests**: in
// SensorService::applyReading(), replace `reading.acquiredAtMs` with
// `arduinoAdapter->millis()` - the timestamp of the moment the loop got round to it rather than of
// the moment the bus answered - and additionally stamp it on a failed reading. Both tests then
// fail, and the second one fails in the way that matters: the rule keeps acting on a value the bus
// stopped confirming, because every drain makes the stale value look new.

#include <unity.h>

#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"

#include "Mock/Core/Domain/Rules/MockRuleStateNotifier.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/UseCases/ModuleCommand/MockModuleCommandExecutionUseCase.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using hostsim::HostBusLock;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimRs485Bus;
using hostsim::VirtualClock;

namespace {

// EvaluateRulesUseCase::SENSOR_FRESHNESS_WINDOW_MS, not redefined here - just named.
const unsigned long FRESHNESS_WINDOW_MS = 300000;

SensorEntity makeSingleChannelSensor(const std::string& uid, int port, uint8_t address)
{
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.protocol = "ModBus";
    sensor.model = "ACQ-1";
    sensor.sensorType = "temperature";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.pin = port;
    sensor.modbusDeviceAddress = address;
    sensor.baudRate = 9600;

    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = 0x0101;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channel = 1;
    config.channelName = "temperature";
    config.sensorType = "temperature";
    sensor.modbusRegisterConfigs.push_back(config);

    return sensor;
}

class SingleDeviceModel : public IDeviceModel {
public:
    Device* device = nullptr;

    bool init() override { return true; }
    Device* getDeviceByUid(const char* uid) override
    {
        if (device && device->deviceUid && uid && std::string(device->deviceUid) == uid)
        {
            return device;
        }
        return nullptr;
    }
    std::vector<Device*> getAllDevices() override
    {
        std::vector<Device*> all;
        if (device) all.push_back(device);
        return all;
    }
    int getDeviceCount() const override { return device ? 1 : 0; }
    bool addDevice(Device*) override { return true; }
    bool removeDevice(const char*) override { return true; }
    void clearDevices() override {}
};

/**
 * @brief The acquisition half of the module: four simulated ports, the real reader, the real task.
 *
 * `SensorService` is wired with the acquisition service but driven by the test body rather than by
 * a control-loop task - what is under test here is what ends up in the cache and when, and a task
 * would only add scheduling to it. S7 is where the scheduling is under test.
 */
struct AcquisitionFixture {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;
    HostBusLock busLock;

    SimRs485Bus port0;
    SimRs485Bus port1;
    SimRs485Bus port2;
    SimRs485Bus port3;
    std::vector<IModbusRS485*> ports;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockSensorRepository sensorRepository;
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;

    ModbusSensorReader modbusSensorReader;
    SensorReader sensorReader;
    SensorAcquisitionService acquisition;
    SensorService sensorService;

    AcquisitionFixture()
        : scheduler(clock),
          arduino(clock, &scheduler),
          busLock(scheduler, clock),
          port0(clock, "sim-port-0"),
          port1(clock, "sim-port-1"),
          port2(clock, "sim-port-2"),
          port3(clock, "sim-port-3"),
          ports{&port0, &port1, &port2, &port3},
          moduleConfig(&logger, &debugSystem, &settings),
          modbusSensorReader(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          sensorService(&moduleConfig,
                        &moduleStructure,
                        &sensorRepository,
                        &sensorReader,
                        &sensorValueModel,
                        nullptr,
                        &arduino,
                        nullptr,
                        &logger,
                        &debugSystem)
    {
        debugSystem.featureEnabled = false;
        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        sensorService.setAcquisition(&acquisition);
        // The line as a booted module has it: the boot begins the UARTs before the first frame
        // (ModuleSetupService::setupSensorBus()), and a line nobody began carries nothing.
        sensorReader.init();
    }

    /**
     * @brief Ports 0 and 1 on one UART and 2 and 3 on the other, as globals.h wires them: what a
     *        model scan of a port takes down is the other port of its wire.
     */
    void wireThePortsLikeTheBoard()
    {
        port0.shareTheLineWith(port1);
        port2.shareTheLineWith(port3);
    }

    void addSensor(const SensorEntity& sensor)
    {
        sensorRepository.sensors.push_back(sensor);
        sensorRepository.modbusSensors = sensorRepository.sensors;
        acquisition.setSensors(sensorRepository.modbusSensors);
    }

    void addAnsweringDevice(SimRs485Bus& port, uint8_t address, uint16_t rawValue)
    {
        SimRs485Bus::Device device;
        device.address = address;
        device.turnaroundUs = 20000;
        device.defaultRegisterValue = rawValue;
        device.registerValues.push_back(rawValue);
        port.addDevice(device);
    }

    void addSilentDevice(SimRs485Bus& port, uint8_t address)
    {
        SimRs485Bus::Device device;
        device.address = address;
        device.silent = true;
        port.addDevice(device);
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// The stamp travels with the reading
// --------------------------------------------------------------------------------------------

static void test_a_reading_carries_the_time_the_bus_answered_not_the_time_it_was_applied(void)
{
    AcquisitionFixture f;
    f.addAnsweringDevice(f.port0, 1, 250);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    f.acquisition.step();
    const uint64_t answeredAtMs = f.clock.nowMs();
    TEST_ASSERT_TRUE_MESSAGE(answeredAtMs > 0, "the poll took no time at all - nothing happened");

    // The loop is busy elsewhere for a while before it drains. On the device this gap is one
    // iteration; here it is made large so that confusing the two moments cannot pass unnoticed.
    f.clock.advanceMs(5000);
    f.sensorService.loop();

    const uint32_t stampedAtMs = f.sensorValueModel.getLastReadTime("acq.0");

    char message[224];
    snprintf(message, sizeof(message),
             "the reading is stamped %lu ms, the bus answered at %lu ms and the loop applied it at "
             "%lu ms: a value stamped when it is applied never ages",
             (unsigned long)stampedAtMs, (unsigned long)answeredAtMs,
             (unsigned long)f.clock.nowMs());

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)answeredAtMs, stampedAtMs, message);

    // Read back as a channel, not as a sensor: SensorEntity::isMultiChannel() is true for every
    // sensor that has register configs, so a one-register Modbus sensor is stored channel-wise,
    // under "<sensor uid>.01". See the note at the end of this file.
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(25.0f, f.sensorValueModel.getChannelValue("acq.0", "acq.0.01"),
                                    "the value did not reach the cache");
}

// --------------------------------------------------------------------------------------------
// A rule does not get a stale value without that being visible
// --------------------------------------------------------------------------------------------

// Bound to the **sensor** UID, deliberately, and that is not the binding the field uses - the
// backend only ever learns `<sensorUid>.NN` (SensorStructureUseCase registers `uid + ".01"` even
// for a one-channel sensor), so a real rule carries a channel UID. This test uses the sensor UID
// because it is the only binding on which the mechanism under test - "a failed read moves no
// freshness stamp, and after the window the rule is told" - is reachable at all. With a channel
// UID the verdict is `never_seen -> STALE` from boot regardless of what the bus does, for the
// reason recorded at the end of this file and characterised by the next test.
static void test_a_rule_is_told_the_sensor_is_unreachable_once_the_bus_stops_confirming_it(void)
{
    AcquisitionFixture f;
    f.addAnsweringDevice(f.port0, 1, 250); // 25.0 after the 0.1 multiplier
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    // A rule that watches this sensor, on a device of its own.
    Rule rule;
    rule.uid = "rule-1";
    rule.version = 1;
    rule.ruleType = RuleType::SENSOR;
    rule.deviceUid = "dev-1";
    rule.sensorUid = "acq.0";
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;

    MockRuleRepository ruleRepository;
    ruleRepository.rules.push_back(rule);

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase rules(&deviceModel, &ruleRepository, &f.arduino, &ntp,
                              &f.sensorValueModel, nullptr, nullptr, &notifier);

    // --- while the bus answers, the rule works off a value it can trust -----------------------
    f.acquisition.step();
    f.sensorService.loop();
    const uint32_t firstStampMs = f.sensorValueModel.getLastReadTime("acq.0");
    TEST_ASSERT_TRUE_MESSAGE(firstStampMs > 0, "the first reading never reached the cache");

    rules.loop();
    TEST_ASSERT_FALSE_MESSAGE(notifier.sensorUnreachableCalled,
                              "a sensor that has just been read was reported unreachable");

    // --- the line goes dead ------------------------------------------------------------------
    // Not "the task stops": the task keeps sweeping and keeps failing, which is the case that can
    // go wrong quietly. Every failed sweep drains a reading into the loop, and none of them may
    // move the sensor's freshness stamp.
    f.port0.device(1)->silent = true;

    while (f.clock.nowMs() < firstStampMs + FRESHNESS_WINDOW_MS + 60000)
    {
        // step() reports how long the task would sleep before the next one - between sweeps that
        // is the rest of the 15 s interval. The test body owns the clock here, so it has to spend
        // that sleep itself; the task body does it through the scheduler.
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        if (sleepMs > 0)
        {
            f.clock.advanceMs(sleepMs);
        }
    }

    const uint32_t stampAfterOutageMs = f.sensorValueModel.getLastReadTime("acq.0");

    char stampMessage[224];
    snprintf(stampMessage, sizeof(stampMessage),
             "the freshness stamp moved from %lu to %lu ms while the bus answered nothing: "
             "failed reads are refreshing the value",
             (unsigned long)firstStampMs, (unsigned long)stampAfterOutageMs);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(firstStampMs, stampAfterOutageMs, stampMessage);

    // --- and the rule says so ------------------------------------------------------------------
    rules.loop();

    char ruleMessage[224];
    snprintf(ruleMessage, sizeof(ruleMessage),
             "at t=%lu ms the newest reading is %lu ms old (window %lu ms) and the rule was not "
             "told: it is acting on a value the bus stopped confirming",
             (unsigned long)f.clock.nowMs(), (unsigned long)(f.clock.nowMs() - stampAfterOutageMs),
             (unsigned long)FRESHNESS_WINDOW_MS);

    TEST_ASSERT_TRUE_MESSAGE(notifier.sensorUnreachableCalled, ruleMessage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, notifier.sensorUnreachableCount,
                                  "the rule reported the sensor unreachable more than once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("acq.0", notifier.lastUnreachableSensorUid.c_str(),
                                     "the rule named a different sensor as unreachable");
    TEST_ASSERT_EQUAL_INT_MESSAGE(DEVICE_STATE_OFF, device.state,
                                  "the device was left on by a rule whose sensor is unreachable");
}

// --------------------------------------------------------------------------------------------
// What a rule bound the way the field binds it actually does - recorded, not endorsed.
//
// This test asserts behaviour that is wrong. It is here because the defect is invisible otherwise:
// every existing rule test seeds the value map directly, so none of them ever crosses the join
// between a real read and a rule, and the staleness test above had to use a binding the backend
// never produces in order to reach the mechanism it is about.
//
// The defect:
// `SensorEntity::isMultiChannel()` is true for any sensor with register configs, so every Modbus
// sensor stores channel-wise; `EvaluateRulesUseCase::getSensorValue(uid)` reads the *top level* of
// the value map; and nothing ever writes a local sensor's channel UID there. So the value is 0.0
// and the freshness stamp is absent, which after the 300 s window becomes `sensor_unreachable`
// permanently - on a perfectly healthy bus with a fresh reading sitting in the cache.
//
// **When the rules path is fixed, this test must fail**, and the assertions marked below are the
// ones to invert: the value should be 25.0 and the rule should not report the sensor unreachable.
// That is the point of writing it down rather than leaving the gap silent.
// --------------------------------------------------------------------------------------------

static void test_a_rule_bound_the_way_the_backend_binds_it_reports_unreachable_on_a_healthy_bus(void)
{
    AcquisitionFixture f;
    f.addAnsweringDevice(f.port0, 1, 250); // 25.0 after the multiplier
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    // The UID the backend holds: SensorStructureUseCase registers "<sensor uid>.NN", and ".01"
    // even for a single-channel sensor.
    const std::string channelUid = "acq.0.01";

    Rule rule;
    rule.uid = "rule-ch";
    rule.version = 1;
    rule.ruleType = RuleType::SENSOR;
    rule.deviceUid = "dev-1";
    rule.sensorUid = channelUid;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;

    MockRuleRepository ruleRepository;
    ruleRepository.rules.push_back(rule);

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase rules(&deviceModel, &ruleRepository, &f.arduino, &ntp,
                              &f.sensorValueModel, nullptr, nullptr, &notifier);

    // A bus that answers, every time, for well past the freshness window.
    while (f.clock.nowMs() < FRESHNESS_WINDOW_MS + 60000)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        rules.loop();
        if (sleepMs > 0)
        {
            f.clock.advanceMs(sleepMs);
        }
    }

    // The reading is there, fresh, and correct.
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(25.0f, f.sensorValueModel.getChannelValue("acq.0", channelUid),
                                    "the bus stopped answering - this test needs a healthy one");
    TEST_ASSERT_TRUE_MESSAGE(f.sensorValueModel.getLastReadTime("acq.0") > 0,
                             "the sensor was never read at all");

    // And the rule cannot see any of it. INVERT THESE WHEN THE RULES PATH IS FIXED:
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, f.sensorValueModel.getSensorValue(channelUid),
                                    "the channel value is now reachable through getSensorValue - "
                                    "the defect is fixed and this test should be inverted");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, f.sensorValueModel.getLastReadTime(channelUid),
                                     "the channel now has a freshness stamp - the defect is fixed "
                                     "and this test should be inverted");

    char message[256];
    snprintf(message, sizeof(message),
             "the rule bound to %s did NOT report its sensor unreachable after %lu ms of a healthy "
             "bus - the rules path has been fixed and this characterisation is out of date",
             channelUid.c_str(), (unsigned long)f.clock.nowMs());
    TEST_ASSERT_TRUE_MESSAGE(notifier.sensorUnreachableCalled, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DEVICE_STATE_OFF, device.state,
                                  "the device is on - the rules path has been fixed and this "
                                  "characterisation is out of date");
}

// --------------------------------------------------------------------------------------------
// The sweep
// --------------------------------------------------------------------------------------------

static void test_the_sweep_goes_round_the_ports_rather_than_down_one(void)
{
    AcquisitionFixture f;

    // Two sensors on port 0, one on port 1. Walking port 0 to its end first would put port 1's
    // sensor last; going round the ports puts it second.
    f.addAnsweringDevice(f.port0, 1, 100);
    f.addAnsweringDevice(f.port0, 2, 200);
    f.addAnsweringDevice(f.port1, 1, 300);

    f.sensorRepository.sensors.push_back(makeSingleChannelSensor("p0.a", 0, 1));
    f.sensorRepository.sensors.push_back(makeSingleChannelSensor("p0.b", 0, 2));
    f.sensorRepository.sensors.push_back(makeSingleChannelSensor("p1.a", 1, 1));
    f.sensorRepository.modbusSensors = f.sensorRepository.sensors;
    f.acquisition.setSensors(f.sensorRepository.modbusSensors);

    std::vector<std::string> order;
    for (int i = 0; i < 3; i++)
    {
        const uint32_t sleepMs = f.acquisition.step();
        if (sleepMs > 0)
        {
            f.clock.advanceMs(sleepMs);
        }
        std::vector<AcquiredReading> readings;
        if (f.acquisition.drain(readings))
        {
            for (size_t r = 0; r < readings.size(); r++)
            {
                order.push_back(readings[r].sensorUid);
            }
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)order.size(), "the sweep did not poll all three sensors");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("p0.a", order[0].c_str(), "the sweep did not start at port 0");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("p1.a", order[1].c_str(),
                                     "the sweep stayed on port 0 instead of moving to port 1: a "
                                     "silent sensor on one port would delay every other port");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("p0.b", order[2].c_str(),
                                     "the second round did not come back to port 0");
}

static void test_a_dead_bus_keeps_the_sweep_running_and_marks_the_sensors(void)
{
    AcquisitionFixture f;
    f.addSilentDevice(f.port0, 1);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    for (int i = 0; i < 5; i++)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        if (sleepMs > 0)
        {
            f.clock.advanceMs(sleepMs);
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.pollCount() >= 1,
                             "the acquisition task stopped polling a bus that does not answer");
    TEST_ASSERT_TRUE_MESSAGE(f.sensorRepository.updateSensorStatusCallCount >= 1,
                             "a sensor that never answered was not marked");
    TEST_ASSERT_TRUE_MESSAGE(f.sensorRepository.lastStatusIsError,
                             "a sensor that never answered was not marked as being in error");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, f.sensorValueModel.getLastReadTime("acq.0"),
                                     "a sensor that never answered got a freshness stamp anyway");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, f.acquisition.droppedReadingCount(),
                                     "readings were lost while the loop was draining every step");
}

// --------------------------------------------------------------------------------------------
// The task itself
// --------------------------------------------------------------------------------------------

static void test_a_refused_acquisition_task_is_reported_and_retried(void)
{
    AcquisitionFixture f;
    f.addAnsweringDevice(f.port0, 1, 250);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    // The one failure FreeRtosTaskScheduler can report: no heap for the stack.
    f.scheduler.failNextCreateTasks(2);

    TEST_ASSERT_FALSE_MESSAGE(f.acquisition.ensureRunning(), "a refused task was reported as running");
    TEST_ASSERT_FALSE_MESSAGE(f.acquisition.ensureRunning(), "a refused task was reported as running");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, f.acquisition.taskStartFailureCount(),
                                     "the refusals were not counted");

    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.ensureRunning(),
                             "the task was never retried after the heap freed up: a module that "
                             "lost that race at boot would never read a Modbus sensor again");
    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.taskStarted(), "the task flag does not match the outcome");
}

static void test_undrained_readings_are_capped_and_counted(void)
{
    AcquisitionFixture f;
    f.addAnsweringDevice(f.port0, 1, 250);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    // The loop never drains. What the acquisition task holds must not grow with how long that
    // lasts, and what it drops must be countable rather than silent.
    for (size_t i = 0; i < SensorAcquisitionService::MAX_PENDING_READINGS + 20; i++)
    {
        f.acquisition.step();
        f.clock.advanceMs(SensorAcquisitionService::SWEEP_INTERVAL_MS);
    }

    std::vector<AcquiredReading> readings;
    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.drain(readings), "nothing was produced at all");

    char message[192];
    snprintf(message, sizeof(message),
             "the acquisition task was holding %d readings; the cap is %d",
             (int)readings.size(), (int)SensorAcquisitionService::MAX_PENDING_READINGS);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)SensorAcquisitionService::MAX_PENDING_READINGS,
                                  (int)readings.size(), message);
    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.droppedReadingCount() > 0,
                             "readings were dropped without being counted");
}

// --------------------------------------------------------------------------------------------
// Port isolation, now that scanning and polling are two tasks
//
// `SensorReader::scanModbusSensor()` cuts power to the three other ports, waits 10 s for the
// target to settle, then pings. With one task nothing could interfere: scanning and polling ran on
// the same task. With the acquisition task they overlap, and the overlap is not harmless - ports 0
// and 1 share one UART (globals.h, `modbusRS485_pin1` to `modbusRS485_pin4`), and `ModbusRS485::sendData()` raises a port's power
// pin again whenever it finds it down. So a poll of port 1 during a scan of port 0 both undoes the
// isolation and puts frames on the line being isolated.
//
// To see the red: remove the `BusLockGuard` from `SensorReader::scanModbusSensor()`.
// --------------------------------------------------------------------------------------------

static void test_a_scan_holds_the_line_for_its_whole_isolate_warmup_ping_sequence(void)
{
    AcquisitionFixture f;
    f.wireThePortsLikeTheBoard();

    // Enough silent sensors on port 1 - the port that shares a UART with the one being scanned -
    // that the acquisition sweep is still running throughout the scan's 10 s warm-up. With a
    // single quick sensor the sweep finishes in 45 ms and then sleeps for 15 s, and the two never
    // overlap: the test would pass without the lock and prove nothing. That is not hypothetical,
    // a single quick sensor passes without the lock.
    for (int i = 0; i < 12; i++)
    {
        f.addSilentDevice(f.port1, (uint8_t)(10 + i));
        f.sensorRepository.sensors.push_back(
            makeSingleChannelSensor("poll.1." + std::to_string(i), 1, (uint8_t)(10 + i)));
    }
    f.sensorRepository.modbusSensors = f.sensorRepository.sensors;
    f.acquisition.setSensors(f.sensorRepository.modbusSensors);

    // The sensor being scanned lives on port 0 and answers, so the scan runs its full length.
    f.addAnsweringDevice(f.port0, 2, 250);
    SensorEntity scanned = makeSingleChannelSensor("scan.0", 0, 2);
    scanned.modbusRegisterConfigs[0].canConnectMinValue = 0.0f;
    scanned.modbusRegisterConfigs[0].canConnectMaxValue = 1000.0f;
    scanned.modbusPingAddress = 0x0101;

    size_t framesOnPollPortBefore = 0;
    size_t framesOnPollPortAfter = 0;
    uint64_t scanStartedAtMs = 0;
    uint64_t scanFinishedAtMs = 0;
    bool scanRan = false;

    // The control loop, doing what it does when the app asks to add a sensor.
    f.scheduler.createTask("arduino-loop", 512 * 1024, 1, [&]() {
        f.scheduler.delayMs(50); // let the acquisition task get going first
        framesOnPollPortBefore = f.port1.sentFrames.size();
        scanStartedAtMs = f.clock.nowMs();
        f.sensorReader.scanSensor(scanned);
        scanFinishedAtMs = f.clock.nowMs();
        framesOnPollPortAfter = f.port1.sentFrames.size();
        scanRan = true;
        f.scheduler.delayMs(1000000);
    });

    f.acquisition.start();
    f.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE_MESSAGE(f.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_FALSE_MESSAGE(f.busLock.deadlockDetected(),
                              "a task waited for the bus lock in a way that could never be served");
    TEST_ASSERT_TRUE_MESSAGE(scanRan, "the scan never completed");

    // The window has to be real, or the assertion below is about nothing: the 10 s warm-up is what
    // makes this dangerous in the first place.
    const uint64_t scanDurationMs = scanFinishedAtMs - scanStartedAtMs;
    char windowMessage[192];
    snprintf(windowMessage, sizeof(windowMessage),
             "the scan took %lu ms; without its 10 s warm-up there is no window to protect",
             (unsigned long)scanDurationMs);
    TEST_ASSERT_TRUE_MESSAGE(scanDurationMs >= 10000, windowMessage);

    // And the collision has to have been real. This is the guard that makes the assertion below
    // mean something: the acquisition task was mid-sweep and was actually held off by the lock,
    // rather than happening to be asleep for the whole window.
    TEST_ASSERT_TRUE_MESSAGE(f.busLock.waitsBy(SensorAcquisitionService::TASK_NAME) > 0,
                             "the acquisition task never found the line busy during the scan - it "
                             "was idle, so this test proves nothing");
    TEST_ASSERT_TRUE_MESSAGE(framesOnPollPortBefore > 0,
                             "the acquisition task had not started polling port 1 before the scan");

    char message[256];
    snprintf(message, sizeof(message),
             "%d frames went out on port 1 while port 0 was being isolated and scanned: the poll "
             "raises the paired port's power pin again and puts frames on the line being isolated",
             (int)(framesOnPollPortAfter - framesOnPollPortBefore));

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)framesOnPollPortBefore, (int)framesOnPollPortAfter, message);
}

// --------------------------------------------------------------------------------------------
// Port recovery, carried over from the control loop
//
// The move was not a pure transplant and the two differences are deliberate, so they are stated
// and held here rather than asserted to be absent:
//
//  - `observe()` is passed the sensor's own `isError`, as the control loop passed it - not a
//    hard-coded `true`. The difference is observable: `observe(..., false, ...)` erases the
//    disconnected-since stamp and a hard-coded `true` never does, so a recovered port would have
//    stayed in recovery for good.
//  - `observe()` after a read is stamped with the time taken *before* the transaction, as the
//    control loop stamped it. Stamping from the end would shift every observation by the cost of
//    the transaction - 1100 ms on a silent sensor - against a 60 s threshold.
//
// For the first of those to work the snapshot has to carry a current `isError`, which is why
// `setSensors()` refreshes the entities every time even when the set is unchanged.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief A discovery that holds one port dark for as long as a test says its window is open. */
class HeldWindow : public IStartupSensorScan {
public:
    bool open = false;
    int darkPort = -1;

    void describeStoredSensors(Node&) override {}
    void arm() override {}
    void rescan(int) override {}
    void armPreparationOnly() override {}
    bool isRunning() const override { return open; }
    bool isScanEntryRunning() const override { return false; }
    bool takeNextStep(StartupScanStep&) override { return false; }
    void returnStep() override {}
    StartupScanOutcome runStepOnBus(const StartupScanStep&) override { return StartupScanOutcome(); }
    void applyStepOutcome(const StartupScanOutcome&) override {}
    bool holdsAWindowOpen() const override { return open; }
    bool keepsThePortDark(int pin) const override { return open && pin == darkPort; }
    void reviewSensorLiveness() override {}
    void onDescribedStructureSettled(bool) override {}
};

} // namespace

// While the discovery holds a port dark the sweep leaves it alone - no frame goes out through it,
// which would raise its power, and nothing is counted against the sensor on it, which would start
// the port recovery on a sensor that is fine. And no bus command is served: every one of them
// reaches the line through a port. Both resume the moment the window closes.
static void test_a_port_held_dark_by_the_discovery_is_neither_read_nor_recovered(void)
{
    AcquisitionFixture f;
    HeldWindow window;
    window.open = true;
    window.darkPort = 0;
    f.acquisition.setStartupSensorScan(&window);

    // A sensor that would not answer anyway, so that one read of it would start the recovery clock.
    f.addSilentDevice(f.port0, 1);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.addAnsweringDevice(f.port2, 3, 250);
    f.addSensor(makeSingleChannelSensor("acq.2", 2, 3));
    TEST_ASSERT_TRUE(f.acquisition.submitBusCommand("ModbusRead", "{}") != ISensorAcquisition::kBusCommandRejected);

    // Longer than the recovery threshold, with the port dark all of it.
    while (f.clock.nowMs() < 90000)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }

    char message[200];
    snprintf(message, sizeof(message),
             "while the port was held dark it saw %u frame(s), the recovery acted %lu time(s) and "
             "%lu bus command(s) were served",
             (unsigned)f.port0.sentFrames.size(), (unsigned long)f.acquisition.recoveryActionCount(),
             (unsigned long)f.acquisition.busCommandCount());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)f.port0.sentFrames.size(), message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, f.acquisition.recoveryActionCount(), message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, f.acquisition.busCommandCount(), message);
    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.heldDarkSkipCount() > 0, "the sweep never came to the dark port");
    TEST_ASSERT_TRUE_MESSAGE(f.port2.sentFrames.size() > 0,
                             "the sweep stopped reading the other wire as well");

    window.open = false;
    const uint64_t closedAtMs = f.clock.nowMs();
    while (f.clock.nowMs() < closedAtMs + 30000)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, f.acquisition.busCommandCount(),
                                     "the command that waited for the window was not served after it");
    TEST_ASSERT_TRUE_MESSAGE(f.port0.sentFrames.size() > 0,
                             "the sweep did not come back to the port after the window closed");
}

static void test_a_port_whose_sensor_stays_silent_is_power_cycled_and_comes_back(void)
{
    AcquisitionFixture f;
    f.addSilentDevice(f.port0, 1);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.port0.powerOnPort();

    const int powerOffBefore = f.port0.powerOffTransitions;
    const int powerOnBefore = f.port0.powerOnTransitions;

    // Well past SensorRecoveryUseCase::DISCONNECT_THRESHOLD_MS (60 s) of continuous failure.
    while (f.clock.nowMs() < 90000)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        if (sleepMs > 0)
        {
            f.clock.advanceMs(sleepMs);
        }
    }

    char message[224];
    snprintf(message, sizeof(message),
             "after 90 s of a sensor that never answers the port was power-cycled %d times off "
             "and %d times on; recovery never fired",
             f.port0.powerOffTransitions - powerOffBefore,
             f.port0.powerOnTransitions - powerOnBefore);

    TEST_ASSERT_TRUE_MESSAGE(f.port0.powerOffTransitions > powerOffBefore, message);
    TEST_ASSERT_TRUE_MESSAGE(f.port0.powerOnTransitions > powerOnBefore, message);
    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.recoveryActionCount() >= 2,
                             "recovery did not take both the power-off and the power-on step");
    TEST_ASSERT_TRUE_MESSAGE(f.port0.isPowered(),
                             "the port was left without power after a recovery cycle");
}

static void test_a_sensor_that_answers_again_takes_its_port_out_of_recovery(void)
{
    AcquisitionFixture f;
    f.addSilentDevice(f.port0, 1);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.port0.powerOnPort();

    while (f.clock.nowMs() < 90000)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        if (sleepMs > 0)
        {
            f.clock.advanceMs(sleepMs);
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.recoveryActionCount() >= 2,
                             "recovery never started, so there is nothing to recover from");

    // The sensor comes back.
    f.port0.device(1)->silent = false;
    f.port0.device(1)->defaultRegisterValue = 250;
    f.port0.device(1)->registerValues.push_back(250);

    const uint64_t recoveryActionsBefore = f.acquisition.recoveryActionCount();

    while (f.clock.nowMs() < 300000)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        if (sleepMs > 0)
        {
            f.clock.advanceMs(sleepMs);
        }
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)(f.acquisition.recoveryActionCount() - recoveryActionsBefore),
                                     "the port kept being power-cycled after its sensor answered "
                                     "again: observe() is being told the sensor is still in error");
    TEST_ASSERT_TRUE_MESSAGE(f.sensorValueModel.getLastReadTime("acq.0") > 0,
                             "the sensor never produced a reading after it came back");
}

// --------------------------------------------------------------------------------------------
// A port a step of the discovery or a command left dark
//
// A model scan powers its own port and takes the power of every other one, and does not give it
// back (SensorReader::scanModbusSensor()). What is on those ports needs its power for a while before
// it answers - the firmware's belief is ten seconds - and ModbusRS485::sendData() raises the power
// only a hundred milliseconds before the frame. So a read of such a port is a miss, and the port
// recovery counts it.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief A discovery whose one step is a model scan of a sensor on another port of the wire. */
class ScanOnAnotherPort : public IStartupSensorScan {
public:
    ScanOnAnotherPort(ISensorReader& reader, const SensorEntity& scanned)
        : _reader(reader), _scanned(scanned)
    {
    }

    uint64_t endedAtMs = 0;
    VirtualClock* clock = nullptr;

    void describeStoredSensors(Node&) override {}
    void arm() override {}
    void rescan(int) override {}
    void armPreparationOnly() override {}
    bool isRunning() const override { return false; }
    bool isScanEntryRunning() const override { return false; }
    bool takeNextStep(StartupScanStep&) override { return false; }
    void returnStep() override {}
    StartupScanOutcome runStepOnBus(const StartupScanStep&) override
    {
        StartupScanOutcome outcome;
        outcome.answered = _reader.scanSensor(_scanned);
        endedAtMs = clock != nullptr ? clock->nowMs() : 0;
        return outcome;
    }
    void applyStepOutcome(const StartupScanOutcome&) override {}
    bool holdsAWindowOpen() const override { return false; }
    bool keepsThePortDark(int) const override { return false; }
    void reviewSensorLiveness() override {}
    void onDescribedStructureSettled(bool) override {}

private:
    ISensorReader& _reader;
    SensorEntity _scanned;
};

/** @brief The firmware's belief about how long a sensor takes to come up. */
const uint32_t A_SENSOR_COMES_UP_IN_MS = 10000;

void runTheSweepUntil(AcquisitionFixture& f, uint64_t untilMs)
{
    while (f.clock.nowMs() < untilMs)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }
}

/** @brief Frames on this port, from this moment, while it had had its power for less than that. */
unsigned framesBeforeTheWakeUp(const SimRs485Bus& port, uint64_t fromMs)
{
    unsigned early = 0;
    for (size_t i = 0; i < port.sentAtUs.size() && i < port.sentAfterPowerForMs.size(); i++)
    {
        if (port.sentAtUs[i] / 1000 >= fromMs && port.sentAfterPowerForMs[i] < A_SENSOR_COMES_UP_IN_MS)
        {
            early++;
        }
    }
    return early;
}

} // namespace

// After a model scan of the other port of its wire, the sensor's port is given its power back and
// left alone until it has come up: no frame to a sensor that cannot answer yet, no miss, and the
// next read answers.
static void test_a_port_a_step_left_dark_is_not_read_until_it_has_come_up(void)
{
    AcquisitionFixture f;
    f.wireThePortsLikeTheBoard();
    // The board's power pin: a frame to a dark port raises it, and a device needs its warm-up.
    f.port0.setPortPowerLikeTheBoard(true);
    f.port0.recordSendTimes(true);
    f.addAnsweringDevice(f.port0, 1, 250);
    f.port0.device(1)->wakeUpMs = A_SENSOR_COMES_UP_IN_MS;
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.addAnsweringDevice(f.port1, 5, 250);
    SensorEntity scanned = makeSingleChannelSensor("scan.1", 1, 5);
    scanned.modbusRegisterConfigs[0].canConnectMaxValue = 1000.0f;

    ScanOnAnotherPort discovery(f.sensorReader, scanned);
    discovery.clock = &f.clock;
    f.acquisition.setStartupSensorScan(&discovery);

    // Up and answering: the device has had its power for longer than its warm-up.
    f.clock.advanceMs(A_SENSOR_COMES_UP_IN_MS + 5000);
    runTheSweepUntil(f, 40000);
    const uint32_t answeredBefore = f.sensorValueModel.getLastReadTime("acq.0");
    TEST_ASSERT_TRUE_MESSAGE(answeredBefore > 0, "the sensor never answered before the scan");

    StartupScanStep step;
    step.kind = StartupScanStep::VERIFY_KNOWN;
    step.pin = 1;
    TEST_ASSERT_TRUE(f.acquisition.submitStartupScanStep(step) !=
                     ISensorAcquisition::kBusCommandRejected);
    bool markedInError = false;
    while (f.clock.nowMs() < 120000)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        markedInError = markedInError || f.sensorRepository.lastStatusIsError;
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }

    TEST_ASSERT_TRUE_MESSAGE(discovery.endedAtMs > 0, "the scan never ran");
    TEST_ASSERT_FALSE_MESSAGE(f.port0.powerOffTransitions == 0,
                              "the scan did not take the sensor's port down - there is no dark to test");
    char message[256];
    snprintf(message, sizeof(message),
             "after the scan the sweep sent %u frame(s) to the sensor's port before it had come up",
             framesBeforeTheWakeUp(f.port0, discovery.endedAtMs));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, framesBeforeTheWakeUp(f.port0, discovery.endedAtMs), message);
    TEST_ASSERT_FALSE_MESSAGE(markedInError,
                              "a sensor that answers once its port is up was marked in error");
    TEST_ASSERT_TRUE_MESSAGE(f.sensorValueModel.getLastReadTime("acq.0") > discovery.endedAtMs,
                             "the sensor was not read again after the scan");
    TEST_ASSERT_TRUE_MESSAGE(f.port0.isPowered(), "the sensor's port was left dark");
}

// And it is read the moment its port has come up - not at the next sweep: the visit that finds the
// port dark gives it its power, and the read follows PortPower::WAKE_UP_MS later, while the sweep
// keeps its own cadence.
static void test_a_port_given_its_power_back_is_read_as_soon_as_it_has_come_up(void)
{
    AcquisitionFixture f;
    f.wireThePortsLikeTheBoard();
    f.port0.setPortPowerLikeTheBoard(true);
    f.port0.recordSendTimes(true);
    f.port0.recordPowerOnTimes(true);
    f.addAnsweringDevice(f.port0, 1, 250);
    f.port0.device(1)->wakeUpMs = A_SENSOR_COMES_UP_IN_MS;
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.addAnsweringDevice(f.port1, 5, 250);
    SensorEntity scanned = makeSingleChannelSensor("scan.1", 1, 5);
    scanned.modbusRegisterConfigs[0].canConnectMaxValue = 1000.0f;

    ScanOnAnotherPort discovery(f.sensorReader, scanned);
    discovery.clock = &f.clock;
    f.acquisition.setStartupSensorScan(&discovery);

    f.clock.advanceMs(A_SENSOR_COMES_UP_IN_MS + 5000);
    runTheSweepUntil(f, 40000);
    StartupScanStep step;
    step.kind = StartupScanStep::VERIFY_KNOWN;
    step.pin = 1;
    TEST_ASSERT_TRUE(f.acquisition.submitStartupScanStep(step) !=
                     ISensorAcquisition::kBusCommandRejected);
    runTheSweepUntil(f, 120000);

    TEST_ASSERT_TRUE_MESSAGE(discovery.endedAtMs > 0, "the scan never ran");
    // The sweep gave the port its power back after the scan: its last power-on.
    TEST_ASSERT_FALSE_MESSAGE(f.port0.powerOnAtUs.empty(), "the port was never given its power");
    const uint64_t backAtMs = f.port0.powerOnAtUs.back() / 1000;
    uint64_t readAtMs = 0;
    for (size_t i = 0; i < f.port0.sentAtUs.size() && readAtMs == 0; i++)
    {
        if (f.port0.sentAtUs[i] / 1000 >= backAtMs)
        {
            readAtMs = f.port0.sentAtUs[i] / 1000;
        }
    }
    char message[224];
    snprintf(message, sizeof(message),
             "the port came back at t=%lu ms and could be read from t=%lu ms; the sensor was read at "
             "t=%lu ms",
             (unsigned long)backAtMs, (unsigned long)(backAtMs + PortPower::WAKE_UP_MS),
             (unsigned long)readAtMs);
    TEST_ASSERT_TRUE_MESSAGE(backAtMs > discovery.endedAtMs, message);
    TEST_ASSERT_TRUE_MESSAGE(readAtMs >= backAtMs + PortPower::WAKE_UP_MS, message);
    TEST_ASSERT_TRUE_MESSAGE(readAtMs <= backAtMs + PortPower::WAKE_UP_MS + 500, message);
    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.heldSensorReadCount() >= 1, message);
    // Once: the early read and a visit of the sweep do not both read it (a read of this sensor is
    // one frame, answered in well under a second).
    unsigned framesSoonAfter = 0;
    for (size_t i = 0; i < f.port0.sentAtUs.size(); i++)
    {
        const uint64_t atMs = f.port0.sentAtUs[i] / 1000;
        framesSoonAfter += (atMs >= readAtMs && atMs < readAtMs + 1000) ? 1 : 0;
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, framesSoonAfter, "the sensor was read twice within a second");
}

// A sensor that had already missed reads when a window took its port, and answers again by the time
// the window is over: the window is not added to its silence, so it is not power-cycled the moment
// it is read again.
static void test_a_silence_that_began_before_a_window_is_not_finished_by_it(void)
{
    AcquisitionFixture f;
    HeldWindow window;
    window.darkPort = 0;
    f.acquisition.setStartupSensorScan(&window);
    f.addAnsweringDevice(f.port0, 1, 250);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    runTheSweepUntil(f, 30000);
    f.port0.device(1)->silent = true;
    runTheSweepUntil(f, 55000);
    TEST_ASSERT_TRUE_MESSAGE(f.sensorRepository.lastStatusIsError,
                             "the sensor had not missed a read before the window - nothing to test");

    // The window takes the port down for its length and gives it back at its close, as the
    // presence poll that opens one does (StartupSensorScanUseCase::isolateTheLineForThePoll()).
    window.open = true;
    f.port0.powerOffPort();
    f.port0.device(1)->silent = false;
    runTheSweepUntil(f, 125000);
    window.open = false;
    f.port0.powerOnPort();
    runTheSweepUntil(f, 180000);

    char message[160];
    snprintf(message, sizeof(message),
             "the port recovery acted %lu time(s) on a sensor that answers once the window is over",
             (unsigned long)f.acquisition.recoveryActionCount());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, f.acquisition.recoveryActionCount(), message);
    TEST_ASSERT_TRUE_MESSAGE(f.sensorValueModel.getLastReadTime("acq.0") > 125000,
                             "the sensor was not read after the window");
}

// A sensor silent with its port up is still power-cycled across a window: the recovery acts once
// the silence the port could have broken passes the threshold - not before it, and not much after.
static void test_a_sensor_silent_with_its_port_up_is_power_cycled_across_a_window(void)
{
    AcquisitionFixture f;
    HeldWindow window;
    window.darkPort = 0;
    f.acquisition.setStartupSensorScan(&window);
    f.addSilentDevice(f.port0, 1);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    uint64_t firstActionAtMs = 0;
    while (f.clock.nowMs() < 240000 && firstActionAtMs == 0)
    {
        // The window takes the port down at its first step and gives it back at its close.
        const bool open = f.clock.nowMs() >= 20000 && f.clock.nowMs() < 50000;
        if (open != window.open)
        {
            window.open = open;
            if (open)
            {
                f.port0.powerOffPort();
            }
            else
            {
                f.port0.powerOnPort();
            }
        }
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        if (f.acquisition.recoveryActionCount() > 0)
        {
            firstActionAtMs = f.clock.nowMs();
        }
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }

    // Silent from its first read at 0. The port could not answer from the window's first step at
    // 20 s to 10 s after it came back at 50 s - 40 s the recovery must not count - so a minute of
    // silence it could have broken is over at 100 s, and the sweep, reading every 15 s, notices it
    // within two of its intervals.
    const uint64_t couldNotAnswerMs = 30000 + PortPower::WAKE_UP_MS;
    const uint64_t aMinuteItCouldBreakMs = SensorRecoveryUseCase::DISCONNECT_THRESHOLD_MS + couldNotAnswerMs;
    char message[200];
    snprintf(message, sizeof(message),
             "the port recovery first acted at t=%lu ms on a sensor silent from t=0 whose port could "
             "not answer for 40 s of it; a minute of silence it could break is over at t=%lu ms",
             (unsigned long)firstActionAtMs, (unsigned long)aMinuteItCouldBreakMs);
    TEST_ASSERT_TRUE_MESSAGE(firstActionAtMs >= aMinuteItCouldBreakMs, message);
    TEST_ASSERT_TRUE_MESSAGE(firstActionAtMs <= aMinuteItCouldBreakMs +
                                                    2 * SensorAcquisitionService::SWEEP_INTERVAL_MS,
                             message);
}

// In the polling after a power cycle, a stretch the port could not answer in is not polling: a
// window takes the port down for 40 s of the minute - 50 s with the wake-up after it - and the power
// cycle is retried only after a minute of polling a port that could answer, and still retried.
static void test_a_retried_power_cycle_counts_only_polling_a_port_that_could_answer(void)
{
    AcquisitionFixture f;
    HeldWindow window;
    window.darkPort = 0;
    f.acquisition.setStartupSensorScan(&window);
    f.port0.recordPowerOffTimes(true);
    f.port0.recordPowerOnTimes(true);
    f.addSilentDevice(f.port0, 1);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));

    uint64_t poweredOnAtMs = 0;
    uint64_t retriedAtMs = 0;
    while (f.clock.nowMs() < 600000 && retriedAtMs == 0)
    {
        // The window opens once the polling has begun - at the first visit after the power came
        // back, within a sweep interval of it.
        const bool open = poweredOnAtMs > 0 && f.clock.nowMs() >= poweredOnAtMs + 20000 &&
                          f.clock.nowMs() < poweredOnAtMs + 60000;
        if (open != window.open)
        {
            window.open = open;
            if (open)
            {
                f.port0.powerOffPort();
            }
            else
            {
                f.port0.powerOnPort();
            }
        }
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        if (poweredOnAtMs == 0 && f.acquisition.recoveryActionCount() == 2)
        {
            poweredOnAtMs = f.clock.nowMs();
        }
        if (f.acquisition.recoveryActionCount() >= 3)
        {
            retriedAtMs = f.clock.nowMs();
        }
        // Small steps, so the window opens and closes when it says it does.
        f.clock.advanceMs(sleepMs == 0 ? 1 : (sleepMs < 500 ? sleepMs : 500));
    }

    TEST_ASSERT_TRUE_MESSAGE(poweredOnAtMs > 0, "the recovery never switched the port back on");
    TEST_ASSERT_TRUE_MESSAGE(f.port0.powerOffAtUs.size() >= 2 && f.port0.powerOnAtUs.size() >= 2,
                             "the window never took the port down and gave it back");
    // The window's own transitions, from the stand-in: the last down and up before the retry.
    uint64_t windowDownMs = 0;
    uint64_t windowUpMs = 0;
    for (size_t i = 0; i < f.port0.powerOffAtUs.size(); i++)
    {
        const uint64_t atMs = f.port0.powerOffAtUs[i] / 1000;
        if (atMs > poweredOnAtMs && atMs < retriedAtMs)
        {
            windowDownMs = atMs;
        }
    }
    for (size_t i = 0; i < f.port0.powerOnAtUs.size(); i++)
    {
        const uint64_t atMs = f.port0.powerOnAtUs[i] / 1000;
        if (atMs > windowDownMs && atMs < retriedAtMs)
        {
            windowUpMs = atMs;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(windowDownMs > 0 && windowUpMs > windowDownMs,
                             "the window's transitions are not between the power cycle and its retry");

    // The polling begins at the visit after the power came back, no earlier; of it, the port could
    // not answer from the window's first moment to the wake-up after its last. So a minute of
    // polling it could answer is over no earlier than that much after the power came back - and the
    // sweep, every 15 s, retries within two of its intervals, the one read owed after the stretch and
    // the interval the polling began in.
    const uint64_t couldNotAnswerMs = windowUpMs + PortPower::WAKE_UP_MS - windowDownMs;
    const uint64_t earliestMs = poweredOnAtMs + SensorRecoveryUseCase::POLL_WINDOW_MS + couldNotAnswerMs;
    const uint64_t latestMs = earliestMs + 4 * SensorAcquisitionService::SWEEP_INTERVAL_MS;
    char message[256];
    snprintf(message, sizeof(message),
             "the power came back at t=%lu ms and the cycle was retried at t=%lu ms; with %lu ms of the "
             "polling dark or coming up, no earlier than t=%lu ms and no later than t=%lu ms",
             (unsigned long)poweredOnAtMs, (unsigned long)retriedAtMs, (unsigned long)couldNotAnswerMs,
             (unsigned long)earliestMs, (unsigned long)latestMs);
    TEST_ASSERT_TRUE_MESSAGE(retriedAtMs >= earliestMs, message);
    TEST_ASSERT_TRUE_MESSAGE(retriedAtMs <= latestMs, message);
}

// The darkness the recovery makes itself is its own to end: a port it switched off is switched back
// on by the recovery at the next visit, whatever bus work ran in between.
static void test_a_port_the_recovery_switched_off_is_switched_on_by_it(void)
{
    AcquisitionFixture f;
    f.addSilentDevice(f.port0, 1);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.addAnsweringDevice(f.port2, 5, 250);
    SensorEntity scanned = makeSingleChannelSensor("scan.2", 2, 5);
    scanned.modbusRegisterConfigs[0].canConnectMaxValue = 1000.0f;
    ScanOnAnotherPort discovery(f.sensorReader, scanned);
    discovery.clock = &f.clock;
    f.acquisition.setStartupSensorScan(&discovery);

    while (f.clock.nowMs() < 120000 && f.acquisition.recoveryActionCount() == 0)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, f.acquisition.recoveryActionCount(),
                                     "the recovery never switched the port off");
    TEST_ASSERT_FALSE(f.port0.isPowered());

    StartupScanStep step;
    step.kind = StartupScanStep::VERIFY_KNOWN;
    step.pin = 2;
    f.acquisition.submitStartupScanStep(step);
    while (f.clock.nowMs() < 240000 && f.acquisition.recoveryActionCount() < 2)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }

    TEST_ASSERT_TRUE_MESSAGE(discovery.endedAtMs > 0, "the step never ran");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(2, f.acquisition.recoveryActionCount(),
                                     "the recovery did not switch its port back on itself");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, f.acquisition.darkPortPoweredCount(),
                                     "the sweep gave power to a port the recovery had switched off");
    TEST_ASSERT_TRUE(f.port0.isPowered());
}

namespace {

/** @brief A discovery whose steps are named by the test; the one on the scanned sensor's port is a
 *         model scan there. */
class StepsByPin : public IStartupSensorScan {
public:
    StepsByPin(ISensorReader& reader, const SensorEntity& scanned, VirtualClock& clock)
        : _reader(reader), _scanned(scanned), _clock(clock)
    {
    }

    uint64_t scanStartedAtMs = 0;

    void describeStoredSensors(Node&) override {}
    void arm() override {}
    void rescan(int) override {}
    void armPreparationOnly() override {}
    bool isRunning() const override { return false; }
    bool isScanEntryRunning() const override { return false; }
    bool takeNextStep(StartupScanStep&) override { return false; }
    void returnStep() override {}
    StartupScanOutcome runStepOnBus(const StartupScanStep& step) override
    {
        StartupScanOutcome outcome;
        if (step.pin == _scanned.pin)
        {
            scanStartedAtMs = _clock.nowMs();
            outcome.answered = _reader.scanSensor(_scanned);
        }
        return outcome;
    }
    void applyStepOutcome(const StartupScanOutcome&) override {}
    bool holdsAWindowOpen() const override { return false; }
    bool keepsThePortDark(int) const override { return false; }
    void reviewSensorLiveness() override {}
    void onDescribedStructureSettled(bool) override {}

private:
    ISensorReader& _reader;
    SensorEntity _scanned;
    VirtualClock& _clock;
};

/** @brief Serve one step now - after the sensor, if a sweep is due - without moving the clock. */
void serveAStep(AcquisitionFixture& f, StartupScanStep::Kind kind, int pin)
{
    StartupScanStep step;
    step.kind = kind;
    step.pin = pin;
    const uint64_t served = f.acquisition.startupScanStepCount();
    TEST_ASSERT_TRUE(f.acquisition.submitStartupScanStep(step) !=
                     ISensorAcquisition::kBusCommandRejected);
    for (int guard = 0; guard < 4 && f.acquisition.startupScanStepCount() == served; guard++)
    {
        f.acquisition.step();
        f.sensorService.loop();
    }
    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.startupScanStepCount() > served, "the step was not served");
}

} // namespace

// The stretch the recovery leaves out runs from the moment the port's pin went down to the moment
// the port had had its power back for the wake-up - read off the port's own record, not off the work
// around it: a step about the port's own sensor and a step that only gives power, served just before
// the scan that takes the port down, move nothing and start nothing. The tape line that ends the
// stretch says how long it was, and the stand-in says when the pin moved.
static void test_a_stretch_runs_from_the_pin_going_down_to_the_wake_up_after_it_comes_back(void)
{
    AcquisitionFixture f;
    f.wireThePortsLikeTheBoard();
    f.debugSystem.featureEnabled = true;
    f.port0.setPortPowerLikeTheBoard(true);
    f.port0.recordPowerOffTimes(true);
    f.port0.recordPowerOnTimes(true);
    f.addAnsweringDevice(f.port0, 1, 250);
    f.port0.device(1)->wakeUpMs = A_SENSOR_COMES_UP_IN_MS;
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.addAnsweringDevice(f.port1, 5, 250);
    SensorEntity onPort1 = makeSingleChannelSensor("scan.1", 1, 5);
    onPort1.modbusRegisterConfigs[0].canConnectMaxValue = 1000.0f;
    StepsByPin discovery(f.sensorReader, onPort1, f.clock);
    f.acquisition.setStartupSensorScan(&discovery);

    f.clock.advanceMs(A_SENSOR_COMES_UP_IN_MS + 5000);
    runTheSweepUntil(f, 31000);
    // The read that is due: port 0 is seen up.
    f.acquisition.step();
    f.sensorService.loop();

    // Before the next sweep, a few seconds apart: a step about port 0's own sensor, a step that only
    // gives power, and the model scan of port 1 that takes port 0 down.
    serveAStep(f, StartupScanStep::VERIFY_KNOWN, 0);
    f.clock.advanceMs(3000);
    serveAStep(f, StartupScanStep::POWER_FREE_PORTS, -1);
    f.clock.advanceMs(3000);
    serveAStep(f, StartupScanStep::VERIFY_KNOWN, 1);
    TEST_ASSERT_TRUE_MESSAGE(discovery.scanStartedAtMs > 0, "the scan of port 1 never ran");

    unsigned long leftOutMs = 0;
    bool ended = false;
    while (f.clock.nowMs() < 120000 && !ended)
    {
        const size_t before = f.logger.messages.size();
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        for (size_t i = before; i < f.logger.messages.size() && !ended; i++)
        {
            const char* line = strstr(f.logger.messages[i].c_str(), "up again - ");
            ended = line != nullptr && sscanf(line, "up again - %lu ms", &leftOutMs) == 1;
        }
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }

    TEST_ASSERT_TRUE_MESSAGE(ended, "the port's stretch never came to its end on the tape");
    TEST_ASSERT_FALSE_MESSAGE(f.port0.powerOffAtUs.empty() || f.port0.powerOnAtUs.empty(),
                              "the port was never taken down and given back - nothing to test");
    // The record rounds the way down to the millisecond before and the way up to the one after.
    const uint64_t downAtMs = f.port0.powerOffAtUs.back() / 1000;
    const uint64_t upAtMs = (f.port0.powerOnAtUs.back() + 999) / 1000;
    const uint64_t expectedMs = upAtMs + PortPower::WAKE_UP_MS - downAtMs;
    char message[224];
    snprintf(message, sizeof(message),
             "the tape left %lu ms out; the pin went down at %lu ms and came back at %lu ms, which "
             "with the wake-up is %lu ms",
             leftOutMs, (unsigned long)downAtMs, (unsigned long)upAtMs, (unsigned long)expectedMs);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(expectedMs, (uint64_t)leftOutMs, message);
    TEST_ASSERT_TRUE_MESSAGE(downAtMs >= discovery.scanStartedAtMs,
                             "the stretch began before the scan that took the port down");
}

namespace {

/** @brief Steps that only move port 0's power, the way a poll's isolation and its give-back do. */
class PortZeroSwitchSteps : public IStartupSensorScan {
public:
    explicit PortZeroSwitchSteps(ISensorReader& reader) : _reader(reader) {}

    void describeStoredSensors(Node&) override {}
    void arm() override {}
    void rescan(int) override {}
    void armPreparationOnly() override {}
    bool isRunning() const override { return false; }
    bool isScanEntryRunning() const override { return false; }
    bool takeNextStep(StartupScanStep&) override { return false; }
    void returnStep() override {}
    StartupScanOutcome runStepOnBus(const StartupScanStep& step) override
    {
        _reader.setModbusPortPower(0, step.kind == StartupScanStep::POWER_FREE_PORTS);
        return StartupScanOutcome();
    }
    void applyStepOutcome(const StartupScanOutcome&) override {}
    bool holdsAWindowOpen() const override { return false; }
    bool keepsThePortDark(int) const override { return false; }
    void reviewSensorLiveness() override {}
    void onDescribedStructureSettled(bool) override {}

private:
    ISensorReader& _reader;
};

/** @brief The bus half of commands that only move port 0's power, as a model scan's isolation does. */
class PortZeroSwitchCommands : public MockModuleCommandExecutionUseCase {
public:
    explicit PortZeroSwitchCommands(ISensorReader& reader) : _reader(reader) {}

    ComplexCommandOutcome executeBusWork(const ComplexCommandWork& work) override
    {
        _reader.setModbusPortPower(0, work.command.command == "PortUp");
        ComplexCommandOutcome outcome;
        outcome.status = ModuleCommandStatus::DONE;
        return outcome;
    }

private:
    ISensorReader& _reader;
};

/**
 * @brief Take port 0 down, give it back, take it down and give it back again, a second apart and
 *        all between two visits of the sweep - by steps of the discovery or by bus commands - and
 *        read what the tape says was left out.
 */
unsigned long leftOutAcrossTwoDarks(bool byCommands, uint64_t& firstDownMs, uint64_t& lastUpMs)
{
    AcquisitionFixture f;
    f.debugSystem.featureEnabled = true;
    f.port0.setPortPowerLikeTheBoard(true);
    f.port0.recordPowerOffTimes(true);
    f.port0.recordPowerOnTimes(true);
    f.addAnsweringDevice(f.port0, 1, 250);
    f.port0.device(1)->wakeUpMs = A_SENSOR_COMES_UP_IN_MS;
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    PortZeroSwitchSteps discovery(f.sensorReader);
    f.acquisition.setStartupSensorScan(&discovery);
    PortZeroSwitchCommands commands(f.sensorReader);
    f.acquisition.setRawCommandExecutor(&commands);

    f.clock.advanceMs(A_SENSOR_COMES_UP_IN_MS + 5000);
    runTheSweepUntil(f, 31000);
    // The read that is due: port 0 is seen up.
    f.acquisition.step();
    f.sensorService.loop();

    const size_t offsBefore = f.port0.powerOffAtUs.size();
    for (int i = 0; i < 4; i++)
    {
        const bool down = (i % 2) == 0;
        if (byCommands)
        {
            ComplexCommandWork work;
            work.onBus = true;
            work.command.command = down ? "PortDown" : "PortUp";
            const uint64_t served = f.acquisition.busCommandCount();
            TEST_ASSERT_TRUE(f.acquisition.submitComplexBusCommand(work) !=
                             ISensorAcquisition::kBusCommandRejected);
            f.acquisition.step();
            TEST_ASSERT_TRUE_MESSAGE(f.acquisition.busCommandCount() > served, "the command was not served");
        }
        else
        {
            serveAStep(f, down ? StartupScanStep::RELEASE_FREE_PORTS : StartupScanStep::POWER_FREE_PORTS, -1);
        }
        f.clock.advanceMs(1000);
    }

    unsigned long leftOutMs = 0;
    bool ended = false;
    while (f.clock.nowMs() < 120000 && !ended)
    {
        const size_t before = f.logger.messages.size();
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        for (size_t i = before; i < f.logger.messages.size() && !ended; i++)
        {
            const char* line = strstr(f.logger.messages[i].c_str(), "up again - ");
            ended = line != nullptr && sscanf(line, "up again - %lu ms", &leftOutMs) == 1;
        }
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }
    TEST_ASSERT_TRUE_MESSAGE(ended, "the port's stretch never came to its end on the tape");
    TEST_ASSERT_TRUE_MESSAGE(f.port0.powerOffAtUs.size() == offsBefore + 2,
                             "the port did not go down twice");
    firstDownMs = f.port0.powerOffAtUs[offsBefore] / 1000;
    lastUpMs = (f.port0.powerOnAtUs.back() + 999) / 1000;
    return leftOutMs;
}

} // namespace

// A port taken down twice between two visits of the sweep, and never up long enough in between to
// come up: one stretch, from the first time it went down - by steps and by bus commands alike. Each
// piece of work moves the pin at most once each way, and the ports are looked at after every one,
// so the first power-off is not lost behind the second.
static void test_two_darks_between_two_visits_are_one_stretch_from_the_first(void)
{
    const bool byCommands[2] = {false, true};
    for (int i = 0; i < 2; i++)
    {
        uint64_t firstDownMs = 0;
        uint64_t lastUpMs = 0;
        const unsigned long leftOutMs = leftOutAcrossTwoDarks(byCommands[i], firstDownMs, lastUpMs);
        const uint64_t expectedMs = lastUpMs + PortPower::WAKE_UP_MS - firstDownMs;
        char message[224];
        snprintf(message, sizeof(message),
                 "by %s: the tape left %lu ms out; the port first went down at %lu ms and was last "
                 "given back at %lu ms, which with the wake-up is %lu ms",
                 byCommands[i] ? "bus commands" : "steps", leftOutMs, (unsigned long)firstDownMs,
                 (unsigned long)lastUpMs, (unsigned long)expectedMs);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(expectedMs, (uint64_t)leftOutMs, message);
    }
}

// The recovery's own power cycle is the recovery's to count: no stretch is left out of it, and the
// tape does not say one was.
static void test_the_recoverys_own_power_cycle_is_no_stretch(void)
{
    AcquisitionFixture f;
    f.debugSystem.featureEnabled = true;
    f.addSilentDevice(f.port0, 1);
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.port0.powerOnPort();

    unsigned stretches = 0;
    while (f.clock.nowMs() < 150000)
    {
        const size_t before = f.logger.messages.size();
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        for (size_t i = before; i < f.logger.messages.size(); i++)
        {
            stretches += strstr(f.logger.messages[i].c_str(), "up again - ") != nullptr ? 1 : 0;
        }
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }

    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.recoveryActionCount() >= 2,
                             "the recovery did not take the power off and back on");
    char message[160];
    snprintf(message, sizeof(message),
             "the tape left %u stretch(es) out of a port only the recovery switched off and on",
             stretches);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, stretches, message);
}

// A port nothing can switch on is not held for its power: the sweep reads it, as it always has, and
// what the read finds decides - held, it would stay unread and unrecovered for good.
static void test_a_port_nothing_can_switch_on_is_read_as_it_is(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    SimArduino arduino(clock, &scheduler);
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = false;
    MockSensorReader reader;
    reader.portPowered[0] = false;
    reader.setModbusPortPowerReturnValue = false;
    SensorAcquisitionService acquisition(&reader, &arduino, &scheduler, &logger, &debugSystem);
    acquisition.setSensors(std::vector<SensorEntity>(1, makeSingleChannelSensor("acq.0", 0, 1)));

    while (clock.nowMs() < 60000)
    {
        const uint32_t sleepMs = acquisition.step();
        clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }

    char message[160];
    snprintf(message, sizeof(message),
             "a sensor on a port nothing could switch on was read %lu time(s) in a minute and held "
             "%lu time(s)",
             (unsigned long)acquisition.pollCount(), (unsigned long)acquisition.heldDarkSkipCount());
    TEST_ASSERT_TRUE_MESSAGE(acquisition.pollCount() >= 3, message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, acquisition.heldDarkSkipCount(), message);
}

// Unless a step or a bus command works on that very port before it has come up: work about a port -
// a scan, an address write - may change the address its device answers at, and the sweep's list of
// sensors is refreshed only at its own interval. So the held sensor is left to the next sweep.
static void heldPortWorkedOnBeforeItCameUp(bool byCommand)
{
    AcquisitionFixture f;
    f.wireThePortsLikeTheBoard();
    f.port0.setPortPowerLikeTheBoard(true);
    f.port0.recordSendTimes(true);
    f.port0.recordPowerOnTimes(true);
    f.addAnsweringDevice(f.port0, 1, 250);
    f.port0.device(1)->wakeUpMs = A_SENSOR_COMES_UP_IN_MS;
    f.addSensor(makeSingleChannelSensor("acq.0", 0, 1));
    f.addAnsweringDevice(f.port1, 5, 250);
    SensorEntity scanned = makeSingleChannelSensor("scan.1", 1, 5);
    scanned.modbusRegisterConfigs[0].canConnectMaxValue = 1000.0f;

    StepsByPin discovery(f.sensorReader, scanned, f.clock);
    f.acquisition.setStartupSensorScan(&discovery);
    PortZeroSwitchCommands commands(f.sensorReader);
    f.acquisition.setRawCommandExecutor(&commands);

    f.clock.advanceMs(A_SENSOR_COMES_UP_IN_MS + 5000);
    runTheSweepUntil(f, 40000);
    serveAStep(f, StartupScanStep::VERIFY_KNOWN, 1);
    // Until the sweep has given the port its power back and left the sensor unread.
    const uint64_t heldBefore = f.acquisition.heldDarkSkipCount();
    while (f.clock.nowMs() < 120000 && f.acquisition.heldDarkSkipCount() == heldBefore)
    {
        const uint32_t sleepMs = f.acquisition.step();
        f.sensorService.loop();
        f.clock.advanceMs(sleepMs > 0 ? sleepMs : 1);
    }
    TEST_ASSERT_TRUE_MESSAGE(f.acquisition.heldDarkSkipCount() > heldBefore,
                             "the sweep never left the sensor unread after the scan");
    const uint64_t backAtMs = f.port0.powerOnAtUs.back() / 1000;
    const uint64_t earlyReadsBefore = f.acquisition.heldSensorReadCount();
    f.clock.advanceMs(1000);
    if (byCommand)
    {
        // A command about port 0 whose bus work moves no pin: the port is up already.
        ComplexCommandWork work;
        work.onBus = true;
        work.pin = 0;
        work.command.command = "PortUp";
        const uint64_t served = f.acquisition.busCommandCount();
        TEST_ASSERT_TRUE(f.acquisition.submitComplexBusCommand(work) !=
                         ISensorAcquisition::kBusCommandRejected);
        f.acquisition.step();
        TEST_ASSERT_TRUE_MESSAGE(f.acquisition.busCommandCount() > served, "the command was not served");
    }
    else
    {
        // A step about port 0 that moves no pin: the stand's discovery scans only port 1.
        serveAStep(f, StartupScanStep::VERIFY_KNOWN, 0);
    }
    runTheSweepUntil(f, backAtMs + 40000);

    uint64_t readAtMs = 0;
    for (size_t i = 0; i < f.port0.sentAtUs.size() && readAtMs == 0; i++)
    {
        if (f.port0.sentAtUs[i] / 1000 >= backAtMs)
        {
            readAtMs = f.port0.sentAtUs[i] / 1000;
        }
    }
    char message[224];
    snprintf(message, sizeof(message),
             "a %s about port 0 ran while its sensor was held; the port came back at t=%lu ms and the "
             "sensor was read at t=%lu ms, %lu time(s) out of the sweep's order",
             byCommand ? "bus command" : "step", (unsigned long)backAtMs, (unsigned long)readAtMs,
             (unsigned long)(f.acquisition.heldSensorReadCount() - earlyReadsBefore));
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(earlyReadsBefore, f.acquisition.heldSensorReadCount(), message);
    TEST_ASSERT_TRUE_MESSAGE(readAtMs > backAtMs + PortPower::WAKE_UP_MS + 1000, message);
}

static void test_work_about_a_held_port_leaves_its_sensor_to_the_next_sweep(void)
{
    heldPortWorkedOnBeforeItCameUp(false);
    heldPortWorkedOnBeforeItCameUp(true);
}

extern "C" void run_sensor_acquisition_tests(void)
{
    printf("\n----- Running Sensor Acquisition Tests -----\n");

    RUN_TEST(test_a_reading_carries_the_time_the_bus_answered_not_the_time_it_was_applied);
    RUN_TEST(test_a_rule_is_told_the_sensor_is_unreachable_once_the_bus_stops_confirming_it);
    RUN_TEST(test_a_rule_bound_the_way_the_backend_binds_it_reports_unreachable_on_a_healthy_bus);
    RUN_TEST(test_the_sweep_goes_round_the_ports_rather_than_down_one);
    RUN_TEST(test_a_dead_bus_keeps_the_sweep_running_and_marks_the_sensors);
    RUN_TEST(test_a_refused_acquisition_task_is_reported_and_retried);
    RUN_TEST(test_undrained_readings_are_capped_and_counted);
    RUN_TEST(test_a_scan_holds_the_line_for_its_whole_isolate_warmup_ping_sequence);
    RUN_TEST(test_a_port_whose_sensor_stays_silent_is_power_cycled_and_comes_back);
    RUN_TEST(test_a_port_held_dark_by_the_discovery_is_neither_read_nor_recovered);
    RUN_TEST(test_a_sensor_that_answers_again_takes_its_port_out_of_recovery);
    RUN_TEST(test_a_port_a_step_left_dark_is_not_read_until_it_has_come_up);
    RUN_TEST(test_a_port_given_its_power_back_is_read_as_soon_as_it_has_come_up);
    RUN_TEST(test_work_about_a_held_port_leaves_its_sensor_to_the_next_sweep);
    RUN_TEST(test_a_silence_that_began_before_a_window_is_not_finished_by_it);
    RUN_TEST(test_a_sensor_silent_with_its_port_up_is_power_cycled_across_a_window);
    RUN_TEST(test_a_retried_power_cycle_counts_only_polling_a_port_that_could_answer);
    RUN_TEST(test_a_port_the_recovery_switched_off_is_switched_on_by_it);
    RUN_TEST(test_a_stretch_runs_from_the_pin_going_down_to_the_wake_up_after_it_comes_back);
    RUN_TEST(test_two_darks_between_two_visits_are_one_stretch_from_the_first);
    RUN_TEST(test_the_recoverys_own_power_cycle_is_no_stretch);
    RUN_TEST(test_a_port_nothing_can_switch_on_is_read_as_it_is);

    printf("\n----- Sensor Acquisition Tests Complete -----\n");
}

// --------------------------------------------------------------------------------------------
// A note about the shape of the cache, found while wiring these tests and left alone.
//
// `SensorEntity::isMultiChannel()` is true for any sensor that has register
// configs, so every Modbus sensor - including a one-register one - is stored channel-wise:
// `SensorValueModel::setChannelValue(sensorUid, channelUid, value)` puts the value in the
// SensorValue of the *sensor*, in a list keyed by channel UID.
//
// `EvaluateRulesUseCase::getSensorValue(uid)` reads
// `ISensorValueModel::getSensorValue(uid)`, which looks `uid` up in the top level of the map. The
// only writers of that top level are `SensorService::setSensorValue(sensor.uid, ...)` on the
// single-channel branch - unreachable for a Modbus sensor, by the line above - and
// `RemoteSensorClient`, which writes it for sensors arriving over UDP from another module.
//
// And the UID a rule carries is the *channel* UID: `SensorStructureUseCase` registers only
// `<sensorUid>.NN` with the backend, `".01"` even for a single-channel sensor, which matches the
// rule payloads in test_rule_converter.cpp. So there is no entry
// under that key at all - not an entry whose currentValue was never written.
//
// Consequence, characterised by
// test_a_rule_bound_the_way_the_backend_binds_it_reports_unreachable_on_a_healthy_bus above: on a
// healthy bus with a fresh reading in the cache, the rule reads 0.0, never gets a freshness stamp,
// and after the 300 s window is put into `sensor_unreachable` permanently - the stamp under that
// key can never appear. Automation on locally attached sensors does not work, and not quietly.
//
// This predates the acquisition task - the inline path stored values exactly the same way - and it
// belongs to the rules path, not to this one, so it is reported and recorded rather than changed
// here.
