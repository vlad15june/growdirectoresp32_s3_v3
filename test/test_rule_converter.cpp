#include <unity.h>
#include "Core/Domain/Data/Entities/Rule.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"

// Sample step rule JSON
static const char* test_step_rule_json = R"({
    "sensorUid":"84:F7:03:D7:8E:60.00.01",
    "valueTarget":50,
    "cyclesToSetPoint":null,
    "direction":"down",
    "workingTime":60,
    "waitingTime":600,
    "conditions":[
        {
            "sensorUids":["84:F7:03:D7:8E:60.00.01"],
            "valueTarget":50,
            "direction":"down"
        },
        {
            "sensorUids":["84:F7:03:D7:8E:60.01.01"],
            "valueTarget":15,
            "direction":"down"
        }
    ],
    "conditionOr":true,
    "delayOn":10,
    "delayOff":null,
    "uid":"ff60a9e6-4dc7-4d7e-a659-55fb6d9f98db",
    "version":3,
    "deviceUid":"7C:DF:A1:96:B4:94.01",
    "ruleType":"step",
    "isRuleOn":false,
    "mode":"Off",
    "modeReason":null,
    "protectiveShutdown":false,
    "state":"Synchronized",
    "daysOfWeek":[],
    "daysOfMonth":[]
})";

// Sample sensor rule JSON
static const char* test_sensor_rule_json = R"({
    "sensorUid":"84:F7:03:FE:88:30.00.01",
    "valueLow":26,
    "valueHigh":28,
    "timeToSetPoint":60,
    "isAdvanced":false,
    "direction":"up",
    "startDayTime":"2025-04-25T05:00:00+00:00",
    "endDayTime":"2025-04-25T18:00:00+00:00",
    "advancedRuleValues":[],
    "conditions":[
        {
            "sensorUids":["84:F7:03:FE:88:30.00.01","84:F7:03:FE:88:30.01.01"],
            "valueLow":26,
            "valueHigh":28,
            "direction":"up"
        }
    ],
    "conditionOr":null,
    "delayOn":10,
    "delayOff":null,
    "uid":"404df737-d0c1-462b-949e-32db28267b01",
    "version":2,
    "deviceUid":"7C:DF:A1:96:B4:94.00",
    "ruleType":"sensor",
    "isRuleOn":false,
    "mode":"Off",
    "modeReason":null,
    "protectiveShutdown":true,
    "state":"Synchronized",
    "daysOfWeek":[],
    "daysOfMonth":[]
})";

// Sample schedule rule JSON
static const char* test_schedule_rule_json = R"({
    "schedule":[
        {
            "startAt":"2025-04-30T15:00:00+00:00",
            "duration":3600
        }
    ],
    "uid":"0d22c6b4-ef9b-42d0-951b-f9e635638f93",
    "version":0,
    "deviceUid":"7C:DF:A1:96:B4:94.03",
    "ruleType":"schedule",
    "isRuleOn":false,
    "mode":"Off",
    "modeReason":null,
    "protectiveShutdown":false,
    "state":"Synchronized",
    "daysOfWeek":[],
    "daysOfMonth":[]
})";

// Sample rules array JSON
static const char* test_rules_array_json = R"([
    {
        "sensorUid":"84:F7:03:D7:8E:60.00.01",
        "valueTarget":50,
        "cyclesToSetPoint":null,
        "direction":"down",
        "workingTime":60,
        "waitingTime":600,
        "conditions":[
            {
                "sensorUids":["84:F7:03:D7:8E:60.00.01"],
                "valueTarget":50,
                "direction":"down"
            }
        ],
        "conditionOr":true,
        "delayOn":10,
        "delayOff":null,
        "uid":"ff60a9e6-4dc7-4d7e-a659-55fb6d9f98db",
        "version":3,
        "deviceUid":"7C:DF:A1:96:B4:94.01",
        "ruleType":"step",
        "isRuleOn":false,
        "mode":"Off",
        "modeReason":null,
        "protectiveShutdown":false,
        "state":"Synchronized",
        "daysOfWeek":[],
        "daysOfMonth":[]
    },
    {
        "schedule":[
            {
                "startAt":"2025-04-30T15:00:00+00:00",
                "duration":3600
            }
        ],
        "uid":"0d22c6b4-ef9b-42d0-951b-f9e635638f93",
        "version":0,
        "deviceUid":"7C:DF:A1:96:B4:94.03",
        "ruleType":"schedule",
        "isRuleOn":false,
        "mode":"Off",
        "modeReason":null,
        "protectiveShutdown":false,
        "state":"Synchronized",
        "daysOfWeek":[],
        "daysOfMonth":[]
    }
])";

void test_step_rule_parse_json_converter(void) {
    JsonConverter jsonConverter;
    Rule rule;
    
    TEST_ASSERT_TRUE(jsonConverter.ruleFromJson(test_step_rule_json, rule));
    
    // Check common properties
    TEST_ASSERT_EQUAL_STRING("ff60a9e6-4dc7-4d7e-a659-55fb6d9f98db", rule.uid.c_str());
    TEST_ASSERT_EQUAL(3, rule.version);
    TEST_ASSERT_EQUAL_STRING("7C:DF:A1:96:B4:94.01", rule.deviceUid.c_str());
    TEST_ASSERT_EQUAL(RuleType::STEP, rule.ruleType);
    TEST_ASSERT_EQUAL(RuleMode::OFF, rule.mode);
    TEST_ASSERT_TRUE(rule.modeReason.empty());
    TEST_ASSERT_FALSE(rule.protectiveShutdown);
    TEST_ASSERT_EQUAL(RuleState::SYNCHRONIZED, rule.state);
    
    // Check step rule specific properties
    TEST_ASSERT_EQUAL_STRING("84:F7:03:D7:8E:60.00.01", rule.sensorUid.c_str());
    TEST_ASSERT_EQUAL(RuleDirection::DOWN, rule.direction);
    TEST_ASSERT_EQUAL_FLOAT(50.0, rule.valueTarget);
    TEST_ASSERT_EQUAL(0, rule.cyclesToSetPoint); // null in JSON
    TEST_ASSERT_EQUAL(60, rule.workingTime);
    TEST_ASSERT_EQUAL(600, rule.waitingTime);
    
    // Check conditions
    TEST_ASSERT_EQUAL(2, rule.conditions.size());
    TEST_ASSERT_EQUAL(1, rule.conditions[0].sensorUids.size());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:D7:8E:60.00.01", rule.conditions[0].sensorUids[0].c_str());
    TEST_ASSERT_EQUAL_FLOAT(50.0, rule.conditions[0].valueTarget);
    TEST_ASSERT_EQUAL(RuleDirection::DOWN, rule.conditions[0].direction);
    
    TEST_ASSERT_EQUAL(1, rule.conditions[1].sensorUids.size());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:D7:8E:60.01.01", rule.conditions[1].sensorUids[0].c_str());
    TEST_ASSERT_EQUAL_FLOAT(15.0, rule.conditions[1].valueTarget);
    TEST_ASSERT_EQUAL(RuleDirection::DOWN, rule.conditions[1].direction);
    
    TEST_ASSERT_TRUE(rule.conditionOr);
    TEST_ASSERT_EQUAL(10, rule.delayOn);
    TEST_ASSERT_EQUAL(0, rule.delayOff); // null in JSON
}

void test_sensor_rule_parse_json_converter(void) {
    JsonConverter jsonConverter;
    Rule rule;
    
    TEST_ASSERT_TRUE(jsonConverter.ruleFromJson(test_sensor_rule_json, rule));
    
    // Check common properties
    TEST_ASSERT_EQUAL_STRING("404df737-d0c1-462b-949e-32db28267b01", rule.uid.c_str());
    TEST_ASSERT_EQUAL(2, rule.version);
    TEST_ASSERT_EQUAL_STRING("7C:DF:A1:96:B4:94.00", rule.deviceUid.c_str());
    TEST_ASSERT_EQUAL(RuleType::SENSOR, rule.ruleType);
    TEST_ASSERT_EQUAL(RuleMode::OFF, rule.mode);
    TEST_ASSERT_TRUE(rule.modeReason.empty());
    TEST_ASSERT_TRUE(rule.protectiveShutdown);
    TEST_ASSERT_EQUAL(RuleState::SYNCHRONIZED, rule.state);
    
    // Check sensor rule specific properties
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30.00.01", rule.sensorUid.c_str());
    TEST_ASSERT_EQUAL(RuleDirection::UP, rule.direction);
    TEST_ASSERT_EQUAL_FLOAT(26.0, rule.valueLow);
    TEST_ASSERT_EQUAL_FLOAT(28.0, rule.valueHigh);
    TEST_ASSERT_EQUAL(60, rule.timeToSetPoint);
    TEST_ASSERT_FALSE(rule.isAdvanced);
    
    // Check conditions
    TEST_ASSERT_EQUAL(1, rule.conditions.size());
    TEST_ASSERT_EQUAL(2, rule.conditions[0].sensorUids.size());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30.00.01", rule.conditions[0].sensorUids[0].c_str());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30.01.01", rule.conditions[0].sensorUids[1].c_str());
    TEST_ASSERT_EQUAL_FLOAT(26.0, rule.conditions[0].valueLow);
    TEST_ASSERT_EQUAL_FLOAT(28.0, rule.conditions[0].valueHigh);
    TEST_ASSERT_EQUAL(RuleDirection::UP, rule.conditions[0].direction);
    
    TEST_ASSERT_FALSE(rule.conditionOr);
    TEST_ASSERT_EQUAL(10, rule.delayOn);
    TEST_ASSERT_EQUAL(0, rule.delayOff); // null in JSON
}

void test_schedule_rule_parse_json_converter(void) {
    JsonConverter jsonConverter;
    Rule rule;
    
    TEST_ASSERT_TRUE(jsonConverter.ruleFromJson(test_schedule_rule_json, rule));
    
    // Check common properties
    TEST_ASSERT_EQUAL_STRING("0d22c6b4-ef9b-42d0-951b-f9e635638f93", rule.uid.c_str());
    TEST_ASSERT_EQUAL(0, rule.version);
    TEST_ASSERT_EQUAL_STRING("7C:DF:A1:96:B4:94.03", rule.deviceUid.c_str());
    TEST_ASSERT_EQUAL(RuleType::SCHEDULE, rule.ruleType);
    TEST_ASSERT_EQUAL(RuleMode::OFF, rule.mode);
    TEST_ASSERT_TRUE(rule.modeReason.empty());
    TEST_ASSERT_FALSE(rule.protectiveShutdown);
    TEST_ASSERT_EQUAL(RuleState::SYNCHRONIZED, rule.state);
    
    // Check schedule rule specific properties
    TEST_ASSERT_EQUAL(1, rule.schedule.size());
    TEST_ASSERT_EQUAL(3600, rule.schedule[0].duration);
}

void test_rule_roundtrip_json_converter(void) {
    JsonConverter jsonConverter;
    Rule rule;
    
    // Parse and then serialize step rule
    TEST_ASSERT_TRUE(jsonConverter.ruleFromJson(test_step_rule_json, rule));
    std::string stepRuleJson = jsonConverter.ruleToJson(rule);
    
    // Parse again to verify round-trip integrity
    Rule rule2;
    TEST_ASSERT_TRUE(jsonConverter.ruleFromJson(stepRuleJson, rule2));
    
    // Verify key properties match
    TEST_ASSERT_EQUAL_STRING(rule.uid.c_str(), rule2.uid.c_str());
    TEST_ASSERT_EQUAL(rule.version, rule2.version);
    TEST_ASSERT_EQUAL(rule.ruleType, rule2.ruleType);
    TEST_ASSERT_EQUAL_FLOAT(rule.valueTarget, rule2.valueTarget);
    TEST_ASSERT_EQUAL(rule.workingTime, rule2.workingTime);
    TEST_ASSERT_EQUAL(rule.waitingTime, rule2.waitingTime);
    TEST_ASSERT_EQUAL(rule.conditions.size(), rule2.conditions.size());
}

void test_rule_array_parse_json_converter(void) {
    JsonConverter jsonConverter;
    std::vector<Rule> rules;
    
    TEST_ASSERT_TRUE(jsonConverter.rulesFromJson(test_rules_array_json, rules));
    TEST_ASSERT_EQUAL(2, rules.size());
    
    // Check first rule (step rule)
    TEST_ASSERT_EQUAL(RuleType::STEP, rules[0].ruleType);
    TEST_ASSERT_EQUAL_STRING("ff60a9e6-4dc7-4d7e-a659-55fb6d9f98db", rules[0].uid.c_str());
    
    // Check second rule (schedule rule)
    TEST_ASSERT_EQUAL(RuleType::SCHEDULE, rules[1].ruleType);
    TEST_ASSERT_EQUAL_STRING("0d22c6b4-ef9b-42d0-951b-f9e635638f93", rules[1].uid.c_str());
}

void test_rule_array_roundtrip_json_converter(void) {
    JsonConverter jsonConverter;
    std::vector<Rule> rules;
    
    // Parse rules array
    TEST_ASSERT_TRUE(jsonConverter.rulesFromJson(test_rules_array_json, rules));
    
    // Serialize back to JSON
    std::string rulesJson = jsonConverter.rulesToJson(rules);
    
    // Parse again
    std::vector<Rule> rules2;
    TEST_ASSERT_TRUE(jsonConverter.rulesFromJson(rulesJson, rules2));
    
    // Verify arrays are the same size
    TEST_ASSERT_EQUAL(rules.size(), rules2.size());
    
    // Check key properties of first rule
    TEST_ASSERT_EQUAL_STRING(rules[0].uid.c_str(), rules2[0].uid.c_str());
    TEST_ASSERT_EQUAL(rules[0].ruleType, rules2[0].ruleType);
    
    // Check key properties of second rule
    TEST_ASSERT_EQUAL_STRING(rules[1].uid.c_str(), rules2[1].uid.c_str());
    TEST_ASSERT_EQUAL(rules[1].ruleType, rules2[1].ruleType);
}

extern "C" void run_rule_converter_tests(void) {
    RUN_TEST(test_step_rule_parse_json_converter);
    RUN_TEST(test_sensor_rule_parse_json_converter);
    RUN_TEST(test_schedule_rule_parse_json_converter);
    RUN_TEST(test_rule_roundtrip_json_converter);
    RUN_TEST(test_rule_array_parse_json_converter);
    RUN_TEST(test_rule_array_roundtrip_json_converter);
}
