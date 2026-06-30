#include <unity.h>
#include <string>

#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Application/DTOs/RuleStateUpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Application/Services/CommandService.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Core/Domain/MockRemoteSensorReceiver.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"

static const char *kValidSensorRule =
    R"([{"uid":"rule-1","version":7,"deviceUid":"dev-1","ruleType":"sensor","sensorUid":"sensor-1","valueLow":10,"valueHigh":20,"direction":"down","mode":"Off","state":"Synchronized","daysOfWeek":[],"daysOfMonth":[],"conditions":[]}])";

static const char *kInvalidSensorRule =
    R"([{"uid":"rule-2","version":3,"deviceUid":"dev-2","ruleType":"sensor","sensorUid":"sensor-2","valueLow":0,"valueHigh":0,"direction":"down","mode":"Off","state":"Synchronized","daysOfWeek":[],"daysOfMonth":[],"conditions":[]}])";

static bool contains(const std::string &s, const std::string &needle)
{
    return s.find(needle) != std::string::npos;
}

static void test_rulestatedto_version_default_and_ctor(void)
{
    RuleStateDto d;
    TEST_ASSERT_EQUAL_INT(0, d.version);

    RuleStateDto d2("uid", "Synchronized", "Off", "", "", 9);
    TEST_ASSERT_EQUAL_INT(9, d2.version);
}

static void test_bulk_ack_carries_version_and_synchronizing(void)
{
    JsonConverter c;
    RuleStateUpdateDto u;
    u.addRuleState("r1", "Synchronizing", "On", "", "", 5);

    std::string json = c.ruleStateUpdateToJson(u);
    TEST_ASSERT_TRUE(contains(json, "\"version\":5"));
    TEST_ASSERT_TRUE(contains(json, "\"state\":\"Synchronizing\""));
    TEST_ASSERT_TRUE(contains(json, "\"rules\":["));
}

static void test_per_uid_ack_shape_flat(void)
{
    JsonConverter c;
    RuleStateDto item("r1", "Synchronized", "Off", "", "", 7);

    std::string json = c.ruleStateUpdateSingleToJson(item, "2026-06-05T10:00:00Z");
    TEST_ASSERT_TRUE(contains(json, "\"uid\":\"r1\""));
    TEST_ASSERT_TRUE(contains(json, "\"state\":\"Synchronized\""));
    TEST_ASSERT_TRUE(contains(json, "\"mode\":\"Off\""));
    TEST_ASSERT_TRUE(contains(json, "\"timestamp\":\"2026-06-05T10:00:00Z\""));

    TEST_ASSERT_FALSE(contains(json, "\"rules\""));
    TEST_ASSERT_FALSE(contains(json, "ruleUid"));
    TEST_ASSERT_FALSE(contains(json, "version"));
}

static void test_per_uid_ack_error_carries_errortype(void)
{
    JsonConverter c;
    RuleStateDto item("r2", "Error", "Off", "bad field", "wrong_format", 3);

    std::string json = c.ruleStateUpdateSingleToJson(item, "t");
    TEST_ASSERT_TRUE(contains(json, "\"uid\":\"r2\""));
    TEST_ASSERT_TRUE(contains(json, "\"state\":\"Error\""));
    TEST_ASSERT_TRUE(contains(json, "\"errorType\":\"wrong_format\""));
    TEST_ASSERT_FALSE(contains(json, "version"));
}

static void test_per_uid_ack_carries_accountuid(void)
{
    JsonConverter c;
    RuleStateDto item("r1", "Synchronized", "On", "", "", 0);
    item.accountUid = "acc-123";

    std::string json = c.ruleStateUpdateSingleToJson(item, "t");
    TEST_ASSERT_TRUE(contains(json, "\"accountUid\":\"acc-123\""));

    RuleStateDto noAcc("r2", "Synchronized", "On", "", "", 0);
    std::string jsonNoAcc = c.ruleStateUpdateSingleToJson(noAcc, "t");
    TEST_ASSERT_FALSE(contains(jsonNoAcc, "accountUid"));
}

static void test_bulk_ack_carries_accountuid(void)
{
    JsonConverter c;
    RuleStateUpdateDto u;
    u.accountUid = "acc-123";
    u.addRuleState("r1", "Synchronized", "On", "", "", 0);

    std::string json = c.ruleStateUpdateToJson(u);
    TEST_ASSERT_TRUE(contains(json, "\"accountUid\":\"acc-123\""));
}

static void test_state_literal_synchronizing_roundtrip(void)
{
    JsonConverter c;

    Rule r;
    r.uid = "r1";
    r.version = 1;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "d";
    r.state = RuleState::SYNCHRONIZING;
    std::string j = c.ruleToJson(r);
    TEST_ASSERT_TRUE(contains(j, "\"state\":\"Synchronizing\""));

    Rule parsed;
    c.ruleFromJson("{\"state\":\"Synchronizing\"}", parsed);
    TEST_ASSERT_TRUE(parsed.state == RuleState::SYNCHRONIZING);

    Rule legacy;
    c.ruleFromJson("{\"state\":\"Pending\"}", legacy);
    TEST_ASSERT_TRUE(legacy.state == RuleState::UNSYNCHRONIZED);
}

static void test_push_drop_malformed_payload(void)
{
    JsonConverter converter;
    MockHybridAPIService api;
    MockRuleRepository repo;
    MockRemoteSensorReceiver receiver;
    MockNTPTimeAdapter time;

    CommandService svc(nullptr, nullptr, nullptr, &api, nullptr,
                       nullptr, nullptr, &time, &converter, &repo);
    svc.setRemoteSensorReceiver(&receiver);

    CommandDto cmd;
    cmd.type = "RuleCommandNotification";
    cmd.ruleUid = "rule-x";
    cmd.rulesPayload = "{not json";

    bool result = svc.process(cmd);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(0, repo.synchronizeCallCount);
    TEST_ASSERT_FALSE(api.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_EQUAL_INT(0, receiver.onRuleSetChangedCallCount);
}

static void test_push_drop_empty_array(void)
{
    JsonConverter converter;
    MockHybridAPIService api;
    MockRuleRepository repo;
    MockRemoteSensorReceiver receiver;
    MockNTPTimeAdapter time;

    CommandService svc(nullptr, nullptr, nullptr, &api, nullptr,
                       nullptr, nullptr, &time, &converter, &repo);
    svc.setRemoteSensorReceiver(&receiver);

    CommandDto cmd;
    cmd.type = "RuleCommandNotification";
    cmd.rulesPayload = "[]";

    bool result = svc.process(cmd);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(0, repo.synchronizeCallCount);
    TEST_ASSERT_FALSE(api.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_EQUAL_INT(0, receiver.onRuleSetChangedCallCount);
}

static void test_push_apply_valid_rule(void)
{
    JsonConverter converter;
    MockHybridAPIService api;
    MockRuleRepository repo;
    repo.synchronizeReturnValue = 1;
    MockRemoteSensorReceiver receiver;
    MockNTPTimeAdapter time;

    CommandService svc(nullptr, nullptr, nullptr, &api, nullptr,
                       nullptr, nullptr, &time, &converter, &repo);
    svc.setRemoteSensorReceiver(&receiver);

    CommandDto cmd;
    cmd.type = "RuleCommandNotification";
    cmd.rulesPayload = kValidSensorRule;

    bool result = svc.process(cmd);

    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL_INT(1, repo.synchronizeCallCount);
    TEST_ASSERT_EQUAL_INT(1, (int)repo.lastSynchronizeRules.size());
    TEST_ASSERT_EQUAL_STRING("rule-1", repo.lastSynchronizeRules[0].uid.c_str());

    TEST_ASSERT_TRUE(api.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_EQUAL_STRING("rule-1", api.lastByUidRuleStateUid.c_str());

    TEST_ASSERT_TRUE(contains(api.lastByUidRuleStateCommandStr, "\"uid\":\"rule-1\""));
    TEST_ASSERT_TRUE(contains(api.lastByUidRuleStateCommandStr, "\"state\":\"Synchronized\""));
    TEST_ASSERT_TRUE(contains(api.lastByUidRuleStateCommandStr, "\"timestamp\":"));
    TEST_ASSERT_FALSE(contains(api.lastByUidRuleStateCommandStr, "\"rules\":["));
    TEST_ASSERT_FALSE(contains(api.lastByUidRuleStateCommandStr, "ruleUid"));
    TEST_ASSERT_FALSE(contains(api.lastByUidRuleStateCommandStr, "version"));

    TEST_ASSERT_EQUAL_INT(1, receiver.onRuleSetChangedCallCount);
}

static void test_push_invalid_rule_acks_error(void)
{
    JsonConverter converter;
    MockHybridAPIService api;
    MockRuleRepository repo;
    repo.synchronizeReturnValue = 1;
    MockRemoteSensorReceiver receiver;
    MockNTPTimeAdapter time;

    CommandService svc(nullptr, nullptr, nullptr, &api, nullptr,
                       nullptr, nullptr, &time, &converter, &repo);
    svc.setRemoteSensorReceiver(&receiver);

    CommandDto cmd;
    cmd.type = "RuleCommandNotification";
    cmd.rulesPayload = kInvalidSensorRule;

    bool result = svc.process(cmd);

    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL_INT(0, repo.synchronizeCallCount);
    TEST_ASSERT_TRUE(api.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_EQUAL_INT(1, receiver.onRuleSetChangedCallCount);
}

static void test_push_storage_failure_skips_ack(void)
{
    JsonConverter converter;
    MockHybridAPIService api;
    MockRuleRepository repo;
    repo.synchronizeReturnValue = 0;
    MockRemoteSensorReceiver receiver;
    MockNTPTimeAdapter time;

    CommandService svc(nullptr, nullptr, nullptr, &api, nullptr,
                       nullptr, nullptr, &time, &converter, &repo);
    svc.setRemoteSensorReceiver(&receiver);

    CommandDto cmd;
    cmd.type = "RuleCommandNotification";
    cmd.rulesPayload = kValidSensorRule;

    bool result = svc.process(cmd);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(1, repo.synchronizeCallCount);

    TEST_ASSERT_FALSE(api.sendByUidRuleStateCommandCalled);

    TEST_ASSERT_EQUAL_INT(1, receiver.onRuleSetChangedCallCount);
}

static Rule makeRule(const std::string &uid, int version, RuleMode mode, double valueHigh)
{
    Rule r;
    r.uid = uid;
    r.version = version;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.sensorUid = "sensor-1";
    r.mode = mode;
    r.valueLow = 10;
    r.valueHigh = valueHigh;
    r.direction = RuleDirection::UP;
    r.state = RuleState::SYNCHRONIZED;
    return r;
}

static Rule findStored(RuleRepository &repo, const std::string &uid)
{
    for (const auto &r : repo.getAll())
    {
        if (r.uid == uid) return r;
    }
    return Rule();
}

static void test_sync_new_rule_is_stored_and_counted(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);

    int synced = repo.synchronizeRules({makeRule("u1", 0, RuleMode::OFF, 30)});

    TEST_ASSERT_EQUAL_INT(1, synced);
    Rule stored = findStored(repo, "u1");
    TEST_ASSERT_EQUAL_STRING("u1", stored.uid.c_str());
    TEST_ASSERT_TRUE(stored.state == RuleState::SYNCHRONIZED);
}

static void test_sync_same_version_same_mode_keeps_and_counts(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);
    repo.synchronizeRules({makeRule("u1", 0, RuleMode::OFF, 30)});

    int synced = repo.synchronizeRules({makeRule("u1", 0, RuleMode::OFF, 99)});

    TEST_ASSERT_EQUAL_INT(1, synced);
    Rule stored = findStored(repo, "u1");
    TEST_ASSERT_TRUE(stored.mode == RuleMode::OFF);

    TEST_ASSERT_EQUAL_INT(30, (int)stored.valueHigh);
}

static void test_sync_same_version_mode_changed_applies_incoming(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);
    repo.synchronizeRules({makeRule("u1", 0, RuleMode::OFF, 30)});

    int synced = repo.synchronizeRules({makeRule("u1", 0, RuleMode::ON, 99)});

    TEST_ASSERT_EQUAL_INT(1, synced);
    Rule stored = findStored(repo, "u1");
    TEST_ASSERT_TRUE(stored.mode == RuleMode::ON);
    TEST_ASSERT_EQUAL_INT(99, (int)stored.valueHigh);
    TEST_ASSERT_TRUE(stored.state == RuleState::SYNCHRONIZED);
}

static void test_sync_higher_version_applies_incoming_mode(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);

    repo.synchronizeRules({makeRule("u1", 0, RuleMode::ON, 30)});

    int synced = repo.synchronizeRules({makeRule("u1", 1, RuleMode::OFF, 99)});

    TEST_ASSERT_EQUAL_INT(1, synced);
    Rule stored = findStored(repo, "u1");
    TEST_ASSERT_EQUAL_INT(1, stored.version);
    TEST_ASSERT_EQUAL_INT(99, (int)stored.valueHigh);
    TEST_ASSERT_TRUE(stored.mode == RuleMode::OFF);
}

extern "C" void run_rule_phase2_sync_tests(void)
{
    RUN_TEST(test_rulestatedto_version_default_and_ctor);
    RUN_TEST(test_bulk_ack_carries_version_and_synchronizing);
    RUN_TEST(test_per_uid_ack_shape_flat);
    RUN_TEST(test_per_uid_ack_error_carries_errortype);
    RUN_TEST(test_per_uid_ack_carries_accountuid);
    RUN_TEST(test_bulk_ack_carries_accountuid);
    RUN_TEST(test_state_literal_synchronizing_roundtrip);
    RUN_TEST(test_push_drop_malformed_payload);
    RUN_TEST(test_push_drop_empty_array);
    RUN_TEST(test_push_apply_valid_rule);
    RUN_TEST(test_push_invalid_rule_acks_error);
    RUN_TEST(test_push_storage_failure_skips_ack);
    RUN_TEST(test_sync_new_rule_is_stored_and_counted);
    RUN_TEST(test_sync_same_version_same_mode_keeps_and_counts);
    RUN_TEST(test_sync_same_version_mode_changed_applies_incoming);
    RUN_TEST(test_sync_higher_version_applies_incoming_mode);
}
