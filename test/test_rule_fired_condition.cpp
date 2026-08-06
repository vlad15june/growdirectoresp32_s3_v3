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

class FiredConditionDeviceModel : public IDeviceModel {
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

static DeviceDigitalOut makeFiredConditionDevice(DeviceStateEnum state) {
    DeviceDigitalOut d{};
    d.deviceUid = "dev-1";
    d.deviceType = DEVICE_TYPE_DIGITAL;
    d.state = state;
    d.ruleStopReason = "";
    d.ruleUid = "";
    d.sensorUid = "";
    d.currentCycle = 0;
    d.cyclesToSetPoint = 0;
    d.isRuleTurnOnDevice = false;
    return d;
}

static RuleCondition makeSensorCondition(const char* sensorUid, RuleDirection direction,
                                        double valueLow, double valueHigh, RuleJoinOp joinOp) {
    RuleCondition c;
    c.sensorUids.push_back(sensorUid);
    c.direction = direction;
    c.valueLow = valueLow;
    c.valueHigh = valueHigh;
    c.joinOp = joinOp;
    return c;
}

static RuleCondition makeStepCondition(const char* sensorUid, RuleDirection direction,
                                       double valueTarget, RuleJoinOp joinOp) {
    RuleCondition c;
    c.sensorUids.push_back(sensorUid);
    c.direction = direction;
    c.valueTarget = valueTarget;
    c.joinOp = joinOp;
    return c;
}

// Backend mirrors conditions[0] into the rule-level sensorUid/valueLow/valueHigh
// (RuleProfile.cs), so the rule-level descriptor always points at the first condition.
static Rule makeSensorRuleWithConditions(bool conditionOr) {
    Rule r;
    r.uid = "uFC";
    r.version = 1;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.sensorUid = "s1";
    r.direction = RuleDirection::DOWN;
    r.valueLow = 24;
    r.valueHigh = 26;
    r.conditionOr = conditionOr;
    return r;
}

// Step rules carry no valueLow/valueHigh over the wire (StepHwRuleDto), so the
// rule-level thresholds arrive as zeros.
static Rule makeStepRuleWithConditions(bool conditionOr) {
    Rule r;
    r.uid = "uFCS";
    r.version = 1;
    r.ruleType = RuleType::STEP;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.sensorUid = "s1";
    r.direction = RuleDirection::UP;
    r.valueTarget = 7001;
    r.valueLow = 0;
    r.valueHigh = 0;
    r.workingTime = 63;
    r.waitingTime = 10;
    r.cyclesToSetPoint = 0;
    r.conditionOr = conditionOr;
    return r;
}

static void markSensorsFresh(MockSensorValueModel& sensors, uint32_t now) {
    for (auto& entry : sensors.values) {
        sensors.lastReadTimes[entry.first] = now;
    }
}

// --- combineConditionsDnf: the fired index itself -----------------------------------------

static void test_combine_reports_fired_condition_index(void) {
    std::vector<RuleCondition> legacy;
    legacy.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    legacy.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::UNSET));
    legacy.push_back(makeSensorCondition("s3", RuleDirection::DOWN, 40, 42, RuleJoinOp::UNSET));

    int fired = -2;
    TEST_ASSERT_TRUE(combineConditionsDnf(legacy, {false, true, true}, true, &fired));
    TEST_ASSERT_EQUAL_INT(1, fired);

    fired = -2;
    TEST_ASSERT_FALSE(combineConditionsDnf(legacy, {false, false, false}, true, &fired));
    TEST_ASSERT_EQUAL_INT(-1, fired);

    fired = -2;
    TEST_ASSERT_TRUE(combineConditionsDnf(legacy, {true, true, true}, false, &fired));
    TEST_ASSERT_EQUAL_INT(0, fired);

    fired = -2;
    TEST_ASSERT_FALSE(combineConditionsDnf(legacy, {true, false, true}, false, &fired));
    TEST_ASSERT_EQUAL_INT(-1, fired);

    // (c0 AND c1) OR c2 -> the second group holds the rule, its start index is 2
    std::vector<RuleCondition> dnf;
    dnf.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    dnf.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::AND));
    dnf.push_back(makeSensorCondition("s3", RuleDirection::DOWN, 40, 42, RuleJoinOp::OR));

    fired = -2;
    TEST_ASSERT_TRUE(combineConditionsDnf(dnf, {true, false, true}, false, &fired));
    TEST_ASSERT_EQUAL_INT(2, fired);

    fired = -2;
    TEST_ASSERT_TRUE(combineConditionsDnf(dnf, {true, true, true}, false, &fired));
    TEST_ASSERT_EQUAL_INT(0, fired);

    // c0 OR (c1 AND c2) -> the AND group holds it, reported by its first condition
    std::vector<RuleCondition> dnfTail;
    dnfTail.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    dnfTail.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::OR));
    dnfTail.push_back(makeSensorCondition("s3", RuleDirection::DOWN, 40, 42, RuleJoinOp::AND));

    fired = -2;
    TEST_ASSERT_TRUE(combineConditionsDnf(dnfTail, {false, true, true}, false, &fired));
    TEST_ASSERT_EQUAL_INT(1, fired);
}

// --- SENSOR rules -------------------------------------------------------------------------

// GDV1-3755: OR rule over two different sensors, only the SECOND condition fires.
static void test_sensor_or_reports_second_condition(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleWithConditions(true);
    rule.conditions.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    rule.conditions.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::UNSET));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 20.0f; // below its low, rule is off -> HOLD -> does not fire
    sensors.values["s2"] = 35.0f; // above its high -> TURN_ON -> fires
    markSensorsFresh(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s2", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(32.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(35.0f, device.sensorValue);
}

// (c0 AND c1) OR c2 -> the standalone third condition holds the rule.
static void test_sensor_dnf_reports_holding_group(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleWithConditions(false);
    rule.conditions.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    rule.conditions.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::AND));
    rule.conditions.push_back(makeSensorCondition("s3", RuleDirection::DOWN, 40, 42, RuleJoinOp::OR));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 35.0f; // fires
    sensors.values["s2"] = 20.0f; // does not fire -> first group is false
    sensors.values["s3"] = 50.0f; // fires -> second group holds the rule
    markSensorsFresh(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s3", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, device.sensorValue);
}

// c0 OR (c1 AND c2) -> the AND group is reported by its first condition.
static void test_sensor_dnf_reports_and_group_start(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleWithConditions(false);
    rule.conditions.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    rule.conditions.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::OR));
    rule.conditions.push_back(makeSensorCondition("s3", RuleDirection::DOWN, 40, 42, RuleJoinOp::AND));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 20.0f; // does not fire
    sensors.values["s2"] = 35.0f; // fires
    sensors.values["s3"] = 50.0f; // fires -> (c1 AND c2) holds the rule
    markSensorsFresh(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s2", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(32.0f, device.sensorValueHigh);
}

// Regression: all-AND rule keeps reporting the first condition, exactly as before the fix.
static void test_sensor_all_and_keeps_first_condition(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleWithConditions(false);
    rule.conditions.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    rule.conditions.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::UNSET));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 35.0f;
    sensors.values["s2"] = 40.0f;
    markSensorsFresh(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(26.0f, device.sensorValueHigh);
}

// Regression: single-sensor rule without conditions keeps the rule-level descriptor.
static void test_sensor_without_conditions_keeps_rule_level(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleWithConditions(false);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 35.0f;
    markSensorsFresh(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(26.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(35.0f, device.sensorValue);
}

// Hysteresis stickiness: on DELAY_OFF -> REQUEST_ON a condition sitting in its dead zone
// counts as fired; the descriptor must stay on the condition that actually triggered.
static void test_sensor_sticky_keeps_previous_trigger_on_delay_off_recovery(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleWithConditions(true);
    rule.delayOff = 10;
    rule.conditions.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    rule.conditions.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::UNSET));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    // 1. the second condition triggers the rule
    sensors.values["s1"] = 20.0f;
    sensors.values["s2"] = 35.0f;
    markSensorsFresh(sensors, 400000);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s2", device.sensorUid);

    // 2. it releases, nothing else holds the rule -> delayOff starts
    sensors.values["s2"] = 20.0f;
    markSensorsFresh(sensors, 401000);
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_OFF, device.state);
    TEST_ASSERT_EQUAL_STRING("s2", device.sensorUid);

    // 3. the first condition lands in its dead zone (HOLD -> counts as fired) and the
    //    second one triggers again -> the delay is cancelled, s2 stays the trigger
    sensors.values["s1"] = 25.0f;
    sensors.values["s2"] = 35.0f;
    markSensorsFresh(sensors, 402000);
    arduino.setMillis(402000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s2", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(32.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(35.0f, device.sensorValue);
}

// Hand-off: the original trigger no longer holds the rule -> the first condition that does
// takes the descriptor over.
static void test_sensor_sticky_hands_off_when_previous_trigger_released(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRuleWithConditions(true);
    rule.delayOff = 10;
    rule.conditions.push_back(makeSensorCondition("s1", RuleDirection::DOWN, 24, 26, RuleJoinOp::UNSET));
    rule.conditions.push_back(makeSensorCondition("s2", RuleDirection::DOWN, 30, 32, RuleJoinOp::UNSET));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    sensors.values["s1"] = 20.0f;
    sensors.values["s2"] = 35.0f;
    markSensorsFresh(sensors, 400000);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_STRING("s2", device.sensorUid);

    sensors.values["s2"] = 20.0f;
    markSensorsFresh(sensors, 401000);
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_OFF, device.state);

    // s2 stays released, s1 goes above its high and takes over
    sensors.values["s1"] = 35.0f;
    markSensorsFresh(sensors, 402000);
    arduino.setMillis(402000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(26.0f, device.sensorValueHigh);
}

// --- STEP rules ---------------------------------------------------------------------------

// GDV1-3755, the ticket's own case: multi-condition step rule, the first condition is false.
static void test_step_or_reports_second_condition_target(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRuleWithConditions(true);
    rule.conditions.push_back(makeStepCondition("s1", RuleDirection::UP, 7001, RuleJoinOp::UNSET));
    rule.conditions.push_back(makeStepCondition("s2", RuleDirection::UP, 28, RuleJoinOp::UNSET));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 7656.0f; // 7656 > 7001 -> does not fire
    sensors.values["s2"] = 20.0f;   // 20 <= 28 -> fires
    markSensorsFresh(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s2", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, device.sensorValue);
    TEST_ASSERT_EQUAL_FLOAT(28.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(28.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, device.ruleSensorValueStart);
}

// The payload shape from the ticket: 4 conditions joined null / or / and / or.
static void test_step_dnf_reports_holding_group_start(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRuleWithConditions(false);
    rule.conditions.push_back(makeStepCondition("s1", RuleDirection::UP, 7001, RuleJoinOp::UNSET));
    rule.conditions.push_back(makeStepCondition("s2", RuleDirection::UP, 28, RuleJoinOp::OR));
    rule.conditions.push_back(makeStepCondition("s3", RuleDirection::UP, 48, RuleJoinOp::AND));
    rule.conditions.push_back(makeStepCondition("s4", RuleDirection::UP, 1, RuleJoinOp::OR));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 7656.0f; // does not fire
    sensors.values["s2"] = 20.0f;   // fires
    sensors.values["s3"] = 30.0f;   // fires -> (c1 AND c2) holds the rule
    sensors.values["s4"] = 5.0f;    // does not fire
    markSensorsFresh(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s2", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(28.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(28.0f, device.sensorValueHigh);
}

// Regression: step rule without conditions keeps the rule-level descriptor.
static void test_step_without_conditions_keeps_rule_level(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRuleWithConditions(false);
    rule.direction = RuleDirection::DOWN;
    rule.valueTarget = 25;
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeFiredConditionDevice(DEVICE_STATE_OFF);
    FiredConditionDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 30.0f;
    markSensorsFresh(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, device.sensorValue);
}

extern "C" void run_rule_fired_condition_tests(void) {
    RUN_TEST(test_combine_reports_fired_condition_index);
    RUN_TEST(test_sensor_or_reports_second_condition);
    RUN_TEST(test_sensor_dnf_reports_holding_group);
    RUN_TEST(test_sensor_dnf_reports_and_group_start);
    RUN_TEST(test_sensor_all_and_keeps_first_condition);
    RUN_TEST(test_sensor_without_conditions_keeps_rule_level);
    RUN_TEST(test_sensor_sticky_keeps_previous_trigger_on_delay_off_recovery);
    RUN_TEST(test_sensor_sticky_hands_off_when_previous_trigger_released);
    RUN_TEST(test_step_or_reports_second_condition_target);
    RUN_TEST(test_step_dnf_reports_holding_group_start);
    RUN_TEST(test_step_without_conditions_keeps_rule_level);
}
