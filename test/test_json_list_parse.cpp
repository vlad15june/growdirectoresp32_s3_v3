// "Parsed, and empty" is a third outcome, told apart from a body that could not be read.
//
// What this file guards: rulesFromJson() and sensorTypesFromJson() must not answer with a bool that
// honestly means "did I build at least one item" when the caller reads it as "did the parse succeed"
// when it wires up httpFailed. An account with no rules answers 200 and `[]`, so a correct answer
// would be reported as a transport failure - four outbox attempts, 10.5 s of backoff, a failed
// synchronization recorded, and the same again five minutes later, for ever.
//
// The table runs against ArduinoJsonConverter, which is what globals.h gives the device and what
// the firmware image contains: the answers below are the module's answers and not another
// implementation's.

#include <unity.h>

// Arduino.h stand-in first: ArduinoJsonConverter.h pulls in headers that expect it (env:native).
#include "Mock/ArduinoMock.h"

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

// One rule, enough fields for the converter to build something from it.
const char *const kOneRule =
    "[{\"uid\":\"rule-1\",\"name\":\"r\",\"type\":\"step\",\"isActive\":true,\"deviceUid\":\"d1\"}]";

const char *const kOneSensorType =
    "[{\"uid\":\"type-1\",\"name\":\"t\",\"protocol\":\"ModBus\"}]";

struct Case {
    const char *body;
    JsonListParse expected;
    const char *why;
};

// The three outcomes, and the bodies that must produce each. Shared by both list kinds: the rule
// is about the shape of the body, not about what the items mean.
const Case kCases[] = {
    {"[]",          JsonListParse::Empty,  "an empty array is the server's answer, not a fault"},
    {"[ ]",         JsonListParse::Empty,  "whitespace inside an empty array changes nothing"},
    {"  []  ",      JsonListParse::Empty,  "whitespace around an empty array changes nothing"},
    {"",            JsonListParse::Failed, "no body at all is not an empty list"},
    {"   ",         JsonListParse::Failed, "a body of whitespace is not an empty list"},
    {"{}",          JsonListParse::Failed, "an object is not a list"},
    {"{\"a\":[]}",  JsonListParse::Failed, "a list wrapped in an object is not a list"},
    {"garbage",     JsonListParse::Failed, "a bare word is not a list"},
    {"[",           JsonListParse::Failed, "a truncated body is not a list"},
    {"[{\"uid\":1", JsonListParse::Failed, "a body cut off mid-object is not a list"},
    {"null",        JsonListParse::Failed, "null is not a list"},
};

const char *name(JsonListParse value)
{
    switch (value)
    {
    case JsonListParse::Ok:     return "Ok";
    case JsonListParse::Empty:  return "Empty";
    case JsonListParse::Failed: return "Failed";
    }
    return "?";
}

void checkRuleCases(IJsonConverter &converter, const char *converterName)
{
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
    {
        std::vector<Rule> rules;
        const JsonListParse actual = converter.rulesFromJson(kCases[i].body, rules);

        char message[256];
        snprintf(message, sizeof(message), "%s::rulesFromJson(\"%s\") answered %s, expected %s - %s",
                 converterName, kCases[i].body, name(actual), name(kCases[i].expected),
                 kCases[i].why);
        TEST_ASSERT_EQUAL_MESSAGE(kCases[i].expected, actual, message);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(0, rules.size(), message);
    }

    // And the outcome that was never in doubt, so that "Empty" cannot be reached by simply
    // answering Empty to everything.
    std::vector<Rule> rules;
    char message[128];
    snprintf(message, sizeof(message), "%s::rulesFromJson() did not read a one-rule array",
             converterName);
    TEST_ASSERT_EQUAL_MESSAGE(JsonListParse::Ok, converter.rulesFromJson(kOneRule, rules), message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, rules.size(), message);
}

void checkSensorTypeCases(IJsonConverter &converter, const char *converterName)
{
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
    {
        std::vector<SensorType> types;
        const JsonListParse actual = converter.sensorTypesFromJson(kCases[i].body, types);

        char message[256];
        snprintf(message, sizeof(message),
                 "%s::sensorTypesFromJson(\"%s\") answered %s, expected %s - %s", converterName,
                 kCases[i].body, name(actual), name(kCases[i].expected), kCases[i].why);
        TEST_ASSERT_EQUAL_MESSAGE(kCases[i].expected, actual, message);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(0, types.size(), message);
    }

    std::vector<SensorType> types;
    char message[128];
    snprintf(message, sizeof(message), "%s::sensorTypesFromJson() did not read a one-type array",
             converterName);
    TEST_ASSERT_EQUAL_MESSAGE(JsonListParse::Ok, converter.sensorTypesFromJson(kOneSensorType, types),
                              message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, types.size(), message);
}

} // namespace

// The converter the device runs.
void test_arduino_converter_tells_an_empty_rule_list_from_an_unreadable_one(void)
{
    ArduinoJsonConverter converter;
    checkRuleCases(converter, "ArduinoJsonConverter");
}

void test_arduino_converter_tells_an_empty_catalog_from_an_unreadable_one(void)
{
    ArduinoJsonConverter converter;
    checkSensorTypeCases(converter, "ArduinoJsonConverter");
}

// What the converter does with a list whose *elements* are junk. Pinned as measured:
//
//     body            rulesFromJson        sensorTypesFromJson
//     [1,2,3]         Failed, 0 rules      Failed, 0 types
//     [null] [[]]     Failed, 0 rules      Failed, 0 types
//     ["a"] [true]    Failed, 0 rules      Failed, 0 types
//     [{}]            Ok, 1 rule           Ok, 1 type
//     [{"nope":1}]    Ok, 1 rule           Ok, 1 type
//
// The line that matters is the last two: an element that is object-shaped always yields an item,
// however empty - ruleFromJson() hands back a default-constructed Rule, and the catalog parser
// requires neither uid nor name. So a catalog of one nameless model is a catalog of one model,
// which costs a slot and matches no sensor, and not a refusal of the whole body.
//
// That is the trade this pins. A non-empty array with nothing object-shaped in it is a body the
// module cannot use at all and is refused whole; a body it can partly use is kept, because the
// catalog and the rule list are what the module has - refusing the lot over one bad element is a
// module with no models, which is a module that cannot scan.
void test_element_level_junk_is_pinned_as_the_converter_behaves(void)
{
    ArduinoJsonConverter converterImpl;
    IJsonConverter &converter = converterImpl;

    std::vector<Rule> rules;

    TEST_ASSERT_EQUAL_MESSAGE(JsonListParse::Failed, converter.rulesFromJson("[1,2,3]", rules),
                              "the converter started building rules out of bare numbers");
    TEST_ASSERT_EQUAL_size_t(0, rules.size());

    // An object-shaped element always yields a rule, however empty.
    TEST_ASSERT_EQUAL(JsonListParse::Ok, converter.rulesFromJson("[{}]", rules));
    TEST_ASSERT_EQUAL_size_t(1, rules.size());

    // A non-empty array with nothing object-shaped in it produces no types, and that is a body the
    // module could not use - not an empty catalog. Reading it as a list one entry shorter is what
    // must not happen: a short snapshot deletes models.
    const char *const noObjects[] = {"[null]", "[[]]", "[\"a\"]", "[true]", "[1,2,3]"};
    for (size_t i = 0; i < sizeof(noObjects) / sizeof(noObjects[0]); i++)
    {
        std::vector<SensorType> types;
        char message[192];
        snprintf(message, sizeof(message),
                 "sensorTypesFromJson(\"%s\") called a body with no sensor type in it an empty "
                 "catalog", noObjects[i]);
        TEST_ASSERT_EQUAL_MESSAGE(JsonListParse::Failed,
                                  converter.sensorTypesFromJson(noObjects[i], types), message);
        TEST_ASSERT_EQUAL_size_t(0, types.size());
    }

    // And the other side of the same rule, for the catalog: an object is a model even with nothing
    // in it. Characterization, not a requirement - such a model holds a catalog slot and matches
    // no sensor - but it is what the board does with a body like this today, and it is the reason
    // one unreadable model does not cost the catalog.
    const char *const objectShaped[] = {"[{}]", "[{\"uid\":\"t-1\",\"name\":\"\"}]"};
    for (size_t i = 0; i < sizeof(objectShaped) / sizeof(objectShaped[0]); i++)
    {
        std::vector<SensorType> types;
        char message[192];
        snprintf(message, sizeof(message),
                 "sensorTypesFromJson(\"%s\") refused a body it could read part of - one unusable "
                 "model must not cost the catalog", objectShaped[i]);
        TEST_ASSERT_EQUAL_MESSAGE(JsonListParse::Ok,
                                  converter.sensorTypesFromJson(objectShaped[i], types), message);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, types.size(), message);
    }
}

extern "C" void run_json_list_parse_tests(void)
{
    printf("\n----- Running JSON List Parse Tests -----\n");

    RUN_TEST(test_arduino_converter_tells_an_empty_rule_list_from_an_unreadable_one);
    RUN_TEST(test_arduino_converter_tells_an_empty_catalog_from_an_unreadable_one);
    RUN_TEST(test_element_level_junk_is_pinned_as_the_converter_behaves);

    printf("\n----- JSON List Parse Tests Complete -----\n");
}
