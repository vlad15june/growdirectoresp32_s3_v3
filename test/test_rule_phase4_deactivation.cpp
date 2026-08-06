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

#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"

class DeactDeviceModel : public IDeviceModel {
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

class FilteringRuleRepo : public MockRuleRepository {
public:
    std::vector<Rule> getActiveRules() override {
        std::vector<Rule> out;
        for (const auto& r : rules) {
            if (r.mode == RuleMode::ON && r.state != RuleState::DELETING) {
                out.push_back(r);
            }
        }
        return out;
    }
};

static Rule makeOnSensorRule() {
    Rule r;
    r.uid = "u1";
    r.version = 3;
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

static void test_disabling_rule_switches_device_off(void) {
    FilteringRuleRepo repo;
    repo.rules = {makeOnSensorRule()};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    sensors.lastReadTimes["sensor-1"] = 1;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    repo.rules[0].mode = RuleMode::OFF;
    arduino.setMillis(4000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("rule_manualy_off", device.ruleStopReason);
    // full field set on completion
    TEST_ASSERT_TRUE(device.workByRule);
    TEST_ASSERT_NOT_NULL(device.ruleUid);
    TEST_ASSERT_EQUAL_STRING("u1", device.ruleUid);
    TEST_ASSERT_TRUE(device.ruleType == RuleType::SENSOR);
    TEST_ASSERT_EQUAL_INT(3, device.ruleVersion);
    TEST_ASSERT_EQUAL_STRING("sensor-1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, device.sensorValueHigh);
    TEST_ASSERT_NOT_NULL(device.ruleSensorStartAt);
    TEST_ASSERT_TRUE(std::string(device.ruleSensorStartAt).size() > 0); // preserved from ON
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.ruleSensorValueStart);
    TEST_ASSERT_NOT_NULL(device.ruleSensorStopAt);
    TEST_ASSERT_TRUE(std::string(device.ruleSensorStopAt).size() > 0);
}

// GDV1-3794: automation switched off while the device sits inside the delayOff window.
// The relay is closed in delayOff (ProcessDeviceStateUseCase::processDelayOff -> switchRelayOn),
// so the device must be switched off exactly as it is from state on.
static void test_disabling_rule_in_delay_off_switches_device_off(void) {
    FilteringRuleRepo repo;
    Rule r = makeOnSensorRule();
    r.delayOff = 30;  // seconds
    repo.rules = {r};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;  // above valueHigh -> rule turns the device on
    sensors.lastReadTimes["sensor-1"] = 1;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    // sensor leaves the window: with delayOff armed the device stays powered in delayOff
    sensors.values["sensor-1"] = 5.0f;
    arduino.setMillis(4000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_OFF, device.state);

    // automation off while in delayOff - before the fix nothing happened and the relay stayed closed
    repo.rules[0].mode = RuleMode::OFF;
    arduino.setMillis(6000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("rule_manualy_off", device.ruleStopReason);
}

// GDV1-3794: the shared predicates must follow what the relay actually does.
static void test_device_state_powered_predicates_follow_relay(void) {
    TEST_ASSERT_TRUE(isDeviceStatePowered(DEVICE_STATE_ON));
    TEST_ASSERT_TRUE(isDeviceStatePowered(DEVICE_STATE_STEP_ON));
    TEST_ASSERT_TRUE(isDeviceStatePowered(DEVICE_STATE_REQUEST_ON));
    TEST_ASSERT_TRUE(isDeviceStatePowered(DEVICE_STATE_REQUEST_STEP_ON));
    TEST_ASSERT_TRUE(isDeviceStatePowered(DEVICE_STATE_DELAY_OFF));  // relay still closed
    TEST_ASSERT_FALSE(isDeviceStatePowered(DEVICE_STATE_DELAY_ON));  // relay still open
    TEST_ASSERT_FALSE(isDeviceStatePowered(DEVICE_STATE_OFF));
    TEST_ASSERT_FALSE(isDeviceStatePowered(DEVICE_STATE_OFF_BY_POWER_ON));
    TEST_ASSERT_FALSE(isDeviceStatePowered(DEVICE_STATE_STEP_WAITING));

    TEST_ASSERT_TRUE(isDeviceStateNotPowered(DEVICE_STATE_OFF));
    TEST_ASSERT_TRUE(isDeviceStateNotPowered(DEVICE_STATE_OFF_BY_POWER_ON));
    TEST_ASSERT_TRUE(isDeviceStateNotPowered(DEVICE_STATE_DELAY_ON));
    TEST_ASSERT_TRUE(isDeviceStateNotPowered(DEVICE_STATE_STEP_WAITING));
    TEST_ASSERT_FALSE(isDeviceStateNotPowered(DEVICE_STATE_DELAY_OFF));
    TEST_ASSERT_FALSE(isDeviceStateNotPowered(DEVICE_STATE_ON));

    // an unknown state is neither powered nor safely off - unchanged by this fix
    TEST_ASSERT_FALSE(isDeviceStatePowered(DEVICE_STATE_NONE));
    TEST_ASSERT_FALSE(isDeviceStateNotPowered(DEVICE_STATE_NONE));
}

static void test_active_rule_not_deactivated(void) {
    FilteringRuleRepo repo;
    repo.rules = {makeOnSensorRule()};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    sensors.lastReadTimes["sensor-1"] = 1;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();
    arduino.setMillis(4000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_disabling_rule_with_device_off_no_action(void) {
    FilteringRuleRepo repo;
    Rule r = makeOnSensorRule();
    r.mode = RuleMode::OFF;
    repo.rules = {r};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_NULL(device.ruleStopReason);
}

static void test_deleting_active_rule_sets_manualy_deleted(void) {
    FilteringRuleRepo repo;
    repo.rules = {makeOnSensorRule()};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    sensors.lastReadTimes["sensor-1"] = 1;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);

    repo.rules[0].state = RuleState::DELETING;
    arduino.setMillis(4000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("rule_manualy_deleted", device.ruleStopReason);
    // full field set on completion
    TEST_ASSERT_TRUE(device.workByRule);
    TEST_ASSERT_NOT_NULL(device.ruleUid);
    TEST_ASSERT_EQUAL_STRING("u1", device.ruleUid);
    TEST_ASSERT_TRUE(device.ruleType == RuleType::SENSOR);
    TEST_ASSERT_EQUAL_INT(3, device.ruleVersion);
    TEST_ASSERT_EQUAL_STRING("sensor-1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, device.sensorValueHigh);
    TEST_ASSERT_NOT_NULL(device.ruleSensorStartAt);
    TEST_ASSERT_TRUE(std::string(device.ruleSensorStartAt).size() > 0); // preserved from ON
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.ruleSensorValueStart);
    TEST_ASSERT_NOT_NULL(device.ruleSensorStopAt);
    TEST_ASSERT_TRUE(std::string(device.ruleSensorStopAt).size() > 0);
}

class NoActiveRuleRepo : public MockRuleRepository {
public:
    std::vector<Rule> getActiveRules() override { return {}; }
};

static void test_cycling_window_closed_sets_by_date(void) {
    Rule loopRule;
    loopRule.uid = "u1";
    loopRule.version = 1;
    loopRule.ruleType = RuleType::LOOP;
    loopRule.deviceUid = "dev-1";
    loopRule.mode = RuleMode::ON;
    loopRule.state = RuleState::SYNCHRONIZED;

    NoActiveRuleRepo repo;
    repo.rules = {loopRule};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_STEP_ON;
    device.ruleUid = "u1";
    device.ruleStopReason = nullptr;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("rule_off_by_date", device.ruleStopReason);
}

extern "C" void run_rule_phase4_deactivation_tests(void) {
    RUN_TEST(test_disabling_rule_switches_device_off);
    RUN_TEST(test_disabling_rule_in_delay_off_switches_device_off);
    RUN_TEST(test_device_state_powered_predicates_follow_relay);
    RUN_TEST(test_active_rule_not_deactivated);
    RUN_TEST(test_disabling_rule_with_device_off_no_action);
    RUN_TEST(test_deleting_active_rule_sets_manualy_deleted);
    RUN_TEST(test_cycling_window_closed_sets_by_date);
}
