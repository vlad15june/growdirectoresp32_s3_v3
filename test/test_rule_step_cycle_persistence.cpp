// GDV1-3811: the STEP cycle counter must survive a reboot.
//
// currentCycle lives on the device entity, i.e. in RAM. Before this fix every restart - the
// nightly preventive one, the technical one after an offline period, an OTA, a power cut - handed
// the actuator a fresh cyclesToSetPoint budget, so the protective shutdown was bounded by the
// uptime rather than by the configured number of cycles. The counter is now persisted with the
// identity of what it counts and restored at boot for the same rule/device/limit.
//
// A reboot is simulated the way it actually happens: the device entity and the use case are built
// again from scratch, while the store is carried over - exactly what the RTC memory of the clock
// does to the table (the counter is deliberately NOT in the EEPROM, see StepCycleStore).

#include <unity.h>
#include <cstring>
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
#include "Mock/Core/Domain/Rules/MockRuleStateNotifier.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Infrastructure/Configuration/StepCycleStore.h"

namespace {

// The production store over a plain table: as far as it is concerned RTC memory is just a struct,
// so the tests exercise the real store and a "reboot" is simply keeping the table alive. The
// counters make the write budget assertable.
class TestStepCycleStore : public StepCycleStore {
public:
    int stepCycleWrites = 0;
    int stepCycleClears = 0;

    TestStepCycleStore() : StepCycleStore(_table) { memset(&_table, 0, sizeof(_table)); }

    void setStepCycleCounter(const StepCycleCounter& counter) override {
        stepCycleWrites++;
        StepCycleStore::setStepCycleCounter(counter);
    }

    void clearStepCycleCounter(const char* deviceUid) override {
        stepCycleClears++;
        StepCycleStore::clearStepCycleCounter(deviceUid);
    }

    bool hasStepCycleCounter(const char* deviceUid) {
        StepCycleCounter counter;
        return StepCycleStore::getStepCycleCounter(deviceUid, counter);
    }

private:
    StepCycleTable _table;
};

class SingleDeviceModel : public IDeviceModel {
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

// The shared MockRuleRepository returns every rule from getActiveRules(); the real repository
// keeps only the rules in mode ON (RuleRepository.h:363-395). A rule switched off has to look
// switched off here, otherwise the deactivation path is never reached.
class ModeAwareRuleRepository : public MockRuleRepository {
public:
    std::vector<Rule> getActiveRules() override {
        std::vector<Rule> active;
        for (const auto& rule : rules) {
            if (rule.mode == RuleMode::ON) {
                active.push_back(rule);
            }
        }
        return active;
    }
};

Rule makeStepRule(bool protectiveShutdown, int cyclesToSetPoint, const std::string& uid = "rule-1") {
    Rule r;
    r.uid = uid;
    r.version = 3;
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

// Sensor value 30 with direction DOWN and target 25 keeps the step rule firing.
void armFiringSensor(MockSensorValueModel& sensors, unsigned long readTime) {
    sensors.values["sensor-1"] = 30.0f;
    sensors.lastReadTimes["sensor-1"] = readTime;
}

void initDevice(DeviceDigitalOut& device) {
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.currentCycle = 0;
    device.cyclesToSetPoint = 0;
}

}  // namespace

// A run interrupted mid-way by a reboot resumes counting instead of starting from one.
static void test_step_cycle_restored_after_reboot(void) {
    TestStepCycleStore store;
    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule(true, 5));

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    // --- before the reboot: the rule runs and reaches its third cycle ---
    DeviceDigitalOut device{};
    initDevice(device);
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_EQUAL_INT(1, device.currentCycle);

    // ProcessDeviceStateUseCase increments the counter at the end of every completed cycle
    device.state = DEVICE_STATE_STEP_WAITING;
    device.currentCycle = 3;
    arduino.setMillis(401000);
    uc.loop();

    StepCycleCounter stored;
    TEST_ASSERT_TRUE(store.getStepCycleCounter("dev-1", stored));
    TEST_ASSERT_EQUAL_INT(3, stored.currentCycle);
    TEST_ASSERT_EQUAL_INT(5, stored.cyclesToSetPoint);
    TEST_ASSERT_EQUAL_STRING("rule-1", stored.ruleUid.c_str());

    // --- the reboot: entities are rebuilt, the store (RTC memory) is not ---
    DeviceDigitalOut rebooted{};
    initDevice(rebooted);
    SingleDeviceModel rebootedModel;
    rebootedModel.device = &rebooted;

    EvaluateRulesUseCase ucAfterBoot(&rebootedModel, &repo, &arduino, &ntp, &sensors,
                                     nullptr, nullptr, &notifier, &store);

    armFiringSensor(sensors, 500000);
    arduino.setMillis(500000);
    ucAfterBoot.loop();

    // The rule re-arms with the counter it had, not with a fresh budget
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, rebooted.state);
    TEST_ASSERT_EQUAL_INT(3, rebooted.currentCycle);
    // and the value the app sees in hardware/update is that same restored one
    TEST_ASSERT_EQUAL_INT(5, rebooted.cyclesToSetPoint);
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);
}

// The protective shutdown fires at the configured cycle even when the cap was reached before the
// restart: this is the behaviour the whole limit exists for.
static void test_step_protective_shutdown_fires_across_reboot(void) {
    TestStepCycleStore store;
    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule(true, 3));

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    DeviceDigitalOut device{};
    initDevice(device);
    device.state = DEVICE_STATE_STEP_WAITING;
    device.ruleUid = "rule-1";
    device.cyclesToSetPoint = 3;
    device.currentCycle = 3;
    device.isRuleTurnOnDevice = true;
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    // The counter is at the cap and stored; the shutdown would fire on this very tick
    StepCycleCounter atCap;
    atCap.deviceUid = "dev-1";
    atCap.ruleUid = "rule-1";
    atCap.cyclesToSetPoint = 3;
    atCap.currentCycle = 3;
    store.setStepCycleCounter(atCap);

    // --- the reboot happens first: fresh device entity, counter back to zero in RAM ---
    DeviceDigitalOut rebooted{};
    initDevice(rebooted);
    SingleDeviceModel rebootedModel;
    rebootedModel.device = &rebooted;

    EvaluateRulesUseCase uc(&rebootedModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_TRUE(notifier.protectiveShutdownCalled);
    TEST_ASSERT_EQUAL_INT(1, notifier.protectiveShutdownCount);
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, rebooted.state);
    TEST_ASSERT_EQUAL_STRING("protective_shutdown", rebooted.ruleStopReason);
    TEST_ASSERT_EQUAL_INT(0, rebooted.currentCycle);
    // and the record is gone, so the next run of this rule starts from scratch
    TEST_ASSERT_FALSE(store.hasStepCycleCounter("dev-1"));
}

// Editing the rule (a new limit) invalidates what was stored: the counter is not carried over.
static void test_stored_cycle_discarded_after_rule_edited(void) {
    TestStepCycleStore store;
    StepCycleCounter stale;
    stale.deviceUid = "dev-1";
    stale.ruleUid = "rule-1";
    stale.cyclesToSetPoint = 5;
    stale.currentCycle = 4;
    store.setStepCycleCounter(stale);

    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule(true, 8));  // the user changed the limit from 5 to 8

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    DeviceDigitalOut device{};
    initDevice(device);
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_STEP_ON, device.state);
    TEST_ASSERT_EQUAL_INT(1, device.currentCycle);
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);

    StepCycleCounter stored;
    TEST_ASSERT_TRUE(store.getStepCycleCounter("dev-1", stored));
    TEST_ASSERT_EQUAL_INT(8, stored.cyclesToSetPoint);
    TEST_ASSERT_EQUAL_INT(1, stored.currentCycle);
}

// The same for a rule replaced by another one on the same device.
static void test_stored_cycle_discarded_for_another_rule(void) {
    TestStepCycleStore store;
    StepCycleCounter stale;
    stale.deviceUid = "dev-1";
    stale.ruleUid = "rule-old";
    stale.cyclesToSetPoint = 5;
    stale.currentCycle = 4;
    store.setStepCycleCounter(stale);

    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule(true, 5, "rule-new"));

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    DeviceDigitalOut device{};
    initDevice(device);
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(1, device.currentCycle);
    TEST_ASSERT_FALSE(notifier.protectiveShutdownCalled);

    StepCycleCounter stored;
    TEST_ASSERT_TRUE(store.getStepCycleCounter("dev-1", stored));
    TEST_ASSERT_EQUAL_STRING("rule-new", stored.ruleUid.c_str());
    TEST_ASSERT_EQUAL_INT(1, stored.currentCycle);
}

// Flash endurance: nothing is written for a rule without the protective shutdown, and a running
// rule costs at most one write per completed cycle.
static void test_step_cycle_not_persisted_without_protective_shutdown(void) {
    TestStepCycleStore store;
    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule(false, 5));

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    DeviceDigitalOut device{};
    initDevice(device);
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();
    device.state = DEVICE_STATE_STEP_WAITING;
    device.currentCycle = 2;
    arduino.setMillis(401000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(0, store.stepCycleWrites);
    TEST_ASSERT_FALSE(store.hasStepCycleCounter("dev-1"));
}

static void test_step_cycle_written_once_per_cycle(void) {
    TestStepCycleStore store;
    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule(true, 10));

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    DeviceDigitalOut device{};
    initDevice(device);
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();                       // STEP_START, cycle 1 -> one write
    TEST_ASSERT_EQUAL_INT(1, store.stepCycleWrites);

    device.state = DEVICE_STATE_STEP_WAITING;
    unsigned long now = 401000;
    for (int tick = 0; tick < 5; tick++) {   // five evaluations, still the same cycle
        arduino.setMillis(now);
        uc.loop();
        now += 1000;
    }
    TEST_ASSERT_EQUAL_INT(1, store.stepCycleWrites);

    device.currentCycle = 2;                 // a cycle completed
    arduino.setMillis(now);
    uc.loop();
    TEST_ASSERT_EQUAL_INT(2, store.stepCycleWrites);
}

// A run that ends the way it should - the setpoint was reached - leaves nothing behind, so the
// next run of the same rule starts from one even if a reboot happens in between.
static void test_step_cycle_cleared_when_setpoint_reached(void) {
    TestStepCycleStore store;
    MockRuleRepository repo;
    repo.rules.push_back(makeStepRule(true, 5));

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    DeviceDigitalOut device{};
    initDevice(device);
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();
    device.state = DEVICE_STATE_STEP_WAITING;
    device.currentCycle = 3;
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_TRUE(store.hasStepCycleCounter("dev-1"));

    // the sensor came back inside its band - the rule stops on setpoint_reach
    sensors.values["sensor-1"] = 20.0f;
    sensors.lastReadTimes["sensor-1"] = 402000;
    arduino.setMillis(402000);
    uc.loop();

    TEST_ASSERT_EQUAL_STRING("setpoint_reach", device.ruleStopReason);
    TEST_ASSERT_FALSE(store.hasStepCycleCounter("dev-1"));
}

// A rule switched off by the user drops its stored counter, even if the module was down when it
// was switched off (nothing of the run is left in RAM to notice it).
static void test_step_cycle_cleared_when_rule_switched_off(void) {
    TestStepCycleStore store;
    StepCycleCounter stored;
    stored.deviceUid = "dev-1";
    stored.ruleUid = "rule-1";
    stored.cyclesToSetPoint = 5;
    stored.currentCycle = 4;
    store.setStepCycleCounter(stored);

    ModeAwareRuleRepository repo;
    Rule off = makeStepRule(true, 5);
    off.mode = RuleMode::OFF;
    repo.rules.push_back(off);

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    DeviceDigitalOut device{};
    initDevice(device);
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();

    TEST_ASSERT_FALSE(store.hasStepCycleCounter("dev-1"));
    TEST_ASSERT_EQUAL_INT(0, device.currentCycle);
}

// The same when the rule was running in this uptime and is switched off mid-run: the record has
// already been read by the boot-time restore, and it still has to go.
static void test_step_cycle_cleared_when_running_rule_switched_off(void) {
    TestStepCycleStore store;
    ModeAwareRuleRepository repo;
    repo.rules.push_back(makeStepRule(true, 5));

    MockSensorValueModel sensors;
    armFiringSensor(sensors, 400000);
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);
    MockRuleStateNotifier notifier;

    DeviceDigitalOut device{};
    initDevice(device);
    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors,
                            nullptr, nullptr, &notifier, &store);

    arduino.setMillis(400000);
    uc.loop();
    device.state = DEVICE_STATE_STEP_WAITING;
    device.currentCycle = 3;
    arduino.setMillis(401000);
    uc.loop();
    TEST_ASSERT_TRUE(store.hasStepCycleCounter("dev-1"));

    repo.rules[0].mode = RuleMode::OFF;   // the user switches the rule off mid-run
    arduino.setMillis(402000);
    uc.loop();

    TEST_ASSERT_FALSE(store.hasStepCycleCounter("dev-1"));
}

// The table lives in RTC memory, which a power cut leaves with whatever bytes were there: such a
// slot must not be handed back as a counter (StepCycleStore::isValidSlot / dropInvalidSlots).
static void test_garbage_table_after_power_cut_is_not_restored(void) {
    StepCycleTable table;
    memset(&table, 0xA5, sizeof(table));   // power-on garbage, no valid marker
    StepCycleStore store(table);

    StepCycleCounter counter;
    TEST_ASSERT_FALSE(store.getStepCycleCounter("dev-1", counter));

    store.dropInvalidSlots();
    for (int slot = 0; slot < StepCycleTable::SLOTS; slot++) {
        TEST_ASSERT_EQUAL_UINT32(0, table.slots[slot].marker);
    }

    // and a record written after that is readable again
    StepCycleCounter fresh;
    fresh.deviceUid = "dev-1";
    fresh.ruleUid = "rule-1";
    fresh.cyclesToSetPoint = 5;
    fresh.currentCycle = 2;
    store.setStepCycleCounter(fresh);

    TEST_ASSERT_TRUE(store.getStepCycleCounter("dev-1", counter));
    TEST_ASSERT_EQUAL_INT(2, counter.currentCycle);
    TEST_ASSERT_EQUAL_STRING("rule-1", counter.ruleUid.c_str());
}

extern "C" void run_rule_step_cycle_persistence_tests(void) {
    RUN_TEST(test_step_cycle_restored_after_reboot);
    RUN_TEST(test_step_protective_shutdown_fires_across_reboot);
    RUN_TEST(test_stored_cycle_discarded_after_rule_edited);
    RUN_TEST(test_stored_cycle_discarded_for_another_rule);
    RUN_TEST(test_step_cycle_not_persisted_without_protective_shutdown);
    RUN_TEST(test_step_cycle_written_once_per_cycle);
    RUN_TEST(test_step_cycle_cleared_when_setpoint_reached);
    RUN_TEST(test_step_cycle_cleared_when_rule_switched_off);
    RUN_TEST(test_step_cycle_cleared_when_running_rule_switched_off);
    RUN_TEST(test_garbage_table_after_power_cut_is_not_restored);
}
