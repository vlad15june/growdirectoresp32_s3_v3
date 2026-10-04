// What the module tells the server about its rules, and what it does with a snapshot it cannot
// store whole.
//
// The acknowledgement the module sends after a rule snapshot is the server's only picture of what
// the module is actually running: the backend takes every rule of it and writes the state, the
// mode and the error type into its own record, and a rule that is *missing* from the report is
// marked failed there. So the report has to describe the module, not repeat the snapshot back:
//
//   - a rule whose sensor stopped answering is in error on the module, and saying "Synchronized"
//     about it hides the one thing the greenhouse owner needs to see;
//   - a rule the registry had no room for is not running at all, and must not be acknowledged as
//     if it were;
//   - and every rule the server sent has to appear, or the backend fails the ones that did not.
//
// The report is therefore built by reading the registry - storage plus the runtime state the
// module holds in RAM - at the moment of sending, rather than from the snapshot that arrived.
//
// What runs here is production code: the real SyncRulesUseCase over the real RuleRepository, the
// real SpiffsRuleDao and the converter the device ships with. Replaced: the flash device, the
// clock and the server.

#include <unity.h>

#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* const DEVICE_A = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";
const char* const SENSOR_A = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";

/** @brief A rule of the shape the server sends, with a GUID-sized uid. */
Rule makeRule(size_t index)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a%04x", (unsigned)index);

    Rule rule;
    rule.uid = uid;
    rule.version = 3;
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

/** @brief A rule this module has no validator for: the server offers types the module does not. */
Rule makeRuleOfAnUnknownType(size_t index)
{
    Rule rule = makeRule(index);
    rule.ruleType = RuleType::UNKNOWN;
    return rule;
}

/** @brief The module, from the server's answer down to the rule file. */
struct SyncStand
{
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockNTPTimeAdapter ntpTimeAdapter;
    MockArduinoAdapter arduino;
    MockSettings settings;
    MockHybridAPIService api;

    SpiffsRuleDao dao;
    RuleRepository repository;
    SyncRulesUseCase sync;

    SyncStand()
        : dao(&spiffs, &jsonConverter, &debugSystem, &logger),
          repository(&dao, &ntpTimeAdapter, &debugSystem, &logger),
          sync(&api, &repository, &arduino, &ntpTimeAdapter, &jsonConverter, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = true;
        ntpTimeAdapter.setEpochTime(1787000000);
        sync.setSettings(&settings);
    }

    /** @brief Put a rule in the file the way an earlier sync left it. */
    void seed(const Rule& rule)
    {
        TEST_ASSERT_TRUE(dao.saveRule(rule));
        repository.refreshCache();
    }

    /** @brief What the server answers with on the next pull. */
    void publish(const std::vector<Rule>& rules) { api.ruleListReturnValue = rules; }

    /** @brief One pull, applied, acknowledged. */
    void runOneSync()
    {
        TEST_ASSERT_TRUE(sync.forceSync());
        sync.loop();
    }

    /** @brief The acknowledgement the module sent. */
    const std::string& report() const { return api.lastRuleStateCommandStr; }
};

/** @brief How many times a needle appears in the report. */
int occurrences(const std::string& haystack, const std::string& needle)
{
    int count = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
    {
        count++;
    }
    return count;
}

/** @brief The entry the report makes for one rule, or "" when the rule is not in it. */
std::string reportEntryFor(const std::string& report, const std::string& uid)
{
    const size_t at = report.find("\"uid\":\"" + uid + "\"");
    if (at == std::string::npos)
    {
        return "";
    }
    const size_t end = report.find('}', at);
    return report.substr(at, (end == std::string::npos) ? std::string::npos : end - at + 1);
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The report describes the module, not the snapshot
// --------------------------------------------------------------------------------------------

// The rule the server asks about is in error because its sensor stopped answering. The module
// knows that; the report is where the server learns it.
static void test_the_state_report_carries_the_live_rule_state(void)
{
    SyncStand stand;
    stand.seed(makeRule(0));
    const std::string uid = makeRule(0).uid;

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(uid.c_str(), RuleState::ERROR,
                                                      "sensor_unreachable", "sensor_unreachable"));

    // The routine pull: the same rule, at the same version, as the server holds it.
    stand.publish(std::vector<Rule>(1, makeRule(0)));
    stand.runOneSync();

    const std::string entry = reportEntryFor(stand.report(), uid);
    char message[288];
    snprintf(message, sizeof(message),
             "the module reported %s for a rule whose sensor it cannot reach; the server writes "
             "that into its own record and the greenhouse owner is shown a rule that is working",
             entry.empty() ? "nothing at all" : entry.c_str());

    TEST_ASSERT_FALSE_MESSAGE(entry.empty(), message);
    TEST_ASSERT_TRUE_MESSAGE(entry.find("\"state\":\"Error\"") != std::string::npos, message);
    TEST_ASSERT_TRUE_MESSAGE(entry.find("\"errorType\":\"sensor_unreachable\"") != std::string::npos,
                             message);
    TEST_ASSERT_TRUE_MESSAGE(entry.find("\"version\":3") != std::string::npos,
                             "the report named a version the backend will not match, and a state "
                             "update with the wrong version is discarded there");
}

// And the other direction: a rule that is doing what it was told is reported synchronized, with
// no error type invented for it.
static void test_a_healthy_rule_is_still_reported_synchronized(void)
{
    SyncStand stand;
    stand.seed(makeRule(0));
    const std::string uid = makeRule(0).uid;

    stand.publish(std::vector<Rule>(1, makeRule(0)));
    stand.runOneSync();

    const std::string entry = reportEntryFor(stand.report(), uid);
    TEST_ASSERT_FALSE(entry.empty());
    TEST_ASSERT_TRUE_MESSAGE(entry.find("\"state\":\"Synchronized\"") != std::string::npos,
                             "a rule the module is running was not reported as synchronized");
    TEST_ASSERT_TRUE_MESSAGE(entry.find("errorType") == std::string::npos,
                             "an error type was reported for a rule that has none");
    TEST_ASSERT_TRUE_MESSAGE(entry.find("\"mode\":\"On\"") != std::string::npos,
                             "the mode the module holds for the rule was not reported");
    TEST_ASSERT_TRUE_MESSAGE(stand.report().find("\"status\":\"Success\"") != std::string::npos,
                             "a snapshot the module applied was reported as an error");
}

// A rule the module could not validate keeps its validation verdict: that is what the server has
// to see about a rule it published in a shape this module has no reader for.
static void test_a_rule_the_module_rejected_is_reported_with_its_validation_error(void)
{
    SyncStand stand;
    stand.publish(std::vector<Rule>(1, makeRuleOfAnUnknownType(0)));
    stand.runOneSync();

    const std::string entry = reportEntryFor(stand.report(), makeRule(0).uid);
    TEST_ASSERT_FALSE_MESSAGE(entry.empty(), "the rejected rule was not reported at all");
    TEST_ASSERT_TRUE(entry.find("\"state\":\"Error\"") != std::string::npos);
    TEST_ASSERT_TRUE(entry.find("\"errorType\":\"wrong_format\"") != std::string::npos);
    TEST_ASSERT_TRUE_MESSAGE(stand.report().find("\"status\":\"Error\"") != std::string::npos,
                             "a snapshot the module could not use was reported as a success");
}

// --------------------------------------------------------------------------------------------
// 2. Every rule the server sent appears in the report
// --------------------------------------------------------------------------------------------

// A rule missing from the acknowledgement is not a rule the server has no news about: the backend
// substitutes an error for it. So the report is as long as the snapshot, at every size the module
// can hold.
static void test_the_state_report_names_every_rule_the_server_sent(void)
{
    SyncStand stand;

    std::vector<Rule> published;
    for (size_t i = 0; i < RuleRepository::MAX_RULES; i++)
    {
        published.push_back(makeRule(i));
    }
    stand.publish(published);
    stand.runOneSync();

    char message[256];
    snprintf(message, sizeof(message),
             "the module acknowledged %d of the %u rules the server sent; the backend marks every "
             "rule missing from the report failed",
             occurrences(stand.report(), "\"uid\":\""), (unsigned)published.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)published.size(),
                                  occurrences(stand.report(), "\"uid\":\""), message);

    for (size_t i = 0; i < published.size(); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(reportEntryFor(stand.report(), published[i].uid).empty(),
                                  message);
    }
}

// --------------------------------------------------------------------------------------------
// 3. The registry has a hard capacity, and reaching it refuses one rule rather than losing all
// --------------------------------------------------------------------------------------------

static void test_a_snapshot_past_the_registry_capacity_refuses_the_extra_rules(void)
{
    SyncStand stand;

    const size_t excess = 3;
    std::vector<Rule> published;
    for (size_t i = 0; i < RuleRepository::MAX_RULES + excess; i++)
    {
        published.push_back(makeRule(i));
    }
    stand.publish(published);
    stand.runOneSync();

    char message[256];
    snprintf(message, sizeof(message),
             "a snapshot of %u rules left the registry holding %d against a capacity of %u, with "
             "%u refusals counted",
             (unsigned)published.size(), stand.repository.count(),
             (unsigned)RuleRepository::MAX_RULES,
             (unsigned)stand.repository.refusedRuleCount());

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)RuleRepository::MAX_RULES, stand.repository.count(),
                                  message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)excess, stand.repository.refusedRuleCount(),
                                     message);

    // The rules that did fit are the rules the module runs: the refusal costs the extra rule, not
    // the snapshot.
    for (size_t i = 0; i < RuleRepository::MAX_RULES; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(stand.repository.exists(published[i].uid.c_str()), message);
    }

    // And what does not fit is not acknowledged as if it did.
    for (size_t i = RuleRepository::MAX_RULES; i < published.size(); i++)
    {
        const std::string entry = reportEntryFor(stand.report(), published[i].uid);
        TEST_ASSERT_FALSE_MESSAGE(entry.empty(),
                                  "a rule the registry refused was left out of the report, which "
                                  "is how the backend is told nothing at all");
        TEST_ASSERT_TRUE_MESSAGE(entry.find("\"state\":\"Error\"") != std::string::npos,
                                 "a rule the module does not hold was acknowledged as if it were "
                                 "running");
    }
}

// A rule already stored keeps its place: the capacity refuses what arrives, never what the module
// is already running.
static void test_a_full_registry_still_applies_the_rules_it_holds(void)
{
    SyncStand stand;

    std::vector<Rule> published;
    for (size_t i = 0; i < RuleRepository::MAX_RULES; i++)
    {
        published.push_back(makeRule(i));
    }
    stand.publish(published);
    stand.runOneSync();
    TEST_ASSERT_EQUAL_INT((int)RuleRepository::MAX_RULES, stand.repository.count());

    // The user switches the first rule off and adds one more. The registry is full, so the new
    // rule is refused - and the edit to the rule the module already has still lands.
    published[0].mode = RuleMode::OFF;
    published.push_back(makeRule(RuleRepository::MAX_RULES));
    stand.publish(published);
    stand.runOneSync();

    Utils::Optional<Rule> edited = stand.repository.get(published[0].uid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(edited.has_value(), "the rule the module was running was dropped");
    TEST_ASSERT_TRUE_MESSAGE(edited.value().mode == RuleMode::OFF,
                             "a full registry stopped the module from switching a rule off");
    TEST_ASSERT_EQUAL_INT((int)RuleRepository::MAX_RULES, stand.repository.count());
}

// --------------------------------------------------------------------------------------------
// 4. A synchronization that stored nothing is not a failed synchronization
// --------------------------------------------------------------------------------------------

// The account's rules are all of a type this module has no reader for. Nothing is stored, the
// server is told exactly that for every one of them - and the exchange itself completed. Calling
// it a failed synchronization says the module and the server did not speak, which is the opposite
// of what happened, and it holds back the last-successful-sync stamp for ever.
static void test_a_snapshot_the_module_stores_nothing_from_is_not_a_failed_sync(void)
{
    SyncStand stand;
    std::vector<Rule> published;
    published.push_back(makeRuleOfAnUnknownType(0));
    published.push_back(makeRuleOfAnUnknownType(1));
    stand.publish(published);

    stand.runOneSync();

    char message[224];
    snprintf(message, sizeof(message),
             "a snapshot of %u rules the module has no reader for was recorded as a failed "
             "synchronization (lastSyncStatus=%d), and the report the server got named %d of them",
             (unsigned)published.size(), stand.sync.getLastSyncStatus() ? 1 : 0,
             occurrences(stand.report(), "\"uid\":\""));

    TEST_ASSERT_TRUE_MESSAGE(stand.sync.getLastSyncStatus(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, occurrences(stand.report(), "\"uid\":\""), message);
    TEST_ASSERT_TRUE_MESSAGE(stand.sync.getLastSyncTime() != 0,
                             "the successful exchange did not move the last-sync stamp");
}

// An empty snapshot from a server that answered has always been an answer: the module keeps what
// it has and the synchronization succeeds. Pinned next to the case above, because the two are the
// same statement about a module that stored nothing.
static void test_an_empty_snapshot_is_still_a_successful_sync(void)
{
    SyncStand stand;
    stand.seed(makeRule(0));
    stand.publish(std::vector<Rule>());

    stand.runOneSync();

    TEST_ASSERT_TRUE_MESSAGE(stand.sync.getLastSyncStatus(),
                             "an empty answer from a reachable server was recorded as a failure");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stand.repository.count(),
                                  "an empty answer deleted the rules the module had");
}

// The rule file does not read back when a snapshot arrives. The merge is not run on what the failed
// read left: nothing is stored, the synchronization is recorded as failed - the next one, five
// minutes on, tries again - and the server is told the rule it sent is not running, rather than
// "Synchronized".
static void test_a_sync_whose_stored_rules_did_not_read_back_stores_nothing_and_says_so(void)
{
    SyncStand stand;
    stand.seed(makeRule(0));
    stand.seed(makeRule(1));
    // A write through the repository: the cache is read back from the file on the next access.
    TEST_ASSERT_TRUE(stand.repository.save(makeRule(1)));
    const std::string stored = stand.spiffs.getFileContent("/data/rules.json");

    stand.api.getRuleListReadWhole = false;
    stand.publish(std::vector<Rule>(1, makeRule(5)));
    stand.spiffs.refuseNextReads(1000);
    stand.runOneSync();
    stand.spiffs.refuseNextReads(0);

    const std::string entry = reportEntryFor(stand.report(), makeRule(5).uid);
    printf("  rules: a synchronization whose rule file did not read back was %s, and reported %s\n",
           stand.sync.getLastSyncStatus() ? "successful" : "failed",
           entry.empty() ? "nothing" : entry.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(stored.c_str(), stand.spiffs.getFileContent("/data/rules.json").c_str(),
                                     "a synchronization was merged into a cache a failed read left");
    TEST_ASSERT_FALSE_MESSAGE(stand.sync.getLastSyncStatus(),
                              "a synchronization that stored nothing was recorded as successful");
    TEST_ASSERT_TRUE_MESSAGE(entry.find("\"state\":\"Error\"") != std::string::npos &&
                                 entry.find("\"errorType\":\"sync_failed\"") != std::string::npos,
                             "the server was not told the rule it sent is not running");
}

extern "C" void run_rule_sync_reporting_tests(void)
{
    printf("\n----- Running Rule Sync Reporting Tests -----\n");

    RUN_TEST(test_the_state_report_carries_the_live_rule_state);
    RUN_TEST(test_a_healthy_rule_is_still_reported_synchronized);
    RUN_TEST(test_a_rule_the_module_rejected_is_reported_with_its_validation_error);
    RUN_TEST(test_the_state_report_names_every_rule_the_server_sent);
    RUN_TEST(test_a_snapshot_past_the_registry_capacity_refuses_the_extra_rules);
    RUN_TEST(test_a_full_registry_still_applies_the_rules_it_holds);
    RUN_TEST(test_a_snapshot_the_module_stores_nothing_from_is_not_a_failed_sync);
    RUN_TEST(test_an_empty_snapshot_is_still_a_successful_sync);
    RUN_TEST(test_a_sync_whose_stored_rules_did_not_read_back_stores_nothing_and_says_so);

    printf("\n----- Rule Sync Reporting Tests Complete -----\n");
}
