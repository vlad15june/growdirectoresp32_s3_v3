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
#include "Infrastructure/Adapters/Json/JsonConverter.h"

#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"

class OperationTimeDeviceModel : public IDeviceModel {
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

static Rule makeSensorRuleWithWindow(time_t startSec, time_t endSec)
{
    Rule r;
    r.uid = "op-1";
    r.version = 1;
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.sensorUid = "sensor-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.direction = RuleDirection::DOWN;
    r.valueLow = 10;
    r.valueHigh = 20;
    r.startDayTime = startSec;
    r.endDayTime = endSec;
    return r;
}

static void test_daytime_window_gates_sensor_rule(void)
{
    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    OperationTimeDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    Rule rule = makeSensorRuleWithWindow(8 * 3600, 10 * 3600);

    ntp.setEpochTime(9 * 3600);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    ntp.setEpochTime(11 * 3600);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("time_reach", device.ruleStopReason);
}

static void test_night_window_gap_keeps_device_off(void)
{
    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    OperationTimeDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    Rule rule = makeSensorRuleWithWindow(16 * 3600 + 19 * 60, 16 * 3600 + 15 * 60);

    ntp.setEpochTime(16 * 3600 + 20 * 60);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    ntp.setEpochTime(16 * 3600 + 16 * 60);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_EQUAL_STRING("time_reach", device.ruleStopReason);

    ntp.setEpochTime(16 * 3600 + 10 * 60);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_no_window_no_days_runs_anytime(void)
{
    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    OperationTimeDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    Rule rule = makeSensorRuleWithWindow(0, 0);

    ntp.setEpochTime(3 * 3600);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_days_of_week_gate(void)
{
    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    OperationTimeDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    Rule rule = makeSensorRuleWithWindow(0, 0);
    rule.daysOfWeek.push_back(1);

    ntp.setEpochTime(4 * 86400 + 9 * 3600);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    ntp.setEpochTime(5 * 86400 + 9 * 3600);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_EQUAL_STRING("rule_off_by_date", device.ruleStopReason);
}

static void test_operation_time_converter_round_trip(void)
{
    JsonConverter converter;
    Rule rule;
    rule.uid = "op-rt";
    rule.version = 1;
    rule.ruleType = RuleType::SENSOR;
    rule.deviceUid = "dev-1";
    rule.sensorUid = "sensor-1";
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.startDayTime = 16 * 3600 + 19 * 60;
    rule.endDayTime = 16 * 3600 + 15 * 60;

    std::string json = converter.ruleToJson(rule);

    Rule parsed;
    TEST_ASSERT_TRUE(converter.ruleFromJson(json, parsed));
    TEST_ASSERT_EQUAL_INT(16 * 3600 + 19 * 60, static_cast<int>(parsed.startDayTime));
    TEST_ASSERT_EQUAL_INT(16 * 3600 + 15 * 60, static_cast<int>(parsed.endDayTime));
}

extern "C" void run_rule_operation_time_tests(void)
{
    RUN_TEST(test_daytime_window_gates_sensor_rule);
    RUN_TEST(test_night_window_gap_keeps_device_off);
    RUN_TEST(test_no_window_no_days_runs_anytime);
    RUN_TEST(test_days_of_week_gate);
    RUN_TEST(test_operation_time_converter_round_trip);
}
