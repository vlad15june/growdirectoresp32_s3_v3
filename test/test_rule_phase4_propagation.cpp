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

class Phase4DeviceModel : public IDeviceModel {
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

static Rule makeDownSensorRule() {
    Rule r;
    r.uid = "u1";
    r.version = 7;
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

static void test_sensor_turn_on_propagates_context(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.workByRule = false;
    device.isAuto = false;

    Phase4DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    Rule rule = makeDownSensorRule();
    uc.executeRuleActions(rule);

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_TRUE(device.workByRule);
    TEST_ASSERT_TRUE(device.isAuto);
    TEST_ASSERT_TRUE(device.ruleType == RuleType::SENSOR);
    TEST_ASSERT_EQUAL_INT(7, device.ruleVersion);
    TEST_ASSERT_NOT_NULL(device.ruleUid);
    TEST_ASSERT_EQUAL_STRING("u1", device.ruleUid);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, device.sensorValue);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);
}

static void test_sensor_turn_off_sets_setpoint_reach(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;

    Phase4DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    Rule rule = makeDownSensorRule();

    sensors.values["sensor-1"] = 25.0f;
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    sensors.values["sensor-1"] = 5.0f;
    uc.executeRuleActions(rule);

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("setpoint_reach", device.ruleStopReason);
}

static void test_sensor_deadband_no_device_change(void) {
    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    Phase4DeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 15.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    Rule rule = makeDownSensorRule();
    uc.executeRuleActions(rule);

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.workByRule);
}

extern "C" void run_rule_phase4_propagation_tests(void) {
    RUN_TEST(test_sensor_turn_on_propagates_context);
    RUN_TEST(test_sensor_turn_off_sets_setpoint_reach);
    RUN_TEST(test_sensor_deadband_no_device_change);
}
