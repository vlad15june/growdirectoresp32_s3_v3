// The rule snapshot must never outlive what it mirrors.
//
// EvaluateRulesUseCase keeps a snapshot of the rule registry between evaluation ticks and
// re-reads the repository only when its revision or the calendar day changes. The snapshot is
// an optimization with one failure mode worse than the copying it replaced: a missed
// invalidation path means the rules run against a dead list. Each test here takes one path a
// registry can change through - a new rule saved, a rule switched off, a rule marked for
// deletion, the calendar day rolling over a day-of-week filter - brings the evaluator to a
// steady state first, applies the change, and requires the very next tick to act on it.

#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"

namespace {

// Midday of a fixed UTC day; the fixture pins TZ=UTC, so a +24h jump moves the calendar day
// by exactly one.
const unsigned long kEpochStart = 1609502400UL;  // 2021-01-01 12:00:00 UTC

// Pins TZ=UTC for the lifetime of a fixture and restores the host's setting afterwards.
//
// The day tests must behave the same on every host, and the code under test resolves days
// through two different lenses it inherited: the active-rule filter uses localtime_r
// (RuleRepository::getActiveRules), the per-rule activity window uses gmtime_r
// (EvaluateRulesUseCase::isRuleActiveNow). On a host where the local calendar day differs
// from the UTC day (TZ >= UTC+12 at this epoch), a rule admitted by the filter is rejected
// by the window and the assertions below would measure the host's timezone instead of the
// snapshot's invalidation. With TZ=UTC both lenses agree by construction.
struct ScopedUtcTimezone {
    std::string savedValue;
    bool hadValue;

    ScopedUtcTimezone() {
        const char* current = getenv("TZ");
        hadValue = current != nullptr;
        if (hadValue) {
            savedValue = current;
        }
        setenv("TZ", "UTC", 1);
        tzset();
    }

    ~ScopedUtcTimezone() {
        if (hadValue) {
            setenv("TZ", savedValue.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }
};

const char* kFirstRuleUid = "rule-first-aaaaaaaa-bbbb-cccc-dddd-0001";
const char* kSecondRuleUid = "rule-second-aaaaaaaa-bbbb-cccc-dddd-002";
const char* kFirstDevice = "device-first-aaaaaaaa-bbbb-cccc-dddd-01";
const char* kSecondDevice = "device-second-aaaaaaaa-bbbb-cccc-dddd-2";
const char* kFirstSensor = "sensor-first-aaaaaaaa-bbbb-cccc-dddd-01";
const char* kSecondSensor = "sensor-second-aaaaaaaa-bbbb-cccc-dddd-2";

struct InvalidationFixture {
    ScopedUtcTimezone utcTimezone;  // first member: everything below runs under TZ=UTC
    MockSpiffsRuleDao dao;
    MockNTPTimeAdapter ntp;
    RuleRepository repo;
    MockDeviceModel deviceModel;
    MockSensorValueModel sensorValues;
    MockArduinoAdapter arduino;
    EvaluateRulesUseCase useCase;

    DeviceDigitalOut firstDevice{};
    DeviceDigitalOut secondDevice{};

    unsigned long epoch;

    InvalidationFixture()
        : repo(&dao, &ntp),
          useCase(&deviceModel, &repo, &arduino, &ntp, &sensorValues),
          epoch(kEpochStart) {
        addDevice(firstDevice, kFirstDevice);
        addDevice(secondDevice, kSecondDevice);

        sensorValues.values[kFirstSensor] = 25.0f;
        sensorValues.values[kSecondSensor] = 25.0f;
        sensorValues.lastReadTimes[kFirstSensor] = 500;
        sensorValues.lastReadTimes[kSecondSensor] = 500;

        arduino.setMillis(1000);
        ntp.setEpochTime(epoch);
    }

    void addDevice(DeviceDigitalOut& device, const char* uid) {
        device.deviceUid = uid;
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = DEVICE_STATE_OFF;
        deviceModel.addDevice(&device);
    }

    // A sensor rule that holds its device on while the reading (25) stays above valueHigh.
    static Rule holdingSensorRule(const char* uid, const char* deviceUid, const char* sensorUid) {
        Rule rule;
        rule.uid = uid;
        rule.version = 1;
        rule.deviceUid = deviceUid;
        rule.sensorUid = sensorUid;
        rule.ruleType = RuleType::SENSOR;
        rule.mode = RuleMode::ON;
        rule.state = RuleState::SYNCHRONIZED;
        rule.direction = RuleDirection::DOWN;
        rule.valueLow = 10;
        rule.valueHigh = 20;
        return rule;
    }

    void tick() {
        arduino.setMillis(arduino.currentMillis + 1000);
        epoch += 1;
        ntp.setEpochTime(epoch);
        useCase.loop();
    }

    void settle() {
        for (int i = 0; i < 5; ++i) {
            tick();
        }
    }
};

}  // namespace

void test_a_rule_saved_after_the_snapshot_is_evaluated_on_the_next_tick(void) {
    InvalidationFixture f;
    f.repo.save(InvalidationFixture::holdingSensorRule(kFirstRuleUid, kFirstDevice, kFirstSensor));
    f.settle();
    TEST_ASSERT_EQUAL(DEVICE_STATE_REQUEST_ON, f.firstDevice.state);
    TEST_ASSERT_EQUAL(DEVICE_STATE_OFF, f.secondDevice.state);

    f.repo.save(InvalidationFixture::holdingSensorRule(kSecondRuleUid, kSecondDevice, kSecondSensor));

    f.tick();
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_ON, f.secondDevice.state,
        "a newly saved rule must be picked up by the very next evaluation tick");
}

void test_a_rule_delivered_by_cloud_sync_is_evaluated_on_the_next_tick(void) {
    InvalidationFixture f;
    f.repo.save(InvalidationFixture::holdingSensorRule(kFirstRuleUid, kFirstDevice, kFirstSensor));
    f.settle();
    TEST_ASSERT_EQUAL(DEVICE_STATE_REQUEST_ON, f.firstDevice.state);
    TEST_ASSERT_EQUAL(DEVICE_STATE_OFF, f.secondDevice.state);

    // The cloud path: synchronizeRules is how a rule reaches the module in production
    // (rule push over MQTT and the periodic sync both land here).
    std::vector<Rule> incoming;
    incoming.push_back(InvalidationFixture::holdingSensorRule(kSecondRuleUid, kSecondDevice, kSecondSensor));
    TEST_ASSERT_EQUAL(1, f.repo.synchronizeRules(incoming));

    f.tick();
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_ON, f.secondDevice.state,
        "a rule delivered by cloud synchronization must be picked up by the very next tick");
}

void test_a_rule_sent_to_error_after_the_snapshot_enters_recovery_on_the_next_tick(void) {
    InvalidationFixture f;
    f.repo.save(InvalidationFixture::holdingSensorRule(kFirstRuleUid, kFirstDevice, kFirstSensor));
    f.settle();
    TEST_ASSERT_EQUAL(DEVICE_STATE_REQUEST_ON, f.firstDevice.state);

    // Send the rule to the sensor_unreachable error state through the repository, as the
    // staleness transition does. The evaluator only enters its recovery branch for a rule
    // whose snapshot entry shows that state; the sensor is fresh here, so the recovery must
    // fire on the next tick and write the rule back to SYNCHRONIZED. A snapshot that missed
    // the error write keeps evaluating the rule as healthy and the repository stays in ERROR.
    TEST_ASSERT_TRUE(f.repo.updateRuleError(kFirstRuleUid, RuleState::ERROR, "sensor_unreachable", "stale"));

    f.tick();
    Utils::Optional<Rule> recovered = f.repo.get(kFirstRuleUid);
    TEST_ASSERT_TRUE(recovered.has_value());
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(RuleState::SYNCHRONIZED),
        static_cast<int>(recovered.value().state),
        "the tick after the error write must see the ERROR state and recover the rule");
    TEST_ASSERT_EQUAL_STRING("", recovered.value().errorType.c_str());
}

void test_a_rule_switched_off_after_the_snapshot_releases_its_device_on_the_next_tick(void) {
    InvalidationFixture f;
    f.repo.save(InvalidationFixture::holdingSensorRule(kFirstRuleUid, kFirstDevice, kFirstSensor));
    f.settle();
    TEST_ASSERT_EQUAL(DEVICE_STATE_REQUEST_ON, f.firstDevice.state);

    f.repo.updateRuleMode(kFirstRuleUid, RuleMode::OFF, "user");

    f.tick();
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_OFF, f.firstDevice.state,
        "a rule switched off must release its device on the very next evaluation tick");
    TEST_ASSERT_NOT_NULL(f.firstDevice.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("rule_manualy_off", f.firstDevice.ruleStopReason);
}

void test_a_rule_marked_deleting_after_the_snapshot_is_purged_on_the_next_tick(void) {
    InvalidationFixture f;
    f.repo.save(InvalidationFixture::holdingSensorRule(kFirstRuleUid, kFirstDevice, kFirstSensor));
    f.settle();
    TEST_ASSERT_EQUAL(DEVICE_STATE_REQUEST_ON, f.firstDevice.state);

    Rule deleting = InvalidationFixture::holdingSensorRule(kFirstRuleUid, kFirstDevice, kFirstSensor);
    deleting.state = RuleState::DELETING;
    f.repo.save(deleting);

    f.tick();
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_OFF, f.firstDevice.state,
        "a rule marked for deletion must switch its device off on the very next evaluation tick");
    TEST_ASSERT_FALSE_MESSAGE(f.repo.exists(kFirstRuleUid),
        "the deleting rule must be purged from the repository");
}

void test_the_day_rolling_over_admits_a_rule_scheduled_for_that_day(void) {
    InvalidationFixture f;

    // A loop rule bound to TOMORROW's day of week (asked from localtime, which the fixture
    // pins to UTC): the active-rule filter excludes it today, so it is not in the snapshot at
    // all - only the day rolling over can bring it in, without any repository write. A
    // snapshot invalidated by repository revision alone would keep this rule invisible
    // forever.
    time_t startEpoch = static_cast<time_t>(kEpochStart);
    struct tm startDay;
    localtime_r(&startEpoch, &startDay);

    Rule loop;
    loop.uid = kFirstRuleUid;
    loop.version = 1;
    loop.deviceUid = kFirstDevice;
    loop.ruleType = RuleType::LOOP;
    loop.mode = RuleMode::ON;
    loop.state = RuleState::SYNCHRONIZED;
    loop.workingTime = 5;
    loop.waitingTime = 5;
    loop.daysOfWeek.push_back((startDay.tm_wday + 1) % 7);
    f.repo.save(loop);

    f.settle();
    TEST_ASSERT_EQUAL(DEVICE_STATE_OFF, f.firstDevice.state);

    // Midnight passes: nothing was written to the repository, only the calendar moved.
    f.epoch += 24 * 3600;

    f.tick();
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_STEP_ON, f.firstDevice.state,
        "a rule whose day arrived must start on the tick after the day rolled over");
}

void test_the_day_rolling_over_drops_a_day_filtered_rule_without_any_repository_write(void) {
    InvalidationFixture f;

    // A running loop rule bound to today's day of week (asked from localtime, which the
    // fixture pins to UTC).
    time_t startEpoch = static_cast<time_t>(kEpochStart);
    struct tm startDay;
    localtime_r(&startEpoch, &startDay);

    Rule loop;
    loop.uid = kFirstRuleUid;
    loop.version = 1;
    loop.deviceUid = kFirstDevice;
    loop.ruleType = RuleType::LOOP;
    loop.mode = RuleMode::ON;
    loop.state = RuleState::SYNCHRONIZED;
    loop.workingTime = 5;
    loop.waitingTime = 5;
    loop.daysOfWeek.push_back(startDay.tm_wday);
    f.repo.save(loop);

    f.settle();
    TEST_ASSERT_EQUAL(DEVICE_STATE_REQUEST_STEP_ON, f.firstDevice.state);

    // Midnight passes: nothing was written to the repository, only the calendar moved.
    f.epoch += 24 * 3600;

    f.tick();
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_OFF, f.firstDevice.state,
        "a day-of-week rule must stop cycling on the tick after the day rolled over");
    TEST_ASSERT_NOT_NULL(f.firstDevice.ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("rule_off_by_date", f.firstDevice.ruleStopReason);
}

extern "C" void run_rule_snapshot_invalidation_tests(void) {
    RUN_TEST(test_a_rule_saved_after_the_snapshot_is_evaluated_on_the_next_tick);
    RUN_TEST(test_a_rule_delivered_by_cloud_sync_is_evaluated_on_the_next_tick);
    RUN_TEST(test_a_rule_sent_to_error_after_the_snapshot_enters_recovery_on_the_next_tick);
    RUN_TEST(test_a_rule_switched_off_after_the_snapshot_releases_its_device_on_the_next_tick);
    RUN_TEST(test_a_rule_marked_deleting_after_the_snapshot_is_purged_on_the_next_tick);
    RUN_TEST(test_the_day_rolling_over_admits_a_rule_scheduled_for_that_day);
    RUN_TEST(test_the_day_rolling_over_drops_a_day_filtered_rule_without_any_repository_write);
}
