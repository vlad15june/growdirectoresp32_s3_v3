#include <unity.h>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"

#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"

class Phase8DeviceModel : public IDeviceModel {
public:
    bool init() override { return true; }
    Device* getDeviceByUid(const char*) override { return nullptr; }
    std::vector<Device*> getAllDevices() override { return {}; }
    int getDeviceCount() const override { return 0; }
    bool addDevice(Device*) override { return true; }
    bool removeDevice(const char*) override { return true; }
    void clearDevices() override {}
};

static Rule makeSensorRule() {
    Rule r;
    r.uid = "u1";
    r.version = 7;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.sensorUid = "sensor-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.direction = RuleDirection::DOWN;
    r.valueLow = 10;
    r.valueHigh = 20;
    return r;
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static void test_stale_sensor_transitions_and_reports(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    sensors.lastReadTimes["sensor-1"] = 50000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockHybridAPIService hybridApi;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    JsonConverter json;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &hybridApi, &json, &settings, &moduleConfig);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_TRUE(hybridApi.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_TRUE(hybridApi.sendRuleSensorOfflineEventCalled);
    TEST_ASSERT_TRUE(contains(hybridApi.lastByUidRuleStateCommandStr, "sensor_unreachable"));
    TEST_ASSERT_TRUE(contains(hybridApi.lastByUidRuleStateCommandStr, "\"state\":\"Error\""));
    TEST_ASSERT_TRUE(contains(hybridApi.lastRuleSensorOfflineEventStr, "\"eventType\":\"rule_sensor_offline\""));
    TEST_ASSERT_TRUE(contains(hybridApi.lastRuleSensorOfflineEventStr, "\"sensorUid\":\"sensor-1\""));
    TEST_ASSERT_TRUE(contains(hybridApi.lastRuleSensorOfflineEventStr, "\"ruleUid\":\"u1\""));
}

static void test_fresh_sensor_no_error(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    sensors.lastReadTimes["sensor-1"] = 399000;
    sensors.values["sensor-1"] = 5.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockHybridAPIService hybridApi;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    JsonConverter json;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &hybridApi, &json, &settings, &moduleConfig);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_FALSE(hybridApi.sendRuleSensorOfflineEventCalled);
}

static void test_never_seen_waits_no_error(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockHybridAPIService hybridApi;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    JsonConverter json;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &hybridApi, &json, &settings, &moduleConfig);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_FALSE(hybridApi.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_FALSE(hybridApi.sendRuleSensorOfflineEventCalled);
}

static void test_never_seen_past_grace_errors(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockHybridAPIService hybridApi;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    JsonConverter json;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &hybridApi, &json, &settings, &moduleConfig);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_FALSE(hybridApi.sendRuleSensorOfflineEventCalled);

    arduino.setMillis(700001);
    uc.loop();

    TEST_ASSERT_TRUE(hybridApi.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_TRUE(hybridApi.sendRuleSensorOfflineEventCalled);
}

static void test_recovery_from_sensor_unreachable(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.state = RuleState::ERROR;
    rule.errorType = "sensor_unreachable";
    repo.rules.push_back(rule);

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    sensors.lastReadTimes["sensor-1"] = 399000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockHybridAPIService hybridApi;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    JsonConverter json;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &hybridApi, &json, &settings, &moduleConfig);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_TRUE(hybridApi.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_FALSE(hybridApi.sendRuleSensorOfflineEventCalled);
    TEST_ASSERT_TRUE(contains(hybridApi.lastByUidRuleStateCommandStr, "\"state\":\"Synchronized\""));
    TEST_ASSERT_FALSE(contains(hybridApi.lastByUidRuleStateCommandStr, "sensor_unreachable"));
}

static void test_rule_errortype_json_roundtrip(void) {
    JsonConverter json;
    Rule rule = makeSensorRule();
    rule.errorType = "sensor_unreachable";

    std::string serialized = json.ruleToJson(rule);
    TEST_ASSERT_TRUE(contains(serialized, "\"errorType\":\"sensor_unreachable\""));

    Rule parsed;
    TEST_ASSERT_TRUE(json.ruleFromJson(serialized, parsed));
    TEST_ASSERT_EQUAL_STRING("sensor_unreachable", parsed.errorType.c_str());
}

extern "C" void run_rule_phase8_freshness_tests(void) {
    RUN_TEST(test_stale_sensor_transitions_and_reports);
    RUN_TEST(test_fresh_sensor_no_error);
    RUN_TEST(test_never_seen_waits_no_error);
    RUN_TEST(test_never_seen_past_grace_errors);
    RUN_TEST(test_recovery_from_sensor_unreachable);
    RUN_TEST(test_rule_errortype_json_roundtrip);
}
