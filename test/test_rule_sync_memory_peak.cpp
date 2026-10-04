// What one rule synchronization has to have free at once.
//
// The rule list endpoint answers with an array of rules - 600 to 700 bytes of JSON each, so a
// module with two dozen rules is answered with ~15 KB. Reading that answer must not cost the answer
// itself twice over: the body in one string, a JSON document holding the whole array, a second
// document per element and a third the element was re-parsed into. All of it alive at the moment
// the module also holds the rules it already has and the rules it is building out of the answer.
//
// So the number this file is about is not "how much does the sync allocate" - it is "how much does
// it need on top of what it produces, and does that follow the number of rules". A cost that is a
// function of the account's rule set is a module that works in a test and fails in the field.
//
// Measured with Sim/HostAllocationCounter.h, which counts every operator new the test binary makes
// and samples the live balance at each of them. Each scenario is measured twice - at eight rules
// and at three times that - and the verdict is about the difference between the two, so nothing
// here depends on a magic number: the plank is derived from the weight of one rule as this module
// holds it, computed from the same rule sets the scenario runs on.
//
// Two windows, because they have different floors and only one of them is about a box:
//
//   - reading the answer, where nothing but one rule may be alive on top of the rules produced;
//   - applying it, where the merge is given the answer and the module's own rules and writes the
//     rule file. The file is one string of the whole rule set by the stored format, and the flash
//     device here is a string in this same heap, so this window cannot be flat by construction -
//     what it must not do is hold the answer a second and a third time. The plank is named in
//     stored rule sets and the measurement is printed either way.
//
// What runs is production code: the real RestAPIService reading a body the real
// ArduinoJsonConverter parses, the real SyncRulesUseCase merging into the real RuleRepository over
// the real SpiffsRuleDao. Replaced: the socket, the flash device and time.

#include <unity.h>

#include "Application/Services/Net/Outbox.h"
#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/HostAllocationCounter.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimHttpClient.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimHttpClient;
using hostsim::VirtualClock;

namespace {

/** See the comment at the task construction below: host frames, not the device's. */
const uint32_t HOST_TASK_STACK_BYTES = 512 * 1024;

/** A rule set of the size a module in the field has. */
const size_t SMALL_RULE_SET = 8;

/** And three times that - the top of what the registry can hold. */
const size_t LARGE_RULE_SET = 24;

/**
 * And an answer far past that, which is the measurement that means something: the module keeps
 * MAX_RULES rules, so an answer of any length must cost it the same as an answer of MAX_RULES.
 * Measuring only up to the cap would be measuring the cap.
 */
const size_t ANSWER_PAST_THE_CAP = 100;

const char* const SERVER_URL = "http://gd.test";
const char* const TOKEN = "TOK123";
const char* const MODULE_UID = "UID-1";

const char* const DEVICE_A = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";
const char* const SENSOR_A = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";

Rule makeRule(size_t index, int version = 3)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a%04x", (unsigned)index);

    Rule rule;
    rule.uid = uid;
    rule.version = version;
    rule.deviceUid = DEVICE_A;
    rule.sensorUid = SENSOR_A;
    rule.ruleType = RuleType::SENSOR;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.timeToSetPoint = 10;
    return rule;
}

std::vector<Rule> makeRuleSet(size_t count, int version = 3)
{
    std::vector<Rule> rules;
    rules.reserve(count);
    for (size_t i = 0; i < count; i++)
    {
        rules.push_back(makeRule(i, version));
    }
    return rules;
}

/** @brief The rule list as the backend puts it on the wire. */
std::string ruleListJson(const std::vector<Rule>& rules, IJsonConverter& converter)
{
    std::string body = "[";
    for (size_t i = 0; i < rules.size(); i++)
    {
        if (i > 0)
        {
            body += ",";
        }
        body += converter.ruleToJson(rules[i]);
    }
    body += "]";
    return body;
}

/**
 * @brief What a measured step held, and what it needed on top of that.
 *
 * `before` and `after` are the live balances the step runs between - both hold whatever the module
 * already had, so neither is a number to read on its own. `peak` is the largest balance seen
 * inside the step. What the step needed above the states it connects is the peak minus the larger
 * of the two, and that is the only figure any assertion here is made on.
 */
struct PeakMeasurement {
    long long before = 0;
    long long after = 0;
    long long peak = 0;
    size_t ruleCount = 0;
    /** Bytes of one rule as this module holds it, for the plank. */
    long long ruleBytes = 0;

    long long overhead() const
    {
        const long long floorBytes = (after > before) ? after : before;
        return peak - floorBytes;
    }

    /**
     * @brief What the step needed above the state it started from, what it produced included.
     *
     * The figure for a step whose product is memory the module then holds - a merge ends with the
     * rule file and the registry's cache in hand, and the board has to have room for those as much
     * as for the transients on the way to them. overhead() answers zero for such a step, because
     * its peak is its own result.
     */
    long long neededAboveStart() const { return peak - before; }
};

/** @brief Bytes a rule set of `count` rules occupies in RAM, measured rather than assumed. */
long long ruleSetBytesFor(size_t count)
{
    const long long before = HostAllocationCounter::liveBytes();
    std::vector<Rule> rules = makeRuleSet(count);
    const long long after = HostAllocationCounter::liveBytes();
    return after - before;
}

/**
 * @brief What building a list of `count` rules costs above holding it.
 *
 * A vector filled one rule at a time reallocates its array as it grows, and both arrays are alive
 * for the length of that copy. Nobody can know the length of a list before reading it, so this is
 * the floor under any reader that answers with a vector - and it belongs in the plank rather than
 * in the verdict, measured on the same rule set the scenario runs on rather than guessed at.
 */
long long listGrowthBytesFor(size_t count)
{
    HostAllocationCounter::resetPeak();
    const long long before = HostAllocationCounter::liveBytes();
    std::vector<Rule> rules;
    for (size_t i = 0; i < count; i++)
    {
        rules.push_back(makeRule(i));
    }
    const long long peak = HostAllocationCounter::peakBytes();
    const long long after = HostAllocationCounter::liveBytes();
    return peak - ((after > before) ? after : before);
}

// --- 1. reading the answer ---------------------------------------------------------------------

/** @brief The transport, with the socket replaced and nothing else. */
struct FetchFixture {
    MockModuleConfig moduleConfig;
    MockSettings settings;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ArduinoJsonConverter jsonConverter;
    SimHttpClient http;
    RestAPIService service;

    FetchFixture()
        : service(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter)
    {
        debugSystem.featureEnabled = false;
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        moduleConfig.setModuleUid(MODULE_UID);
        service.init();
    }
};

/** @brief How many rules the module builds out of an answer of `published` rules. */
size_t rulesTakenFrom(size_t published)
{
    return (published < RuleRepository::MAX_RULES) ? published : RuleRepository::MAX_RULES;
}

PeakMeasurement measureRuleFetch(size_t publishedCount, bool holdOneListInside = false)
{
    FetchFixture fixture;
    const std::string body = ruleListJson(makeRuleSet(publishedCount), fixture.jsonConverter);
    fixture.http.alwaysRespond(200, body.c_str());

    const size_t expected = rulesTakenFrom(publishedCount);

    {
        // One fetch before the measured one. Whatever a first parse allocates once - the JSON
        // document pool takes its slots on first use - belongs to neither rule set size and would
        // be read as a cost of the smaller one.
        bool warmupFailed = false;
        std::vector<Rule> warmup = fixture.service.getRuleList(&warmupFailed);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(expected, warmup.size(),
                                         "the warm-up fetch did not read the rule list");
    }

    PeakMeasurement measurement;
    measurement.ruleBytes = ruleSetBytesFor(1);

    HostAllocationCounter::resetPeak();
    measurement.before = HostAllocationCounter::liveBytes();

    std::vector<Rule> rules;
    {
        // The control: one copy of the rule set, alive only inside the window and released before
        // it closes - the shape of transient this measurement exists to forbid.
        std::vector<Rule> transientCopy;
        if (holdOneListInside)
        {
            transientCopy = makeRuleSet(rulesTakenFrom(publishedCount));
        }

        bool httpFailed = false;
        rules = fixture.service.getRuleList(&httpFailed);
        measurement.peak = HostAllocationCounter::peakBytes();

        TEST_ASSERT_FALSE_MESSAGE(httpFailed, "the rule list fetch failed - the scenario did not "
                                              "happen");
    }

    measurement.after = HostAllocationCounter::liveBytes();
    measurement.ruleCount = rules.size();

    TEST_ASSERT_EQUAL_size_t_MESSAGE(expected, rules.size(),
                                     "the fetch did not take the rules it should have");
    return measurement;
}

// --- 2. applying the answer --------------------------------------------------------------------

/**
 * @brief A module wired the way globals.h wires it, from the socket to the rule file.
 *
 * The fetch runs on the outbox's own task, the way it does on the board, so the measured window
 * below contains the control loop's half of a synchronization and nothing of the network's.
 */
struct SyncFixture {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;

    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    SpiffsRuleDao dao;
    RuleRepository repository;

    SimHttpClient http;
    RestAPIService restApi;
    HybridAPIService api;

    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    SyncRulesUseCase sync;

    SyncFixture()
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          dao(&spiffs, &jsonConverter, &debugSystem, &logger),
          repository(&dao, &timeAdapter, &debugSystem, &logger),
          restApi(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter),
          api(&restApi, nullptr, &moduleConfig, &logger, &debugSystem),
          completions(16),
          // Host stacks, not the device's: a fiber gets exactly the bytes it is asked for, and a
          // host frame is several times a device frame. What is measured here is the heap; the
          // stack watermarks are measured on the board.
          outbox(&api, &scheduler, &completions, &logger, &debugSystem, HOST_TASK_STACK_BYTES),
          sync(&api, &repository, &arduino, &timeAdapter, &jsonConverter, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false;
        timeAdapter.setEpochTime(1787000000);
        moduleConfig.setModuleUid(MODULE_UID);
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        restApi.init();
        sync.setSettings(&settings);
        sync.setOutbox(&outbox);
    }

    /** @brief What the server answers the next pull with. */
    void publish(const std::vector<Rule>& rules)
    {
        http.alwaysRespond(200, ruleListJson(rules, jsonConverter).c_str());
    }

    /**
     * @brief Ask, and let the network task answer.
     *
     * The completion queue also carries the acknowledgement of the previous synchronization, so
     * the rule list is picked out of it rather than assumed to be first.
     */
    void fetch(OutboxCompletion& completion)
    {
        TEST_ASSERT_TRUE(sync.forceSync());
        sync.loop();
        scheduler.runForMs(60000);

        bool gotRuleList = false;
        OutboxCompletion next;
        while (completions.pop(next))
        {
            if (next.kind == OutboxKind::RULE_LIST)
            {
                completion = next;
                gotRuleList = true;
                break;
            }
        }

        TEST_ASSERT_TRUE_MESSAGE(gotRuleList, "the outbox never answered the rule list request");
        TEST_ASSERT_TRUE_MESSAGE(completion.delivered,
                                 "the rule list was not delivered - the scenario did not happen");
    }
};

/**
 * @brief Measure the merge and the acknowledgement: the step the control loop runs on an answer.
 *
 * The rules the server sent are in the outbox's slot and the rules the module holds are in the
 * registry's cache before the window opens, so the figure is what applying one to the other costs
 * on top of them - the rule file on its way to storage included, which is why the plank is named
 * in stored rule sets.
 */
PeakMeasurement measureRuleMerge(size_t ruleCount)
{
    SyncFixture fixture;

    // One synchronization before the measured one, carried to its end: it creates the network
    // task, and it leaves the module holding exactly what the server publishes.
    fixture.publish(makeRuleSet(ruleCount));
    OutboxCompletion warmup;
    fixture.fetch(warmup);
    fixture.sync.onOutboxCompletion(warmup);
    fixture.scheduler.runForMs(60000);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)ruleCount, fixture.repository.count(),
                                  "the first synchronization did not store the rule set");

    // And the answer the measurement is about: the server has edited every rule, so the measured
    // merge does the most work a merge can do.
    fixture.publish(makeRuleSet(ruleCount, 4));
    OutboxCompletion completion;
    fixture.fetch(completion);

    PeakMeasurement measurement;
    measurement.ruleBytes = ruleSetBytesFor(1);

    HostAllocationCounter::resetPeak();
    measurement.before = HostAllocationCounter::liveBytes();

    fixture.sync.onOutboxCompletion(completion);

    measurement.peak = HostAllocationCounter::peakBytes();
    measurement.after = HostAllocationCounter::liveBytes();
    measurement.ruleCount = ruleCount;

    TEST_ASSERT_TRUE_MESSAGE(fixture.sync.getLastSyncStatus(),
                             "the synchronization did not succeed - the scenario did not happen");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)ruleCount, fixture.repository.count(),
                                  "the stored rule set is not the one the server published");

    // The measured window did the work, rather than finding it already done by the warm-up: the
    // rules carry the version the second answer brought.
    const Utils::Optional<Rule> merged = fixture.repository.get(makeRule(0).uid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(merged.has_value(), "the merged rule is not in the registry");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, merged.value().version,
                                  "the measured window did not apply the answer it was given");

    printf("    merge window at %u rules: before %lld, peak %lld, after %lld\n",
           (unsigned)ruleCount, measurement.before, measurement.peak, measurement.after);
    return measurement;
}

/**
 * @brief How much more the overhead may be at three times the rules.
 *
 * One rule, plus the growth of the list itself. Reading the answer costs what it costs for one
 * rule at a time, so sixteen rules more may cost nothing measurable beyond the vector the rules
 * are handed back in - and that vector's growth is measured here rather than allowed for. What
 * this refuses is anything else proportional to the list: the body of the answer, a document
 * holding it, a second copy of the rules. Any of those is 10 KB at the sizes measured here,
 * against a plank of about a tenth of that.
 */
long long fetchGrowthBudget(long long ruleBytes)
{
    return ruleBytes + (listGrowthBytesFor(RuleRepository::MAX_RULES) -
                        listGrowthBytesFor(SMALL_RULE_SET));
}

/**
 * @brief Print a pair of measurements.
 *
 * The counts are the answers the scenario published, not the rules the module built out of them -
 * the two differ once an answer runs past the module's capacity, and the answer is what the
 * measurement is about.
 */
void report(const char* what, size_t smallAnswer, const PeakMeasurement& small,
            size_t largeAnswer, const PeakMeasurement& large)
{
    printf("  rule %s: an answer of %u rules needed %lld bytes above what it held (built %u), an "
           "answer of %u needed %lld (built %u); one rule is %lld bytes\n",
           what,
           (unsigned)smallAnswer, small.overhead(), (unsigned)small.ruleCount,
           (unsigned)largeAnswer, large.overhead(), (unsigned)large.ruleCount,
           large.ruleBytes);
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. Reading the answer. What the transport needs on top of the rules it hands back must be one
//    rule's worth of text and one document - not the body of the answer.
// --------------------------------------------------------------------------------------------

static void test_reading_a_rule_list_costs_one_rule_and_not_the_list(void)
{
    const PeakMeasurement small = measureRuleFetch(SMALL_RULE_SET);
    const PeakMeasurement large = measureRuleFetch(LARGE_RULE_SET);
    report("fetch", SMALL_RULE_SET, small, LARGE_RULE_SET, large);

    const long long growth = large.overhead() - small.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "reading %u rules needed %lld bytes above the rules it built and reading %u needed "
             "%lld - %lld bytes more for %u rules more, which is the answer being held whole "
             "(one rule is %lld bytes)",
             (unsigned)SMALL_RULE_SET, small.overhead(), (unsigned)LARGE_RULE_SET,
             large.overhead(), growth, (unsigned)(LARGE_RULE_SET - SMALL_RULE_SET),
             large.ruleBytes);
    TEST_ASSERT_TRUE_MESSAGE(growth <= fetchGrowthBudget(large.ruleBytes), message);
}

/**
 * And the measurement that is not about the cap: an answer four times longer than the module
 * keeps. Nothing above the rules it takes may follow the length of the answer - not the body, not
 * a document over it, and not the rules past the cap, which are refused before they are built.
 */
static void test_reading_an_answer_past_the_cap_costs_no_more_than_the_cap(void)
{
    const PeakMeasurement small = measureRuleFetch(SMALL_RULE_SET);
    const PeakMeasurement huge = measureRuleFetch(ANSWER_PAST_THE_CAP);
    report("fetch past the cap", SMALL_RULE_SET, small, ANSWER_PAST_THE_CAP, huge);

    const long long growth = huge.overhead() - small.overhead();

    char message[320];
    snprintf(message, sizeof(message),
             "reading an answer of %u rules built %u of them and needed %lld bytes above them, "
             "against %lld for an answer of %u - %lld bytes more, and the plank is %lld (one rule "
             "is %lld bytes)",
             (unsigned)ANSWER_PAST_THE_CAP, (unsigned)huge.ruleCount, huge.overhead(),
             small.overhead(), (unsigned)SMALL_RULE_SET, growth,
             fetchGrowthBudget(huge.ruleBytes), huge.ruleBytes);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(RuleRepository::MAX_RULES, huge.ruleCount,
                                     "the module built more rules than it keeps");
    TEST_ASSERT_TRUE_MESSAGE(growth <= fetchGrowthBudget(huge.ruleBytes), message);
}

/**
 * And the control on the measurement above: a window that cannot see a copy of the list inside it
 * proves nothing by staying green.
 */
static void test_the_fetch_window_sees_a_rule_list_held_inside_it(void)
{
    const PeakMeasurement small = measureRuleFetch(SMALL_RULE_SET, true);
    const PeakMeasurement large = measureRuleFetch(LARGE_RULE_SET, true);
    report("fetch with one rule set held inside the window", SMALL_RULE_SET, small,
           LARGE_RULE_SET, large);

    const long long growth = large.overhead() - small.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "a copy of the rule set held inside the measured window moved it by %lld bytes at %u "
             "rules and %lld at %u - a growth of %lld against a plank of %lld, so the window "
             "cannot see a transient of that size and its verdict means nothing",
             small.overhead(), (unsigned)SMALL_RULE_SET, large.overhead(),
             (unsigned)LARGE_RULE_SET, growth, fetchGrowthBudget(large.ruleBytes));
    TEST_ASSERT_TRUE_MESSAGE(growth > fetchGrowthBudget(large.ruleBytes), message);
}

// --------------------------------------------------------------------------------------------
// 2. Applying the answer. The merge is given the rules the server sent and the rules the module
//    holds; the rule file it writes is one string of the whole set by the stored format, and that
//    is the floor. What it must not need on top of that is another copy of the answer.
// --------------------------------------------------------------------------------------------

static void test_applying_a_rule_snapshot_stays_within_the_rule_file_it_writes(void)
{
    const PeakMeasurement small = measureRuleMerge(SMALL_RULE_SET);
    const PeakMeasurement large = measureRuleMerge(LARGE_RULE_SET);
    printf("  rule merge: %u rules needed %lld bytes above the state the module was in, %u rules "
           "needed %lld (one rule is %lld bytes)\n",
           (unsigned)small.ruleCount, small.neededAboveStart(),
           (unsigned)large.ruleCount, large.neededAboveStart(), large.ruleBytes);

    // This window is not flat and cannot be: the merge ends with the rule file and the registry's
    // cache in hand, and both are the whole rule set. On the way there it validates the answer,
    // decides a rule set out of it and the stored one, and builds the acknowledgement - each of
    // those is another rule set, and they overlap.
    //
    // So what is fenced here is the slope, in rules as this module holds one. Measured at about
    // six and a half rule weights per rule; the fence is at eight, which leaves room for the
    // difference between two standard libraries' strings and none for another copy of the rule
    // set. It is a fence around what the merge costs today, not a budget the design earned - and
    // the number printed above says plainly that applying a snapshot, not reading it, is where the
    // memory of a rule synchronization now goes.
    const long long RULE_WEIGHTS_PER_RULE = 8;
    const long long budget =
        RULE_WEIGHTS_PER_RULE * large.ruleBytes * (long long)(LARGE_RULE_SET - SMALL_RULE_SET);
    const long long growth = large.neededAboveStart() - small.neededAboveStart();

    char message[320];
    snprintf(message, sizeof(message),
             "applying %u rules needed %lld bytes above the state the module was in, applying %u "
             "needed %lld - %lld bytes more for %u rules more (%lld per rule, against a rule of "
             "%lld bytes), and the plank is %lld",
             (unsigned)SMALL_RULE_SET, small.neededAboveStart(), (unsigned)LARGE_RULE_SET,
             large.neededAboveStart(), growth, (unsigned)(LARGE_RULE_SET - SMALL_RULE_SET),
             growth / (long long)(LARGE_RULE_SET - SMALL_RULE_SET), large.ruleBytes, budget);
    TEST_ASSERT_TRUE_MESSAGE(growth <= budget, message);
}

extern "C" void run_rule_sync_memory_peak_tests(void)
{
    printf("\n----- Running Rule Sync Memory Peak Tests -----\n");

    RUN_TEST(test_reading_a_rule_list_costs_one_rule_and_not_the_list);
    RUN_TEST(test_reading_an_answer_past_the_cap_costs_no_more_than_the_cap);
    RUN_TEST(test_the_fetch_window_sees_a_rule_list_held_inside_it);
    RUN_TEST(test_applying_a_rule_snapshot_stays_within_the_rule_file_it_writes);

    printf("\n----- Rule Sync Memory Peak Tests Complete -----\n");
}
