// The record DeviceService stamps switchedAt from must survive the device list changing shape.
//
// The service keeps one entry per device - UID and the state last seen - in device-list order, and
// on an iteration where the list is unchanged it walks both by index and compares enums: no lookup
// key, no allocation, thousands of times a second. When the list no longer lines up - devices
// reordered, one inserted in the middle, one gone - that pass gives up and the record is rebuilt by
// UID, and entries for devices no longer in the list are carried over to the tail instead of being
// dropped.
//
// The tail is the part worth a test. A channel that a reconfiguration removes and adds back is
// rebuilt as a new object, and its state may well have changed in between; if its old entry was
// dropped while it was away, the device reads as never-seen, the switch is never stamped, and the
// server keeps a switchedAt from before the reconfiguration with nothing to correct it. The device-list
// order alone cannot be trusted for the merge either: the same devices in another order must stamp
// nobody, and an insertion in the middle must not shift the devices behind it onto the wrong
// entries.
//
// To see red, in DeviceService::refreshSwitchedAtForChangedDevices():
//   - drop the loop that appends entries of absent devices to the rebuilt list (the tail), or
//   - match the previous entry by index instead of by UID, or
//   - stamp every device the rebuild re-registers.
//
// The last test follows the stamp out of the service: switchedAt is refreshed before the update is
// built, so the value the server is given in the same iteration is the new one.

#include <unity.h>

#include "Application/Services/Devices/DeviceService.h"

#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

#include "Sim/HostAllocationCounter.h"

#include <cstdio>
#include <string>

namespace {

// Backend UIDs are GUID-sized. Short UIDs would sit in the small-string buffer of std::string and
// hide the allocations the settled-iteration test below counts.
const char* kDeviceA = "7c1a4f60-2d93-4b18-9f52-0a6e83c71d24";
const char* kDeviceB = "8d2b5a71-3ea4-4c29-a063-1b7f94d82e35";
const char* kDeviceC = "9e3c6b82-4fb5-4d3a-b174-2c80a5e93f46";

// The stamp a device carries before this service touches it, and the reading of the clock any
// stamp made during a test produces. They differ, so a stamp is always visible.
const char* kStampBefore = "2026-08-22T08:00:00Z";
const char* kStampNow = "2026-08-22T09:30:00Z";

/**
 * A digital output as DeviceModel::init() builds it - every field set, nothing left over from the
 * stack, because the update DTO walks fields this test does not care about.
 */
struct TrackedDevice {
    DeviceDigitalOut device{};

    TrackedDevice(const char* uid, DeviceStateEnum state)
    {
        device.deviceUid = uid;
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = state;
        device.isError = false;
        device.timeOn = 0;
        device.timeOff = 0;
        device.durationOn = 0;
        device.durationOff = 0;
        device.createdDate = "2026-08-22T07:00:00Z";
        device.switchedAt = kStampBefore;
        device.isAuto = false;
        device.isManual = false;
        device.sensorUid = nullptr;
        device.sensorValue = 0.0f;
        device.sensorValueTime = 0;
        device.valueTarget = 0.0f;
        device.valueLow = 0.0f;
        device.valueHigth = 0.0f;
        device.direction = RuleDirection::UNKNOWN;
        device.range = false;
        device.ruleUid = nullptr;
        device.ruleType = RuleType::UNKNOWN;
        device.ruleVersion = 0;
        device.workflowUid = nullptr;
        device.workflowVersion = 0;
        device.sensorValueLow = 0.0f;
        device.sensorValueHigh = 0.0f;
        device.ruleScheduleStartAt = nullptr;
        device.ruleScheduleDuration = 0;
        device.workByRule = false;
        device.commandId = 0;
        device.workManual = false;
        device.needReportingRuleUid = false;
        device.needToSwitchBack = false;
        device.mixTime = 0;
        device.ruleSensorStartAt = nullptr;
        device.ruleSensorValueStart = 0.0f;
        device.ruleSensorStopAt = nullptr;
        device.ruleSensorValueStop = 0.0f;
        device.ruleStopReason = nullptr;
        device.isRuleTurnOnDevice = false;
        device.timeToSetPoint = 0;
        device.cyclesToSetPoint = 0;
        device.currentCycle = 0;
        device.delayStartTime = 0;
        device.isResquestOn = false;
        device.timeResquestOn = 0;
    }

    const char* stamp() const { return device.switchedAt ? device.switchedAt : "(null)"; }
};

/**
 * DeviceService with nothing but the device list wired up.
 *
 * No use-case factory: loop() reaches the state machine only through a null check, and these tests
 * are about the tracking pass, not about driving relays. With no API service and no outbox, loop()
 * returns right after refreshSwitchedAtForChangedDevices(), so what the tests observe is that pass
 * alone. The last test passes an API service in to follow the stamp into the update.
 */
struct DeviceTracking {
    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    MockDeviceModel deviceModel;
    DeviceService deviceService;

    DeviceTracking(IHybridAPIService* api = nullptr)
        : moduleConfig(&logger, &debugSystem, &settings),
          deviceService(&moduleConfig, nullptr, &deviceModel, &timeAdapter, &arduino, nullptr, api,
                        &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false;
        settings.setUser("tracking@example.com");
        settings.setAccountUid("tracking-account");
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid("tracking-module");
        timeAdapter.setFormattedTime(kStampNow);
    }

    void tick() { deviceService.loop(); }
};

} // namespace

static void test_registering_a_device_list_for_the_first_time_stamps_nobody(void)
{
    DeviceTracking module;
    TrackedDevice a(kDeviceA, DEVICE_STATE_ON);
    TrackedDevice b(kDeviceB, DEVICE_STATE_OFF);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&b.device);

    module.tick();

    // A device seen for the first time has not switched - the state it is in is simply recorded.
    // Without this the other tests could not tell a stamp from a first sighting.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, a.stamp(),
                                     "a device seen for the first time was stamped as if it had "
                                     "just switched");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, b.stamp(),
                                     "a device seen for the first time was stamped as if it had "
                                     "just switched");
}

static void test_a_device_that_leaves_the_list_and_comes_back_changed_is_stamped(void)
{
    DeviceTracking module;
    TrackedDevice a(kDeviceA, DEVICE_STATE_ON);
    TrackedDevice b(kDeviceB, DEVICE_STATE_ON);
    TrackedDevice c(kDeviceC, DEVICE_STATE_OFF);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&b.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    // A reconfiguration drops channel B from the list.
    module.deviceModel.removeDevice(kDeviceB);
    module.tick();

    // It comes back the way DeviceModel::init() brings it back - a new object with the same UID,
    // in a different place in the list - and it is off now, not on as when it left.
    TrackedDevice returningB(kDeviceB, DEVICE_STATE_OFF);
    module.deviceModel.clearDevices();
    module.deviceModel.addDevice(&returningB.device);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampNow, returningB.stamp(),
                                     "a device that left the list ON and came back OFF was not "
                                     "stamped: the state it left with was forgotten while it was "
                                     "away, so the switch is invisible to the server");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, a.stamp(),
                                     "a device that did not change was stamped while the list was "
                                     "being rebuilt");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, c.stamp(),
                                     "a device that did not change was stamped while the list was "
                                     "being rebuilt");
}

static void test_a_device_that_comes_back_in_the_state_it_left_with_is_not_stamped(void)
{
    DeviceTracking module;
    TrackedDevice a(kDeviceA, DEVICE_STATE_ON);
    TrackedDevice b(kDeviceB, DEVICE_STATE_ON);
    TrackedDevice c(kDeviceC, DEVICE_STATE_OFF);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&b.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    module.deviceModel.removeDevice(kDeviceB);
    module.tick();

    // Same UID, same state as when it left: nothing switched, so nothing may be stamped. This is
    // the other half of the claim - the tail is a comparison, not a reason to stamp.
    TrackedDevice returningB(kDeviceB, DEVICE_STATE_ON);
    module.deviceModel.clearDevices();
    module.deviceModel.addDevice(&returningB.device);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, returningB.stamp(),
                                     "a device that came back in the state it left with was "
                                     "stamped as if it had switched");
}

static void test_reordering_the_device_list_stamps_nobody(void)
{
    DeviceTracking module;
    // The states differ from position to position on purpose: a merge that compared the rebuilt
    // list with the old one by index instead of by UID would find a mismatch at two of the three
    // positions and stamp devices that never switched.
    TrackedDevice a(kDeviceA, DEVICE_STATE_ON);
    TrackedDevice b(kDeviceB, DEVICE_STATE_OFF);
    TrackedDevice c(kDeviceC, DEVICE_STATE_ON);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&b.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    module.deviceModel.clearDevices();
    module.deviceModel.addDevice(&b.device);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, b.stamp(),
                                     "reordering the device list stamped a device that did not "
                                     "switch: the states are being matched by position, not by UID");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, a.stamp(),
                                     "reordering the device list stamped a device that did not "
                                     "switch: the states are being matched by position, not by UID");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, c.stamp(),
                                     "reordering the device list stamped a device that did not "
                                     "switch");
}

static void test_an_insertion_in_the_middle_keeps_the_devices_behind_it_tracked(void)
{
    DeviceTracking module;
    TrackedDevice a(kDeviceA, DEVICE_STATE_ON);
    TrackedDevice c(kDeviceC, DEVICE_STATE_OFF);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    // A channel appears between the two known ones, and in the same reconfiguration C switches on.
    // C is now at the index B's entry would be at, so a positional merge loses its state.
    TrackedDevice b(kDeviceB, DEVICE_STATE_OFF);
    c.device.state = DEVICE_STATE_ON;
    module.deviceModel.clearDevices();
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&b.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampNow, c.stamp(),
                                     "a device that switched was not stamped because another "
                                     "device was inserted ahead of it in the list");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, b.stamp(),
                                     "a device seen for the first time was stamped as if it had "
                                     "just switched");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampBefore, a.stamp(),
                                     "a device that did not change was stamped while the list was "
                                     "being rebuilt");
}

static void test_iterations_after_a_rebuild_cost_nothing(void)
{
    DeviceTracking module;
    TrackedDevice a(kDeviceA, DEVICE_STATE_ON);
    TrackedDevice b(kDeviceB, DEVICE_STATE_OFF);
    TrackedDevice c(kDeviceC, DEVICE_STATE_ON);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&b.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    // Reorder and drop one, so the next iteration goes through the rebuild and the record ends up
    // holding an entry for a device that is no longer in the list.
    module.deviceModel.clearDevices();
    module.deviceModel.addDevice(&c.device);
    module.deviceModel.addDevice(&a.device);
    module.tick();

    // The shape has settled again. If the rebuild left the record in an order the device list does
    // not follow - the carried-over entry in front of the present ones, say - every iteration from
    // here on would rebuild again and allocate, which is the cost the index-aligned record exists
    // to avoid.
    HostAllocationCounter::reset();
    for (int iteration = 0; iteration < 100; iteration++) {
        module.tick();
    }
    const unsigned long long allocations = HostAllocationCounter::newCount();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)0, (uint32_t)allocations,
                                     "iterations after a rebuild allocate: the rebuilt record does "
                                     "not line up with the device list, so the fast path never "
                                     "takes over again");
}

static void test_the_stamp_a_returning_device_gets_reaches_the_server_update(void)
{
    MockHybridAPIService api;
    DeviceTracking module(&api);
    TrackedDevice a(kDeviceA, DEVICE_STATE_ON);
    TrackedDevice b(kDeviceB, DEVICE_STATE_ON);
    TrackedDevice c(kDeviceC, DEVICE_STATE_OFF);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&b.device);
    module.deviceModel.addDevice(&c.device);

    module.tick();

    // The middle device leaves, so the record is rebuilt while it is away and its entry has to be
    // carried over; the same course as the tests above, followed one step further out.
    module.deviceModel.removeDevice(kDeviceB);
    module.tick();

    TrackedDevice returningB(kDeviceB, DEVICE_STATE_OFF);
    module.deviceModel.clearDevices();
    module.deviceModel.addDevice(&returningB.device);
    module.deviceModel.addDevice(&a.device);
    module.deviceModel.addDevice(&c.device);

    api.sendModuleUpdateDataCalled = false;
    module.tick();

    TEST_ASSERT_TRUE_MESSAGE(api.sendModuleUpdateDataCalled,
                             "the iteration that stamped the returning device sent no update, so "
                             "this test proves nothing");

    std::string reportedStamp;
    for (const DeviceDto& device : api.lastUpdateData.devices) {
        if (device.uid == kDeviceB) {
            reportedStamp = device.switchedAt;
        }
    }

    TEST_ASSERT_EQUAL_STRING_MESSAGE(kStampNow, reportedStamp.c_str(),
                                     "the update the server was given carries the old switchedAt "
                                     "of the returning device");
}

extern "C" void run_device_state_tracking_tests(void)
{
    printf("\n----- Running Device State Tracking Tests -----\n");

    RUN_TEST(test_registering_a_device_list_for_the_first_time_stamps_nobody);
    RUN_TEST(test_a_device_that_leaves_the_list_and_comes_back_changed_is_stamped);
    RUN_TEST(test_a_device_that_comes_back_in_the_state_it_left_with_is_not_stamped);
    RUN_TEST(test_reordering_the_device_list_stamps_nobody);
    RUN_TEST(test_an_insertion_in_the_middle_keeps_the_devices_behind_it_tracked);
    RUN_TEST(test_iterations_after_a_rebuild_cost_nothing);
    RUN_TEST(test_the_stamp_a_returning_device_gets_reaches_the_server_update);

    printf("\n----- Device State Tracking Tests Complete -----\n");
}
