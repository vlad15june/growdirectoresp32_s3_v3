// Reading the rule list off the socket instead of out of one 16 KB document.
//
// Every path that reads a list of rules - the periodic pull from the server and the rule file on
// flash - went through one PooledJsonDocument(16384) holding the whole array, plus a second
// document per element, plus a third the element was re-parsed into. Two things follow from that
// shape, and both are what this file is about:
//
//   - the cost of reading an answer is the length of the answer, at the one moment the module is
//     also holding the rules it already has and the rules it is building;
//   - and the document is a wall rather than a limit: a rule set that does not fit yields *no
//     rules at all*, not the rules that did fit. The module then reads the answer as unusable, and
//     a rule file one rule too long comes back as an empty registry.
//
// The array is cut into its objects as the body arrives and each object is parsed on its own, so
// what is alive is one rule. A rule this module cannot parse is left out and counted; the rules
// around it stand. A body that stops in the middle is still the whole answer failing - a snapshot
// is authoritative, and half of one deletes the rules that did not arrive.
//
// The bodies below are read at several piece sizes, including one byte, because where a piece
// boundary falls is the socket's business.

#include <unity.h>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Json/RuleStreamReader.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/SimHttpClient.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::SimHttpClient;

namespace {

const char* const SERVER_URL = "http://gd.test";
const char* const TOKEN = "TOK123";
const char* const MODULE_UID = "UID-1";

// A document that holds the whole array is 16 384 bytes and a rule costs about 420 of
// them whatever it contains, so the twenty-first rule of the smallest shape the format can express
// would end the parse. Both numbers below are above that line: the first is one rule past it,
// the second is a rule set no such document could ever hold.
const size_t ONE_PAST_THE_DOCUMENT = 21;
const size_t FAR_PAST_THE_DOCUMENT = 60;

/** @brief A rule of the shape the server sends, with a GUID-sized uid. */
Rule makeRule(size_t index)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a%04x", (unsigned)index);

    Rule rule;
    rule.uid = uid;
    rule.version = 3;
    rule.deviceUid = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";
    rule.sensorUid = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";
    rule.ruleType = RuleType::SENSOR;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.timeToSetPoint = 10;
    return rule;
}

/**
 * @brief A schedule rule with `slots` windows in it - the shape that makes one rule large.
 *
 * A window is ~50 bytes of JSON, so this is the rule that grows without the account owning more
 * rules: an irrigation schedule with a window per event.
 */
Rule makeScheduleRule(size_t slots)
{
    Rule rule;
    rule.uid = "5d8e2f40-9a16-4c73-8e91-4f7a0b2c3d56";
    rule.version = 4;
    rule.deviceUid = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";
    rule.ruleType = RuleType::SCHEDULE;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    for (size_t i = 0; i < slots; i++)
    {
        rule.schedule.push_back(ScheduleItem(1787000000 + (time_t)i * 3600, 600));
    }
    return rule;
}

/**
 * @brief A schedule rule as the backend puts it on the wire: only the members it sends.
 *
 * The point of building it by hand rather than through ruleToJson(): the two forms of one rule
 * are different sizes, and everything in this file that is about limits is about which form the
 * limit was measured on.
 */
std::string wireScheduleRuleJson(size_t slots)
{
    std::string json =
        "{\"uid\":\"5d8e2f40-9a16-4c73-8e91-4f7a0b2c3d56\",\"version\":4,"
        "\"deviceUid\":\"7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71\",\"ruleType\":\"schedule\","
        "\"mode\":\"On\",\"state\":\"Synchronized\",\"schedule\":[";
    for (size_t i = 0; i < slots; i++)
    {
        if (i > 0)
        {
            json += ",";
        }
        char item[96];
        struct tm timeinfo;
        const time_t startAt = 1787000000 + (time_t)i * 3600;
        gmtime_r(&startAt, &timeinfo);
        char iso[32];
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        snprintf(item, sizeof(item), "{\"startAt\":\"%s\",\"duration\":600}", iso);
        json += item;
    }
    json += "]}";
    return json;
}

/** @brief The list as the backend puts it on the wire. */
std::string ruleListJson(size_t count, IJsonConverter& converter)
{
    std::string body = "[";
    for (size_t i = 0; i < count; i++)
    {
        if (i > 0)
        {
            body += ",";
        }
        body += converter.ruleToJson(makeRule(i));
    }
    body += "]";
    return body;
}

/**
 * @brief An object of the right shape that no reader of this module can turn into a rule.
 *
 * Nested past the parser's depth limit, so it is the single element that is unreadable - not the
 * text around it. Everything the cutting needs to see is intact: it is one balanced object.
 */
std::string unreadableRuleJson()
{
    std::string json = "{\"uid\":\"deep\",\"conditions\":";
    const int depth = 24;
    for (int i = 0; i < depth; i++)
    {
        json += "[";
    }
    json += "1";
    for (int i = 0; i < depth; i++)
    {
        json += "]";
    }
    json += "}";
    return json;
}

/** @brief How many times a needle appears in a string. */
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

/** @brief The transport, with the socket replaced and nothing else. */
struct TransportFixture {
    MockModuleConfig moduleConfig;
    MockSettings settings;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ArduinoJsonConverter jsonConverter;
    SimHttpClient http;
    RestAPIService service;

    TransportFixture()
        : service(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter)
    {
        debugSystem.featureEnabled = true;
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        moduleConfig.setModuleUid(MODULE_UID);
        service.init();
        http.alwaysRespond(-1, "");
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The list is as long as the account's rule set, not as long as a document
// --------------------------------------------------------------------------------------------

static void test_a_rule_list_longer_than_one_document_is_read_whole(void)
{
    ArduinoJsonConverter converter;

    const std::string body = ruleListJson(ONE_PAST_THE_DOCUMENT, converter);
    std::vector<Rule> rules;
    const JsonListParse verdict = converter.rulesFromJson(body, rules);

    char message[256];
    snprintf(message, sizeof(message),
             "%u rules of %u bytes came back as %d rules: a list one rule too long for the "
             "document would yield nothing at all, which is a module reading its own rule file "
             "as an empty registry",
             (unsigned)ONE_PAST_THE_DOCUMENT, (unsigned)body.size(), (int)rules.size());

    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Ok), static_cast<int>(verdict),
                                  message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(ONE_PAST_THE_DOCUMENT, rules.size(), message);

    // And far past it, because a wall one rule beyond the last measurement is still a wall.
    const std::string longBody = ruleListJson(FAR_PAST_THE_DOCUMENT, converter);
    std::vector<Rule> manyRules;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(JsonListParse::Ok),
                          static_cast<int>(converter.rulesFromJson(longBody, manyRules)));
    TEST_ASSERT_EQUAL_size_t(FAR_PAST_THE_DOCUMENT, manyRules.size());

    // Every rule came back whole, not just counted.
    TEST_ASSERT_EQUAL_STRING(makeRule(FAR_PAST_THE_DOCUMENT - 1).uid.c_str(),
                             manyRules[FAR_PAST_THE_DOCUMENT - 1].uid.c_str());
    TEST_ASSERT_EQUAL_INT(3, manyRules[FAR_PAST_THE_DOCUMENT - 1].version);
    TEST_ASSERT_TRUE(manyRules[FAR_PAST_THE_DOCUMENT - 1].ruleType == RuleType::SENSOR);
    TEST_ASSERT_EQUAL_INT(20, (int)manyRules[FAR_PAST_THE_DOCUMENT - 1].valueHigh);
    TEST_ASSERT_EQUAL_INT(10, (int)manyRules[FAR_PAST_THE_DOCUMENT - 1].valueLow);
}

// A rule the module cannot read is one rule missing, not a list missing. Failing the whole answer
// over one element would stop every other rule of the account from ever being updated again.
static void test_a_rule_the_module_cannot_read_is_left_out_and_the_rest_stand(void)
{
    ArduinoJsonConverter converter;

    const std::string body = "[" + converter.ruleToJson(makeRule(0)) + "," + unreadableRuleJson() +
                             "," + converter.ruleToJson(makeRule(1)) + "]";

    std::vector<Rule> rules;
    const JsonListParse verdict = converter.rulesFromJson(body, rules);

    char message[224];
    snprintf(message, sizeof(message),
             "a list of three elements, one of them unreadable, yielded %d rules and the verdict "
             "%d: the two readable rules must survive their neighbour",
             (int)rules.size(), (int)verdict);

    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Ok), static_cast<int>(verdict),
                                  message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, rules.size(), message);
    TEST_ASSERT_EQUAL_STRING(makeRule(0).uid.c_str(), rules[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING(makeRule(1).uid.c_str(), rules[1].uid.c_str());
}

// --------------------------------------------------------------------------------------------
// 2. The transport reads the same list whatever the socket hands it in
// --------------------------------------------------------------------------------------------

static void test_the_transport_reads_the_same_rule_list_at_any_piece_size(void)
{
    // One byte (every boundary there is), a prime that lands inside keys and values, the ~1.4 KB
    // the framework reads a socket in, and the whole body at once.
    const size_t pieceSizes[] = {1, 7, 64, 1460, 0};

    TransportFixture f;
    const std::string body = ruleListJson(ONE_PAST_THE_DOCUMENT, f.jsonConverter);
    f.http.alwaysRespond(200, body.c_str());

    for (size_t i = 0; i < sizeof(pieceSizes) / sizeof(pieceSizes[0]); i++)
    {
        f.http.setBodyPieceBytes(pieceSizes[i]);

        bool httpFailed = true;
        const std::vector<Rule> rules = f.service.getRuleList(&httpFailed);

        char message[224];
        snprintf(message, sizeof(message),
                 "the transport read %d of %u rules when the body arrived in %u-byte pieces",
                 (int)rules.size(), (unsigned)ONE_PAST_THE_DOCUMENT, (unsigned)pieceSizes[i]);
        TEST_ASSERT_FALSE_MESSAGE(httpFailed, message);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(ONE_PAST_THE_DOCUMENT, rules.size(), message);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(makeRule(0).uid.c_str(), rules[0].uid.c_str(), message);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            makeRule(ONE_PAST_THE_DOCUMENT - 1).uid.c_str(),
            rules[ONE_PAST_THE_DOCUMENT - 1].uid.c_str(), message);
    }
}

// The three answers this endpoint has to keep apart, over the transport: a list, "this module has
// no rules", and an answer the module could not read. The middle one is not a failure - reporting
// it as one made a module with no rules spend a full retry budget every five minutes for ever -
// and the last one is not an empty list, because an empty snapshot is authoritative.
static void test_the_transport_keeps_an_empty_list_apart_from_an_unusable_answer(void)
{
    TransportFixture f;

    f.http.alwaysRespond(200, "[]");
    bool httpFailed = true;
    std::vector<Rule> rules = f.service.getRuleList(&httpFailed);
    TEST_ASSERT_FALSE_MESSAGE(httpFailed, "an empty rule list was reported as a transport failure");
    TEST_ASSERT_EQUAL_size_t(0, rules.size());

    f.http.alwaysRespond(200, "");
    httpFailed = false;
    rules = f.service.getRuleList(&httpFailed);
    TEST_ASSERT_TRUE_MESSAGE(httpFailed, "a 200 with no body was taken for an empty rule list");
    TEST_ASSERT_EQUAL_size_t(0, rules.size());

    // A body that stops inside a rule: the rules before the cut are the beginning of a snapshot,
    // and handing them on deletes every rule that did not arrive.
    const std::string cut = ruleListJson(3, f.jsonConverter).substr(0, 200);
    f.http.alwaysRespond(200, cut.c_str());
    httpFailed = false;
    rules = f.service.getRuleList(&httpFailed);
    TEST_ASSERT_TRUE_MESSAGE(httpFailed, "a truncated snapshot was accepted as a rule list");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, rules.size(),
                                     "the rules read before the body stopped were kept");
}

// A rule left out of the answer is a rule the module will not run, so it is not left out quietly.
static void test_a_skipped_rule_is_counted_in_the_log(void)
{
    TransportFixture f;
    const std::string body = "[" + f.jsonConverter.ruleToJson(makeRule(0)) + "," +
                             unreadableRuleJson() + "]";
    f.http.alwaysRespond(200, body.c_str());

    bool httpFailed = true;
    const std::vector<Rule> rules = f.service.getRuleList(&httpFailed);

    TEST_ASSERT_FALSE(httpFailed);
    TEST_ASSERT_EQUAL_size_t(1, rules.size());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("could not be read and were skipped"),
                             "a rule the module could not read was left out without a word");
}

// --------------------------------------------------------------------------------------------
// 3. And the same list on the way back to flash
// --------------------------------------------------------------------------------------------

// The rule file is written through the same converter that reads it. A writer that stops at a
// document boundary loses rules on the way to storage instead of on the way back, which is worse:
// nothing above it can tell.
static void test_a_rule_set_larger_than_one_document_survives_the_file(void)
{
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = false;
    SpiffsRuleDao dao(&spiffs, &converter, &debugSystem, &logger);

    std::vector<Rule> rules;
    for (size_t i = 0; i < ONE_PAST_THE_DOCUMENT; i++)
    {
        rules.push_back(makeRule(i));
    }

    TEST_ASSERT_TRUE_MESSAGE(dao.saveAll(rules), "the rule set was not written at all");

    std::vector<Rule> readBack;
    TEST_ASSERT_TRUE(dao.getAll(readBack));

    char message[224];
    snprintf(message, sizeof(message),
             "%u rules were written and %u came back: a rule set the module cannot write whole is "
             "a rule set the module loses on the next boot",
             (unsigned)rules.size(), (unsigned)readBack.size());
    TEST_ASSERT_EQUAL_size_t_MESSAGE(ONE_PAST_THE_DOCUMENT, readBack.size(), message);
}

// One rule can be large without the account owning more rules, and the two halves of the storage
// path have to agree about it: what the module writes, it has to be able to read back. A rule
// written short is a schedule missing irrigation windows; a rule written whole and read back as
// nothing is a registry that comes up empty - and an empty registry is not just rules missing, it
// is the sweep that switches devices off having nothing to work from.
static void test_a_large_rule_survives_the_file_whole(void)
{
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = false;
    SpiffsRuleDao dao(&spiffs, &converter, &debugSystem, &logger);

    const size_t SLOTS = 40;
    const Rule large = makeScheduleRule(SLOTS);

    // The writing side first: the file has to carry every window of the schedule.
    const std::string written = converter.ruleToJson(large);
    char writeMessage[224];
    snprintf(writeMessage, sizeof(writeMessage),
             "a schedule of %u windows was serialized into %d bytes holding %d of them: a rule "
             "written short is a schedule the greenhouse will not run",
             (unsigned)SLOTS, (int)written.size(), occurrences(written, "\"startAt\""));
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)SLOTS, occurrences(written, "\"startAt\""), writeMessage);

    // And the reading side: the module stores it and reads it back with everything in it.
    TEST_ASSERT_TRUE(dao.saveRule(large));

    std::vector<Rule> readBack;
    char readMessage[224];
    const bool loaded = dao.getAll(readBack);
    snprintf(readMessage, sizeof(readMessage),
             "the registry read %d rules back out of a file holding one rule of %d bytes: a file "
             "the module cannot read is a registry that comes up empty",
             (int)readBack.size(), (int)written.size());
    TEST_ASSERT_TRUE_MESSAGE(loaded, readMessage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)readBack.size(), readMessage);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)SLOTS, (int)readBack[0].schedule.size(), readMessage);
    TEST_ASSERT_EQUAL_STRING(large.uid.c_str(), readBack[0].uid.c_str());
    TEST_ASSERT_EQUAL_INT(large.version, readBack[0].version);
}

// The promise between the two forms of one rule, walked to the edge of what the module accepts.
//
// A rule has two sizes: the one it arrives in and the one it is stored in, and they are not the
// same number - the stored form carries members the server does not send. So a limit measured on
// the arriving form says nothing about the stored one, and the only statement worth making is
// end to end: every rule the module takes off the wire, it stores, and reads back whole.
//
// The shapes are walked up to and past the point where the module stops accepting them, and the
// test asserts on what the registry holds - never on a guard that decides which shapes count.
static void test_every_rule_the_module_takes_reads_back_out_of_the_file(void)
{
    ArduinoJsonConverter converter;

    int accepted = 0;
    int overCap = 0;
    for (size_t slots = 8; slots <= 96; slots += 4)
    {
        // The rule as the backend puts it on the wire: only the members it sends.
        const std::string wire = wireScheduleRuleJson(slots);

        std::vector<Rule> taken;
        {
            RuleStreamReader reader(converter, taken, RuleLimits::MAX_RULES_PER_MODULE);
            const std::string body = "[" + wire + "]";
            reader.feed(body.data(), body.size());
            reader.finish();
        }
        if (taken.empty())
        {
            // Past what the module accepts: refused at intake, which is a decision and not a
            // failure. Nothing is stored, so there is nothing to read back.
            continue;
        }
        accepted++;

        MockSpiffsAdapter spiffs;
        MockLogger logger;
        MockDebugSystem debugSystem;
        debugSystem.featureEnabled = false;
        SpiffsRuleDao dao(&spiffs, &converter, &debugSystem, &logger);

        const std::string stored = converter.ruleToJson(taken[0]);
        if (stored.size() > RuleStreamReader::MAX_RULE_BYTES)
        {
            overCap++;
        }

        // The chain that keeps the file readable at all: the stored form of the largest rule the
        // module takes still has to be an object the reader will cut out of a body at all.
        char runMessage[224];
        snprintf(runMessage, sizeof(runMessage),
                 "a rule taken at %d bytes is stored as %d, past the %d bytes an object of a rule "
                 "list may run to",
                 (int)wire.size(), (int)stored.size(), (int)RuleStreamReader::MAX_OBJECT_BYTES);
        TEST_ASSERT_TRUE_MESSAGE(stored.size() < RuleStreamReader::MAX_OBJECT_BYTES, runMessage);

        TEST_ASSERT_TRUE(dao.saveRule(taken[0]));

        std::vector<Rule> readBack;
        const bool loaded = dao.getAll(readBack);

        char message[288];
        snprintf(message, sizeof(message),
                 "a schedule rule of %u windows arrives in %d bytes, is taken, is stored as %d "
                 "bytes - and %d rules come back out of the file: what the module stores it has "
                 "to be able to read, or the registry comes up empty and nothing switches the "
                 "device off",
                 (unsigned)slots, (int)wire.size(), (int)stored.size(), (int)readBack.size());
        TEST_ASSERT_TRUE_MESSAGE(loaded, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)readBack.size(), message);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)slots, (int)readBack[0].schedule.size(), message);
    }

    // The walk has to have covered both sides of the interesting line, or it proves nothing: some
    // shapes the module takes, and among them some whose stored form is past the intake limit.
    char coverage[224];
    snprintf(coverage, sizeof(coverage),
             "the walk took %d shapes, %d of them stored above the intake limit of %d bytes",
             accepted, overCap, (int)RuleStreamReader::MAX_RULE_BYTES);
    TEST_ASSERT_TRUE_MESSAGE(accepted > 0, coverage);
    TEST_ASSERT_TRUE_MESSAGE(overCap > 0, coverage);
}

// And the same rule with ordinary neighbours in the file, because a rule set is how a rule is
// stored: a rule the module cannot read back does not merely go missing on its own - it takes the
// whole registry with it, and then quietly disappears from the file on the next write.
static void test_a_rule_stored_next_to_others_does_not_take_the_registry_with_it(void)
{
    ArduinoJsonConverter converter;
    MockSpiffsAdapter spiffs;
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = false;
    SpiffsRuleDao dao(&spiffs, &converter, &debugSystem, &logger);
    MockNTPTimeAdapter ntpTimeAdapter;
    RuleRepository repository(&dao, &ntpTimeAdapter, &debugSystem, &logger);

    // The largest schedule the module still takes off the wire.
    std::vector<Rule> taken;
    size_t slotsTaken = 0;
    for (size_t slots = 96; slots >= 8 && taken.empty(); slots -= 4)
    {
        const std::string body = "[" + wireScheduleRuleJson(slots) + "]";
        RuleStreamReader reader(converter, taken, RuleLimits::MAX_RULES_PER_MODULE);
        reader.feed(body.data(), body.size());
        reader.finish();
        slotsTaken = slots;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)taken.size(),
                                  "no schedule shape was taken at all - the walk found nothing");

    std::vector<Rule> stored;
    stored.push_back(makeRule(0));
    stored.push_back(taken[0]);
    stored.push_back(makeRule(1));
    TEST_ASSERT_TRUE(dao.saveAll(stored));

    // What a module that has just booted on this file holds.
    std::vector<Rule> readBack;
    dao.getAll(readBack);

    char message[288];
    snprintf(message, sizeof(message),
             "three rules were stored, one of them a schedule of %u windows (%d bytes stored), "
             "and %d came back: the registry is what the evaluation tick works from, and an empty "
             "one switches nothing off",
             (unsigned)slotsTaken, (int)converter.ruleToJson(taken[0]).size(),
             (int)readBack.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)readBack.size(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, repository.count(), message);
    TEST_ASSERT_TRUE_MESSAGE(repository.exists(taken[0].uid.c_str()), message);
}

// The file format itself does not move: the array is the rules, separated by commas, in the order
// they were given, and each rule is exactly what ruleToJson() makes of it.
static void test_the_rule_file_is_written_byte_for_byte_as_before(void)
{
    ArduinoJsonConverter converter;

    std::vector<Rule> rules;
    rules.push_back(makeRule(0));
    rules.push_back(makeRule(1));

    const std::string expected =
        "[" + converter.ruleToJson(rules[0]) + "," + converter.ruleToJson(rules[1]) + "]";
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.c_str(), converter.rulesToJson(rules).c_str(),
                                     "the stored rule file is not the rules it is made of");

    TEST_ASSERT_EQUAL_STRING("[]", converter.rulesToJson(std::vector<Rule>()).c_str());

    // And the file the module writes is a file the module reads back: the rules return whole and
    // in the order they were written. This is the round trip a reboot makes.
    std::vector<Rule> readBack;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(JsonListParse::Ok),
                          static_cast<int>(converter.rulesFromJson(converter.rulesToJson(rules),
                                                                   readBack)));
    TEST_ASSERT_EQUAL_size_t(2, readBack.size());
    TEST_ASSERT_EQUAL_STRING(rules[0].uid.c_str(), readBack[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING(rules[1].uid.c_str(), readBack[1].uid.c_str());
}

extern "C" void run_rule_stream_read_tests(void)
{
    printf("\n----- Running Rule Stream Read Tests -----\n");

    RUN_TEST(test_a_rule_list_longer_than_one_document_is_read_whole);
    RUN_TEST(test_a_rule_the_module_cannot_read_is_left_out_and_the_rest_stand);
    RUN_TEST(test_the_transport_reads_the_same_rule_list_at_any_piece_size);
    RUN_TEST(test_the_transport_keeps_an_empty_list_apart_from_an_unusable_answer);
    RUN_TEST(test_a_skipped_rule_is_counted_in_the_log);
    RUN_TEST(test_a_rule_set_larger_than_one_document_survives_the_file);
    RUN_TEST(test_a_large_rule_survives_the_file_whole);
    RUN_TEST(test_every_rule_the_module_takes_reads_back_out_of_the_file);
    RUN_TEST(test_a_rule_stored_next_to_others_does_not_take_the_registry_with_it);
    RUN_TEST(test_the_rule_file_is_written_byte_for_byte_as_before);

    printf("\n----- Rule Stream Read Tests Complete -----\n");
}
