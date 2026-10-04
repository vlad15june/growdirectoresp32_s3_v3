// The contract of the outbox itself: what it promises producers, and what it does when it cannot
// keep up.
//
// What is under test is the production Outbox driving a production HybridAPIService. Replaced:
// the server (a stand-in that can stall, fail, or answer) and time (the virtual clock).
//
// Four claims:
//
//  1. Messages are not equal. State collapses by kind - a fresher sensor snapshot *replaces* the
//     one waiting, it does not queue behind it - while an event is never dropped in silence: the
//     submission is refused on the spot so the producer keeps its state and can try again.
//  2. Handing a message over is not delivering it. A completion says delivered only if the server
//     took it, which is what lets DeviceService keep a device's reason for switching on.
//  3. Retries back off instead of pausing a flat 200 ms, and there is one policy rather than one
//     per endpoint.
//  4. The network task is under the task watchdog. Nothing in this firmware that touches the
//     network was, because nothing that touches the network had a bounded worst case.
//
// Plus one guard: a task the RTOS refused to
// create must be reported and retried, not assumed.

#include <unity.h>

#include "Application/Services/Net/Outbox.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::VirtualClock;

namespace {

// One HTTP attempt in this firmware: RestApiService sets a 30 s timeout and makes one call;
// no `while (tries < 3)` loop wraps it in the three methods the outbox drives -
// see RestApiService::getRuleList/sendDiagnosticData/sendModuleUpdateData.
const uint32_t ATTEMPT_TIMEOUT_MS = 30000;

/**
 * A server that costs virtual time and can be told what to answer.
 *
 * It blocks the *calling task* through the scheduler, which is what a synchronous socket call
 * does on the device: other tasks keep running, the caller does not.
 */
class ScriptedServer : public MockHybridAPIService {
public:
    ScriptedServer(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock)
    {
    }

    /** How long every call takes before it answers. */
    void setLatencyMs(uint32_t ms) { _latencyMs = ms; }

    /** When false, every call costs its latency and then reports failure. */
    void setReachable(bool reachable) { _reachable = reachable; }

    /** Rules to answer a rule-list fetch with. */
    void setRuleCount(size_t count)
    {
        ruleListReturnValue.clear();
        for (size_t i = 0; i < count; ++i)
        {
            Rule rule;
            rule.uid = "rule-" + std::to_string(i);
            ruleListReturnValue.push_back(rule);
        }
    }

    bool sendModuleUpdateData(const UpdateDto& update, ApiPriority = ApiPriority::UNDEFINED) override
    {
        record("update:" + update.time);
        return answer();
    }

    bool sendDiagnosticData(const DiagnosticDto& diagnostic, ApiPriority = ApiPriority::UNDEFINED) override
    {
        record("diagnostic:" + diagnostic.timestamp);
        return answer();
    }

    bool sendRuleStateCommand(const char* msg, ApiPriority = ApiPriority::UNDEFINED) override
    {
        record(std::string("rule-state-bulk:") + (msg != nullptr ? msg : ""));
        return answer();
    }

    bool sendByUidRuleStateCommand(const char* uid, const char* msg) override
    {
        record(std::string("rule-state:") + (uid != nullptr ? uid : "") + ":" +
               (msg != nullptr ? msg : ""));
        return answer();
    }

    bool sendRuleSensorOfflineEvent(const char* msg) override
    {
        record(std::string("sensor-offline:") + (msg != nullptr ? msg : ""));
        return answer();
    }

    std::vector<Rule> getRuleList(bool* httpFailed = nullptr, bool* readWhole = nullptr) override
    {
        record("rule-list");
        const bool ok = answer();
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        if (readWhole != nullptr)
        {
            *readWhole = ok;
        }
        return ok ? ruleListReturnValue : std::vector<Rule>();
    }

    /** Device commands to answer a device-command fetch with. */
    void setDeviceCommandCount(size_t count)
    {
        deviceCommandsReturnValue.clear();
        for (size_t i = 0; i < count; ++i)
        {
            DeviceCommandDto command;
            command.uid = "device-" + std::to_string(i);
            deviceCommandsReturnValue.push_back(command);
        }
    }

    std::vector<DeviceCommandDto> getDeviceCommand(bool* httpFailed = nullptr) override
    {
        record("device-command");
        const bool ok = answer();
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        return ok ? deviceCommandsReturnValue : std::vector<DeviceCommandDto>();
    }

    /** Module commands to answer a module-command fetch with. */
    void setModuleCommandCount(size_t count)
    {
        moduleCommandsReturnValue.clear();
        for (size_t i = 0; i < count; ++i)
        {
            ModuleCommandDto command;
            command.uid = "module-cmd-" + std::to_string(i);
            command.command = "AddWifi";
            moduleCommandsReturnValue.push_back(command);
        }
    }

    std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) override
    {
        record("module-command");
        const bool ok = answer();
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        return ok ? moduleCommandsReturnValue : std::vector<ModuleCommandDto>();
    }

    bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& update) override
    {
        record("device-command-result:" + update.uid + ":" + update.status);
        return answer();
    }

    /** Sensor types to answer a catalog fetch with. */
    void setSensorTypeCount(size_t count)
    {
        sensorTypesListReturnValue.clear();
        for (size_t i = 0; i < count; ++i)
        {
            SensorType sensorType;
            sensorType.uid = "sensor-type-" + std::to_string(i);
            sensorType.name = "type_" + std::to_string(i);
            sensorTypesListReturnValue.push_back(sensorType);
        }
    }

    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        record("sensor-types");
        const bool ok = answer();
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        return ok ? sensorTypesListReturnValue : std::vector<SensorType>();
    }

    bool sendToServerModuleStructure(const ModuleStructure&, ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        record("module-structure:" + std::to_string((int)priority));
        return answer();
    }

    /** Every call, in order, as "kind:payload". */
    std::vector<std::string> calls;
    /** When each call started, on the virtual clock. */
    std::vector<uint64_t> callStartedAtMs;

private:
    void record(const std::string& what)
    {
        calls.push_back(what);
        callStartedAtMs.push_back(_clock.nowMs());
    }

    bool answer()
    {
        _scheduler.delayMs(_latencyMs);
        return _reachable;
    }

    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
    uint32_t _latencyMs = 0;
    bool _reachable = true;
};

/** Everything the outbox needs, wired the way globals.h wires it. */
struct OutboxUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ScriptedServer server;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;

    OutboxUnderTest()
        : scheduler(clock),
          server(scheduler, clock),
          completions(8),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false;
    }

    /** Collect every completion the network task has produced so far. */
    std::vector<OutboxCompletion> drain()
    {
        std::vector<OutboxCompletion> collected;
        OutboxCompletion completion;
        while (outbox.pollCompletion(completion))
        {
            collected.push_back(completion);
        }
        return collected;
    }
};

UpdateDto updateStamped(const char* stamp)
{
    UpdateDto dto;
    dto.time = stamp;
    return dto;
}

DiagnosticDto diagnosticStamped(const char* stamp)
{
    DiagnosticDto dto;
    dto.timestamp = stamp;
    return dto;
}

/**
 * The radio's two looks at the link, answering what the test says, after the time the board's would
 * take: a few milliseconds for an answer, the whole wait for silence.
 */
class ScriptedLinkProbe : public ILinkProbe {
public:
    ScriptedLinkProbe(HostTaskScheduler& scheduler, VirtualClock& clock) : _scheduler(scheduler), _clock(clock) {}

    LinkReach gateway = LinkReach::ANSWERED;
    LinkReach publicAddress = LinkReach::SILENT;
    std::vector<uint64_t> askedAtMs;

    LinkReach askGateway(uint32_t waitMs) override
    {
        askedAtMs.push_back(_clock.nowMs());
        _scheduler.delayMs(gateway == LinkReach::SILENT ? waitMs : 5);
        return gateway;
    }

    LinkReach echo(uint32_t, uint32_t waitMs) override
    {
        _scheduler.delayMs(publicAddress == LinkReach::SILENT ? waitMs : 30);
        return publicAddress;
    }

private:
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
};

} // namespace

// --------------------------------------------------------------------------------------------
// 1. Overflow policy: state collapses, events do not
// --------------------------------------------------------------------------------------------

static void test_state_collapses_to_the_freshest_and_events_are_never_dropped(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);

    // Nothing runs yet: the scheduler is cooperative and the test body is not a task, so the
    // network task created on the first submit stays parked until runForMs() below. That is what
    // makes "what is waiting in the outbox" observable at all.
    for (int i = 1; i <= 5; ++i)
    {
        char stamp[8];
        snprintf(stamp, sizeof(stamp), "s%d", i);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(IOutbox::kRejected, module.outbox.submitSensorUpdate(updateStamped(stamp)),
                                      "a state submission must never be refused - it replaces, it does not queue");
    }

    for (int i = 1; i <= 3; ++i)
    {
        char stamp[8];
        snprintf(stamp, sizeof(stamp), "d%d", i);
        module.outbox.submitDiagnostic(diagnosticStamped(stamp));
    }

    // Five sensor updates and three diagnostic snapshots went in; one of each is waiting.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.outbox.pendingCount(),
                                     "state did not collapse: more than one request per kind is waiting");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, module.outbox.collapsedCount(),
                                     "the replaced requests were not counted");

    // Events: the ring takes its capacity and then refuses, in the open, rather than quietly
    // discarding one to make room.
    uint32_t accepted = 0;
    uint32_t refused = 0;
    for (size_t i = 0; i < Outbox::EVENT_RING_CAPACITY + 2; ++i)
    {
        const std::string json = "{\"n\":" + std::to_string(i) + "}";
        if (module.outbox.submitRuleStateByUid("rule-" + std::to_string(i), json) == IOutbox::kRejected)
        {
            refused++;
        }
        else
        {
            accepted++;
        }
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)Outbox::EVENT_RING_CAPACITY, accepted,
                                     "the event ring accepted a different number of events than it holds");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, refused,
                                     "events past the ring's capacity were not refused at submission");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.outbox.refusedEventCount(),
                                     "the refusals were not counted");

    // Now let the network task run and see what actually went out.
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");

    // The freshest state, not the oldest: replacement, not eviction.
    bool sawFreshSensor = false;
    bool sawStaleSensor = false;
    bool sawFreshDiagnostic = false;
    uint32_t eventsSent = 0;
    for (const std::string& call : module.server.calls)
    {
        if (call == "update:s5") sawFreshSensor = true;
        if (call == "update:s1" || call == "update:s2" || call == "update:s3" || call == "update:s4")
            sawStaleSensor = true;
        if (call == "diagnostic:d3") sawFreshDiagnostic = true;
        if (call.rfind("rule-state:", 0) == 0) eventsSent++;
    }

    TEST_ASSERT_TRUE_MESSAGE(sawFreshSensor, "the newest sensor update never reached the server");
    TEST_ASSERT_FALSE_MESSAGE(sawStaleSensor,
                              "a superseded sensor update was sent: the queue is evicting the oldest "
                              "instead of replacing with the newest");
    TEST_ASSERT_TRUE_MESSAGE(sawFreshDiagnostic, "the newest diagnostic snapshot never reached the server");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)Outbox::EVENT_RING_CAPACITY, eventsSent,
                                     "an accepted event was lost between submission and the server");
}

// --------------------------------------------------------------------------------------------
// 2. A hand-off is not a delivery
// --------------------------------------------------------------------------------------------

static void test_a_completion_reports_delivery_not_acceptance(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(ATTEMPT_TIMEOUT_MS);
    module.server.setReachable(false);

    const uint32_t ticket = module.outbox.submitDeviceUpdate(updateStamped("d"));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(IOutbox::kRejected, ticket, "the submission was refused");

    module.scheduler.runForMs(600000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)completions.size(),
                                     "exactly one completion was expected for one request");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(ticket, completions[0].ticket,
                                     "the completion does not carry the ticket the submission returned");
    TEST_ASSERT_FALSE_MESSAGE(completions[0].delivered,
                              "the server refused every attempt and the outbox still called it delivered - "
                              "this is exactly the mistake that would erase a device's reason for switching on");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, (uint32_t)completions[0].attempts,
                                     "the request was not tried the number of times the policy allows");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.undeliveredCount(), "the failure was not counted");
}

static void test_a_delivered_request_says_so(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(100);

    const uint32_t ticket = module.outbox.submitDeviceUpdate(updateStamped("d"));
    module.scheduler.runForMs(10000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_EQUAL_UINT32(ticket, completions[0].ticket);
    TEST_ASSERT_TRUE_MESSAGE(completions[0].delivered, "a request the server accepted was reported undelivered");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)completions[0].attempts,
                                     "a request that succeeded first time was tried more than once");
}

// --------------------------------------------------------------------------------------------
// 3. One retry policy, and it backs off
// --------------------------------------------------------------------------------------------

static void test_retries_back_off_instead_of_pausing_a_flat_two_hundred_milliseconds(void)
{
    OutboxUnderTest module;
    // Zero latency so the gaps between attempts are the backoff and nothing else.
    module.server.setLatencyMs(0);
    module.server.setReachable(false);

    module.outbox.submitDeviceUpdate(updateStamped("d"));
    module.scheduler.runForMs(600000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, (uint32_t)module.server.calls.size(),
                                     "the number of attempts is not the number the policy states");

    // 500 ms, then 2 s, then 8 s. The twelve loops this replaced paused a flat 200 ms, which
    // retries hardest exactly when retrying cannot help.
    uint32_t expected = Outbox::DEFAULT_BACKOFF_BASE_MS;
    for (size_t i = 1; i < module.server.callStartedAtMs.size(); ++i)
    {
        const uint64_t gap = module.server.callStartedAtMs[i] - module.server.callStartedAtMs[i - 1];

        char message[192];
        snprintf(message, sizeof(message),
                 "gap before attempt %u was %lu ms, expected %lu ms of backoff",
                 (unsigned)(i + 1), (unsigned long)gap, (unsigned long)expected);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, (uint32_t)gap, message);

        expected *= Outbox::BACKOFF_FACTOR;
    }
}

// --------------------------------------------------------------------------------------------
// 4. The network task is watched
// --------------------------------------------------------------------------------------------

static void test_the_network_task_puts_itself_under_the_task_watchdog(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(100);

    module.outbox.submitDiagnostic(diagnosticStamped("d"));
    module.scheduler.runForMs(10000);

    TEST_ASSERT_TRUE_MESSAGE(module.outbox.taskStarted(), "the network task was never created");

    bool subscribed = false;
    for (const std::string& name : module.scheduler.watchdogSubscribedTaskNames())
    {
        if (name == "outbox-net")
        {
            subscribed = true;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(subscribed,
                             "the outbox network task did not put itself under the task watchdog - "
                             "a hung send would still look like a healthy module");
    TEST_ASSERT_TRUE_MESSAGE(module.scheduler.watchdogFeeds() > 0,
                             "the network task never reported progress to the watchdog");
}

// --------------------------------------------------------------------------------------------
// 5. The rule-list response: one slot, and a truncated snapshot is not a snapshot
// --------------------------------------------------------------------------------------------

static void test_a_rule_list_comes_back_through_the_completion_queue(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(500);
    module.server.setRuleCount(3);

    const uint32_t ticket = module.outbox.submitRuleListRequest();
    module.scheduler.runForMs(10000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_EQUAL_UINT32(ticket, completions[0].ticket);
    TEST_ASSERT_TRUE_MESSAGE(completions[0].delivered, "the rule list fetch was reported failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, (uint32_t)completions[0].responseItemCount,
                                     "the completion does not say how many rules are waiting");

    std::vector<Rule> rules;
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.takeRuleListResponse(rules),
                             "the response slot was empty although the completion said otherwise");
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)rules.size());

    TEST_ASSERT_FALSE_MESSAGE(module.outbox.takeRuleListResponse(rules),
                              "the response slot handed the same answer out twice");
}

static void test_a_long_rule_list_is_handed_over_whole(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(100);
    module.server.setRuleCount(200);

    module.outbox.submitRuleListRequest();
    module.scheduler.runForMs(10000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_TRUE_MESSAGE(completions[0].delivered, "a long rule list was reported as a failed fetch");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(200, (uint32_t)completions[0].responseItemCount,
                                     "the completion under-reports how many rules are waiting");

    std::vector<Rule> rules;
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.takeRuleListResponse(rules), "the response slot was empty");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(200, (uint32_t)rules.size(),
                                     "the rule list was shortened on its way through the outbox - "
                                     "applied as a snapshot that deletes the rules that fell off the "
                                     "end, and refused it means the module never syncs again");
}

// Setting _taskStarted
// before calling createTask() and ignoring the result would let a creation refused for want
// of heap silence the sender until the next reboot - no task to drain the queue, and no second
// attempt ever. Outbox::ensureTaskStarted() is written the right way round, and this is the guard
// that keeps it that way.
//
// To see the red: set _taskStarted = true before the createTask() call in ensureTaskStarted().
static void test_a_refused_network_task_is_reported_and_retried(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(100);

    // Two refusals, as an out-of-heap device would give. The first diagnostic send happens ~30 s
    // into CONFIGURED_DONE with WiFi, MQTT and the TLS pools already up - not a far-fetched moment
    // to be short of memory.
    module.scheduler.failNextCreateTasks(2);

    module.outbox.submitDiagnostic(diagnosticStamped("first"));
    const bool startedAfterFirst = module.outbox.taskStarted();

    module.outbox.submitDiagnostic(diagnosticStamped("second"));
    const bool startedAfterSecond = module.outbox.taskStarted();

    module.outbox.submitDiagnostic(diagnosticStamped("third")); // by now creation succeeds
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE_MESSAGE(startedAfterFirst,
                              "the outbox claimed a task after createTask() refused - the flag lies");
    TEST_ASSERT_FALSE_MESSAGE(startedAfterSecond,
                              "the outbox claimed a task after the second refusal");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.outbox.taskStartFailureCount(),
                                     "the refusals were not counted - the failure is invisible");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.scheduler.refusedTaskCount(),
                                     "the harness did not actually refuse twice");

    // The point of leaving the flag false: the module recovers by itself, without a reboot.
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.taskStarted(),
                             "creation was never retried after the heap recovered - the network is "
                             "silent until the next reboot");
    TEST_ASSERT_TRUE_MESSAGE(module.server.calls.size() >= 1,
                             "nothing was ever sent, so the retry did not actually help");
}

static void test_a_failed_rule_list_fetch_is_not_an_empty_rule_list(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setReachable(false);
    module.server.setRuleCount(4);

    module.outbox.submitRuleListRequest();
    module.scheduler.runForMs(600000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_FALSE_MESSAGE(completions[0].delivered,
                              "a fetch the server never answered was reported as a delivered empty list - "
                              "the caller would apply it as a snapshot and delete every rule it has");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, (uint32_t)completions[0].attempts,
                                     "a failed fetch was not retried under the same policy as a send");

    std::vector<Rule> rules;
    TEST_ASSERT_FALSE_MESSAGE(module.outbox.takeRuleListResponse(rules),
                              "a failed fetch left something in the response slot");
}

// --------------------------------------------------------------------------------------------
// 5b. A command fetch: an empty list from a server that answered is not a failed fetch
// --------------------------------------------------------------------------------------------

static void test_an_empty_device_command_list_from_a_live_server_is_a_delivered_answer(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setDeviceCommandCount(0);

    module.outbox.submitDeviceCommandFetch();
    module.scheduler.runForMs(1000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_TRUE_MESSAGE(completions[0].delivered,
                             "the server answered that there is nothing pending and that was "
                             "reported as a failed fetch - the caller would run the MQTT payload "
                             "instead of doing nothing");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, completions[0].responseItemCount,
                                     "an empty answer was counted as items");

    std::vector<DeviceCommandDto> commands;
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.takeDeviceCommandResponse(commands),
                             "a delivered fetch left no response slot for the caller to take");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)commands.size());
}

static void test_a_failed_device_command_fetch_is_not_an_empty_command_list(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setReachable(false);
    module.server.setDeviceCommandCount(3);

    module.outbox.submitDeviceCommandFetch();
    module.scheduler.runForMs(600000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_FALSE_MESSAGE(completions[0].delivered,
                              "a fetch the server never answered was reported as delivered - the "
                              "caller would take that for 'nothing pending' and lose the fallback");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, (uint32_t)completions[0].attempts,
                                     "a failed fetch was not retried under the same policy as a send");

    std::vector<DeviceCommandDto> commands;
    TEST_ASSERT_FALSE_MESSAGE(module.outbox.takeDeviceCommandResponse(commands),
                              "a failed fetch left something in the response slot");
}

// --------------------------------------------------------------------------------------------
// 5c. The sensor type catalog: the same two outcomes, told apart the same way
// --------------------------------------------------------------------------------------------

static void test_a_sensor_type_catalog_arrives_whole_in_the_response_slot(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setSensorTypeCount(6);

    module.outbox.submitSensorTypesRequest();
    module.scheduler.runForMs(1000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_TRUE_MESSAGE(completions[0].kind == OutboxKind::SENSOR_TYPES_FETCH,
                             "the completion did not come back under its own kind");
    TEST_ASSERT_TRUE(completions[0].delivered);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, completions[0].responseItemCount,
                                     "the completion did not report how many sensor types came back");

    std::vector<SensorType> sensorTypes;
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.takeSensorTypesResponse(sensorTypes),
                             "a delivered fetch left no response slot for the caller to take");
    TEST_ASSERT_EQUAL_UINT32(6, (uint32_t)sensorTypes.size());
    TEST_ASSERT_EQUAL_STRING("sensor-type-5", sensorTypes[5].uid.c_str());

    TEST_ASSERT_FALSE_MESSAGE(module.outbox.takeSensorTypesResponse(sensorTypes),
                              "the response slot was still full after it had been taken");
}

static void test_an_empty_sensor_type_catalog_from_a_live_server_is_a_delivered_answer(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setSensorTypeCount(0);

    module.outbox.submitSensorTypesRequest();
    module.scheduler.runForMs(1000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_TRUE_MESSAGE(completions[0].delivered,
                             "the server answered that this account has no sensor models and that "
                             "was reported as a failed fetch");
    TEST_ASSERT_EQUAL_UINT32(0, completions[0].responseItemCount);

    std::vector<SensorType> sensorTypes;
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.takeSensorTypesResponse(sensorTypes),
                             "a delivered fetch left no response slot for the caller to take");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)sensorTypes.size());
}

static void test_a_failed_sensor_type_fetch_is_not_an_empty_catalog(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setReachable(false);
    module.server.setSensorTypeCount(4);

    module.outbox.submitSensorTypesRequest();
    module.scheduler.runForMs(600000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_FALSE_MESSAGE(completions[0].delivered,
                              "a fetch the server never answered was reported as a delivered empty "
                              "catalog - the caller would record a successful synchronization "
                              "against sensor types it never received");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, (uint32_t)completions[0].attempts,
                                     "a failed fetch was not retried under the same policy as a send");

    std::vector<SensorType> sensorTypes;
    TEST_ASSERT_FALSE_MESSAGE(module.outbox.takeSensorTypesResponse(sensorTypes),
                              "a failed fetch left something in the response slot");
}

// State, like the rule list: two asks while one is waiting are one ask.
static void test_a_second_sensor_type_request_collapses_into_the_one_waiting(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(ATTEMPT_TIMEOUT_MS);
    module.server.setSensorTypeCount(1);

    module.outbox.submitSensorTypesRequest();
    module.outbox.submitSensorTypesRequest();
    module.outbox.submitSensorTypesRequest();
    module.scheduler.runForMs(600000);

    char message[176];
    snprintf(message, sizeof(message),
             "three asks for the same catalog produced %u GETs and %u collapses; a second ask "
             "while one is waiting must be folded into it",
             (unsigned)module.server.calls.size(), (unsigned)module.outbox.collapsedCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.calls.size(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.outbox.collapsedCount(), message);
}

// The structure is the opposite: an event, never collapsed, because two structures are two
// different addresses and the second does not make the first redundant.
static void test_two_module_structures_are_both_sent(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);

    ModuleStructure structure;
    module.outbox.submitModuleStructure(structure, ApiPriority::BOTH);
    module.outbox.submitModuleStructure(structure, ApiPriority::BOTH);
    module.scheduler.runForMs(1000);

    char message[176];
    snprintf(message, sizeof(message),
             "two structures produced %u POSTs and %u collapses; a structure is an event and must "
             "never be collapsed",
             (unsigned)module.server.calls.size(), (unsigned)module.outbox.collapsedCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.server.calls.size(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.outbox.collapsedCount(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("module-structure:5", module.server.calls[0].c_str(),
                                     "the structure did not go out under ApiPriority::BOTH");
}

static void test_an_empty_module_command_list_from_a_live_server_is_a_delivered_answer(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setModuleCommandCount(0);

    module.outbox.submitModuleCommandFetch();
    module.scheduler.runForMs(1000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_TRUE_MESSAGE(completions[0].kind == OutboxKind::MODULE_COMMAND_FETCH,
                             "the completion did not come back under its own kind");
    TEST_ASSERT_TRUE_MESSAGE(completions[0].delivered,
                             "the server answered that there is nothing pending and that was "
                             "reported as a failed fetch");
    TEST_ASSERT_EQUAL_UINT32(0, completions[0].responseItemCount);

    std::vector<ModuleCommandDto> commands;
    TEST_ASSERT_TRUE_MESSAGE(module.outbox.takeModuleCommandResponse(commands),
                             "a delivered fetch left no response slot for the caller to take");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)commands.size());
}

static void test_a_module_command_list_arrives_whole_in_the_response_slot(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setModuleCommandCount(5);

    module.outbox.submitModuleCommandFetch();
    module.scheduler.runForMs(1000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_TRUE(completions[0].delivered);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, completions[0].responseItemCount,
                                     "the completion did not report how many commands came back");

    std::vector<ModuleCommandDto> commands;
    TEST_ASSERT_TRUE(module.outbox.takeModuleCommandResponse(commands));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, (uint32_t)commands.size(),
                                     "the module command list was shortened on the way through");
    TEST_ASSERT_EQUAL_STRING("module-cmd-0", commands[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("module-cmd-4", commands[4].uid.c_str());

    TEST_ASSERT_FALSE_MESSAGE(module.outbox.takeModuleCommandResponse(commands),
                              "the response slot was still full after being taken");
}

static void test_a_failed_module_command_fetch_is_not_an_empty_command_list(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);
    module.server.setReachable(false);
    module.server.setModuleCommandCount(3);

    module.outbox.submitModuleCommandFetch();
    module.scheduler.runForMs(600000);

    std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_FALSE_MESSAGE(completions[0].delivered,
                              "a fetch the server never answered was reported as delivered - the "
                              "caller would take that for 'nothing pending' and stay silent");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, (uint32_t)completions[0].attempts,
                                     "a failed fetch was not retried under the same policy as a send");

    std::vector<ModuleCommandDto> commands;
    TEST_ASSERT_FALSE_MESSAGE(module.outbox.takeModuleCommandResponse(commands),
                              "a failed fetch left something in the response slot");
}

static void test_a_command_report_is_an_event_and_reaches_the_server(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(0);

    DeviceCommandUpdateDto update;
    update.uid = "device-7";
    update.status = "done";

    const uint32_t ticket = module.outbox.submitDeviceCommandResult(update);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(IOutbox::kRejected, ticket, "the report was refused on an empty ring");

    module.scheduler.runForMs(1000);

    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)module.server.calls.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("device-command-result:device-7:done",
                                     module.server.calls[0].c_str(),
                                     "the DTO the network task sent was not the one submitted");
}

// --------------------------------------------------------------------------------------------
// 6. Submitting never costs the producer time, whatever the server is doing
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// The probe of the link: a job of the network task, ahead of the requests
// --------------------------------------------------------------------------------------------

/**
 * Asked for together with a request, the probe goes first - it is asked for while requests are
 * failing and decides whether the radio is restarted under them - and its finding comes back in its
 * completion, which says the job ran.
 *
 * Mutation: take the probe after the requests, or not report what it found.
 */
static void test_a_link_probe_goes_ahead_of_the_requests_and_brings_back_what_it_found(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(100);
    ScriptedLinkProbe probe(module.scheduler, module.clock);
    module.outbox.setLinkProbe(&probe, boardLinkProbePlan());

    module.outbox.submitSensorUpdate(updateStamped("t0"));
    const uint32_t ticket = module.outbox.submitLinkProbe();
    TEST_ASSERT_NOT_EQUAL(IOutbox::kRejected, ticket);
    module.scheduler.runForMs(20000);

    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)probe.askedAtMs.size());
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)module.server.calls.size());
    TEST_ASSERT_TRUE_MESSAGE(probe.askedAtMs[0] <= module.server.callStartedAtMs[0],
                             "the request went ahead of the probe of the link");

    bool seen = false;
    const std::vector<OutboxCompletion> completions = module.drain();
    for (size_t i = 0; i < completions.size(); i++)
    {
        if (completions[i].kind == OutboxKind::LINK_PROBE)
        {
            seen = true;
            TEST_ASSERT_EQUAL_UINT32(ticket, completions[i].ticket);
            TEST_ASSERT_TRUE(completions[i].delivered);
            TEST_ASSERT_EQUAL_UINT8(1, completions[i].attempts);
            const LinkProbeResult found = LinkProbeResult::decode(completions[i].linkProbe);
            TEST_ASSERT_EQUAL(LinkReach::ANSWERED, found.gateway);
            TEST_ASSERT_EQUAL(LinkReach::SILENT, found.publicAddress);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(seen, "the probe produced no completion");
}

/** Two probes asked for while one waits are one probe. */
static void test_two_link_probes_waiting_are_one(void)
{
    OutboxUnderTest module;
    ScriptedLinkProbe probe(module.scheduler, module.clock);
    module.outbox.setLinkProbe(&probe, boardLinkProbePlan());

    module.outbox.submitLinkProbe();
    const uint32_t second = module.outbox.submitLinkProbe();
    TEST_ASSERT_EQUAL_UINT32(1, module.outbox.collapsedCount());
    module.scheduler.runForMs(20000);

    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)probe.askedAtMs.size());
    const std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_EQUAL_UINT32(second, completions[0].ticket);
}

/** With no probe wired the job still ends - saying the gateway could not be asked, which decides nothing. */
static void test_a_link_probe_with_no_probe_wired_says_the_gateway_could_not_be_asked(void)
{
    OutboxUnderTest module;
    module.outbox.submitLinkProbe();
    module.scheduler.runForMs(1000);

    const std::vector<OutboxCompletion> completions = module.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_EQUAL(LinkReach::UNAVAILABLE, LinkProbeResult::decode(completions[0].linkProbe).gateway);
}

/** A network task the heap refused is on the status board until it is created. */
static void test_a_refused_network_task_is_on_the_status_board_until_it_is_created(void)
{
    OutboxUnderTest module;
    ModuleStatusBoard board;
    module.outbox.setStatusBoard(&board);
    module.scheduler.failNextCreateTasks(1);

    module.outbox.submitDiagnostic(diagnosticStamped("first"));
    TEST_ASSERT_TRUE_MESSAGE(board.networkTaskRefused(), "the refusal did not reach the status board");
    module.outbox.submitDiagnostic(diagnosticStamped("second"));
    TEST_ASSERT_TRUE(module.outbox.taskStarted());
    TEST_ASSERT_FALSE(board.networkTaskRefused());
}

static void test_submitting_costs_the_producer_no_time_while_a_send_is_in_flight(void)
{
    OutboxUnderTest module;
    module.server.setLatencyMs(ATTEMPT_TIMEOUT_MS);
    module.server.setReachable(false);

    // Get the network task stuck in a send first.
    module.outbox.submitDiagnostic(diagnosticStamped("first"));
    module.scheduler.runForMs(1000);
    TEST_ASSERT_TRUE_MESSAGE(module.server.calls.size() >= 1, "the network task never started sending");

    const uint64_t before = module.clock.nowMs();
    for (int i = 0; i < 50; ++i)
    {
        module.outbox.submitSensorUpdate(updateStamped("s"));
        module.outbox.submitDeviceUpdate(updateStamped("d"));
        module.outbox.submitRuleStateByUid("rule", "{}");
    }
    const uint64_t after = module.clock.nowMs();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)(after - before),
                                     "submitting cost the producer virtual time while a send was in flight - "
                                     "the producer is waiting on the network after all");
}

extern "C" void run_outbox_tests(void)
{
    printf("\n----- Running Outbox Tests -----\n");

    RUN_TEST(test_state_collapses_to_the_freshest_and_events_are_never_dropped);
    RUN_TEST(test_a_completion_reports_delivery_not_acceptance);
    RUN_TEST(test_a_delivered_request_says_so);
    RUN_TEST(test_retries_back_off_instead_of_pausing_a_flat_two_hundred_milliseconds);
    RUN_TEST(test_the_network_task_puts_itself_under_the_task_watchdog);
    RUN_TEST(test_a_rule_list_comes_back_through_the_completion_queue);
    RUN_TEST(test_a_long_rule_list_is_handed_over_whole);
    RUN_TEST(test_a_refused_network_task_is_reported_and_retried);
    RUN_TEST(test_a_failed_rule_list_fetch_is_not_an_empty_rule_list);
    RUN_TEST(test_an_empty_device_command_list_from_a_live_server_is_a_delivered_answer);
    RUN_TEST(test_a_failed_device_command_fetch_is_not_an_empty_command_list);
    RUN_TEST(test_an_empty_module_command_list_from_a_live_server_is_a_delivered_answer);
    RUN_TEST(test_a_module_command_list_arrives_whole_in_the_response_slot);
    RUN_TEST(test_a_failed_module_command_fetch_is_not_an_empty_command_list);
    RUN_TEST(test_a_sensor_type_catalog_arrives_whole_in_the_response_slot);
    RUN_TEST(test_an_empty_sensor_type_catalog_from_a_live_server_is_a_delivered_answer);
    RUN_TEST(test_a_failed_sensor_type_fetch_is_not_an_empty_catalog);
    RUN_TEST(test_a_second_sensor_type_request_collapses_into_the_one_waiting);
    RUN_TEST(test_two_module_structures_are_both_sent);
    RUN_TEST(test_a_command_report_is_an_event_and_reaches_the_server);
    RUN_TEST(test_submitting_costs_the_producer_no_time_while_a_send_is_in_flight);
    RUN_TEST(test_a_link_probe_goes_ahead_of_the_requests_and_brings_back_what_it_found);
    RUN_TEST(test_two_link_probes_waiting_are_one);
    RUN_TEST(test_a_link_probe_with_no_probe_wired_says_the_gateway_could_not_be_asked);
    RUN_TEST(test_a_refused_network_task_is_on_the_status_board_until_it_is_created);

    printf("\n----- Outbox Tests Complete -----\n");
}
