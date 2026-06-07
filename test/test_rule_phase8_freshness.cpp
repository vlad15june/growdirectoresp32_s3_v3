#include <unity.h>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Application/Services/Rules/RuleStateReportingService.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Core/Domain/Rules/MockRuleStateNotifier.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"

class Phase8DeviceModel : public IDeviceModel {
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

static void test_stale_sensor_transitions_and_notifies(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    sensors.lastReadTimes["sensor-1"] = 50000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_TRUE(notifier.sensorUnreachableCalled);
    TEST_ASSERT_FALSE(notifier.ruleRecoveredCalled);
    TEST_ASSERT_EQUAL_STRING("u1", notifier.lastUnreachableRuleUid.c_str());
    TEST_ASSERT_EQUAL_STRING("sensor-1", notifier.lastUnreachableSensorUid.c_str());
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
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_FALSE(notifier.sensorUnreachableCalled);
}

static void test_never_seen_waits_no_error(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_FALSE(notifier.sensorUnreachableCalled);
    TEST_ASSERT_FALSE(notifier.ruleRecoveredCalled);
}

static void test_never_seen_past_grace_errors(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_FALSE(notifier.sensorUnreachableCalled);

    arduino.setMillis(700001);
    uc.loop();

    TEST_ASSERT_TRUE(notifier.sensorUnreachableCalled);
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
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_TRUE(notifier.ruleRecoveredCalled);
    TEST_ASSERT_FALSE(notifier.sensorUnreachableCalled);
    TEST_ASSERT_EQUAL_STRING("u1", notifier.lastRecoveredRuleUid.c_str());
}

static void test_never_seen_recovers_to_fresh_when_value_arrives(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    DeviceDigitalOut device;
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    Phase8DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_FALSE(notifier.sensorUnreachableCalled);

    sensors.values["sensor-1"] = 5.0f;
    sensors.lastReadTimes["sensor-1"] = 500000;
    arduino.setMillis(500000);
    uc.loop();

    TEST_ASSERT_FALSE(notifier.sensorUnreachableCalled);
}

static void test_last_ahead_of_now_not_stale(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    DeviceDigitalOut device;
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    Phase8DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 5.0f;
    sensors.lastReadTimes["sensor-1"] = 246355;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(246319);
    uc.loop();

    TEST_ASSERT_FALSE(notifier.sensorUnreachableCalled);
}

static void test_multi_sensor_any_stale_errors_without_grace(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    RuleCondition cond;
    cond.sensorUids.push_back("sensor-2");
    rule.conditions.push_back(cond);
    repo.rules.push_back(rule);

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    sensors.lastReadTimes["sensor-1"] = 50000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_TRUE(notifier.sensorUnreachableCalled);
}

static void test_never_seen_no_report_flap(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule());

    Phase8DeviceModel deviceModel;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();
    arduino.setMillis(700001);
    uc.loop();
    arduino.setMillis(705000);
    uc.loop();
    arduino.setMillis(710000);
    uc.loop();
    arduino.setMillis(900000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(1, notifier.sensorUnreachableCount);
    TEST_ASSERT_EQUAL_INT(0, notifier.ruleRecoveredCount);
}

static void test_reporting_service_emits_report_and_event(void) {
    MockHybridAPIService hybridApi;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    JsonConverter json;

    RuleStateReportingService service(&hybridApi, &json, &settings, &moduleConfig, &ntp);

    Rule rule = makeSensorRule();
    service.notifySensorUnreachable(rule, "sensor-1");

    TEST_ASSERT_TRUE(hybridApi.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_TRUE(contains(hybridApi.lastByUidRuleStateCommandStr, "sensor_unreachable"));
    TEST_ASSERT_TRUE(contains(hybridApi.lastByUidRuleStateCommandStr, "\"state\":\"Error\""));

    TEST_ASSERT_TRUE(hybridApi.sendRuleSensorOfflineEventCalled);
    TEST_ASSERT_TRUE(contains(hybridApi.lastRuleSensorOfflineEventStr, "\"eventType\":\"rule_sensor_offline\""));
    TEST_ASSERT_TRUE(contains(hybridApi.lastRuleSensorOfflineEventStr, "\"sensorUid\":\"sensor-1\""));
    TEST_ASSERT_TRUE(contains(hybridApi.lastRuleSensorOfflineEventStr, "\"ruleUid\":\"u1\""));
}

static void test_reporting_service_recovery_report_only(void) {
    MockHybridAPIService hybridApi;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    JsonConverter json;

    RuleStateReportingService service(&hybridApi, &json, &settings, &moduleConfig, &ntp);

    Rule rule = makeSensorRule();
    service.notifyRuleRecovered(rule);

    TEST_ASSERT_TRUE(hybridApi.sendByUidRuleStateCommandCalled);
    TEST_ASSERT_TRUE(contains(hybridApi.lastByUidRuleStateCommandStr, "\"state\":\"Synchronized\""));
    TEST_ASSERT_FALSE(contains(hybridApi.lastByUidRuleStateCommandStr, "sensor_unreachable"));
    TEST_ASSERT_FALSE(hybridApi.sendRuleSensorOfflineEventCalled);
}

static void test_sync_preserves_sensor_unreachable(void) {
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);

    repo.synchronizeRules({makeSensorRule()});
    repo.updateRuleError("u1", RuleState::ERROR, "sensor_unreachable", "");
    repo.synchronizeRules({makeSensorRule()});

    Rule stored;
    for (const auto& r : repo.getAll()) {
        if (r.uid == "u1") stored = r;
    }
    TEST_ASSERT_TRUE(stored.state == RuleState::ERROR);
    TEST_ASSERT_EQUAL_STRING("sensor_unreachable", stored.errorType.c_str());
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
    RUN_TEST(test_stale_sensor_transitions_and_notifies);
    RUN_TEST(test_fresh_sensor_no_error);
    RUN_TEST(test_never_seen_waits_no_error);
    RUN_TEST(test_never_seen_past_grace_errors);
    RUN_TEST(test_recovery_from_sensor_unreachable);
    RUN_TEST(test_never_seen_recovers_to_fresh_when_value_arrives);
    RUN_TEST(test_last_ahead_of_now_not_stale);
    RUN_TEST(test_multi_sensor_any_stale_errors_without_grace);
    RUN_TEST(test_never_seen_no_report_flap);
    RUN_TEST(test_reporting_service_emits_report_and_event);
    RUN_TEST(test_reporting_service_recovery_report_only);
    RUN_TEST(test_sync_preserves_sensor_unreachable);
    RUN_TEST(test_rule_errortype_json_roundtrip);
}
