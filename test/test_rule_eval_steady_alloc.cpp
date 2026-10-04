// The rule evaluation tick against the heap: a steady second must not allocate.
//
// EvaluateRulesUseCase::loop() runs every EVALUATION_INTERVAL_MS for the lifetime of the module.
// When nothing changed since the previous tick - same rules, same sensor picture, no state
// transitions - the tick's work is a pure re-computation, and every heap allocation it makes is
// churn: full copies of the rule registry, a validator built and thrown away per rule, per-tick
// scratch containers. Interleaved with the large TLS and JSON blocks of the network task, that
// churn is what fragments the device heap.
//
// The scenario below holds one rule of every evaluated kind in a live steady state (device held
// ON by a sensor rule, an AND of two conditions, an open schedule window, a running loop rule, a
// running tracked step rule), plus a switched-off rule for the deactivation sweep, evaluates
// until warm, and then meters one tick with HostAllocationCounter: the expected number of heap
// allocations for a tick that changes nothing is zero.
//
// Uids are deliberately longer than any small-string-optimization buffer, so that a copied
// std::string is always a real allocation and cannot hide inside the object.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Infrastructure/Configuration/StepCycleStore.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Sim/HostAllocationCounter.h"

namespace {

// 2021-01-01 00:00:00 UTC - midnight, so a schedule window opened at this epoch is wide open
// for the whole test regardless of how far the ticks advance.
const unsigned long kEpochStart = 1609459200UL;

// Longer than any SSO buffer (see file comment).
const char* kRuleOnUid = "rule-sensor-on-aaaaaaaa-bbbb-cccc-000001";
const char* kRuleOffUid = "rule-sensor-off-aaaaaaaa-bbbb-cccc-00002";
const char* kRuleCondUid = "rule-conditions-aaaaaaaa-bbbb-cccc-00003";
const char* kRuleSchedUid = "rule-schedule-aaaaaaaa-bbbb-cccc-000004";
const char* kRuleLoopUid = "rule-loop-aaaaaaaa-bbbb-cccc-dddd-00005";
const char* kRuleStepUid = "rule-step-aaaaaaaa-bbbb-cccc-dddd-00006";
const char* kRuleDroppedUid = "rule-dropped-aaaaaaaa-bbbb-cccc-000007";

const char* kDevOn = "device-held-on-aaaaaaaa-bbbb-cccc-00001";
const char* kDevOff = "device-held-off-aaaaaaaa-bbbb-cccc-0002";
const char* kDevCond = "device-conditions-aaaaaaaa-bbbb-cccc-03";
const char* kDevSched = "device-schedule-aaaaaaaa-bbbb-cccc-0004";
const char* kDevLoop = "device-loop-aaaaaaaa-bbbb-cccc-dddd-005";
// Kept at 23 characters: StepCycleStore's slot holds a 24-byte device uid, and a longer uid is
// truncated on write and then never matches on read - the counter would be re-written every
// tick. Still longer than the 15-byte SSO buffer of the device toolchain's std::string.
const char* kDevStep = "device-step-aaaa-bb-006";

const char* kSensorHigh = "sensor-reading-high-aaaaaaaa-bbbb-00001";
const char* kSensorLow = "sensor-reading-low-aaaaaaaa-bbbb-000002";
const char* kSensorCondA = "sensor-condition-a-aaaaaaaa-bbbb-000003";
const char* kSensorCondB = "sensor-condition-b-aaaaaaaa-bbbb-000004";
const char* kSensorStep = "sensor-step-target-aaaaaaaa-bbbb-000005";

Rule baseRule(const char* uid, const char* deviceUid, RuleType type) {
    Rule rule;
    rule.uid = uid;
    rule.version = 1;
    rule.deviceUid = deviceUid;
    rule.ruleType = type;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    return rule;
}

// How the fixture's rules reach the repository. kThroughRepository writes them with
// repo.save(), moving the revision with every rule - the state after any online activity.
// kDirectlyIntoDao seeds the DAO behind the repository's back, the way a stored registry
// exists at boot: the repository has loaded everything through loadCache() but has never
// been written to, and its revision must still let the snapshot settle.
enum SeedPath { kThroughRepository, kDirectlyIntoDao };

struct SteadyFixture {
    MockSpiffsRuleDao dao;
    MockNTPTimeAdapter ntp;
    RuleRepository repo;
    MockDeviceModel deviceModel;
    MockSensorValueModel sensorValues;
    MockArduinoAdapter arduino;
    StepCycleTable stepTable;
    StepCycleStore stepStore;
    EvaluateRulesUseCase useCase;

    DeviceDigitalOut devOn{};
    DeviceDigitalOut devOff{};
    DeviceDigitalOut devCond{};
    DeviceDigitalOut devSched{};
    DeviceDigitalOut devLoop{};
    DeviceDigitalOut devStep{};

    unsigned long epoch;

    explicit SteadyFixture(SeedPath seedPath = kThroughRepository)
        : repo(&dao, &ntp),
          stepStore(stepTable),
          useCase(&deviceModel, &repo, &arduino, &ntp, &sensorValues,
                  nullptr, nullptr, nullptr, &stepStore),
          epoch(kEpochStart) {
        memset(&stepTable, 0, sizeof(stepTable));

        std::vector<Rule> rules;

        addDevice(devOn, kDevOn);
        addDevice(devOff, kDevOff);
        addDevice(devCond, kDevCond);
        addDevice(devSched, kDevSched);
        addDevice(devLoop, kDevLoop);
        addDevice(devStep, kDevStep);

        // Held ON: DOWN direction, reading above the high threshold.
        Rule on = baseRule(kRuleOnUid, kDevOn, RuleType::SENSOR);
        on.sensorUid = kSensorHigh;
        on.direction = RuleDirection::DOWN;
        on.valueLow = 10;
        on.valueHigh = 20;
        rules.push_back(on);
        sensorValues.values[kSensorHigh] = 25.0f;

        // Held OFF: same thresholds, reading below the low threshold.
        Rule off = baseRule(kRuleOffUid, kDevOff, RuleType::SENSOR);
        off.sensorUid = kSensorLow;
        off.direction = RuleDirection::DOWN;
        off.valueLow = 10;
        off.valueHigh = 20;
        rules.push_back(off);
        sensorValues.values[kSensorLow] = 5.0f;

        // AND of two conditions, both holding: the multi-condition path with its per-tick
        // fired-flags container.
        Rule cond = baseRule(kRuleCondUid, kDevCond, RuleType::SENSOR);
        cond.conditionOr = false;
        RuleCondition condA;
        condA.sensorUids.push_back(kSensorCondA);
        condA.direction = RuleDirection::DOWN;
        condA.valueLow = 10;
        condA.valueHigh = 20;
        RuleCondition condB;
        condB.sensorUids.push_back(kSensorCondB);
        condB.direction = RuleDirection::UP;
        condB.valueLow = 10;
        condB.valueHigh = 20;
        cond.conditions.push_back(condA);
        cond.conditions.push_back(condB);
        rules.push_back(cond);
        sensorValues.values[kSensorCondA] = 25.0f;  // DOWN: above high fires
        sensorValues.values[kSensorCondB] = 5.0f;   // UP: below low fires

        // Open schedule window: started at the epoch's midnight, two hours long.
        Rule sched = baseRule(kRuleSchedUid, kDevSched, RuleType::SCHEDULE);
        sched.schedule.push_back(ScheduleItem(static_cast<time_t>(kEpochStart), 7200));
        rules.push_back(sched);

        // Running loop rule.
        Rule loop = baseRule(kRuleLoopUid, kDevLoop, RuleType::LOOP);
        loop.workingTime = 5;
        loop.waitingTime = 5;
        rules.push_back(loop);

        // Running step rule whose cycle counter is tracked by the store: the steady tick also
        // covers the counter's "nothing changed" read.
        Rule step = baseRule(kRuleStepUid, kDevStep, RuleType::STEP);
        step.sensorUid = kSensorStep;
        step.direction = RuleDirection::UP;
        step.valueTarget = 50;
        step.workingTime = 5;
        step.waitingTime = 5;
        step.protectiveShutdown = true;
        step.cyclesToSetPoint = 3;
        rules.push_back(step);
        sensorValues.values[kSensorStep] = 10.0f;  // UP: below target keeps firing

        // Switched off by the user: exercises the deactivation sweep every tick.
        Rule dropped = baseRule(kRuleDroppedUid, kDevOff, RuleType::SENSOR);
        dropped.mode = RuleMode::OFF;
        dropped.sensorUid = kSensorLow;
        dropped.direction = RuleDirection::DOWN;
        dropped.valueLow = 10;
        dropped.valueHigh = 20;
        rules.push_back(dropped);

        if (seedPath == kThroughRepository) {
            for (const Rule& rule : rules) {
                repo.save(rule);
            }
        } else {
            dao.saveAll(rules);
        }

        // All sensors have been read recently; nothing is stale or waiting.
        sensorValues.lastReadTimes[kSensorHigh] = 500;
        sensorValues.lastReadTimes[kSensorLow] = 500;
        sensorValues.lastReadTimes[kSensorCondA] = 500;
        sensorValues.lastReadTimes[kSensorCondB] = 500;
        sensorValues.lastReadTimes[kSensorStep] = 500;

        arduino.setMillis(1000);
        ntp.setEpochTime(epoch);
    }

    void addDevice(DeviceDigitalOut& device, const char* uid) {
        device.deviceUid = uid;
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = DEVICE_STATE_OFF;
        deviceModel.addDevice(&device);
    }

    // One evaluated second: both clocks advance, then the use case runs its tick.
    void tick() {
        arduino.setMillis(arduino.currentMillis + 1000);
        epoch += 1;
        ntp.setEpochTime(epoch);
        useCase.loop();
    }
};

}  // namespace

void test_steady_state_rule_evaluation_tick_makes_no_heap_allocations(void) {
    SteadyFixture f;

    for (int i = 0; i < 15; ++i) {
        f.tick();
    }

    // The steady state must be a live one - devices actually held by rules, not an idle pass.
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_ON, f.devOn.state, "sensor rule should hold its device on");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_OFF, f.devOff.state, "below-threshold rule should leave its device off");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_ON, f.devCond.state, "condition rule should hold its device on");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_ON, f.devSched.state, "schedule rule should hold its device on");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_STEP_ON, f.devLoop.state, "loop rule should be running");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_STEP_ON, f.devStep.state, "step rule should be running");

    // Tracing prints nothing while the tick stays allocation-free; if this test ever goes red,
    // stderr carries a backtrace for every allocation the measured tick made.
    HostAllocationCounter::reset();
    HostAllocationCounter::setTrace(true);
    f.tick();
    HostAllocationCounter::setTrace(false);
    unsigned long long allocations = HostAllocationCounter::newCount();

    char message[128];
    snprintf(message, sizeof(message),
             "steady-state evaluation tick made %llu heap allocations, expected 0", allocations);
    TEST_ASSERT_EQUAL_MESSAGE(0, static_cast<unsigned>(allocations), message);
}

void test_steady_state_tick_is_allocation_free_when_no_write_ever_reached_the_repository(void) {
    // The offline cold start: the registry was read from storage through loadCache(), and no
    // save has gone through the repository since boot - no cloud sync, no sensor errors, no
    // deletions. The snapshot must settle in this state exactly like after a write; a
    // revision that answers "unreadable" here forces a full registry re-read on every tick,
    // which is more churn than having no snapshot at all.
    SteadyFixture f(kDirectlyIntoDao);

    for (int i = 0; i < 15; ++i) {
        f.tick();
    }

    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_ON, f.devOn.state, "sensor rule should hold its device on");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_ON, f.devCond.state, "condition rule should hold its device on");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_ON, f.devSched.state, "schedule rule should hold its device on");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_STEP_ON, f.devLoop.state, "loop rule should be running");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_STATE_REQUEST_STEP_ON, f.devStep.state, "step rule should be running");

    HostAllocationCounter::reset();
    HostAllocationCounter::setTrace(true);
    f.tick();
    HostAllocationCounter::setTrace(false);
    unsigned long long allocations = HostAllocationCounter::newCount();

    char message[128];
    snprintf(message, sizeof(message),
             "cold-start steady tick made %llu heap allocations, expected 0", allocations);
    TEST_ASSERT_EQUAL_MESSAGE(0, static_cast<unsigned>(allocations), message);
}

extern "C" void run_rule_eval_steady_alloc_tests(void) {
    RUN_TEST(test_steady_state_rule_evaluation_tick_makes_no_heap_allocations);
    RUN_TEST(test_steady_state_tick_is_allocation_free_when_no_write_ever_reached_the_repository);
}
