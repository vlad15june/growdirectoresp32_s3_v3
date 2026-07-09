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
#include "Application/Services/Rules/RuleValidator.h"

#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"

class ConditionBetweenDeviceModel : public IDeviceModel {
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

static DeviceDigitalOut makeDigitalDevice(const char* uid, DeviceStateEnum state) {
    DeviceDigitalOut d{};
    d.deviceUid = uid;
    d.deviceType = DEVICE_TYPE_DIGITAL;
    d.state = state;
    d.ruleStopReason = "";
    d.ruleUid = "";
    d.currentCycle = 0;
    d.cyclesToSetPoint = 0;
    d.isRuleTurnOnDevice = false;
    return d;
}

static Rule makeStepRuleBase() {
    Rule r;
    r.uid = "uStepCond";
    r.version = 1;
    r.ruleType = RuleType::STEP;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.workingTime = 10;
    r.waitingTime = 20;
    r.cyclesToSetPoint = 5;
    return r;
}

static Rule makeSensorRuleBase() {
    Rule r;
    r.uid = "uSensorCond";
    r.version = 1;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    return r;
}

static void test_step_condition_between_fires_inside_arc(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRuleBase();
    RuleCondition c1;
    c1.sensorUids.push_back("s1");
    c1.direction = RuleDirection::BETWEEN;
    c1.valueLow = 292.5;
    c1.valueHigh = 67.5;
    rule.conditions.push_back(c1);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 45.0f;
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
}

static void test_step_condition_between_blocks_outside_arc(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRuleBase();
    RuleCondition c1;
    c1.sensorUids.push_back("s1");
    c1.direction = RuleDirection::BETWEEN;
    c1.valueLow = 292.5;
    c1.valueHigh = 67.5;
    rule.conditions.push_back(c1);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 90.0f;
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

static void test_step_condition_between_uses_single_sensor_not_average(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRuleBase();
    RuleCondition c1;
    c1.sensorUids.push_back("s1");
    c1.sensorUids.push_back("s2");
    c1.direction = RuleDirection::BETWEEN;
    c1.valueLow = 292.5;
    c1.valueHigh = 67.5;
    rule.conditions.push_back(c1);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 45.0f;  // inside arc
    sensors.values["s2"] = 180.0f; // far outside; linear average with s1 would be ~112.5, also outside
    sensors.lastReadTimes["s1"] = 400000;
    sensors.lastReadTimes["s2"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
}

static void test_step_condition_and_speed_and_direction(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRuleBase();
    rule.conditionOr = false;
    RuleCondition speed;
    speed.sensorUids.push_back("s1");
    speed.direction = RuleDirection::DOWN;
    speed.valueTarget = 25;
    RuleCondition direction;
    direction.sensorUids.push_back("s2");
    direction.direction = RuleDirection::BETWEEN;
    direction.valueLow = 292.5;
    direction.valueHigh = 67.5;
    rule.conditions.push_back(speed);
    rule.conditions.push_back(direction);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 30.0f; // fires (DOWN, value >= target)
    sensors.values["s2"] = 90.0f; // outside arc, does not fire
    sensors.lastReadTimes["s1"] = 400000;
    sensors.lastReadTimes["s2"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    sensors.values["s2"] = 45.0f; // now inside arc, both fire
    arduino.setMillis(401000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
}

static void test_step_single_between_without_conditions(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRuleBase();
    rule.sensorUid = "s1";
    rule.direction = RuleDirection::BETWEEN;
    rule.valueLow = 292.5;
    rule.valueHigh = 67.5;
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 45.0f;
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
}

static void test_sensor_conditions_between_turns_on_inside_arc(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleBase();
    RuleCondition c1;
    c1.sensorUids.push_back("s1");
    c1.direction = RuleDirection::BETWEEN;
    c1.valueLow = 292.5;
    c1.valueHigh = 67.5;
    rule.conditions.push_back(c1);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 45.0f;
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_sensor_conditions_between_turns_off_outside_arc(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleBase();
    RuleCondition c1;
    c1.sensorUids.push_back("s1");
    c1.direction = RuleDirection::BETWEEN;
    c1.valueLow = 292.5;
    c1.valueHigh = 67.5;
    rule.conditions.push_back(c1);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 90.0f;
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

static void test_sensor_conditions_and_speed_and_direction(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleBase();
    rule.conditionOr = false;
    RuleCondition speed;
    speed.sensorUids.push_back("s1");
    speed.direction = RuleDirection::DOWN;
    speed.valueLow = 24;
    speed.valueHigh = 26;
    RuleCondition direction;
    direction.sensorUids.push_back("s2");
    direction.direction = RuleDirection::BETWEEN;
    direction.valueLow = 292.5;
    direction.valueHigh = 67.5;
    rule.conditions.push_back(speed);
    rule.conditions.push_back(direction);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 27.0f; // DOWN fires (value >= valueHigh)
    sensors.values["s2"] = 90.0f; // outside arc
    sensors.lastReadTimes["s1"] = 400000;
    sensors.lastReadTimes["s2"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    sensors.values["s2"] = 45.0f; // now inside arc, both fire
    arduino.setMillis(401000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_sensor_conditions_or_aggregation(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleBase();
    rule.conditionOr = true;
    RuleCondition speed;
    speed.sensorUids.push_back("s1");
    speed.direction = RuleDirection::DOWN;
    speed.valueLow = 24;
    speed.valueHigh = 26;
    RuleCondition direction;
    direction.sensorUids.push_back("s2");
    direction.direction = RuleDirection::BETWEEN;
    direction.valueLow = 292.5;
    direction.valueHigh = 67.5;
    rule.conditions.push_back(speed);
    rule.conditions.push_back(direction);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 27.0f; // DOWN fires
    sensors.values["s2"] = 90.0f; // outside arc, does not fire
    sensors.lastReadTimes["s1"] = 400000;
    sensors.lastReadTimes["s2"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_sensor_condition_empty_sensor_uids_treated_as_false(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleBase();
    rule.conditionOr = true;
    RuleCondition empty;
    empty.direction = RuleDirection::BETWEEN;
    empty.valueLow = 292.5;
    empty.valueHigh = 67.5;
    rule.conditions.push_back(empty);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionBetweenDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

static void test_sensor_validator_accepts_conditions_without_sensor_uid(void) {
    RuleValidator validator;
    Rule rule = makeSensorRuleBase();
    rule.direction = RuleDirection::BETWEEN;
    rule.valueLow = 292.5;
    rule.valueHigh = 67.5;
    RuleCondition c1;
    c1.sensorUids.push_back("s1");
    c1.direction = RuleDirection::BETWEEN;
    c1.valueLow = 292.5;
    c1.valueHigh = 67.5;
    rule.conditions.push_back(c1);

    RuleValidationResult res = validator.validateRule(rule);
    TEST_ASSERT_TRUE(res.isValid);
}

static void test_sensor_validator_rejects_when_no_sensor_and_no_conditions(void) {
    RuleValidator validator;
    Rule rule = makeSensorRuleBase();
    rule.direction = RuleDirection::BETWEEN;
    rule.valueLow = 292.5;
    rule.valueHigh = 67.5;

    RuleValidationResult res = validator.validateRule(rule);
    TEST_ASSERT_FALSE(res.isValid);
    TEST_ASSERT_EQUAL_STRING("wrong_format", res.errorType.c_str());
}

static void test_step_validator_between_requires_thresholds(void) {
    RuleValidator validator;
    Rule rule = makeStepRuleBase();
    rule.sensorUid = "s1";
    rule.direction = RuleDirection::BETWEEN;

    RuleValidationResult res = validator.validateRule(rule);
    TEST_ASSERT_FALSE(res.isValid);
    TEST_ASSERT_EQUAL_STRING("wrong_format", res.errorType.c_str());
}

static void test_step_validator_between_accepts_thresholds_without_target(void) {
    RuleValidator validator;
    Rule rule = makeStepRuleBase();
    rule.sensorUid = "s1";
    rule.direction = RuleDirection::BETWEEN;
    rule.valueLow = 292.5;
    rule.valueHigh = 67.5;

    RuleValidationResult res = validator.validateRule(rule);
    TEST_ASSERT_TRUE(res.isValid);
}

extern "C" void run_rule_condition_between_tests(void) {
    RUN_TEST(test_step_condition_between_fires_inside_arc);
    RUN_TEST(test_step_condition_between_blocks_outside_arc);
    RUN_TEST(test_step_condition_between_uses_single_sensor_not_average);
    RUN_TEST(test_step_condition_and_speed_and_direction);
    RUN_TEST(test_step_single_between_without_conditions);
    RUN_TEST(test_sensor_conditions_between_turns_on_inside_arc);
    RUN_TEST(test_sensor_conditions_between_turns_off_outside_arc);
    RUN_TEST(test_sensor_conditions_and_speed_and_direction);
    RUN_TEST(test_sensor_conditions_or_aggregation);
    RUN_TEST(test_sensor_condition_empty_sensor_uids_treated_as_false);
    RUN_TEST(test_sensor_validator_accepts_conditions_without_sensor_uid);
    RUN_TEST(test_sensor_validator_rejects_when_no_sensor_and_no_conditions);
    RUN_TEST(test_step_validator_between_requires_thresholds);
    RUN_TEST(test_step_validator_between_accepts_thresholds_without_target);
}
