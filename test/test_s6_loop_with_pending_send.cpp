// Scenario S6 - the control loop stays bounded while the network hangs AND the loop itself has
// something to send.
//
// This is the combination S1 does not cover, and the gap is structural rather than accidental. In
// S1 the only network work is one diagnostic snapshot every thirty seconds; between snapshots the
// loop wants nothing from the network at all, so a queue in front of the network is never asked
// to hold anything while a send is in flight. The interesting case is the other one: the server
// has stopped answering, a send is stuck inside its 30 s timeout, and the control loop keeps
// producing - device states changing, sensor readings due, diagnostics due. Every one of those
// would be a synchronous REST call made from the loop without the outbox, so this is precisely the state in which
// a module stops being a controller.
//
// What is under test is production code: the real LoopServiceConfigureDone driving the real
// DeviceService and the real DiagnosticService, both submitting to the real Outbox. Replaced: the
// server (it hangs) and time.
//
// To see the red, change two lines in ModuleUnderTest below and nothing else:
//
//     deviceService(... , &server, &logger, &debugSystem)   // was: nullptr for the API
//     diagnosticService.setOutbox(nullptr);
//     deviceService.setOutbox(nullptr);
//
// That is the shape without the outbox - the loop's own producers send inline - and it measures a worst
// control-loop step of 60 010 ms against a 50 ms limit: two thirty-second sends back to back in
// one iteration, the device update and the diagnostic snapshot.
//
// The guards before the claim are deliberately ones that hold in BOTH shapes, so that the red
// lands on the claim and not on a premise. The two that only hold with the outbox - the loop runs
// thousands of times, and the outbox collapsed superseded state - are asserted after the claim,
// because they are consequences of it rather than preconditions for measuring it.

#include <unity.h>

#include "Application/Services/DiagnosticService.h"
#include "Application/Services/Devices/DeviceService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/Outbox.h"

#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

// The same figures S1 uses: a control loop that never pauses longer than 50 ms reacts to a sensor
// reading or a button within one human-imperceptible step.
const uint32_t CONTROL_PERIOD_MS = 10;
const uint32_t MAX_ACCEPTED_STEP_MS = 50;

// One attempt against a server that has stopped answering: RestApiService sets a 30 s timeout and
// makes one call. The retry loop lives in the outbox dispatcher.
const uint32_t ATTEMPT_TIMEOUT_MS = 30000;

// Four minutes: long enough for several diagnostic intervals, several device-update periods, and
// several full retry budgets of a hanging server.
const uint64_t OBSERVATION_WINDOW_MS = 240000;

class HangingServer : public MockHybridAPIService {
public:
    explicit HangingServer(HostTaskScheduler& scheduler) : _scheduler(scheduler) {}

    bool sendModuleUpdateData(const UpdateDto&, ApiPriority = ApiPriority::UNDEFINED) override
    {
        return hang(updateCalls);
    }

    bool sendDiagnosticData(const DiagnosticDto&, ApiPriority = ApiPriority::UNDEFINED) override
    {
        return hang(diagnosticCalls);
    }

    uint32_t updateCalls = 0;
    uint32_t diagnosticCalls = 0;
    /** Virtual milliseconds this stand-in spent inside a call, in total. */
    uint64_t timeInsideCallsMs = 0;

private:
    bool hang(uint32_t& counter)
    {
        counter++;
        _scheduler.delayMs(ATTEMPT_TIMEOUT_MS);
        timeInsideCallsMs += ATTEMPT_TIMEOUT_MS;
        return false;
    }

    HostTaskScheduler& _scheduler;
};

class CountingSensorService : public ISensorService {
public:
    int loopCount = 0;
    bool init() override { return true; }
    void loop() override { loopCount++; }
    bool readAllSensors() override { return true; }
    bool readSensor(SensorEntity) override { return true; }
};

class CountingRuleService : public IRuleService {
public:
    int loopCount = 0;
    bool init() override { return true; }
    void loop() override { loopCount++; }
    bool forceSyncRules() override { return true; }
};

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

struct ModuleUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    MockRestAPIService restApiService;
    HangingServer server;

    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;

    MockDeviceModel deviceModel;
    DeviceDigitalOut device;

    DiagnosticService diagnosticService;
    DeviceService deviceService;
    CountingSensorService sensorService;
    CountingRuleService ruleService;
    LoopServiceConfigureDone loopService;

    ModuleUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          server(scheduler),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          diagnosticService(
              &restApiService,
              &moduleConfig,
              logger,
              debugSystem,
              nullptr,        // json converter - the send never gets that far here
              &wifiAdapter,
              nullptr,        // NTP time adapter - not part of this scenario
              &arduino,
              &mqttManager,
              &server),
          deviceService(&moduleConfig, nullptr, &deviceModel, &timeAdapter, &arduino, nullptr,
                        nullptr, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false; // keep tens of thousands of loop lines out of the log

        settings.setUser("s6@example.com");
        settings.setAccountUid("s6-account");
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid("s6-module");
        moduleConfig.setFirmwareVersion("test");

        wifiAdapter.setConnected(true);
        mqttManager.isConnectedReturnValue = true;

        device.deviceUid = "device-1";
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = DEVICE_STATE_OFF;
        device.isAuto = true;
        device.isManual = false;
        device.workByRule = false;
        device.workManual = false;
        device.needToSwitchBack = false;
        device.needReportingRuleUid = false;
        device.commandId = 0;
        device.switchedAt = "2026-08-15T10:00:00Z";
        device.ruleUid = nullptr;
        device.isRuleTurnOnDevice = false;
        deviceModel.addDevice(&device);

        diagnosticService.setOutbox(&outbox);
        deviceService.setOutbox(&outbox);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setDiagnosticService(&diagnosticService);
        loopService.setDeviceService(&deviceService);
        loopService.setSensorService(&sensorService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
    }

    // One task, mirroring main.cpp: loop() calls loopService->loop() and nothing else. The one
    // addition is that the device state keeps changing, which is what gives the control loop
    // something of its own to send on every single iteration.
    void startArduinoLoopTask(CadenceRecorder& cadence)
    {
        scheduler.createTask("arduino-loop", 512 * 1024, 1, [this, &cadence]() {
            while (true)
            {
                cadence.tick(clock.nowMs());
                device.commandId++;
                loopService.loop();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }
};

} // namespace

static void test_s6_the_loop_stays_bounded_while_the_network_hangs_and_the_loop_has_work(void)
{
    ModuleUnderTest module;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    // --- the run has to be sound before its verdict means anything --------------------------
    // Every guard here is chosen to hold in BOTH shapes - with the outbox and without it - so
    // that switching the wiring to the form without the outbox (see the header) produces a red on the
    // claim below and not on a premise. A guard that only holds with the outbox belongs after the
    // claim, not before it; two such guards are at the end of this test for that reason.

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");

    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 1,
                             "the control loop never ran twice - the harness, not the firmware, is "
                             "at fault");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)cadence.count(), (uint32_t)module.sensorService.loopCount,
                                     "sensor service was not driven once per loop iteration");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)cadence.count(), (uint32_t)module.ruleService.loopCount,
                                     "rule service was not driven once per loop iteration");

    // The loop really did have work of its own, and it really did reach the network. This is the
    // half S1 does not have: there, the loop wants nothing from the network between snapshots.
    TEST_ASSERT_TRUE_MESSAGE(module.server.updateCalls >= 2,
                             "the loop's own device update never reached the network - this is the "
                             "S1 scenario again, not the one this test exists for");
    TEST_ASSERT_TRUE_MESSAGE(module.server.diagnosticCalls >= 1,
                             "the diagnostic send never reached the network");

    // And the network really was stuck: every call cost its full timeout.
    char busyMessage[192];
    snprintf(busyMessage, sizeof(busyMessage),
             "the server stand-in was only inside a call for %lu of %lu ms - the network was not "
             "meaningfully busy, so the scenario was not reproduced",
             (unsigned long)module.server.timeInsideCallsMs, (unsigned long)OBSERVATION_WINDOW_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.server.timeInsideCallsMs >= OBSERVATION_WINDOW_MS / 2, busyMessage);

    // --- the actual claim of S6 --------------------------------------------------------------
    char message[288];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms), with the "
             "server hanging %lu ms per call and %lu of the loop's own requests superseded while "
             "it waited",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS,
             (unsigned long)ATTEMPT_TIMEOUT_MS,
             (unsigned long)module.outbox.collapsedCount());

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);

    // --- corroboration, only meaningful once the claim above holds ---------------------------
    // A loop bounded to 10 ms steps runs thousands of times in four minutes, and a superseded
    // state request can only exist if the loop kept producing while the network was stuck. Both
    // are consequences of the claim, so they are stated after it.
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the loop met the step limit but ran too few times for that to mean "
                             "anything");
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.collapsedCount() > 100,
                             "nothing of the loop's own was ever superseded, so it was not really "
                             "producing while the network hung");
}

// The other half of the promise: while the network is unreachable the module keeps measuring and
// keeps its accounting, rather than silently losing the fact that anything was lost.
static void test_losses_during_the_outage_are_counted_rather_than_silent(void)
{
    ModuleUnderTest module;

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_TRUE_MESSAGE(module.outbox.undeliveredCount() > 0,
                             "nothing was reported undelivered although the server answered nothing");
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.collapsedCount() > 0,
                             "state was superseded during the outage and not counted");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.outbox.deliveredCount(),
                                     "something was reported delivered by a server that answered nothing");

    // Every outcome the network task produced reached the control loop. The completion queue is
    // sized for the maximum that can be waiting (four state slots plus the eight-deep event ring);
    // a refusal here would mean a producer never hearing about its request.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.outbox.lostCompletionCount(),
                                     "the completion queue dropped an outcome - some producer is "
                                     "waiting for a ticket that will never come back");
}

extern "C" void run_s6_loop_with_pending_send_tests(void)
{
    printf("\n----- Running S6 Loop With Pending Send Tests -----\n");

    RUN_TEST(test_s6_the_loop_stays_bounded_while_the_network_hangs_and_the_loop_has_work);
    RUN_TEST(test_losses_during_the_outage_are_counted_rather_than_silent);

    printf("\n----- S6 Loop With Pending Send Tests Complete -----\n");
}
