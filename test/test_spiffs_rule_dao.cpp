// The rule store's reading of an empty file, pinned so that a converter change cannot move it.
//
// The list converters have a third outcome so that `[]` from the server stops looking like
// a transport failure. SpiffsRuleDao is the caller that must NOT notice: its jsonToEntities() has
// always meant "there is at least one rule in this file", and SpiffsDao::getAll() passes that
// straight on as its own result. An empty stored file answers false and has to
// keep answering false.
//
// Nothing else tests this. That matters more than the two assertions themselves: a
// defect of this kind arises in exactly this way - a converter's return value changes hands
// and the caller that depends on the old meaning is never exercised. A mutation mapping the
// "parsed, empty" outcome to a successful load passes the entire suite without this file.
//
// Whether an empty rule file *should* count as a successful load is a real question, and a
// separate one from what this file pins: that the answer did not change under it.

#include <unity.h>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include <cstdio>
#include <vector>

namespace {

struct RuleDaoFixture {
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsRuleDao dao;

    RuleDaoFixture() : dao(&spiffs, &jsonConverter, &debugSystem, &logger) {}
};

Rule oneRule(const char *uid)
{
    Rule rule;
    rule.uid = uid;
    rule.version = 1;
    rule.ruleType = RuleType::SENSOR;
    rule.deviceUid = "dev-1";
    rule.sensorUid = "sensor-1";
    rule.mode = RuleMode::OFF;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.direction = RuleDirection::UP;
    rule.state = RuleState::SYNCHRONIZED;
    return rule;
}

} // namespace

void test_spiffs_rule_dao_an_empty_stored_file_is_not_a_successful_load(void)
{
    RuleDaoFixture f;

    std::vector<Rule> none;
    f.dao.saveAll(none); // writes "[]"

    std::vector<Rule> retrieved;
    const bool result = f.dao.getAll(retrieved);

    TEST_ASSERT_FALSE_MESSAGE(result,
                              "an empty stored rule file started reporting a successful load - the "
                              "storage path changed meaning, which the converter change was not "
                              "allowed to do");
    TEST_ASSERT_EQUAL(0, retrieved.size());
}

// So the assertion above cannot be satisfied by a DAO that always fails.
void test_spiffs_rule_dao_a_stored_file_with_one_rule_is_a_successful_load(void)
{
    RuleDaoFixture f;

    std::vector<Rule> one = {oneRule("rule-1")};
    f.dao.saveAll(one);

    std::vector<Rule> retrieved;
    TEST_ASSERT_TRUE(f.dao.getAll(retrieved));
    TEST_ASSERT_EQUAL(1, retrieved.size());
    TEST_ASSERT_EQUAL_STRING("rule-1", retrieved[0].uid.c_str());
}

extern "C" void run_spiffs_rule_dao_tests(void)
{
    printf("\n----- Running SpiffsRuleDao Tests -----\n");

    RUN_TEST(test_spiffs_rule_dao_an_empty_stored_file_is_not_a_successful_load);
    RUN_TEST(test_spiffs_rule_dao_a_stored_file_with_one_rule_is_a_successful_load);

    printf("\n----- SpiffsRuleDao Tests Complete -----\n");
}
