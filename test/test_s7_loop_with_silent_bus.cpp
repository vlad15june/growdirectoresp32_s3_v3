// Scenario S7 - the control loop must keep running while the RS-485 line does not answer.
//
// The shape under test: the loop reads a cache while a task of its own waits for the hardware.
//
// What is under test is production code. The real LoopServiceConfigureDone drives the real
// SensorService, which hands the polling to the real SensorAcquisitionService, which drives the
// real SensorReader and the real ModbusSensorReader. Replaced are only the outside world (an
// RS-485 line whose sensors do not answer), time, and the two services whose only job here is to
// show the loop reached them.
//
// What this file guards: SensorService::loop() must not call readAllSensors() inline,
// every 15 s, on the task that runs the Arduino loop(). A silent channel costs three attempts of a
// 300 ms budget with 100 ms between them - 1100 ms - and a three-channel sensor costs three of
// those. Two such sensors parked the control loop for 6.6 s at a time: no rule evaluation, no
// relay switching, no button, every fifteen seconds. A `mqttManager->loop()` call in the middle of
// that poll loop would exist for
// exactly this reason; with the poll off the control loop there is none.
//
// **The switch to flip to see the red for yourself is one line**, marked below in the first test:
// `module.sensorService.setAcquisition(nullptr)`. The polling then happens inline again and the
// step limit fails with 6 600 ms against 50 ms.
//
// The assertions before the verdict are the ones that hold in both shapes, so a red lands on the
// step limit and not on a premise. The ones that only hold with the acquisition task - that it
// exists, that it swept - come after.

#include <unity.h>

#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"

#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostBusLock;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimRs485Bus;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// The same 50 ms S1 and S6 are held to: a loop that never pauses longer than that reacts to a
// sensor reading or a button within one human-imperceptible step.
const uint32_t MAX_ACCEPTED_STEP_MS = 50;

// What one silent channel costs, read off ModbusSensorReader and SensorReader rather than chosen:
// three attempts of kInterFrameGapMs (5) + kResponseWindowMs (295), with SensorReader's 100 ms
// between attempts.
const uint32_t SILENT_CHANNEL_MS = 3 * 300 + 2 * 100; // 1 100 ms
const uint32_t CHANNELS_PER_SENSOR = 3;
const uint32_t SENSOR_COUNT = 2;
const uint32_t SILENT_SWEEP_MS = SILENT_CHANNEL_MS * CHANNELS_PER_SENSOR * SENSOR_COUNT; // 6 600 ms

// One transaction: the inter-frame gap plus the response window, plus the 100 ms a port pays once
// when its power comes up. This is the upper bound on how long a bus command issued from the
// control loop can be made to wait for the acquisition task, and the number the bus-lock bound is
// about.
const uint32_t ONE_TRANSACTION_MS = 400;

// Four sweeps at the 15 s cadence, so a stall has somewhere to show up.
const uint64_t OBSERVATION_WINDOW_MS = 60000;

const char* const LOOP_TASK_NAME = "arduino-loop";

// --- Stand-ins for the services the loop drives, as in S1 -------------------------------------

class CountingDeviceService : public IDeviceService {
public:
    int loopCount = 0;
    bool init() override { return true; }
    void loop() override { loopCount++; }
};

class CountingRuleService : public IRuleService {
public:
    int loopCount = 0;
    bool init() override { return true; }
    void loop() override { loopCount++; }
    bool forceSyncRules() override { return true; }
};

/**
 * @brief An MQTT manager that records when the loop got round to servicing it.
 *
 * `mqttManager->loop()` is no longer called from the middle of the sensor poll. The
 * claim that replaces it is not "the call is gone" but "MQTT is serviced often anyway, because the
 * loop is fast now", and that is a claim about the gap between two services.
 */
class RecordingMqttManager : public IMqttManager {
public:
    bool connect() override { return true; }
    void disconnect() override {}
    bool isConnected() override { return true; }
    bool subscribe(const char*) override { return true; }
    bool unsubscribe(const char*) override { return true; }
    bool publish(const char*, const char*) override { return true; }
    void setMessageProcessor(IMessageProcessor*) override {}
    void setupMqttTopicHandlers() override {}
    int getMqttFails() const override { return 0; }
    void resetMqttFails() override {}
    uint32_t getInboundOversizeDrops() const override { return 0; }
    const char* getClientId() const override { return "s7"; }
    void setClientId(const char*) override {}

    void loop() override
    {
        const uint64_t nowMs = _clock->nowMs();
        if (loopCount > 0 && (nowMs - lastLoopAtMs) > worstGapMs)
        {
            worstGapMs = nowMs - lastLoopAtMs;
            worstGapStartedAtMs = lastLoopAtMs;
        }
        lastLoopAtMs = nowMs;
        loopCount++;
    }

    VirtualClock* _clock = nullptr;
    uint64_t loopCount = 0;
    uint64_t lastLoopAtMs = 0;
    uint64_t worstGapMs = 0;
    uint64_t worstGapStartedAtMs = 0;
};

// Records when the control loop was seen running and how long the longest silence was.
class CadenceRecorder {
public:
    void tick(uint64_t nowMs)
    {
        if (_count > 0)
        {
            const uint64_t gap = nowMs - _lastMs;
            if (gap > _worstGapMs)
            {
                _worstGapMs = gap;
                _worstGapStartedAtMs = _lastMs;
            }
        }
        _lastMs = nowMs;
        _count++;
    }

    void closeAt(uint64_t nowMs)
    {
        if (_count > 0 && (nowMs - _lastMs) > _worstGapMs)
        {
            _worstGapMs = nowMs - _lastMs;
            _worstGapStartedAtMs = _lastMs;
        }
    }

    uint64_t count() const { return _count; }
    uint64_t worstGapMs() const { return _worstGapMs; }
    uint64_t worstGapStartedAtMs() const { return _worstGapStartedAtMs; }

private:
    uint64_t _count = 0;
    uint64_t _lastMs = 0;
    uint64_t _worstGapMs = 0;
    uint64_t _worstGapStartedAtMs = 0;
};

SensorEntity makeModbusSensor(const std::string& uid, int port, uint8_t address)
{
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.protocol = "ModBus";
    sensor.model = "S7-MULTI";
    sensor.sensorType = "temperature";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.pin = port;
    sensor.modbusDeviceAddress = address;
    sensor.baudRate = 9600;

    for (int channel = 1; channel <= static_cast<int>(CHANNELS_PER_SENSOR); channel++)
    {
        ModbusRegisterConfig config;
        config.functionCode = 0x03;
        config.startRegister = static_cast<uint16_t>(0x0100 + channel);
        config.numRegisters = 1;
        config.multiplier = 0.1f;
        config.offset = 0.0f;
        config.channel = channel;
        config.channelName = "ch" + std::to_string(channel);
        config.sensorType = "temperature";
        sensor.modbusRegisterConfigs.push_back(config);
    }

    return sensor;
}

/**
 * @brief The module, wired the way globals.h wires it, with an RS-485 line that does not answer.
 */
struct ModuleUnderTest {
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
    RawBusAccessAdapter rawBusAccess;
    SensorReader sensorReader;

    SensorAcquisitionService acquisition;
    SensorService sensorService;

    CountingDeviceService deviceService;
    CountingRuleService ruleService;
    RecordingMqttManager mqttManager;
    LoopServiceConfigureDone loopService;

    ModuleUnderTest()
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
          rawBusAccess(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          sensorService(&moduleConfig,
                        &moduleStructure,
                        &sensorRepository,
                        &sensorReader,
                        &sensorValueModel,
                        nullptr,      // NTP time adapter - the update packet is not this scenario
                        &arduino,
                        nullptr,      // hybrid API service - nothing is sent here
                        &logger,
                        &debugSystem)
    {
        debugSystem.featureEnabled = false; // keep tens of thousands of poll lines out of the log

        mqttManager._clock = &clock;

        // Two sensors on two different ports, both present and both silent: three attempts on
        // every channel, no answer to any of them.
        SimRs485Bus::Device silent;
        silent.address = 1;
        silent.silent = true;
        port0.addDevice(silent);
        port1.addDevice(silent);

        sensorRepository.sensors.push_back(makeModbusSensor("mod.0", 0, 1));
        sensorRepository.sensors.push_back(makeModbusSensor("mod.1", 1, 1));
        sensorRepository.modbusSensors = sensorRepository.sensors;

        modbusSensorReader.setBusLock(&busLock);
        rawBusAccess.setBusLock(&busLock);
        // The line as a booted module has it: the boot begins the UARTs before the first frame
        // (ModuleSetupService::setupSensorBus()), and a line nobody began carries nothing.
        sensorReader.init();

        sensorService.setAcquisition(&acquisition);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setDeviceService(&deviceService);
        loopService.setSensorService(&sensorService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
    }

    // One task, mirroring main.cpp: loop() calls loopService->loop() and nothing else.
    void startArduinoLoopTask(CadenceRecorder& cadence)
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this, &cadence]() {
            while (true)
            {
                cadence.tick(clock.nowMs());
                loopService.loop();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// S7 - the claim under test
// --------------------------------------------------------------------------------------------

static void test_s7_the_loop_stays_bounded_while_the_bus_is_silent_and_the_loop_has_work(void)
{
    ModuleUnderTest module;

    // The one line that decides this test. Replace it with setAcquisition(nullptr) and the polling
    // goes back onto the control loop, exactly as without the acquisition task; the step limit below then
    // fails with the full silent sweep.
    module.sensorService.setAcquisition(&module.acquisition);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    // --- the run has to be sound before its verdict means anything --------------------------
    // These hold in both shapes: with the polling inline and with it on its own task.

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_FALSE_MESSAGE(module.busLock.deadlockDetected(),
                              "a task waited for the bus lock in a way that could never be served");

    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)cadence.count(), (uint32_t)module.deviceService.loopCount,
                                     "device service was not driven once per loop iteration");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)cadence.count(), (uint32_t)module.ruleService.loopCount,
                                     "rule service was not driven once per loop iteration");

    // Somebody really did spend the time on the line: without this the test would pass on a module
    // that simply never polled.
    TEST_ASSERT_TRUE_MESSAGE(module.port0.sentFrames.size() >= CHANNELS_PER_SENSOR,
                             "no request ever went out on port 0 - nothing slow was exercised");
    TEST_ASSERT_TRUE_MESSAGE(module.port1.sentFrames.size() >= CHANNELS_PER_SENSOR,
                             "no request ever went out on port 1 - the sweep never reached it");

    // --- the actual claim of S7 ---------------------------------------------------------------
    char message[256];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms) "
             "while a sweep of two silent sensors worth %lu ms was in progress",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS,
             (unsigned long)SILENT_SWEEP_MS);

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);

    // --- corroboration, only meaningful once the claim above holds ---------------------------
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.taskStarted(),
                             "the step limit was met but the acquisition task was never created");
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.sweepCount() >= 1,
                             "the acquisition task exists but never finished a sweep");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.acquisition.droppedReadingCount(),
                                     "readings were produced faster than the loop applied them");

    // The acquisition task is bounded work - one sensor is at most three attempts of a 300 ms
    // budget per channel - so it can be put under the task watchdog, as the outbox network task
    // already is. Without this the module would have a task that can hang the RS-485 line
    // forever and nothing watching it.
    bool subscribed = false;
    const std::vector<std::string>& names = module.scheduler.watchdogSubscribedTaskNames();
    for (size_t i = 0; i < names.size(); i++)
    {
        if (names[i] == SensorAcquisitionService::TASK_NAME)
        {
            subscribed = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(subscribed,
                             "the acquisition task did not put itself under the task watchdog");
}

// --------------------------------------------------------------------------------------------
// MQTT stays responsive without the prop inside the sensor poll.
// --------------------------------------------------------------------------------------------

static void test_mqtt_is_serviced_without_the_call_inside_the_sensor_poll(void)
{
    ModuleUnderTest module;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.mqttManager.loopCount > 100,
                             "MQTT was hardly serviced at all - the loop is not running");

    // One service per loop iteration, and nothing else: the poll no longer calls it.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)cadence.count(), (uint32_t)module.mqttManager.loopCount,
                                     "MQTT was serviced a different number of times than the loop ran");

    char message[224];
    snprintf(message, sizeof(message),
             "the longest MQTT was left unserviced was %lu ms starting at t=%lu ms, "
             "with no mqttManager->loop() inside the sensor poll to shorten it",
             (unsigned long)module.mqttManager.worstGapMs,
             (unsigned long)module.mqttManager.worstGapStartedAtMs);

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)module.mqttManager.worstGapMs,
                                           message);
}

// --------------------------------------------------------------------------------------------
// What the control loop can be made to wait for on the bus lock.
//
// The tempting premise - "with polling on its own task the control loop does not touch the bus at
// all, so it never waits on the lock" - is not true.
// MqttManager::loop() dispatches an incoming message on the task that called it
// (MqttManager::loop() -> processMessage() -> CommandTopicHandler -> CommandService::process()), so a ModbusRead,
// ModbusWrite, SerialWrite or a scanSensor triggered from the app runs *on the control loop* and
// does take the line.
//
// So the guarantee is not "never waits". It is "waits for one transaction". That is a property of
// where the lock is taken: ModbusSensorReader takes it per public method, and the retry loop that
// makes three attempts of a channel sits above it, in SensorReader - so the line is free again
// between two attempts and a command never waits for a sweep, a sensor or even a channel.
// --------------------------------------------------------------------------------------------

static void test_a_bus_command_from_the_loop_waits_at_most_one_transaction(void)
{
    ModuleUnderTest module;

    CadenceRecorder cadence;
    uint64_t commandsIssued = 0;

    // The control loop, doing what it does when a ModbusRead command arrives over MQTT: a raw bus
    // request, on the control loop's own task, straight into the same lock the acquisition task is
    // using. Issued every 100 ms so that it lands inside a transaction rather than between two.
    module.scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [&]() {
        uint64_t lastCommandAtMs = 0;
        while (true)
        {
            cadence.tick(module.clock.nowMs());
            module.loopService.loop();

            if (module.clock.nowMs() - lastCommandAtMs >= 100)
            {
                lastCommandAtMs = module.clock.nowMs();
                const uint8_t pdu[] = {0x01, 0x03, 0x01, 0x01, 0x00, 0x01};
                std::vector<uint8_t> answer;
                module.rawBusAccess.modbusRequest(0, pdu, sizeof(pdu), true, 50, answer);
                commandsIssued++;
            }

            module.scheduler.delayMs(CONTROL_PERIOD_MS);
        }
    });

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_FALSE_MESSAGE(module.busLock.deadlockDetected(),
                              "a task waited for the bus lock in a way that could never be served");

    TEST_ASSERT_TRUE_MESSAGE(commandsIssued > 100, "the control loop issued almost no bus commands");
    TEST_ASSERT_TRUE_MESSAGE(module.busLock.takesBy(SensorAcquisitionService::TASK_NAME) > 0,
                             "the acquisition task never took the line - there was nothing to contend with");

    // The collision has to have happened, or the number below is about nothing.
    char collisionMessage[192];
    snprintf(collisionMessage, sizeof(collisionMessage),
             "the control loop never once found the line busy in %lu commands: "
             "this test proves nothing about waiting",
             (unsigned long)commandsIssued);
    TEST_ASSERT_TRUE_MESSAGE(module.busLock.waitsBy(LOOP_TASK_NAME) > 0, collisionMessage);

    // --- the claim of the bus-lock bound ------------------------------------------------------
    // The worst single wait, not the total: the total grows with how long the observation ran, the
    // worst single wait is the property of the design. A command arriving mid-sweep waits for the
    // tail of the transaction in flight and nothing more, because the lock is released between
    // attempts and between channels.
    const uint64_t worstWaitMs = module.busLock.worstWaitMsBy(LOOP_TASK_NAME);

    char message[256];
    snprintf(message, sizeof(message),
             "the longest a bus command from the control loop waited for the line was %lu ms "
             "across %lu waits (limit %lu ms = one transaction); %lu ms in total over the run",
             (unsigned long)worstWaitMs,
             (unsigned long)module.busLock.waitsBy(LOOP_TASK_NAME),
             (unsigned long)ONE_TRANSACTION_MS,
             (unsigned long)module.busLock.waitedMsBy(LOOP_TASK_NAME));

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(ONE_TRANSACTION_MS,
                                           (unsigned int)worstWaitMs,
                                           message);
}

extern "C" void run_s7_loop_with_silent_bus_tests(void)
{
    printf("\n----- Running S7 Silent Bus Control Loop Tests -----\n");

    RUN_TEST(test_s7_the_loop_stays_bounded_while_the_bus_is_silent_and_the_loop_has_work);
    RUN_TEST(test_mqtt_is_serviced_without_the_call_inside_the_sensor_poll);
    RUN_TEST(test_a_bus_command_from_the_loop_waits_at_most_one_transaction);

    printf("\n----- S7 Silent Bus Control Loop Tests Complete -----\n");
}
