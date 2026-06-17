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

namespace {

class CFDeviceModel : public IDeviceModel {
public:
    Device* device = nullptr;
    bool init() override { return true; }
    Device* getDeviceByUid(const char* uid) override {
        if (device && device->deviceUid && uid && std::string(device->deviceUid) == uid) return device;
        return nullptr;
    }
    std::vector<Device*> getAllDevices() override {
        std::vector<Device*> v; if (device) v.push_back(device); return v;
    }
    int getDeviceCount() const override { return device ? 1 : 0; }
    bool addDevice(Device*) override { return true; }
    bool removeDevice(const char*) override { return true; }
    void clearDevices() override {}
};

static bool nonEmpty(const char* s) { return s != nullptr && std::string(s).size() > 0; }

static DeviceDigitalOut makeDevice() {
    DeviceDigitalOut d{};
    d.deviceUid = "dev-1";
    d.deviceType = DEVICE_TYPE_DIGITAL;
    d.state = DEVICE_STATE_OFF;
    d.ruleStopReason = nullptr;
    d.ruleUid = nullptr;
    d.sensorUid = nullptr;
    d.ruleSensorStartAt = nullptr;
    d.ruleSensorStopAt = nullptr;
    d.ruleScheduleStartAt = nullptr;
    return d;
}

// Common fields that must be set on EVERY rule-driven action (start or stop).
static void assertCommonRuleFields(const DeviceDigitalOut& d, const char* ruleUid,
                                   RuleType type, int version) {
    TEST_ASSERT_TRUE(d.workByRule);
    TEST_ASSERT_NOT_NULL(d.ruleUid);
    TEST_ASSERT_EQUAL_STRING(ruleUid, d.ruleUid);
    TEST_ASSERT_TRUE(d.ruleType == type);
    TEST_ASSERT_EQUAL_INT(version, d.ruleVersion);
    TEST_ASSERT_TRUE(nonEmpty(d.switchedAt));
}

static Rule sensorRule() {
    Rule r;
    r.uid = "sen"; r.version = 7; r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1"; r.sensorUid = "s1"; r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED; r.direction = RuleDirection::DOWN;
    r.valueLow = 10; r.valueHigh = 20;
    return r;
}

static Rule averageRule() {
    Rule r;
    r.uid = "avg"; r.version = 3; r.ruleType = RuleType::AVERAGE_SENSOR;
    r.deviceUid = "dev-1"; r.mode = RuleMode::ON; r.state = RuleState::SYNCHRONIZED;
    r.direction = RuleDirection::DOWN; r.valueLow = 24; r.valueHigh = 26;
    RuleCondition c; c.sensorUids.push_back("s1"); c.sensorUids.push_back("s2");
    r.conditions.push_back(c);
    return r;
}

static Rule loopRule() {
    Rule r;
    r.uid = "loop"; r.version = 4; r.ruleType = RuleType::LOOP;
    r.deviceUid = "dev-1"; r.mode = RuleMode::ON; r.state = RuleState::SYNCHRONIZED;
    r.workingTime = 10; r.waitingTime = 20;
    return r;
}

static Rule scheduleRule() {
    Rule r;
    r.uid = "sch"; r.version = 2; r.ruleType = RuleType::SCHEDULE;
    r.deviceUid = "dev-1"; r.mode = RuleMode::ON; r.state = RuleState::SYNCHRONIZED;
    r.schedule.push_back(ScheduleItem(8 * 3600, 3600)); // 08:00..09:00
    return r;
}

// ===================== START =====================

static void test_sensor_start_fields(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors; sensors.values["s1"] = 25.0f;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp; ntp.setEpochTime(1700000000);
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);

    uc.executeRuleActions(sensorRule());

    assertCommonRuleFields(device, "sen", RuleType::SENSOR, 7);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_TRUE(device.isAuto);
    TEST_ASSERT_FALSE(device.isManual);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);
    TEST_ASSERT_FALSE(device.needToSwitchBack);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.sensorValue);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleSensorStartAt));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.ruleSensorValueStart);
}

static void test_average_start_fields(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors; sensors.values["s1"] = 27.0f; sensors.values["s2"] = 27.0f;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp; ntp.setEpochTime(1700000000);
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);

    uc.executeRuleActions(averageRule());

    assertCommonRuleFields(device, "avg", RuleType::AVERAGE_SENSOR, 3);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_TRUE(device.isAuto);
    TEST_ASSERT_FALSE(device.isManual);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(26.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(27.0f, device.sensorValue);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleSensorStartAt));
    TEST_ASSERT_EQUAL_FLOAT(27.0f, device.ruleSensorValueStart);
}

static void test_loop_start_fields(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp; ntp.setEpochTime(1700000000);
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);

    uc.executeRuleActions(loopRule());

    assertCommonRuleFields(device, "loop", RuleType::LOOP, 4);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_TRUE(device.isAuto);
    TEST_ASSERT_FALSE(device.isManual);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);
    TEST_ASSERT_TRUE(device.needToSwitchBack);
    TEST_ASSERT_EQUAL_UINT32(10000, device.durationOn);
    TEST_ASSERT_EQUAL_INT(20000, device.mixTime);
    TEST_ASSERT_EQUAL_INT(0, device.cyclesToSetPoint);
    TEST_ASSERT_EQUAL_INT(1, device.currentCycle);
}

static void test_schedule_start_fields(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp;
    ntp.setEpochTime(8 * 3600 + 300); // 08:05 -> inside window
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);

    uc.executeRuleActions(scheduleRule());

    assertCommonRuleFields(device, "sch", RuleType::SCHEDULE, 2);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_TRUE(device.isAuto);
    TEST_ASSERT_FALSE(device.isManual);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);
    TEST_ASSERT_TRUE(device.needToSwitchBack);
    TEST_ASSERT_EQUAL_INT(0, device.mixTime);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleScheduleStartAt));
    TEST_ASSERT_EQUAL_INT(3600, device.ruleScheduleDuration);
    TEST_ASSERT_EQUAL_UINT32(3300000, device.durationOn); // (09:00-08:05)=3300s
}

// ===================== STOP =====================

static void test_sensor_stop_fields(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp; ntp.setEpochTime(1700000000);
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);
    Rule r = sensorRule();

    sensors.values["s1"] = 25.0f; uc.executeRuleActions(r);   // ON
    sensors.values["s1"] = 5.0f;  uc.executeRuleActions(r);   // OFF

    assertCommonRuleFields(device, "sen", RuleType::SENSOR, 7);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_FALSE(device.isManual);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("setpoint_reach", device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, device.sensorValue);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleSensorStartAt));     // preserved
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.ruleSensorValueStart);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleSensorStopAt));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, device.ruleSensorValueStop);
}

static void test_average_stop_fields(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp; ntp.setEpochTime(1700000000);
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);
    Rule r = averageRule();

    sensors.values["s1"] = 27.0f; sensors.values["s2"] = 27.0f; uc.executeRuleActions(r); // ON
    sensors.values["s1"] = 20.0f; sensors.values["s2"] = 20.0f; uc.executeRuleActions(r); // OFF

    assertCommonRuleFields(device, "avg", RuleType::AVERAGE_SENSOR, 3);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("setpoint_reach", device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(26.0f, device.sensorValueHigh);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, device.sensorValue);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleSensorStartAt));     // preserved
    TEST_ASSERT_EQUAL_FLOAT(27.0f, device.ruleSensorValueStart);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleSensorStopAt));
}

static void test_schedule_stop_fields(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp;
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);
    Rule r = scheduleRule();

    ntp.setEpochTime(8 * 3600 + 300);  uc.executeRuleActions(r); // ON (inside window)
    ntp.setEpochTime(9 * 3600 + 600);  uc.executeRuleActions(r); // OFF (outside)

    assertCommonRuleFields(device, "sch", RuleType::SCHEDULE, 2);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("time_reach", device.ruleStopReason);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleScheduleStartAt));    // preserved by merge
    TEST_ASSERT_EQUAL_INT(3600, device.ruleScheduleDuration);
}

// ===================== SPECIAL (via loop) =====================

static void test_protective_shutdown_fields(void) {
    MockRuleRepository repo;
    Rule r = sensorRule(); r.protectiveShutdown = true; r.timeToSetPoint = 1;
    repo.rules = {r};

    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors; sensors.values["s1"] = 25.0f; sensors.lastReadTimes["s1"] = 1;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp; ntp.setEpochTime(1700000000);
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000); uc.loop();                 // ON (timeOn=2000)
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    arduino.setMillis(5000); uc.loop();                 // elapsed 3s > 1s -> protective

    assertCommonRuleFields(device, "sen", RuleType::SENSOR, 7);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("protective_shutdown", device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, device.sensorValueLow);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, device.sensorValueHigh);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleSensorStartAt));     // preserved
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.ruleSensorValueStart);
}

static void test_sensor_unreachable_fields(void) {
    MockRuleRepository repo;
    repo.rules = {sensorRule()};

    DeviceDigitalOut device = makeDevice();
    CFDeviceModel model; model.device = &device;
    MockSensorValueModel sensors; sensors.values["s1"] = 25.0f; sensors.lastReadTimes["s1"] = 1;
    MockArduinoAdapter arduino; MockNTPTimeAdapter ntp; ntp.setEpochTime(1700000000);
    EvaluateRulesUseCase uc(&model, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000); uc.loop();                 // fresh -> ON
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    arduino.setMillis(2000 + 400000); uc.loop();        // age > 300000 -> stale

    assertCommonRuleFields(device, "sen", RuleType::SENSOR, 7);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("sensor_unreachable", device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("s1", device.sensorUid);
    TEST_ASSERT_TRUE(nonEmpty(device.ruleSensorStartAt));     // preserved
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.ruleSensorValueStart);
}

} // namespace

extern "C" void run_rule_completion_fields_tests(void) {
    RUN_TEST(test_sensor_start_fields);
    RUN_TEST(test_average_start_fields);
    RUN_TEST(test_loop_start_fields);
    RUN_TEST(test_schedule_start_fields);
    RUN_TEST(test_sensor_stop_fields);
    RUN_TEST(test_average_stop_fields);
    RUN_TEST(test_schedule_stop_fields);
    RUN_TEST(test_protective_shutdown_fields);
    RUN_TEST(test_sensor_unreachable_fields);
}
