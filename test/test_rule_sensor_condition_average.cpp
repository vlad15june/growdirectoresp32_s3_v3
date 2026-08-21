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

/**
 * Device model holding the single device the averaged sensor-rule cases switch.
 */
class SensorConditionAverageDeviceModel : public IDeviceModel {
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

static DeviceDigitalOut makeSensorAverageDevice(DeviceStateEnum state) {
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

static Rule makeAveragedSensorRule() {
    Rule r;
    r.uid = "uAvgCond";
    r.version = 1;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    return r;
}

static RuleCondition makeTwoSensorCondition(RuleDirection direction, double valueLow, double valueHigh) {
    RuleCondition c;
    c.sensorUids.push_back("s1");
    c.sensorUids.push_back("s2");
    c.direction = direction;
    c.valueLow = valueLow;
    c.valueHigh = valueHigh;
    return c;
}

/**
 * GDV1-3839: s1 alone (29.25) is above the switch-on threshold 28, the mean of s1 and s2 (26.25)
 * is below it. Judged on the mean the device stays off; judged on the first sensor it would have
 * switched on.
 */
static void test_sensor_condition_stays_off_when_mean_is_below_threshold(void) {
    MockRuleRepository repo;
    Rule rule = makeAveragedSensorRule();
    rule.conditions.push_back(makeTwoSensorCondition(RuleDirection::DOWN, 27.0, 28.0));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeSensorAverageDevice(DEVICE_STATE_OFF);
    SensorConditionAverageDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 29.25f;
    sensors.values["s2"] = 23.25f;
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

/**
 * GDV1-3839, the mirror case: s1 alone (23.25) is below the switch-on threshold 26, the mean of
 * s1 and s2 (26.5) is above it. The device switches on, and the value reported to the server is
 * the mean, not the first sensor's reading.
 */
static void test_sensor_condition_turns_on_when_mean_is_above_threshold(void) {
    MockRuleRepository repo;
    Rule rule = makeAveragedSensorRule();
    rule.conditions.push_back(makeTwoSensorCondition(RuleDirection::DOWN, 25.0, 26.0));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeSensorAverageDevice(DEVICE_STATE_OFF);
    SensorConditionAverageDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 23.25f;
    sensors.values["s2"] = 29.75f;
    sensors.lastReadTimes["s1"] = 400000;
    sensors.lastReadTimes["s2"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(26.5f, device.sensorValue);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(26.0f, device.sensorValueHigh);
}

/**
 * GDV1-3839: a condition holding one sensor keeps its previous behaviour — the mean of one value
 * is that value, both in the verdict and in the reported descriptor.
 */
static void test_sensor_condition_single_sensor_unchanged(void) {
    MockRuleRepository repo;
    Rule rule = makeAveragedSensorRule();
    RuleCondition c;
    c.sensorUids.push_back("s1");
    c.direction = RuleDirection::DOWN;
    c.valueLow = 25.0;
    c.valueHigh = 26.0;
    rule.conditions.push_back(c);
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeSensorAverageDevice(DEVICE_STATE_OFF);
    SensorConditionAverageDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 29.25f;
    sensors.lastReadTimes["s1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_FLOAT(29.25f, device.sensorValue);
}

/**
 * GDV1-3839: BETWEEN is the exception — an arc has no meaningful mean. s1 (45) is inside the arc
 * while the mean of s1 and s2 (112.5) is outside it, and the rule still fires on s1 and reports
 * its raw reading.
 */
static void test_sensor_condition_between_uses_single_sensor_not_average(void) {
    MockRuleRepository repo;
    Rule rule = makeAveragedSensorRule();
    rule.conditions.push_back(makeTwoSensorCondition(RuleDirection::BETWEEN, 292.5, 67.5));
    repo.rules.push_back(rule);

    DeviceDigitalOut device = makeSensorAverageDevice(DEVICE_STATE_OFF);
    SensorConditionAverageDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 45.0f;
    sensors.values["s2"] = 180.0f;
    sensors.lastReadTimes["s1"] = 400000;
    sensors.lastReadTimes["s2"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(45.0f, device.sensorValue);
}

extern "C" void run_rule_sensor_condition_average_tests(void) {
    RUN_TEST(test_sensor_condition_stays_off_when_mean_is_below_threshold);
    RUN_TEST(test_sensor_condition_turns_on_when_mean_is_above_threshold);
    RUN_TEST(test_sensor_condition_single_sensor_unchanged);
    RUN_TEST(test_sensor_condition_between_uses_single_sensor_not_average);
}
