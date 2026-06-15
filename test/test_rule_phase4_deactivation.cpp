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

class DeactDeviceModel : public IDeviceModel {
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

class FilteringRuleRepo : public MockRuleRepository {
public:
    std::vector<Rule> getActiveRules() override {
        std::vector<Rule> out;
        for (const auto& r : rules) {
            if (r.mode == RuleMode::ON && r.state != RuleState::DELETING) {
                out.push_back(r);
            }
        }
        return out;
    }
};

static Rule makeOnSensorRule() {
    Rule r;
    r.uid = "u1";
    r.version = 3;
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

static void test_disabling_rule_switches_device_off(void) {
    FilteringRuleRepo repo;
    repo.rules = {makeOnSensorRule()};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    sensors.lastReadTimes["sensor-1"] = 1;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    repo.rules[0].mode = RuleMode::OFF;
    arduino.setMillis(4000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("rule_manualy_off", device.ruleStopReason);
}

static void test_active_rule_not_deactivated(void) {
    FilteringRuleRepo repo;
    repo.rules = {makeOnSensorRule()};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    sensors.lastReadTimes["sensor-1"] = 1;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();
    arduino.setMillis(4000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_disabling_rule_with_device_off_no_action(void) {
    FilteringRuleRepo repo;
    Rule r = makeOnSensorRule();
    r.mode = RuleMode::OFF;
    repo.rules = {r};

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;

    DeactDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_NULL(device.ruleStopReason);
}

extern "C" void run_rule_phase4_deactivation_tests(void) {
    RUN_TEST(test_disabling_rule_switches_device_off);
    RUN_TEST(test_active_rule_not_deactivated);
    RUN_TEST(test_disabling_rule_with_device_off_no_action);
}
