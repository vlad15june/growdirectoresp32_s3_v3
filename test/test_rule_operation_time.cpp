// Arduino.h is stubbed for the host in test/Mock/native (see platformio.ini,
// env:native). It has to come first: ArduinoJsonConverter.h pulls in headers
// that expect the Arduino core to be present.
#include <Arduino.h>

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
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"

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

// ---------------------------------------------------------------------------
// GDV1-3807: the Operation Time window must reach every rule type, not only
// sensor rules. These cases go through ArduinoJsonConverter - the converter
// that is actually wired into the firmware (src/globals.h) - because the
// legacy JsonConverter used above parses the window unconditionally and would
// stay green while the production path is broken.
// ---------------------------------------------------------------------------

// Window 15:00:00 - 15:20:00 UTC, exactly how the backend sends it.
static const time_t kWindowStartSec = 15 * 3600;
static const time_t kWindowEndSec = 15 * 3600 + 20 * 60;

static const char *kLoopRuleJson =
    "{\"uid\":\"op-loop\",\"version\":1,\"ruleType\":\"loop\",\"deviceUid\":\"dev-1\","
    "\"mode\":\"On\",\"state\":\"Synchronized\",\"workingTime\":15,\"waitingTime\":10,"
    "\"daysOfWeek\":[],\"daysOfMonth\":[],"
    "\"startDayTime\":\"2026-08-11T15:00:00.000Z\",\"endDayTime\":\"2026-08-11T15:20:00.000Z\"}";

static const char *kStepRuleJson =
    "{\"uid\":\"op-step\",\"version\":1,\"ruleType\":\"step\",\"deviceUid\":\"dev-1\","
    "\"sensorUid\":\"sensor-1\",\"mode\":\"On\",\"state\":\"Synchronized\",\"direction\":\"down\","
    "\"valueTarget\":25,\"workingTime\":10,\"waitingTime\":20,\"cyclesToSetPoint\":5,"
    "\"daysOfWeek\":[],\"daysOfMonth\":[],"
    "\"startDayTime\":\"2026-08-11T15:00:00.000Z\",\"endDayTime\":\"2026-08-11T15:20:00.000Z\"}";

static const char *kDimmerRuleJson =
    "{\"uid\":\"op-dimmer\",\"version\":1,\"ruleType\":\"dimmer_manual\",\"deviceUid\":\"dev-1\","
    "\"mode\":\"On\",\"state\":\"Synchronized\",\"daysOfWeek\":[],\"daysOfMonth\":[],"
    "\"startDayTime\":\"2026-08-11T15:00:00.000Z\",\"endDayTime\":\"2026-08-11T15:20:00.000Z\"}";

static void test_arduino_converter_parses_window_for_loop_rule(void)
{
    ArduinoJsonConverter converter;
    Rule parsed;

    TEST_ASSERT_TRUE(converter.ruleFromJson(kLoopRuleJson, parsed));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RuleType::LOOP), static_cast<int>(parsed.ruleType));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kWindowStartSec), static_cast<int>(parsed.startDayTime));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kWindowEndSec), static_cast<int>(parsed.endDayTime));
}

static void test_arduino_converter_parses_window_for_step_rule(void)
{
    ArduinoJsonConverter converter;
    Rule parsed;

    TEST_ASSERT_TRUE(converter.ruleFromJson(kStepRuleJson, parsed));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RuleType::STEP), static_cast<int>(parsed.ruleType));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kWindowStartSec), static_cast<int>(parsed.startDayTime));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kWindowEndSec), static_cast<int>(parsed.endDayTime));
}

static void test_loop_rule_from_json_stops_outside_window(void)
{
    ArduinoJsonConverter converter;
    Rule rule;
    TEST_ASSERT_TRUE(converter.ruleFromJson(kLoopRuleJson, rule));

    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    OperationTimeDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    // Inside the window the loop starts cycling.
    ntp.setEpochTime(kWindowStartSec + 10 * 60);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);

    // Past the end of the window it must stop, not keep cycling forever.
    ntp.setEpochTime(kWindowEndSec + 10 * 60);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("time_reach", device.ruleStopReason);
}

static void test_step_rule_from_json_stops_outside_window(void)
{
    ArduinoJsonConverter converter;
    Rule rule;
    TEST_ASSERT_TRUE(converter.ruleFromJson(kStepRuleJson, rule));

    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    OperationTimeDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 30.0f;
    sensors.lastReadTimes["sensor-1"] = 400000;
    MockArduinoAdapter arduino;
    arduino.setMillis(400000);
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    ntp.setEpochTime(kWindowStartSec + 10 * 60);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);

    ntp.setEpochTime(kWindowEndSec + 10 * 60);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_EQUAL_STRING("time_reach", device.ruleStopReason);
}

static void test_arduino_converter_round_trip_keeps_window_for_loop_rule(void)
{
    ArduinoJsonConverter converter;

    Rule rule;
    rule.uid = "op-loop-rt";
    rule.version = 1;
    rule.ruleType = RuleType::LOOP;
    rule.deviceUid = "dev-1";
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.workingTime = 15;
    rule.waitingTime = 10;
    rule.startDayTime = kWindowStartSec;
    rule.endDayTime = kWindowEndSec;

    std::string json = converter.ruleToJson(rule);

    Rule parsed;
    TEST_ASSERT_TRUE(converter.ruleFromJson(json, parsed));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kWindowStartSec), static_cast<int>(parsed.startDayTime));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kWindowEndSec), static_cast<int>(parsed.endDayTime));
}

static void test_dimmer_rule_from_json_passes_validation(void)
{
    ArduinoJsonConverter converter;
    Rule rule;
    TEST_ASSERT_TRUE(converter.ruleFromJson(kDimmerRuleJson, rule));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kWindowStartSec), static_cast<int>(rule.startDayTime));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kWindowEndSec), static_cast<int>(rule.endDayTime));

    // DimmerManualRuleValidator rejects a zero window with "wrong_format",
    // so a dimmer rule could never validate while the window was dropped.
    RuleValidator validator;
    RuleValidationResult result = validator.validateRule(rule);
    TEST_ASSERT_TRUE(result.isValid);
    TEST_ASSERT_TRUE(result.errorType.empty());
}

extern "C" void run_rule_operation_time_tests(void)
{
    RUN_TEST(test_daytime_window_gates_sensor_rule);
    RUN_TEST(test_night_window_gap_keeps_device_off);
    RUN_TEST(test_no_window_no_days_runs_anytime);
    RUN_TEST(test_days_of_week_gate);
    RUN_TEST(test_operation_time_converter_round_trip);
    RUN_TEST(test_arduino_converter_parses_window_for_loop_rule);
    RUN_TEST(test_arduino_converter_parses_window_for_step_rule);
    RUN_TEST(test_loop_rule_from_json_stops_outside_window);
    RUN_TEST(test_step_rule_from_json_stops_outside_window);
    RUN_TEST(test_arduino_converter_round_trip_keeps_window_for_loop_rule);
    RUN_TEST(test_dimmer_rule_from_json_passes_validation);
}
