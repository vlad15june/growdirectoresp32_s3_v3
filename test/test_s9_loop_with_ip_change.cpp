// Scenario S9 - the control loop stays bounded while the local IP changes and the server answers
// nothing at all.
//
// This is the last place in the firmware where the control loop went to the network and waited, and
// it was by far the most expensive one. OnChangeLocalIpUseCase::perform() is called from every
// iteration; when the address had changed it did the whole exchange inline:
//
//   - sendToServerModuleStructure(..., BOTH) with its own `while (tries < 3)` of 30 s POSTs,
//   - then SyncSensorTypesUseCase::forceSync(), which went straight to getSensorTypesList() and its
//     own three 30 s GETs with a 200 ms pause between them.
//
// Around 180 s of one loop iteration with everything timing out, and up to ~240 s when the catalog
// answered late enough to trigger a structure republish - against esp_task_wdt_init(300, true) fed
// from loop() itself. A margin of 1.25-1.66x, and the only one in the firmware that was near.
//
// What is under test is production code: the real LoopServiceConfigureDone driving the real
// OnChangeLocalIpUseCase and the real SyncSensorTypesUseCase, both submitting to the real Outbox and
// both learning the outcome from the completion queue the loop drains. Replaced: the server (it
// hangs), the WiFi adapter, storage, and time.
//
// To see the red, take the outbox out of the path: in ModuleUnderTest below, drop the two lines
// that put the two use cases behind it, so each of them calls the API on the control loop:
//
//     onChangeLocalIp.setOutbox(&outbox);
//     syncSensorTypes.setOutbox(&outbox);
//
// and give OnChangeLocalIpUseCase back the `server` argument its old constructor took. That is the
// shape without the outbox, and it measures the worst control-loop step this test exists to bound.
//
// The sensor-type half of that red is reachable from this file alone: delete
// `syncSensorTypes.setOutbox(&outbox);` and the use case falls back to its synchronous path, which
// is still production code.

#include <unity.h>

#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/Outbox.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeLocalIpUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Mock/UseCases/Rules/MockSyncRulesUseCase.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

// The same figures S1 and S6 use: a control loop that never pauses longer than 50 ms reacts to a
// sensor reading or a button within one human-imperceptible step.
const uint32_t CONTROL_PERIOD_MS = 10;
const uint32_t MAX_ACCEPTED_STEP_MS = 50;

// One attempt against a server that has stopped answering: RestApiService sets a 30 s timeout and
// makes one call. The retry budget lives in the outbox dispatcher.
const uint32_t ATTEMPT_TIMEOUT_MS = 30000;

// Fifteen minutes, and the number is measured rather than chosen. One outbox job against a silent
// server costs four attempts plus 10.5 s of backoff, and the structure job alternates with the
// catalog job, so how many rounds of each fit in the window decides whether the retry claim below
// can be seen at all.
//
// test_s9_an_unreported_address_keeps_being_offered_... makes FOUR assertions, not two -
// submits >= 2 and POSTs >= 8 for the structure, fetches >= 2 and GETs >= 8 for the catalog - and
// the catalog is the slower of the two, so it is what sets the boundary. Sweeping the window
// against all four (rebuild forced at every step; without that PlatformIO reuses the previous
// binary and the sweep lies):
//
//     window   submits  POSTs  fetches  GETs
//     120 s       1       4       1       0
//     180 s       2       4       1       2
//     240 s       2       4       1       4
//     300 s       2       6       1       4
//     360 s       2       7       1       4
//     420 s       3       8       2       5     <- three of four; GETs still short, test is RED
//     450 s       3       8       2       6
//     480 s       3       8       2       7
//     510 s       3       8       2       8     <- the boundary: all four first hold
//     540 s       3       9       2       8
//     600 s       3      11       2       8
//     750 s       4      12       3      11
//     900 s       4      16       3      12     <- chosen
//
// Verified rather than inferred: at 420 s, with the assertions unchanged, the test fails with
// "the catalog sync handed the outbox 2 fetches and the server saw 5 GETs in 420000 ms of silence".
//
// Chosen 900 s. Window margin over the boundary is 900/510 = 1.76x. Margin on the counts
// themselves is 2x on the structure (4 vs 2, 16 vs 8) and 1.5x on the catalog (3 vs 2, 12 vs 8) -
// not "twice each threshold". Detection is not
// borderline, but shortening this window is how it would be lost, and the number that would lose
// it is 510 s, not 420 s.
const uint64_t OBSERVATION_WINDOW_MS = 900000;

const char* const NEW_IP = "192.168.1.77";

class SilentServer : public MockHybridAPIService {
public:
    explicit SilentServer(HostTaskScheduler& scheduler) : _scheduler(scheduler) {}

    bool sendToServerModuleStructure(const ModuleStructure&, ApiPriority = ApiPriority::UNDEFINED) override
    {
        structureCalls++;
        if (_structureAccepted)
        {
            return true;
        }
        hang();
        return false;
    }

    /** @brief The link comes back: structures are answered from here on, without a wait. */
    void setStructureAcceptedFromNowOn() { _structureAccepted = true; }

    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        sensorTypeCalls++;
        hang();
        if (httpFailed != nullptr)
        {
            *httpFailed = true;
        }
        return std::vector<SensorType>();
    }

    uint32_t structureCalls = 0;
    uint32_t sensorTypeCalls = 0;
    /** Virtual milliseconds this stand-in spent inside a call, in total. */
    uint64_t timeInsideCallsMs = 0;

private:
    void hang()
    {
        _scheduler.delayMs(ATTEMPT_TIMEOUT_MS);
        timeInsideCallsMs += ATTEMPT_TIMEOUT_MS;
    }

    HostTaskScheduler& _scheduler;
    bool _structureAccepted = false;
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
    MockWiFiManager wifiManager;
    MockMqttManager mqttManager;
    MockSensorTypeRepository sensorTypeRepository;
    SilentServer server;

    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;

    ModuleStructure moduleStructure;
    // Rules are off the loop and their forceSync() only raises a flag; the
    // stand-in here records that the flag was raised without dragging the whole rule stack in.
    MockSyncRulesUseCase syncRules;
    SyncSensorTypesUseCase syncSensorTypes;
    OnChangeLocalIpUseCase onChangeLocalIp;
    LoopServiceConfigureDone loopService;

    explicit ModuleUnderTest(uint8_t wasInitialSyncCompleted)
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          server(scheduler),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          syncSensorTypes(&server, &sensorTypeRepository, &arduino, &timeAdapter, nullptr, &logger,
                          &debugSystem),
          onChangeLocalIp(wifiAdapter, wifiManager, moduleStructure, moduleConfig, syncRules,
                          syncSensorTypes, logger, debugSystem)
    {
        debugSystem.featureEnabled = false; // keep tens of thousands of loop lines out of the log

        settings.setUser("s9@example.com");
        settings.setAccountUid("s9-account");
        settings.setWasInitialSyncCompleted(wasInitialSyncCompleted);
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid("s9-module");
        moduleConfig.setFirmwareVersion("test");
        moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);

        Node node;
        node.type = "esp32";
        node.subtype = "relay";
        node.uid = "s9-node";
        node.firmware = "test";
        node.ip = "0.0.0.0";
        node.ssid = "";
        moduleStructure.nodes.push_back(node);

        wifiAdapter.setConnected(true);
        wifiAdapter.setLocalIP(NEW_IP);
        mqttManager.isConnectedReturnValue = true;

        syncSensorTypes.init();

        onChangeLocalIp.setOutbox(&outbox);
        onChangeLocalIp.setClock(&arduino);
        syncSensorTypes.setOutbox(&outbox);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setOnChangeLocalIpUseCase(&onChangeLocalIp);
        loopService.setSyncSensorTypesUseCase(&syncSensorTypes);
        loopService.setMqttManager(&mqttManager);
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

// --- the claim of S9 ---------------------------------------------------------------------------

static void test_s9_the_loop_stays_bounded_while_the_ip_changes_and_the_server_is_silent(void)
{
    ModuleUnderTest module(1); // a module the server already knows

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);
    cadence.closeAt(module.clock.nowMs());

    // --- the run has to be sound before its verdict means anything --------------------------
    // Every guard here holds in both shapes - with the outbox and without it - so that the
    // wiring without the outbox produces a red on the claim below and not on a premise.

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");

    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 1,
                             "the control loop never ran twice - the harness, not the firmware, is "
                             "at fault");

    // The address really did change and the module really did try to report it.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(NEW_IP, module.moduleStructure.nodes[0].ip.c_str(),
                                     "the new address never reached the module structure, so the "
                                     "scenario was not reproduced");
    TEST_ASSERT_TRUE_MESSAGE(module.server.structureCalls >= 2,
                             "the structure never reached the network - nothing was measured");
    TEST_ASSERT_TRUE_MESSAGE(module.server.sensorTypeCalls >= 2,
                             "the sensor type catalog was never fetched - half of the cost "
                             "under test was not exercised");

    // And the network really was stuck: every call cost its full timeout.
    char busyMessage[208];
    snprintf(busyMessage, sizeof(busyMessage),
             "the server stand-in was only inside a call for %lu of %lu ms - the network was not "
             "meaningfully busy, so the scenario was not reproduced",
             (unsigned long)module.server.timeInsideCallsMs, (unsigned long)OBSERVATION_WINDOW_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.server.timeInsideCallsMs >= OBSERVATION_WINDOW_MS / 2, busyMessage);

    // --- the actual claim --------------------------------------------------------------------
    char message[320];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms), with the "
             "server hanging %lu ms per call, %u structure POSTs and %u catalog GETs made while it "
             "was silent",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS,
             (unsigned long)ATTEMPT_TIMEOUT_MS,
             (unsigned)module.server.structureCalls,
             (unsigned)module.server.sensorTypeCalls);

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);

    // --- corroboration, only meaningful once the claim above holds ---------------------------
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 1000,
                             "the loop met the step limit but ran too few times for that to mean "
                             "anything");
}

// --- who restarts and who does not -----------------------------------------------------------

// A module that has been running for months keeps running on the configuration it has. Its rules,
// sensors and devices do not depend on the server knowing the current address.
static void test_s9_a_registered_module_does_not_restart_when_the_server_is_silent(void)
{
    ModuleUnderTest module(1);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_TRUE_MESSAGE(module.outbox.undeliveredCount() > 0,
                             "nothing was reported undelivered although the server answered "
                             "nothing - the outage was not reproduced");

    char message[224];
    snprintf(message, sizeof(message),
             "a registered module asked for mode %d after %u undelivered requests; it must stay in "
             "CONFIGURED_DONE (%d)",
             (int)module.moduleConfig.getRequestedModuleMode(),
             (unsigned)module.outbox.undeliveredCount(),
             (int)ModuleModeEnum::CONFIGURED_DONE);
    TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::CONFIGURED_DONE,
                              module.moduleConfig.getRequestedModuleMode(), message);

    // It kept its configuration: the catalog it had is the catalog it still has, and a failed
    // fetch was not applied as an empty one.
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.sensorTypeRepository.saveCallCount,
                                  "a catalog that never arrived was written to storage");
}

// A module the server has never seen cannot work until the structure that registers it is
// delivered. Against a silent server it keeps running and offers that structure again after its
// pause - and asks for no restart in the whole window.
static void test_s9_an_unregistered_module_keeps_offering_its_structure_and_never_restarts(void)
{
    ModuleUnderTest module(0);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::CONFIGURED_DONE,
                              module.moduleConfig.getRequestedModuleMode(),
                              "a module the server has never seen asked for a restart after its "
                              "registration structure went undelivered");

    char message[192];
    snprintf(message, sizeof(message),
             "the registration structure was handed to the outbox %u time(s) in %lu ms of silence; an "
             "undelivered one must be offered again",
             (unsigned)module.onChangeLocalIp.submitCount(), (unsigned long)OBSERVATION_WINDOW_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.onChangeLocalIp.submitCount() >= 2, message);
}

// --- the retry is real, and it is counted ----------------------------------------------------

// The address is unreported until the server confirms it, so an outage produces one structure
// offer after another rather than one offer and then silence forever.
static void test_s9_an_unreported_address_keeps_being_offered_and_arrives_when_the_link_returns(void)
{
    ModuleUnderTest module(1);

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    // Two full outbox jobs are four POSTs each; anything at or above that is more than one job,
    // which is what "the offer came back" means.
    char message[240];
    snprintf(message, sizeof(message),
             "the module made %u structure POSTs and handed the outbox %u structures in %lu ms of "
             "silence; an unreported address must keep being offered",
             (unsigned)module.server.structureCalls,
             (unsigned)module.onChangeLocalIp.submitCount(),
             (unsigned long)OBSERVATION_WINDOW_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.onChangeLocalIp.submitCount() >= 2, message);
    TEST_ASSERT_TRUE_MESSAGE(module.server.structureCalls >= 8, message);

    // The catalog half of the same claim, and the only assertion that can tell whether the loop
    // really hands SENSOR_TYPES_FETCH completions to the catalog sync: a second fetch can only be
    // asked for once the first one's completion has cleared the in-flight ticket.
    char catalogMessage[240];
    snprintf(catalogMessage, sizeof(catalogMessage),
             "the catalog sync handed the outbox %u fetches and the server saw %u GETs in %lu ms of "
             "silence; without the completion reaching the sync it would stay at one fetch forever",
             (unsigned)module.syncSensorTypes.requestCount(),
             (unsigned)module.server.sensorTypeCalls,
             (unsigned long)OBSERVATION_WINDOW_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.requestCount() >= 2, catalogMessage);
    TEST_ASSERT_TRUE_MESSAGE(module.server.sensorTypeCalls >= 8, catalogMessage);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", module.onChangeLocalIp.reportedIp(),
                                     "an address the server never confirmed was recorded as reported");

    // Nothing was lost on the way: every outcome the network task produced reached the loop.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.outbox.lostCompletionCount(),
                                     "the completion queue dropped an outcome - some producer is "
                                     "waiting for a ticket that will never come back");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.outbox.refusedEventCount(),
                                     "the event ring refused a structure - one address change must "
                                     "never put more than one structure in the ring at a time");

    // Now the link comes back. The very next offer is delivered, the address settles, and the
    // module stops offering it.
    module.server.setStructureAcceptedFromNowOn();
    module.scheduler.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(NEW_IP, module.onChangeLocalIp.reportedIp(),
                                     "the structure never arrived after the server started "
                                     "answering again");

    const uint32_t submitsWhenSettled = module.onChangeLocalIp.submitCount();
    module.scheduler.runForMs(60000);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(submitsWhenSettled, module.onChangeLocalIp.submitCount(),
                                     "the module kept offering an address the server had already "
                                     "confirmed");
}

extern "C" void run_s9_loop_with_ip_change_tests(void)
{
    printf("\n----- Running S9 Loop With IP Change Tests -----\n");

    RUN_TEST(test_s9_the_loop_stays_bounded_while_the_ip_changes_and_the_server_is_silent);
    RUN_TEST(test_s9_a_registered_module_does_not_restart_when_the_server_is_silent);
    RUN_TEST(test_s9_an_unregistered_module_keeps_offering_its_structure_and_never_restarts);
    RUN_TEST(test_s9_an_unreported_address_keeps_being_offered_and_arrives_when_the_link_returns);

    printf("\n----- S9 Loop With IP Change Tests Complete -----\n");
}
