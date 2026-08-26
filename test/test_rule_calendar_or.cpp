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
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"

static const unsigned long CALENDAR_TUESDAY_18TH = 1787054400UL;
static const unsigned long CALENDAR_WEDNESDAY_19TH = CALENDAR_TUESDAY_18TH + 24UL * 3600UL;

class CalendarDeviceModel : public IDeviceModel {
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

static Rule makeCalendarScheduleRule(const std::string& uid,
                                     const std::vector<int>& daysOfWeek,
                                     const std::vector<int>& daysOfMonth)
{
    Rule r;
    r.uid = uid;
    r.version = 1;
    r.ruleType = RuleType::SCHEDULE;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.daysOfWeek = daysOfWeek;
    r.daysOfMonth = daysOfMonth;
    r.schedule.push_back(ScheduleItem(8 * 3600, 8 * 3600));
    return r;
}

static bool containsRule(const std::vector<Rule>& rules, const std::string& uid)
{
    for (const auto& r : rules) {
        if (r.uid == uid) {
            return true;
        }
    }
    return false;
}

/**
 * @brief The pre-filter combines daysOfWeek and daysOfMonth with OR, not with AND
 *
 * Drives the real RuleRepository::getActiveRules, not the shared mock that returns every rule.
 * Matching either array is enough for the rule to reach the evaluator; only a rule that matches
 * neither is dropped. The clock stands at CALENDAR_TUESDAY_18TH, which is 2026-08-18 12:00:00 UTC:
 * a Tuesday (tm_wday 2) on the 18th. The module runs on UTC (NTPTimeAdapter is built with
 * utcOffsetSeconds 0), so the pre-filter and the evaluator gate see the same day.
 */
static void test_prefilter_ors_the_two_calendar_arrays(void)
{
    MockSpiffsRuleDao dao;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(CALENDAR_TUESDAY_18TH);
    RuleRepository repo(&dao, &ntp);

    repo.save(makeCalendarScheduleRule("week-matches", {2}, {1}));
    repo.save(makeCalendarScheduleRule("month-matches", {5}, {18}));
    repo.save(makeCalendarScheduleRule("neither-matches", {5}, {1}));

    std::vector<Rule> active = repo.getActiveRules();

    TEST_ASSERT_TRUE(containsRule(active, "week-matches"));
    TEST_ASSERT_TRUE(containsRule(active, "month-matches"));
    TEST_ASSERT_FALSE(containsRule(active, "neither-matches"));
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(active.size()));
}

/**
 * @brief A schedule rule that leaves the active set switches its device off
 *
 * The rule runs on Tuesday and closes the relay; on Wednesday its daysOfWeek no longer matches, so
 * the pre-filter drops it and nothing evaluates it any more. The relay must not stay closed: the
 * device is switched off with rule_off_by_date on the next evaluation tick.
 */
static void test_dropped_schedule_rule_releases_its_device(void)
{
    MockSpiffsRuleDao dao;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(CALENDAR_TUESDAY_18TH);
    RuleRepository repo(&dao, &ntp);
    repo.save(makeCalendarScheduleRule("sch-tue", {2}, {}));

    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.ruleStopReason = nullptr;

    CalendarDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    MockArduinoAdapter arduino;
    arduino.setMillis(0);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    arduino.setMillis(2000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_TRUE(device.isRuleTurnOnDevice);

    ntp.setEpochTime(CALENDAR_WEDNESDAY_19TH);
    TEST_ASSERT_FALSE(containsRule(repo.getActiveRules(), "sch-tue"));

    arduino.setMillis(4000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device.state);
    TEST_ASSERT_FALSE(device.isRuleTurnOnDevice);
    TEST_ASSERT_NOT_NULL(device.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("rule_off_by_date", device.ruleStopReason);
}

extern "C" void run_rule_calendar_or_tests(void)
{
    RUN_TEST(test_prefilter_ors_the_two_calendar_arrays);
    RUN_TEST(test_dropped_schedule_rule_releases_its_device);
}
