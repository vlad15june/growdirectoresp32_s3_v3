#include <unity.h>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Infrastructure/Repositories/RuleRepository.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Application/Services/CommandService.h"
#include "Application/DTOs/CommandDto.h"

#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Application/MockOutbox.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"

static bool jsonContains(const std::string& s, const std::string& needle)
{
    return s.find(needle) != std::string::npos;
}

class SingleDeviceModel : public IDeviceModel {
public:
    Device* device = nullptr;

    bool init() override { return true; }
    Device* getDeviceByUid(const char* uid) override {
        if (device && device->deviceUid && uid && std::string(device->deviceUid) == uid) {
            return device;
        }
        return nullptr;
    }
    std::vector<Device*> getAllDevices() override {
        std::vector<Device*> v;
        if (device) v.push_back(device);
        return v;
    }
    int getDeviceCount() const override { return device ? 1 : 0; }
    bool addDevice(Device*) override { return true; }
    bool removeDevice(const char*) override { return true; }
    void clearDevices() override {}
};

static Rule makeSensorRule(const std::string& uid, RuleMode mode, RuleState state)
{
    Rule r;
    r.uid = uid;
    r.version = 1;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.sensorUid = "sensor-1";
    r.mode = mode;
    r.valueLow = 10;
    r.valueHigh = 20;
    r.direction = RuleDirection::UP;
    r.state = state;
    return r;
}

static Rule makeActiveScheduleRule(const std::string& uid)
{
    Rule r;
    r.uid = uid;
    r.version = 1;
    r.ruleType = RuleType::SCHEDULE;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.schedule.push_back(ScheduleItem(0, 3600));
    return r;
}

static Rule findStored(RuleRepository& repo, const std::string& uid)
{
    for (const auto& r : repo.getAll()) {
        if (r.uid == uid) return r;
    }
    return Rule();
}

static bool isStored(RuleRepository& repo, const std::string& uid)
{
    for (const auto& r : repo.getAll()) {
        if (r.uid == uid) return true;
    }
    return false;
}

static void test_snapshot_deletes_absent_rule(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);
    repo.synchronizeRules({makeSensorRule("u1", RuleMode::OFF, RuleState::SYNCHRONIZED),
                           makeSensorRule("u2", RuleMode::OFF, RuleState::SYNCHRONIZED)});

    std::set<std::string> snapshotUids = {"u1"};
    repo.synchronizeRules({makeSensorRule("u1", RuleMode::OFF, RuleState::SYNCHRONIZED)}, true, snapshotUids);

    TEST_ASSERT_TRUE(findStored(repo, "u1").state == RuleState::SYNCHRONIZED);
    TEST_ASSERT_TRUE(findStored(repo, "u2").state == RuleState::DELETING);
}

static void test_snapshot_keeps_present_but_invalid_rule(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);
    repo.synchronizeRules({makeSensorRule("u1", RuleMode::OFF, RuleState::SYNCHRONIZED),
                           makeSensorRule("u2", RuleMode::OFF, RuleState::SYNCHRONIZED)});

    std::set<std::string> snapshotUids = {"u1", "u2"};
    repo.synchronizeRules({makeSensorRule("u1", RuleMode::OFF, RuleState::SYNCHRONIZED)}, true, snapshotUids);

    TEST_ASSERT_TRUE(findStored(repo, "u2").state == RuleState::SYNCHRONIZED);
}

static void test_push_keeps_absent_rules(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);
    repo.synchronizeRules({makeSensorRule("u1", RuleMode::OFF, RuleState::SYNCHRONIZED),
                           makeSensorRule("u2", RuleMode::OFF, RuleState::SYNCHRONIZED)});

    repo.synchronizeRules({makeSensorRule("u1", RuleMode::OFF, RuleState::SYNCHRONIZED)}, false, {});

    TEST_ASSERT_TRUE(isStored(repo, "u2"));
    TEST_ASSERT_TRUE(findStored(repo, "u2").state == RuleState::SYNCHRONIZED);
}

static void test_incoming_deleting_stored_and_mode_not_preserved(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);
    repo.synchronizeRules({makeSensorRule("u1", RuleMode::ON, RuleState::SYNCHRONIZED)});

    Rule deleting = makeSensorRule("u1", RuleMode::OFF, RuleState::DELETING);
    repo.synchronizeRules({deleting}, false, {});

    Rule stored = findStored(repo, "u1");
    TEST_ASSERT_TRUE(stored.state == RuleState::DELETING);
    TEST_ASSERT_TRUE(stored.mode == RuleMode::OFF);
}

static void test_empty_snapshot_keeps_local(void)
{
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);
    repo.synchronizeRules({makeSensorRule("u1", RuleMode::OFF, RuleState::SYNCHRONIZED)});

    int result = repo.synchronizeRules({}, true, {});

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(isStored(repo, "u1"));
    TEST_ASSERT_TRUE(findStored(repo, "u1").state == RuleState::SYNCHRONIZED);
}

static void test_consumer_purges_deleting_rule(void)
{
    MockRuleRepository repo;
    repo.rules = {makeSensorRule("u1", RuleMode::OFF, RuleState::DELETING)};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(5000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(1, (int)repo.removedUids.size());
    TEST_ASSERT_EQUAL_STRING("u1", repo.removedUids[0].c_str());
}

static void test_consumer_ignores_non_deleting_rule(void)
{
    MockRuleRepository repo;
    repo.rules = {makeSensorRule("u1", RuleMode::OFF, RuleState::SYNCHRONIZED)};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.state = DEVICE_STATE_OFF;
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(5000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(0, (int)repo.removedUids.size());
}

static void test_consumer_retries_on_purge_failure(void)
{
    MockRuleRepository repo;
    repo.rules = {makeSensorRule("u1", RuleMode::OFF, RuleState::DELETING)};
    repo.removeReturnValue = false;

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.state = DEVICE_STATE_OFF;
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(5000);
    uc.loop();
    arduino.setMillis(10000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(2, (int)repo.removedUids.size());
}

static void test_consumer_switches_off_active_device(void)
{
    MockRuleRepository repo;
    repo.rules = {makeActiveScheduleRule("u1")};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(5000);
    uc.loop();
    TEST_ASSERT_TRUE(device.state != DEVICE_STATE_OFF);

    repo.rules[0].state = RuleState::DELETING;
    arduino.setMillis(10000);
    uc.loop();

    TEST_ASSERT_TRUE(device.state == DEVICE_STATE_OFF);
    TEST_ASSERT_EQUAL_STRING("rule_manualy_deleted", device.ruleStopReason);
}

static void test_converter_parses_deleting_state(void)
{
    ArduinoJsonConverter c;
    Rule r;
    c.ruleFromJson(R"({"uid":"u1","deviceUid":"dev-1","ruleType":"sensor","state":"Deleting"})", r);
    TEST_ASSERT_TRUE(r.state == RuleState::DELETING);
}

static void test_converter_serializes_deleting_roundtrip(void)
{
    ArduinoJsonConverter c;
    Rule r = makeSensorRule("u1", RuleMode::ON, RuleState::DELETING);

    std::string json = c.ruleToJson(r);
    TEST_ASSERT_TRUE(jsonContains(json, "\"state\":\"Deleting\""));

    Rule parsed;
    c.ruleFromJson(json, parsed);
    TEST_ASSERT_TRUE(parsed.state == RuleState::DELETING);
}

static void test_push_deleting_via_command_service(void)
{
    ArduinoJsonConverter converter;
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);
    MockOutbox api;
    MockNTPTimeAdapter time;

    CommandService svc(nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr, &time, &converter, &repo);
    svc.setOutbox(&api);

    CommandDto cmd;
    cmd.type = "RuleCommandNotification";
    cmd.ruleUid = "u1";
    cmd.rulesPayload =
        R"([{"uid":"u1","version":2,"deviceUid":"dev-1","ruleType":"sensor","sensorUid":"sensor-1","valueLow":10,"valueHigh":20,"direction":"down","mode":"On","state":"Deleting","daysOfWeek":[],"daysOfMonth":[],"conditions":[]}])";

    bool result = svc.process(cmd);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(findStored(repo, "u1").state == RuleState::DELETING);
    TEST_ASSERT_EQUAL_INT(1, (int)api.ruleStateUids.size());
    TEST_ASSERT_EQUAL_STRING("u1", api.ruleStateUids[0].c_str());
    TEST_ASSERT_TRUE(jsonContains(api.ruleStateBodies[0], "Synchronized"));
}

extern "C" void run_rule_phase3_deletion_tests(void)
{
    RUN_TEST(test_snapshot_deletes_absent_rule);
    RUN_TEST(test_snapshot_keeps_present_but_invalid_rule);
    RUN_TEST(test_push_keeps_absent_rules);
    RUN_TEST(test_incoming_deleting_stored_and_mode_not_preserved);
    RUN_TEST(test_empty_snapshot_keeps_local);
    RUN_TEST(test_consumer_purges_deleting_rule);
    RUN_TEST(test_consumer_ignores_non_deleting_rule);
    RUN_TEST(test_consumer_retries_on_purge_failure);
    RUN_TEST(test_consumer_switches_off_active_device);
    RUN_TEST(test_converter_parses_deleting_state);
    RUN_TEST(test_converter_serializes_deleting_roundtrip);
    RUN_TEST(test_push_deleting_via_command_service);
}
