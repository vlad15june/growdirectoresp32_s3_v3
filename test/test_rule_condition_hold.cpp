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

class ConditionHoldDeviceModel : public IDeviceModel {
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

static Rule makeSensorRule() {
    Rule r;
    r.uid = "uCondHold";
    r.version = 1;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    return r;
}

static RuleCondition makeArcCondition(const char* sensorUid, RuleJoinOp joinOp = RuleJoinOp::UNSET) {
    RuleCondition c;
    c.sensorUids.push_back(sensorUid);
    c.direction = RuleDirection::BETWEEN;
    c.valueLow = 337.5;
    c.valueHigh = 67.5;
    c.joinOp = joinOp;
    return c;
}

static RuleCondition makeUpCondition(const char* sensorUid, double low, double high,
                                     RuleJoinOp joinOp = RuleJoinOp::UNSET) {
    RuleCondition c;
    c.sensorUids.push_back(sensorUid);
    c.direction = RuleDirection::UP;
    c.valueLow = low;
    c.valueHigh = high;
    c.joinOp = joinOp;
    return c;
}

// GDV1-3793: sensor rule with OR, one condition permanently inside its hysteresis dead zone.
// The dead-zone condition never fired, so it must not keep the rule on once the condition that
// really triggered releases.
static void test_or_deadband_condition_does_not_keep_rule_on(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditionOr = true;
    rule.conditions.push_back(makeArcCondition("wind"));
    rule.conditions.push_back(makeUpCondition("moisture", 10.0, 60.0));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["wind"] = 338.0f;      // inside the arc, condition 1 fires
    sensors.values["moisture"] = 32.7f;   // strictly inside 10..60, condition 2 can only HOLD
    sensors.lastReadTimes["wind"] = 400000;
    sensors.lastReadTimes["moisture"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["wind"] = 311.6f;      // outside the arc, condition 1 released
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    arduino.setMillis(402000);            // nothing changed, the dead zone must not switch it back on
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    sensors.values["wind"] = 8.3f;        // inside the arc again
    arduino.setMillis(403000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_or_deadband_condition_alone_never_turns_rule_on(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditionOr = true;
    rule.conditions.push_back(makeArcCondition("wind"));
    rule.conditions.push_back(makeUpCondition("moisture", 10.0, 60.0));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["wind"] = 180.0f;      // outside the arc
    sensors.values["moisture"] = 32.7f;   // dead zone
    sensors.lastReadTimes["wind"] = 400000;
    sensors.lastReadTimes["moisture"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

// The condition that did fire keeps holding the rule while its value drifts into the dead zone -
// that is what the deadband is for.
static void test_or_fired_condition_keeps_holding_inside_its_deadband(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditionOr = true;
    rule.conditions.push_back(makeUpCondition("temp", 24.0, 26.0));
    rule.conditions.push_back(makeArcCondition("wind"));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["temp"] = 23.0f;       // below low, condition 1 fires
    sensors.values["wind"] = 180.0f;      // outside the arc for the whole test
    sensors.lastReadTimes["temp"] = 400000;
    sensors.lastReadTimes["wind"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["temp"] = 25.0f;       // dead zone after its own trigger, must keep holding
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["temp"] = 26.1f;       // above high, condition 1 releases
    arduino.setMillis(402000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    sensors.values["temp"] = 25.0f;       // dead zone again, but nothing fired since
    arduino.setMillis(403000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

// Regression: a plain rule with one condition behaves exactly as before the fix.
static void test_single_condition_deadband_regression(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditions.push_back(makeUpCondition("temp", 24.0, 26.0));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["temp"] = 25.0f;       // dead zone while off - stays off
    sensors.lastReadTimes["temp"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    sensors.values["temp"] = 23.9f;       // below low - turns on
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["temp"] = 25.0f;       // dead zone while on - keeps holding
    arduino.setMillis(402000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["temp"] = 26.1f;       // above high - turns off
    arduino.setMillis(403000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    sensors.values["temp"] = 25.0f;       // dead zone while off - stays off
    arduino.setMillis(404000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    sensors.values["temp"] = 23.0f;       // below low - turns on again
    arduino.setMillis(405000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

// Regression: AND set with a dead-zone condition stays off - the dead zone is not a satisfied
// condition.
static void test_and_deadband_condition_does_not_turn_rule_on(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditionOr = false;
    rule.conditions.push_back(makeArcCondition("wind"));
    rule.conditions.push_back(makeUpCondition("moisture", 10.0, 60.0));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["wind"] = 338.0f;      // inside the arc
    sensors.values["moisture"] = 32.7f;   // dead zone
    sensors.lastReadTimes["wind"] = 400000;
    sensors.lastReadTimes["moisture"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);

    sensors.values["moisture"] = 9.0f;    // below low, now it really fires
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

// Same defect on the DNF path: (c0 AND c1) OR c2 with c2 sitting in its dead zone.
static void test_dnf_deadband_condition_does_not_keep_rule_on(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditions.push_back(makeArcCondition("wind"));
    rule.conditions.push_back(makeUpCondition("lux", 100.0, 200.0, RuleJoinOp::AND));
    rule.conditions.push_back(makeUpCondition("moisture", 10.0, 60.0, RuleJoinOp::OR));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["wind"] = 338.0f;      // inside the arc
    sensors.values["lux"] = 50.0f;        // below low, always fires
    sensors.values["moisture"] = 32.7f;   // dead zone
    sensors.lastReadTimes["wind"] = 400000;
    sensors.lastReadTimes["lux"] = 400000;
    sensors.lastReadTimes["moisture"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["wind"] = 180.0f;      // the AND group releases
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

// A condition that fired, released and then drifted back into its dead zone must not resume
// holding the rule.
static void test_released_condition_does_not_reacquire_hold(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditionOr = true;
    rule.conditions.push_back(makeArcCondition("wind"));
    rule.conditions.push_back(makeUpCondition("temp", 24.0, 26.0));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["wind"] = 338.0f;      // inside the arc
    sensors.values["temp"] = 23.0f;       // below low, fires too
    sensors.lastReadTimes["wind"] = 400000;
    sensors.lastReadTimes["temp"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["temp"] = 26.5f;       // above high, condition 2 releases; the arc still holds
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["temp"] = 25.0f;       // back into the dead zone, no new trigger
    arduino.setMillis(402000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["wind"] = 180.0f;      // the arc releases, nothing is holding the rule
    arduino.setMillis(403000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

// GDV1-3793: inside the DELAY_ON window the value drifting into the dead zone must NOT cancel the
// pending turn-on - the condition already fired, so the deadband keeps holding it and the timer
// runs to its deadline. Before the fix HOLD resolved to committedOn, which is false in DELAY_ON,
// so the very next tick aborted the window with ruleStopReason=unstable_sensor.
static void test_delay_on_window_survives_drift_into_deadband(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditions.push_back(makeUpCondition("temp", 24.0, 26.0));
    rule.delayOn = 30;
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["temp"] = 23.0f;       // below low, the condition fires
    sensors.lastReadTimes["temp"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_ON, device.state);

    sensors.values["temp"] = 25.0f;       // dead zone inside the window - must not cancel
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_ON, device.state);

    arduino.setMillis(420000);            // still inside the window, still in the dead zone
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_ON, device.state);

    arduino.setMillis(430000);            // deadline reached, the delayed turn-on commits
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

// GDV1-3793: mirror case for DELAY_OFF - the condition released, so the dead zone must not revive
// it and cancel the pending turn-off. Before the fix HOLD resolved to committedOn, which is true in
// DELAY_OFF, so the window bounced back to REQUEST_ON with ruleStopReason=unstable_sensor.
static void test_delay_off_window_survives_drift_into_deadband(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule();
    rule.conditions.push_back(makeUpCondition("temp", 24.0, 26.0));
    rule.delayOff = 30;
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    ConditionHoldDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["temp"] = 23.0f;       // below low, the condition fires
    sensors.lastReadTimes["temp"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["temp"] = 26.5f;       // above high, the condition releases - window opens
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_OFF, device.state);

    sensors.values["temp"] = 25.0f;       // dead zone inside the window - must not cancel
    arduino.setMillis(402000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_OFF, device.state);

    arduino.setMillis(420000);            // still inside the window, still in the dead zone
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_DELAY_OFF, device.state);

    arduino.setMillis(431000);            // deadline reached, the delayed turn-off commits
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
}

extern "C" void run_rule_condition_hold_tests(void) {
    RUN_TEST(test_or_deadband_condition_does_not_keep_rule_on);
    RUN_TEST(test_or_deadband_condition_alone_never_turns_rule_on);
    RUN_TEST(test_or_fired_condition_keeps_holding_inside_its_deadband);
    RUN_TEST(test_single_condition_deadband_regression);
    RUN_TEST(test_and_deadband_condition_does_not_turn_rule_on);
    RUN_TEST(test_dnf_deadband_condition_does_not_keep_rule_on);
    RUN_TEST(test_released_condition_does_not_reacquire_hold);
    RUN_TEST(test_delay_on_window_survives_drift_into_deadband);
    RUN_TEST(test_delay_off_window_survives_drift_into_deadband);
}
