#include <unity.h>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Infrastructure/Repositories/RuleRepository.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"

#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"

class SchedulePeriodsDeviceModel : public IDeviceModel {
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

static Rule makeTwoPeriodScheduleRule()
{
    Rule r;
    r.uid = "sch-1";
    r.version = 1;
    r.ruleType = RuleType::SCHEDULE;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.schedule.push_back(ScheduleItem(8 * 3600, 60));
    r.schedule.push_back(ScheduleItem(8 * 3600 + 300, 60));
    return r;
}

static void test_each_period_retriggers_device(void)
{
    MockRuleRepository repo;
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    SchedulePeriodsDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);
    Rule rule = makeTwoPeriodScheduleRule();

    ntp.setEpochTime(8 * 3600 + 30);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);

    ntp.setEpochTime(8 * 3600 + 90);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("time_reach", device.ruleStopReason);

    ntp.setEpochTime(8 * 3600 + 330);
    uc.executeRuleActions(rule);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);
}

static void test_schedule_rule_active_outside_window(void)
{
    MockSpiffsRuleDao dao;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(8 * 3600 + 90);
    RuleRepository repo(&dao, &ntp);
    repo.synchronizeRules({makeTwoPeriodScheduleRule()});

    std::vector<Rule> active = repo.getActiveRules();

    bool found = false;
    for (const auto& r : active) {
        if (r.uid == "sch-1") found = true;
    }
    TEST_ASSERT_TRUE(found);
}

extern "C" void run_rule_schedule_periods_tests(void)
{
    RUN_TEST(test_each_period_retriggers_device);
    RUN_TEST(test_schedule_rule_active_outside_window);
}
