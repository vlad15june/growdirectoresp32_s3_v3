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
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Core/Domain/Rules/MockRuleStateNotifier.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"

class ProtectiveDeviceModel : public IDeviceModel {
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

static Rule makeProtectiveRule(bool protectiveShutdown, int timeToSetPoint) {
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
    r.protectiveShutdown = protectiveShutdown;
    r.timeToSetPoint = timeToSetPoint;
    return r;
}

static Rule makeStepProtectiveRule(bool protectiveShutdown, int cyclesToSetPoint) {
    Rule r;
    r.uid = "u1";
    r.version = 7;
    r.ruleType = RuleType::STEP;
    r.deviceUid = "dev-1";
    r.sensorUid = "sensor-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.direction = RuleDirection::DOWN;
    r.valueTarget = 25;
    r.workingTime = 10;
    r.waitingTime = 20;
    r.cyclesToSetPoint = cyclesToSetPoint;
    r.protectiveShutdown = protectiveShutdown;
    return r;
}

static void test_protective_shutdown_triggers_after_time_to_setpoint(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeProtectiveRule(true, 10));

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    ProtectiveDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    sensors.lastReadTimes["sensor-1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_NOT_EQUAL(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);

    arduino.setMillis(405000);
    uc.loop();
    TEST_ASSERT_NOT_EQUAL(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);

    arduino.setMillis(411000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_EQUAL_STRING("protective_shutdown", device.ruleStopReason);
    TEST_ASSERT_TRUE(device.workByRule);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_TRUE(notifier.protectiveShutdownCalled);
    TEST_ASSERT_EQUAL_INT(1, notifier.protectiveShutdownCount);
    TEST_ASSERT_EQUAL_STRING("u1", notifier.lastProtectiveShutdownRuleUid.c_str());

    Rule stored;
    for (const auto& r : repo.getAll()) {
        if (r.uid == "u1") stored = r;
    }
    TEST_ASSERT_TRUE(stored.state == RuleState::ERROR);
    TEST_ASSERT_EQUAL_STRING("protective_shutdown", stored.errorType.c_str());
}

static void test_no_shutdown_when_protective_disabled(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeProtectiveRule(false, 10));

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    ProtectiveDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    sensors.lastReadTimes["sensor-1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_NOT_EQUAL(DEVICE_STATE_OFF, device.state);

    arduino.setMillis(411000);
    uc.loop();
    TEST_ASSERT_NOT_EQUAL(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);
}

static void test_no_shutdown_when_time_to_setpoint_zero(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeProtectiveRule(true, 0));

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    ProtectiveDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 25.0f;
    sensors.lastReadTimes["sensor-1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();
    arduino.setMillis(411000);
    uc.loop();
    TEST_ASSERT_NOT_EQUAL(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);
}

static void test_gate_includes_rule_with_protective_config(void) {
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);

    Rule r = makeProtectiveRule(true, 10);
    repo.save(r);

    std::vector<Rule> active = repo.getActiveRules();
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(active.size()));
}

static void test_gate_excludes_waiting_for_user_confirm(void) {
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);

    Rule r = makeProtectiveRule(true, 10);
    repo.save(r);
    repo.updateRuleMode("u1", RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM, "protective_shutdown");

    std::vector<Rule> active = repo.getActiveRules();
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(active.size()));
}

static void test_gate_excludes_error_protective_shutdown(void) {
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);

    Rule r = makeProtectiveRule(true, 10);
    repo.save(r);
    repo.updateRuleError("u1", RuleState::ERROR, "protective_shutdown", "protective_shutdown");

    std::vector<Rule> active = repo.getActiveRules();
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(active.size()));
}

static void test_confirm_clears_protective_shutdown_on_higher_version(void) {
    MockSpiffsRuleDao dao;
    RuleRepository repo(&dao);

    repo.save(makeProtectiveRule(true, 10));
    repo.updateRuleError("u1", RuleState::ERROR, "protective_shutdown", "protective_shutdown");
    repo.updateRuleMode("u1", RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM, "protective_shutdown");

    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(repo.getActiveRules().size()));

    Rule confirm = makeProtectiveRule(false, 10);
    confirm.version = 8;
    confirm.mode = RuleMode::ON;
    repo.synchronizeRules({confirm});

    std::vector<Rule> active = repo.getActiveRules();
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(active.size()));
    TEST_ASSERT_TRUE(active[0].mode == RuleMode::ON);
}

static void test_step_protective_shutdown_triggers_on_cycle_cap(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeStepProtectiveRule(true, 2));

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_STEP_WAITING;
    device.ruleUid = "u1";
    device.cyclesToSetPoint = 2;
    device.currentCycle = 2;
    device.isRuleTurnOnDevice = true;
    ProtectiveDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 30.0f;
    sensors.lastReadTimes["sensor-1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_EQUAL_STRING("protective_shutdown", device.ruleStopReason);
    TEST_ASSERT_TRUE(device.workByRule);
    TEST_ASSERT_FALSE(device.isAuto);
    TEST_ASSERT_EQUAL_INT(0, device.currentCycle);
    TEST_ASSERT_TRUE(notifier.protectiveShutdownCalled);
    TEST_ASSERT_EQUAL_INT(1, notifier.protectiveShutdownCount);
    TEST_ASSERT_EQUAL_STRING("u1", notifier.lastProtectiveShutdownRuleUid.c_str());

    Rule stored;
    for (const auto& r : repo.getAll()) {
        if (r.uid == "u1") stored = r;
    }
    TEST_ASSERT_TRUE(stored.state == RuleState::ERROR);
    TEST_ASSERT_EQUAL_STRING("protective_shutdown", stored.errorType.c_str());

    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(1, notifier.protectiveShutdownCount);
}

static void test_step_no_shutdown_when_protective_disabled(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeStepProtectiveRule(false, 2));

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_STEP_WAITING;
    device.ruleUid = "u1";
    device.cyclesToSetPoint = 2;
    device.currentCycle = 2;
    device.isRuleTurnOnDevice = true;
    ProtectiveDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 30.0f;
    sensors.lastReadTimes["sensor-1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);
}

static void test_step_no_shutdown_below_cycle_cap(void) {
    MockRuleRepository repo;
    repo.rules.push_back(makeStepProtectiveRule(true, 2));

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_STEP_WAITING;
    device.ruleUid = "u1";
    device.cyclesToSetPoint = 2;
    device.currentCycle = 1;
    device.isRuleTurnOnDevice = true;
    ProtectiveDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["sensor-1"] = 30.0f;
    sensors.lastReadTimes["sensor-1"] = 400000;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);
}

extern "C" void run_rule_protective_shutdown_tests(void) {
    RUN_TEST(test_protective_shutdown_triggers_after_time_to_setpoint);
    RUN_TEST(test_step_protective_shutdown_triggers_on_cycle_cap);
    RUN_TEST(test_step_no_shutdown_when_protective_disabled);
    RUN_TEST(test_step_no_shutdown_below_cycle_cap);
    RUN_TEST(test_no_shutdown_when_protective_disabled);
    RUN_TEST(test_no_shutdown_when_time_to_setpoint_zero);
    RUN_TEST(test_gate_includes_rule_with_protective_config);
    RUN_TEST(test_gate_excludes_waiting_for_user_confirm);
    RUN_TEST(test_gate_excludes_error_protective_shutdown);
    RUN_TEST(test_confirm_clears_protective_shutdown_on_higher_version);
}
