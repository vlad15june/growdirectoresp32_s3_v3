// Rule sync across the outbox: what the module does with the answer, and - more important - what
// it does with a non-answer.
//
// A rule snapshot is authoritative. SyncRulesUseCase::processServerRules() builds snapshotUids
// from the list it received and hands it to IRuleRepository::synchronizeRules(rules, true, uids);
// RuleRepository marks every local rule missing from that set DELETING. So anything that shortens
// or invents a snapshot deletes rules on a live greenhouse:
//
//   - a fetch that failed must not arrive as "the server says you have no rules";
//   - a 200 whose body would not parse must not arrive as an empty snapshot either;
//   - and a long list must arrive whole, not cut to whatever a buffer happened to hold.
//
// The last one is why this file exists. A cap on the response - at 64 rules, say -
// that refuses to apply anything longer is safe against deletion but is a
// worse failure: a module with 65 rules would never sync again, in either direction, until it was
// reflashed - and it would never acknowledge the snapshot, so the backend would see a module that
// had never synchronised at all. test_a_long_rule_list_is_applied_whole is red on that version.
//
// What is under test is production code: the real SyncRulesUseCase driving the real Outbox.
// Replaced: the server and time.

#include <unity.h>

#include "Application/Services/Net/Outbox.h"
#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"

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

// More than a 64-rule cap would allow, so that a cap is caught
// rather than accommodated.
const size_t LONG_RULE_LIST = 200;

class RuleServer : public MockHybridAPIService {
public:
    explicit RuleServer(HostTaskScheduler& scheduler) : _scheduler(scheduler) {}

    /** Answer with this many rules. */
    void setRuleCount(size_t count)
    {
        ruleListReturnValue.clear();
        for (size_t i = 0; i < count; ++i)
        {
            Rule rule;
            rule.uid = "server-rule-" + std::to_string(i);
            rule.version = 1;
            rule.ruleType = RuleType::SENSOR;
            rule.deviceUid = "dev-1";
            rule.sensorUid = "sensor-1";
            rule.mode = RuleMode::OFF;
            rule.valueLow = 10;
            rule.valueHigh = 20;
            rule.direction = RuleDirection::UP;
            rule.state = RuleState::SYNCHRONIZED;
            ruleListReturnValue.push_back(rule);
        }
    }

    /**
     * Make the fetch fail the way RestAPIService reports failure: an empty vector *and*
     * httpFailed. That pairing is the whole point - a non-answer and "you have no rules" look
     * identical without the flag.
     */
    void setFetchFails(bool fails) { _fetchFails = fails; }

    std::vector<Rule> getRuleList(bool* httpFailed = nullptr, bool* readWhole = nullptr) override
    {
        fetchCount++;
        _scheduler.delayMs(50);
        if (_fetchFails)
        {
            if (httpFailed != nullptr) { *httpFailed = true; }
            if (readWhole != nullptr) { *readWhole = false; }
            return std::vector<Rule>();
        }
        if (httpFailed != nullptr) { *httpFailed = false; }
        if (readWhole != nullptr) { *readWhole = true; }
        return ruleListReturnValue;
    }

    bool sendRuleStateCommand(const char*, ApiPriority = ApiPriority::UNDEFINED) override
    {
        ackCount++;
        _scheduler.delayMs(10);
        return true;
    }

    uint32_t fetchCount = 0;
    uint32_t ackCount = 0;

private:
    HostTaskScheduler& _scheduler;
    bool _fetchFails = false;
};

/** A rule the module already has, and would lose if a bad snapshot were applied. */
Rule localRule(const std::string& uid)
{
    Rule rule;
    rule.uid = uid;
    rule.version = 1;
    rule.ruleType = RuleType::SENSOR;
    rule.deviceUid = "dev-1";
    rule.sensorUid = "sensor-1";
    rule.mode = RuleMode::ON;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.direction = RuleDirection::UP;
    rule.state = RuleState::SYNCHRONIZED;
    return rule;
}

struct SyncUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockNTPTimeAdapter timeAdapter;
    MockRuleRepository ruleRepository;
    RuleServer server;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    SyncRulesUseCase syncRules;

    SyncUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          server(scheduler),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          // No JSON converter: the acknowledgement it would build is not what this file measures,
          // and sendRuleStateUpdate() returns early without one.
          syncRules(&server, &ruleRepository, &arduino, &timeAdapter, nullptr, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false;
        ruleRepository.rules.push_back(localRule("local-rule-1"));
        ruleRepository.rules.push_back(localRule("local-rule-2"));
        ruleRepository.synchronizeReturnValue = 1;
        syncRules.setOutbox(&outbox);
    }

    /** One control-loop iteration: apply what came back, then ask for what is due. */
    void loopIteration()
    {
        OutboxCompletion completion;
        while (outbox.pollCompletion(completion))
        {
            syncRules.onOutboxCompletion(completion);
        }
        syncRules.loop();
    }

    /** Ask, let the network answer, apply. */
    void runOneSyncCycle()
    {
        loopIteration();                 // submits the fetch
        scheduler.runForMs(600000);      // the network task answers, or exhausts its attempts
        loopIteration();                 // applies the outcome
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// A non-answer must not be applied as a snapshot
// --------------------------------------------------------------------------------------------

static void test_a_failed_rule_list_fetch_leaves_the_modules_rules_untouched(void)
{
    SyncUnderTest module;
    module.server.setFetchFails(true);
    module.server.setRuleCount(0);

    module.runOneSyncCycle();

    // --- the run has to be sound before its verdict means anything --------------------------
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.server.fetchCount >= 1,
                             "the fetch never reached the server - nothing was exercised");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, module.server.fetchCount,
                                     "the failed fetch was not retried under the dispatcher policy");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.undeliveredCount(),
                                     "the fetch was not reported undelivered, so this proves nothing");

    // --- the actual claim --------------------------------------------------------------------
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.ruleRepository.synchronizeCallCount,
                                     "a failed fetch was applied as a snapshot: synchronizeRules() "
                                     "was called with an empty list, which marks every local rule "
                                     "DELETING");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.ruleRepository.rules.size(),
                                     "the module lost rules it already had");
    TEST_ASSERT_FALSE_MESSAGE(module.syncRules.getLastSyncStatus(),
                              "a failed fetch was reported as a successful synchronisation");
}

// --------------------------------------------------------------------------------------------
// A long snapshot must arrive whole
// --------------------------------------------------------------------------------------------

static void test_a_long_rule_list_is_applied_whole(void)
{
    SyncUnderTest module;
    module.server.setRuleCount(LONG_RULE_LIST);
    module.ruleRepository.synchronizeReturnValue = (int)LONG_RULE_LIST;

    module.runOneSyncCycle();

    // --- the run has to be sound before its verdict means anything --------------------------
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.server.fetchCount,
                                     "a fetch the server answered was retried");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.deliveredCount(),
                                     "the fetch was not reported delivered");

    // --- the actual claim --------------------------------------------------------------------
    char countMessage[224];
    snprintf(countMessage, sizeof(countMessage),
             "the module applied %d of the %u rules the server sent: a snapshot cut short deletes "
             "the rules that fell off the end, and a module that refuses to apply it never syncs "
             "again",
             (int)module.ruleRepository.lastSynchronizeRules.size(), (unsigned)LONG_RULE_LIST);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.ruleRepository.synchronizeCallCount,
                                     "the long snapshot was never applied at all");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)LONG_RULE_LIST,
                                     (uint32_t)module.ruleRepository.lastSynchronizeRules.size(),
                                     countMessage);
    TEST_ASSERT_TRUE_MESSAGE(module.ruleRepository.lastIsFullSnapshot,
                             "the snapshot was not applied as a full snapshot");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)LONG_RULE_LIST,
                                     (uint32_t)module.ruleRepository.lastSnapshotUids.size(),
                                     "the snapshot UID set is shorter than the snapshot itself - "
                                     "the rules missing from it would be marked DELETING");
    TEST_ASSERT_TRUE_MESSAGE(module.syncRules.getLastSyncStatus(),
                             "a snapshot that was applied whole was reported as a failed sync");
}

// --------------------------------------------------------------------------------------------
// An empty answer from a server that did answer is a legitimate answer
// --------------------------------------------------------------------------------------------

static void test_an_empty_answer_from_a_reachable_server_keeps_the_local_rules(void)
{
    SyncUnderTest module;
    module.server.setRuleCount(0);

    module.runOneSyncCycle();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.deliveredCount(),
                                     "the fetch was not reported delivered");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.ruleRepository.synchronizeCallCount,
                                     "an empty answer was applied as a snapshot");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.ruleRepository.rules.size(),
                                     "the module lost rules on an empty answer");

    // The two consequences a module with no rules must keep: the synchronization
    // is recorded as having succeeded, and it cost one request rather than a full retry budget.
    // The red for this pair is one level down, in test_rest_api_service.cpp - this harness answers
    // the use case directly and never reaches the converter that must not call `[]` a failure.
    char message[176];
    snprintf(message, sizeof(message),
             "an account with no rules recorded lastSyncStatus=%d after %u fetch(es); it must be a "
             "success, and it must cost one request, not a retry budget",
             module.syncRules.getLastSyncStatus() ? 1 : 0, (unsigned)module.server.fetchCount);
    TEST_ASSERT_TRUE_MESSAGE(module.syncRules.getLastSyncStatus(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.server.fetchCount, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.outbox.undeliveredCount(), message);
}

extern "C" void run_rule_sync_through_outbox_tests(void)
{
    printf("\n----- Running Rule Sync Through Outbox Tests -----\n");

    RUN_TEST(test_a_failed_rule_list_fetch_leaves_the_modules_rules_untouched);
    RUN_TEST(test_a_long_rule_list_is_applied_whole);
    RUN_TEST(test_an_empty_answer_from_a_reachable_server_keeps_the_local_rules);

    printf("\n----- Rule Sync Through Outbox Tests Complete -----\n");
}
