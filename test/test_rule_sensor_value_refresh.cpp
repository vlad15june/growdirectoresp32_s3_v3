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

class SensorRefreshDeviceModel : public IDeviceModel {
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
    d.sensorUid = "";
    d.switchedAt = "";
    d.sensorValue = 0.0f;
    d.sensorValueLow = 0.0f;
    d.sensorValueHigh = 0.0f;
    d.currentCycle = 0;
    d.cyclesToSetPoint = 0;
    d.isRuleTurnOnDevice = false;
    d.workByRule = false;
    return d;
}

static Rule makeSensorRule(const char* sensorUid, double valueLow, double valueHigh) {
    Rule r;
    r.uid = "uSensorRefresh";
    r.version = 1;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.sensorUid = sensorUid;
    r.direction = RuleDirection::UP;
    r.valueLow = valueLow;
    r.valueHigh = valueHigh;
    return r;
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

// GDV1-3792: the light rule from the ticket - LUX far below the lower threshold, so the rule
// switches the device on once and holds it. The descriptor published in hardware/update must
// follow the live reading, not the value latched at the switch-on moment.
static void test_sensor_value_follows_reading_without_state_change(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule("lux", 150001.0, 199001.0));

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    SensorRefreshDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["lux"] = 7830.0f;
    sensors.lastReadTimes["lux"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_FLOAT(7830.0f, device.sensorValue);

    sensors.values["lux"] = 7790.0f;
    sensors.lastReadTimes["lux"] = 401000;
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_FLOAT(7790.0f, device.sensorValue);

    sensors.values["lux"] = 7590.0f;
    sensors.lastReadTimes["lux"] = 402000;
    arduino.setMillis(402000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_FLOAT(7590.0f, device.sensorValue);
}

// Second symptom of the same latch: the whole descriptor, not only the value, was frozen for the
// rest of the ON session. A rule update that moves the thresholds must be reflected right away.
static void test_thresholds_follow_rule_update_inside_on_session(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule("temp", 24.0, 26.0));

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    SensorRefreshDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["temp"] = 23.0f;
    sensors.lastReadTimes["temp"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(26.0f, device.sensorValueHigh);

    repo.rules[0].version = 2;
    repo.rules[0].valueLow = 22.0;
    repo.rules[0].valueHigh = 27.0;
    sensors.lastReadTimes["temp"] = 401000;
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_FLOAT(22.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(27.0f, device.sensorValueHigh);
}

static void test_sensor_uid_follows_rule_update_inside_on_session(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule("tempA", 24.0, 26.0));

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    SensorRefreshDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["tempA"] = 23.0f;
    sensors.values["tempB"] = 25.0f;
    sensors.lastReadTimes["tempA"] = 400000;
    sensors.lastReadTimes["tempB"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("tempA", device.sensorUid);

    repo.rules[0].version = 2;
    repo.rules[0].sensorUid = "tempB";
    sensors.lastReadTimes["tempA"] = 401000;
    sensors.lastReadTimes["tempB"] = 401000;
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("tempB", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.sensorValue);
}

// A multi-condition rule reports the rule-level sensor on develop; the refresh must keep that
// value live too, so it stays correct once the descriptor becomes per-condition (GDV1-3755).
static void test_condition_rule_value_follows_inside_on_session(void) {
    MockRuleRepository repo;
    Rule rule = makeSensorRule("temp", 24.0, 26.0);
    rule.conditionOr = true;
    rule.conditions.push_back(makeUpCondition("temp", 24.0, 26.0));
    rule.conditions.push_back(makeUpCondition("moisture", 10.0, 60.0));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    SensorRefreshDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["temp"] = 23.0f;
    sensors.values["moisture"] = 70.0f;
    sensors.lastReadTimes["temp"] = 400000;
    sensors.lastReadTimes["moisture"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("temp", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(23.0f, device.sensorValue);

    sensors.values["temp"] = 23.4f;
    sensors.lastReadTimes["temp"] = 401000;
    sensors.lastReadTimes["moisture"] = 401000;
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_FLOAT(23.4f, device.sensorValue);
}

// Regression: refreshing the descriptor must not touch the rule decision path - the state
// sequence and switchedAt stay exactly where the transitions put them.
static void test_refresh_does_not_move_state_or_switched_at(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule("temp", 24.0, 26.0));

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    SensorRefreshDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["temp"] = 23.0f;
    sensors.lastReadTimes["temp"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    std::string switchedOn = device.switchedAt ? device.switchedAt : "";
    TEST_ASSERT_TRUE(switchedOn.length() > 0);

    sensors.values["temp"] = 25.0f;
    sensors.lastReadTimes["temp"] = 401000;
    arduino.setMillis(401000);
    ntp.setEpochTime(1700000060);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING(switchedOn.c_str(), device.switchedAt);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.sensorValue);

    sensors.values["temp"] = 26.5f;
    sensors.lastReadTimes["temp"] = 402000;
    arduino.setMillis(402000);
    ntp.setEpochTime(1700000120);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    std::string switchedOff = device.switchedAt ? device.switchedAt : "";
    TEST_ASSERT_TRUE(switchedOff != switchedOn);
}

// Regression: once the rule switched the device off, the descriptor keeps the values reported
// with the stop reason - the refresh only runs while the rule holds the device.
static void test_descriptor_frozen_after_rule_switched_device_off(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeSensorRule("temp", 24.0, 26.0));

    DeviceDigitalOut device = makeDigitalDevice("dev-1", DEVICE_STATE_OFF);
    SensorRefreshDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["temp"] = 23.0f;
    sensors.lastReadTimes["temp"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["temp"] = 26.5f;
    sensors.lastReadTimes["temp"] = 401000;
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_EQUAL_FLOAT(26.5f, device.sensorValue);

    sensors.values["temp"] = 28.0f;
    sensors.lastReadTimes["temp"] = 402000;
    arduino.setMillis(402000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_EQUAL_FLOAT(26.5f, device.sensorValue);
}

extern "C" void run_rule_sensor_value_refresh_tests(void) {
    RUN_TEST(test_sensor_value_follows_reading_without_state_change);
    RUN_TEST(test_thresholds_follow_rule_update_inside_on_session);
    RUN_TEST(test_sensor_uid_follows_rule_update_inside_on_session);
    RUN_TEST(test_condition_rule_value_follows_inside_on_session);
    RUN_TEST(test_refresh_does_not_move_state_or_switched_at);
    RUN_TEST(test_descriptor_frozen_after_rule_switched_device_off);
}
