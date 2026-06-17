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

class Phase7DeviceModel : public IDeviceModel {
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

class DroppedRuleRepo : public MockRuleRepository {
public:
    std::vector<Rule> getActiveRules() override { return {}; }
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

static Rule makeAverageRule() {
    Rule r;
    r.uid = "uA";
    r.version = 1;
    r.ruleType = RuleType::AVERAGE_SENSOR;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.direction = RuleDirection::DOWN;
    r.valueLow = 24;
    r.valueHigh = 26;
    RuleCondition cond;
    cond.sensorUids.push_back("s1");
    cond.sensorUids.push_back("s2");
    r.conditions.push_back(cond);
    return r;
}

static Rule makeStepRule() {
    Rule r;
    r.uid = "uS";
    r.version = 1;
    r.ruleType = RuleType::STEP;
    r.deviceUid = "dev-1";
    r.sensorUid = "s1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.direction = RuleDirection::DOWN;
    r.valueTarget = 25;
    r.workingTime = 10;
    r.waitingTime = 20;
    r.cyclesToSetPoint = 5;
    return r;
}

static Rule makeLoopRule() {
    Rule r;
    r.uid = "uL";
    r.version = 1;
    r.ruleType = RuleType::LOOP;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.workingTime = 10;
    r.waitingTime = 20;
    return r;
}

static void test_average_turns_on_above_high(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeAverageRule());

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 27.0f;
    sensors.values["s2"] = 27.0f;
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

static void test_average_turns_off_below_low(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeAverageRule());

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 27.0f;
    sensors.values["s2"] = 27.0f;
    sensors.lastReadTimes["s1"] = 400000;
    sensors.lastReadTimes["s2"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["s1"] = 20.0f;
    sensors.values["s2"] = 20.0f;
    sensors.lastReadTimes["s1"] = 401000;
    sensors.lastReadTimes["s2"] = 401000;
    arduino.setMillis(401000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_EQUAL_STRING("setpoint_reach", device.ruleStopReason);
}

static void test_step_starts_cycle(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule());

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 30.0f;
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_EQUAL_UINT32(10000, device.durationOn);
    TEST_ASSERT_EQUAL_INT(20000, device.mixTime);
    TEST_ASSERT_EQUAL_INT(5, device.cyclesToSetPoint);
    TEST_ASSERT_EQUAL_INT(1, device.currentCycle);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);
    TEST_ASSERT_NOT_NULL(device.ruleSensorStartAt);
    TEST_ASSERT_TRUE(std::string(device.ruleSensorStartAt).size() > 0);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, device.ruleSensorValueStart);
}

static void test_step_stops_on_setpoint(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule());

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_STEP_ON);
    device.currentCycle = 2;
    device.isRuleTurnOnDevice = true;
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 20.0f; // below target -> not firing -> setpoint reached
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("setpoint_reach", device.ruleStopReason);
}

static void test_step_stops_on_cycle_cap(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule());

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_STEP_WAITING);
    device.currentCycle = 5;
    device.isRuleTurnOnDevice = true;
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 30.0f; // still firing
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("time_reach", device.ruleStopReason);
}

static void test_step_conditions_and_blocks_start(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRule();
    rule.sensorUid = "";
    rule.conditionOr = false; // AND
    RuleCondition c1; c1.sensorUids.push_back("s1"); c1.direction = RuleDirection::DOWN; c1.valueTarget = 25;
    RuleCondition c2; c2.sensorUids.push_back("s2"); c2.direction = RuleDirection::DOWN; c2.valueTarget = 25;
    rule.conditions.push_back(c1);
    rule.conditions.push_back(c2);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 30.0f; // fires
    sensors.values["s2"] = 20.0f; // does not fire
    sensors.lastReadTimes["s1"] = 400000;
    sensors.lastReadTimes["s2"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

static void test_step_conditions_or_starts(void) {
    MockRuleRepository repo;
    Rule rule = makeStepRule();
    rule.sensorUid = "";
    rule.conditionOr = true; // OR
    RuleCondition c1; c1.sensorUids.push_back("s1"); c1.direction = RuleDirection::DOWN; c1.valueTarget = 25;
    RuleCondition c2; c2.sensorUids.push_back("s2"); c2.direction = RuleDirection::DOWN; c2.valueTarget = 25;
    rule.conditions.push_back(c1);
    rule.conditions.push_back(c2);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 30.0f; // fires
    sensors.values["s2"] = 20.0f; // does not fire
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

static void test_loop_starts_cycle(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeLoopRule());

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_EQUAL_UINT32(10000, device.durationOn);
    TEST_ASSERT_EQUAL_INT(20000, device.mixTime);
    TEST_ASSERT_EQUAL_INT(0, device.cyclesToSetPoint);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);
}

static void test_loop_deactivates_when_inactive(void) {
    DroppedRuleRepo repo;
    Rule rule = makeLoopRule();
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_STEP_ON);
    device.ruleUid = "uL";
    device.isRuleTurnOnDevice = true;
    Phase7DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
}

extern "C" void run_rule_phase7_tests(void) {
    RUN_TEST(test_average_turns_on_above_high);
    RUN_TEST(test_average_turns_off_below_low);
    RUN_TEST(test_step_starts_cycle);
    RUN_TEST(test_step_stops_on_setpoint);
    RUN_TEST(test_step_stops_on_cycle_cap);
    RUN_TEST(test_step_conditions_and_blocks_start);
    RUN_TEST(test_step_conditions_or_starts);
    RUN_TEST(test_loop_starts_cycle);
    RUN_TEST(test_loop_deactivates_when_inactive);
}
