// Scenario S1 - the control loop must keep running while the server is slow.
//
// What is under test is production code: the real LoopServiceConfigureDone driving the real
// DiagnosticService, with the send handed over to the real Outbox and made by its network task.
// The only things replaced are the outside world (a server that stalls) and time itself.
//
// What this file guards: LoopServiceConfigureDone::executeLoopImplementation() must not call
// diagnosticService->performAndSend() inline, once every thirty seconds. On the device
// that call is a synchronous network send inside the same task that runs the Arduino loop(), and
// main.cpp::loop() does nothing but call it - so for as long as the server took to answer,
// nothing else in the module ran: no sensor polling, no rule evaluation, no button handling.
//
// The test fails when the send runs inline and passes when the send goes through the
// outbox. Calling `module.diagnosticService.setOutbox(nullptr)` restores the inline shape and the
// failure - that one line is the whole difference, and it is the switch to flip if you want
// to see the red for yourself: the step limit then fails with 30 060 ms against 50 ms. (The third
// test below does exactly that, on purpose.)
//
// The assertions before the claim are ones that hold in both shapes, so that the red lands on the
// step limit rather than on a premise. The one that only holds with the outbox - that its network
// task exists - is asserted after the claim.
//
// What this file measures: the diagnostic send is not wrapped in
// a decorator of its own (the outbox carries it), and there is no
// `while (tries < 3)` loop inside RestApiService::sendDiagnosticData() - retrying is
// the dispatcher's job. So one send is one attempt, and the retry budget belongs to the
// outbox.

#include <unity.h>

#include "Application/Services/DiagnosticService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/Outbox.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"

#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimNetwork.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimNetwork;
using hostsim::VirtualClock;

namespace {

// How fast the control loop is asked to run, and the slowest step S1 accepts. 50 ms is the
// figure from the simulation design note: a control loop that never pauses longer than that
// reacts to a sensor reading or a button within one human-imperceptible step.
const uint32_t CONTROL_PERIOD_MS = 10;
const uint32_t MAX_ACCEPTED_STEP_MS = 50;

// The cost of one diagnostic send, read off the code rather than chosen for the test.
// RestApiService::sendDiagnosticData() first pings /TestConnection with a 2 s timeout, then makes
// one POST with a 30 s timeout. No `while (tries < 3)` loop wraps that POST:
// retrying belongs to the outbox dispatcher, where it is one policy instead of twelve copies.
const uint32_t PING_MS = 50;
const uint32_t ATTEMPT_TIMEOUT_MS = 30000;
const uint32_t SINGLE_SEND_MS = PING_MS + ATTEMPT_TIMEOUT_MS; // 30 050 ms

// What the outbox spends on one snapshot before giving up: four attempts and 500 ms + 2 s + 8 s
// of backoff. This is the number that has to stay under the task watchdog's 300 s.
const uint32_t WORST_CASE_REQUEST_MS =
    Outbox::DEFAULT_MAX_ATTEMPTS * SINGLE_SEND_MS + 500 + 2000 + 8000; // 130 700 ms

// The diagnostic timer in LoopServiceConfigureDone.
const uint32_t DIAGNOSTIC_INTERVAL_MS = 30000;

// Long enough for the first diagnostic send to fire at t = 30 s, run its full worst case, and
// for the loop to be observed for a while afterwards. Without the observation after the answer
// arrives, a stall would fall off the end of the recording and stay invisible.
const uint64_t OBSERVATION_WINDOW_MS = 240000;

// The interval test needs two *completed* sends to have an interval between them. With the send
// running inline and taking its full 30 050 ms, the second one comes back at
// 30 000 + 30 050 + 30 000 + 30 050 = 120 100 ms, so the window has to reach past that.
const uint64_t STREAM_OBSERVATION_WINDOW_MS = 180000;

// --- Stand-ins for the services the loop drives ---------------------------------------------
// Counters, not behaviour: their only job is to show whether the loop reached them.

class CountingDeviceService : public IDeviceService {
public:
    int loopCount = 0;
    bool init() override { return true; }
    void loop() override { loopCount++; }
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

// The REST service the diagnostic send goes through. Everything except the diagnostic call is
// the existing mock; the diagnostic call reproduces the shape of the retry loop in
// RestApiService::sendDiagnosticData() against a stalling server, and therefore takes virtual
// time the way the real one takes real time.
class StallingServerRestApiService : public MockRestAPIService {
public:
    struct Attempt {
        uint64_t startedAtMs;
        uint64_t finishedAtMs;
    };

    StallingServerRestApiService(SimNetwork& network, HostTaskScheduler& scheduler, VirtualClock& clock)
        : _network(network), _scheduler(scheduler), _clock(clock)
    {
    }

    bool sendDiagnosticData(const DiagnosticDto& diagnosticData) override
    {
        MockRestAPIService::sendDiagnosticData(diagnosticData);

        Attempt record;
        record.startedAtMs = _clock.nowMs();

        // isModuleOnline(): a short ping the stalling server answers.
        _network.setResponseLatencyMs(PING_MS);
        _network.send("TestConnection", "");

        // One POST that runs out its 30 s timeout, which is what the method does now.
        _network.setResponseLatencyMs(ATTEMPT_TIMEOUT_MS);
        _network.send("diagnostic", "");

        record.finishedAtMs = _clock.nowMs();
        attempts.push_back(record);
        return false; // the server never returned 200
    }

    std::vector<Attempt> attempts;

private:
    SimNetwork& _network;
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
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

    // The silence between the last observed tick and the end of the window counts too:
    // a loop that stops and never comes back has the worst cadence of all.
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

// Everything the module needs, wired as it is on the device. Held together in one struct because
// three tests below need the same wiring and only differ in what they measure.
struct ModuleUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;
    SimNetwork network;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    StallingServerRestApiService restApiService;
    HybridAPIService hybridApiService;
    DiagnosticService diagnosticService;

    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;

    CountingDeviceService deviceService;
    CountingSensorService sensorService;
    CountingRuleService ruleService;
    LoopServiceConfigureDone loopService;

    ModuleUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          network(clock, scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          restApiService(network, scheduler, clock),
          // REST_ONLY because this scenario has no MQTT server to stall; on the device the same
          // object is created with ApiPriority::BOTH.
          hybridApiService(&restApiService, nullptr, &moduleConfig, &logger, &debugSystem,
                           ApiPriority::REST_ONLY),
          diagnosticService(
              &restApiService,
              &moduleConfig,
              logger,
              debugSystem,
              nullptr,        // json converter - unused on this path
              &wifiAdapter,
              nullptr,        // NTP time adapter - unused on this path
              &arduino,
              &mqttManager,
              &hybridApiService),
          completions(8),
          outbox(&hybridApiService, &scheduler, &completions, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false; // keep tens of thousands of loop lines out of the log

        settings.setUser("s1@example.com");
        settings.setAccountUid("s1-account");

        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid("s1-module");
        moduleConfig.setFirmwareVersion("test");

        wifiAdapter.setConnected(true);
        mqttManager.isConnectedReturnValue = true;

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setDeviceService(&deviceService);
        loopService.setSensorService(&sensorService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
        loopService.setOutbox(&outbox);
        loopService.setDiagnosticService(&diagnosticService);
        diagnosticService.setOutbox(&outbox);
    }

    // One task, mirroring main.cpp: loop() calls loopService->loop() and nothing else.
    void startArduinoLoopTask(CadenceRecorder& cadence)
    {
        scheduler.createTask("arduino-loop", 512 * 1024, 1, [this, &cadence]() {
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
// S1
// --------------------------------------------------------------------------------------------

static void test_s1_control_loop_keeps_running_while_the_server_is_slow(void)
{
    ModuleUnderTest module;

    // The one line that decides this test. Call setOutbox(nullptr) here and the send is made
    // inline again, exactly as without the outbox; the assertion below then fails with the full
    // 30 050 ms stall.
    module.diagnosticService.setOutbox(&module.outbox);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    // --- the run has to be sound before its verdict means anything --------------------------
    // Each of these guards one way this test could pass or fail while proving nothing. They come
    // first on purpose: if any of them trips, the failure names the broken premise instead of
    // being reported as a verdict about the control loop.

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");

    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)cadence.count(), (uint32_t)module.deviceService.loopCount,
                                     "device service was not driven once per loop iteration");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)cadence.count(), (uint32_t)module.sensorService.loopCount,
                                     "sensor service was not driven once per loop iteration");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)cadence.count(), (uint32_t)module.ruleService.loopCount,
                                     "rule service was not driven once per loop iteration");

    TEST_ASSERT_TRUE_MESSAGE(module.restApiService.sendDiagnosticDataCalled,
                             "the diagnostic send never happened - nothing slow was exercised");
    TEST_ASSERT_TRUE_MESSAGE(module.restApiService.attempts.size() >= 1,
                             "no diagnostic send ran to completion");

    const uint64_t sendDurationMs = module.restApiService.attempts[0].finishedAtMs -
                                    module.restApiService.attempts[0].startedAtMs;
    char durationMessage[192];
    snprintf(durationMessage, sizeof(durationMessage),
             "the simulated send took %lu ms, not the %lu ms one attempt costs in the firmware",
             (unsigned long)sendDurationMs, (unsigned long)SINGLE_SEND_MS);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SINGLE_SEND_MS, (uint32_t)sendDurationMs, durationMessage);

    // --- the actual claim of S1 --------------------------------------------------------------
    char message[256];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms) "
             "while a diagnostic request worth up to %lu ms was in flight",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS,
             (unsigned long)WORST_CASE_REQUEST_MS);

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);

    // --- corroboration, only meaningful once the claim above holds ---------------------------
    // This one is deliberately *after* the claim: it is the mechanism, not a premise. Asserted
    // before it, the inline wiring would fail here instead of on the step limit, and whoever
    // flipped the switch to see the red would be told the harness is broken.
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.taskStarted(),
                             "the step limit was met but the outbox network task was never created");
}

// --------------------------------------------------------------------------------------------
// The diagnostic timer must count from the end of a send, not from its start.
//
// This one deliberately keeps the *synchronous* wiring - the loop calls DiagnosticService
// directly - because that is where the fault lives and where the guard has to hold. Were
// LoopServiceConfigureDone to stamp lastDiagnosticTime with the time read before the send,
// a send that outlasted the 30 s interval would make the condition true again on the very next
// iteration, and diagnostics would go out back to back with the loop parked inside them. Ten
// milliseconds of control per ninety seconds of network.
// --------------------------------------------------------------------------------------------

static void test_diagnostics_do_not_turn_into_a_continuous_stream_of_sends(void)
{
    ModuleUnderTest module;
    module.diagnosticService.setOutbox(nullptr); // inline, on purpose

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.scheduler.runForMs(STREAM_OBSERVATION_WINDOW_MS);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.restApiService.attempts.size() >= 2,
                             "fewer than two sends completed - there is no interval to measure");

    for (size_t i = 1; i < module.restApiService.attempts.size(); ++i)
    {
        const uint64_t previousEndMs = module.restApiService.attempts[i - 1].finishedAtMs;
        const uint64_t nextStartMs = module.restApiService.attempts[i].startedAtMs;
        const uint64_t idleMs = nextStartMs - previousEndMs;

        char message[224];
        snprintf(message, sizeof(message),
                 "send %u started %lu ms after send %u came back (interval is %lu ms): "
                 "the diagnostic timer is counting from the start of the send, not its end",
                 (unsigned)i, (unsigned long)idleMs, (unsigned)(i - 1),
                 (unsigned long)DIAGNOSTIC_INTERVAL_MS);

        TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(DIAGNOSTIC_INTERVAL_MS,
                                                  (unsigned int)idleMs,
                                                  message);
    }
}

// --------------------------------------------------------------------------------------------
// A statement about the harness rather than about the firmware: the same clock, the same
// scheduler and the same stalling server, with the send handed to a second task through a queue.
// It is here so that a failure of S1 above can never be blamed on an assertion that nothing
// could satisfy.
// --------------------------------------------------------------------------------------------

static void test_s1_limit_is_reachable_when_the_send_is_off_the_control_loop(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    SimNetwork network(clock, scheduler);
    network.setResponseLatencyMs(WORST_CASE_REQUEST_MS);

    HostMessageQueue<int> sendQueue(4);
    CadenceRecorder cadence;
    uint64_t lastDiagnosticAtMs = 0;
    int enqueued = 0;

    scheduler.createTask("arduino-loop", 256 * 1024, 1, [&]() {
        while (true)
        {
            const uint64_t nowMs = clock.nowMs();
            cadence.tick(nowMs);

            // The same 30 s diagnostic cadence, but the loop only hands the job over.
            if (nowMs - lastDiagnosticAtMs >= DIAGNOSTIC_INTERVAL_MS)
            {
                lastDiagnosticAtMs = nowMs;
                if (nowMs > 0)
                {
                    sendQueue.push(++enqueued);
                }
            }

            scheduler.delayMs(CONTROL_PERIOD_MS);
        }
    });

    scheduler.createTask("network", 256 * 1024, 1, [&]() {
        while (true)
        {
            int job = 0;
            if (sendQueue.pop(job))
            {
                network.send("diagnostic", "");
            }
            else
            {
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        }
    });

    scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(clock.nowMs());

    TEST_ASSERT_TRUE_MESSAGE(network.sendCount() >= 1, "the slow send did not happen at all");

    char message[192];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms with the send moved off the loop",
             (unsigned long)cadence.worstGapMs());

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);
}

extern "C" void run_s1_control_loop_cadence_tests(void)
{
    printf("\n----- Running S1 Control Loop Cadence Tests -----\n");

    RUN_TEST(test_s1_limit_is_reachable_when_the_send_is_off_the_control_loop);
    RUN_TEST(test_s1_control_loop_keeps_running_while_the_server_is_slow);
    RUN_TEST(test_diagnostics_do_not_turn_into_a_continuous_stream_of_sends);

    printf("\n----- S1 Control Loop Cadence Tests Complete -----\n");
}
