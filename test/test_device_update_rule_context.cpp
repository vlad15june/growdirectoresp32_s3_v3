// A device update that was not delivered must not erase why the device was switched on.
//
// DeviceService::clearReportedRuleContext() wipes workByRule, ruleUid, ruleStopReason and
// needReportingRuleUid. That is the module's only record of *why* a pump ran; the server keeps
// the copy, which is why the record is cleared once the server has been told. With a synchronous
// send the two events are the same instant:
//
//     bool sent = sendDeviceUpdate();     // a synchronous POST
//     lastDeviceStateSignature = signature;
//     if (sent) { clearReportedRuleContext(); }
//
// With a queue in between, "sent" would naturally come to mean "queued", and that is the failure
// this file exists to prevent: the queue overflows or the attempts run out, the reason is gone,
// and the server is left with a pump that switched on for no stated reason - with nothing
// anywhere to recover it from. Nothing goes red: the tests pass, the pump waters, and the gaps in
// the history are noticed months later by whoever tries to explain them.
//
// To see the red: in DeviceService::onOutboxCompletion(), drop the `if (!completion.delivered)`
// guard so the rule context is cleared whatever the outcome. That single line is the difference,
// and it is exactly the line a naive move to fire-and-forget would never have written at all.
//
// The second test is the other half of the claim: when the server does take the update, the
// context *is* cleared, so this is not simply "never clear anything".

#include <unity.h>

#include "Application/Services/Devices/DeviceService.h"
#include "Application/Services/Net/Outbox.h"

#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

// One attempt of RestApiService::sendModuleUpdateData(): setTimeout(30000), one POST.
const uint32_t ATTEMPT_TIMEOUT_MS = 30000;

class TimedServer : public MockHybridAPIService {
public:
    TimedServer(HostTaskScheduler& scheduler) : _scheduler(scheduler) {}

    void setLatencyMs(uint32_t ms) { _latencyMs = ms; }
    void setReachable(bool reachable) { _reachable = reachable; }

    bool sendModuleUpdateData(const UpdateDto& update, ApiPriority = ApiPriority::UNDEFINED) override
    {
        updatesSeen++;
        lastRuleUidReported.clear();
        for (const DeviceDto& device : update.devices)
        {
            if (!device.ruleUid.empty())
            {
                lastRuleUidReported = device.ruleUid;
            }
        }
        _scheduler.delayMs(_latencyMs);
        return _reachable;
    }

    uint32_t updatesSeen = 0;
    std::string lastRuleUidReported;

private:
    HostTaskScheduler& _scheduler;
    uint32_t _latencyMs = 0;
    bool _reachable = true;
};

/** A digital output that a rule switched on and whose reason has not been reported yet. */
struct RuleDrivenDevice {
    DeviceDigitalOut device;

    RuleDrivenDevice()
    {
        device.deviceUid = "device-1";
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = DEVICE_STATE_ON;
        device.isAuto = true;
        device.isManual = false;
        device.workManual = false;
        device.needToSwitchBack = false;
        device.commandId = 0;
        device.switchedAt = "2026-08-15T10:00:00Z";
        device.isRuleTurnOnDevice = false;

        // The record under test.
        device.workByRule = true;
        device.ruleUid = "rule-1";
        device.ruleStopReason = "sensor_value_reached";
        device.needReportingRuleUid = true;
        device.ruleType = RuleType::SENSOR;
        device.ruleVersion = 1;
        device.workflowUid = "";
        device.workflowVersion = 0;
        device.sensorUid = "sensor-1";
        device.ruleSensorStartAt = nullptr;
        device.ruleSensorStopAt = nullptr;
        device.ruleScheduleStartAt = nullptr;
    }
};

struct DeviceServiceUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    MockDeviceModel deviceModel;
    TimedServer server;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    RuleDrivenDevice ruleDriven;
    DeviceService deviceService;

    DeviceServiceUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          server(scheduler),
          completions(8),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          // No use-case factory: loop() only ever calls it through a null check, and this test is
          // about what happens after the update, not about driving relays.
          deviceService(&moduleConfig, nullptr, &deviceModel, &timeAdapter, &arduino, nullptr,
                        nullptr, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false;
        settings.setUser("dod4@example.com");
        settings.setAccountUid("dod4-account");
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid("dod4-module");

        deviceModel.addDevice(&ruleDriven.device);
        deviceService.setOutbox(&outbox);
    }

    /** Hand the outcomes back the way LoopServiceConfigureDone does, on this task. */
    void drainCompletionsIntoDeviceService()
    {
        OutboxCompletion completion;
        while (outbox.pollCompletion(completion))
        {
            deviceService.onOutboxCompletion(completion);
        }
    }
};

} // namespace

static void test_an_undelivered_device_update_keeps_the_reason_the_device_switched_on(void)
{
    DeviceServiceUnderTest module;
    module.server.setLatencyMs(ATTEMPT_TIMEOUT_MS);
    module.server.setReachable(false);

    // One control-loop iteration: the device state is new, so an update is submitted.
    module.deviceService.loop();

    // Long enough for every attempt and every backoff the outbox policy allows.
    module.scheduler.runForMs(600000);
    module.drainCompletionsIntoDeviceService();

    // --- the run has to be sound before its verdict means anything --------------------------
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.server.updatesSeen > 0,
                             "no update ever reached the server stand-in - nothing was exercised");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.undeliveredCount(),
                                     "the update was not reported undelivered, so this test proves nothing");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("rule-1", module.server.lastRuleUidReported.c_str(),
                                     "the update that failed did not even carry the rule uid");

    // --- the actual claim --------------------------------------------------------------------
    const Device& device = module.ruleDriven.device;

    TEST_ASSERT_NOT_NULL_MESSAGE(device.ruleUid,
                                 "ruleUid was erased although the update never reached the server: "
                                 "the reason this device switched on is now unrecoverable");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("rule-1", device.ruleUid, "ruleUid was replaced");
    TEST_ASSERT_NOT_NULL_MESSAGE(device.ruleStopReason,
                                 "ruleStopReason was erased on an update that was not delivered");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("sensor_value_reached", device.ruleStopReason,
                                     "ruleStopReason was replaced");
    TEST_ASSERT_TRUE_MESSAGE(device.workByRule,
                             "workByRule was cleared on an update that was not delivered");
    TEST_ASSERT_TRUE_MESSAGE(device.needReportingRuleUid,
                             "needReportingRuleUid was cleared, so the reason will never be re-sent");
}

static void test_a_delivered_device_update_does_clear_the_reason(void)
{
    DeviceServiceUnderTest module;
    module.server.setLatencyMs(100);
    module.server.setReachable(true);

    module.deviceService.loop();
    module.scheduler.runForMs(10000);
    module.drainCompletionsIntoDeviceService();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.deliveredCount(),
                                     "the update was not delivered, so this test proves nothing");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("rule-1", module.server.lastRuleUidReported.c_str(),
                                     "the delivered update did not carry the rule uid");

    const Device& device = module.ruleDriven.device;

    TEST_ASSERT_FALSE_MESSAGE(device.needReportingRuleUid,
                              "the server took the update and the module still thinks it owes a report");
    TEST_ASSERT_FALSE_MESSAGE(device.workByRule,
                              "the rule context was not cleared after a delivered update");
    TEST_ASSERT_NULL_MESSAGE(device.ruleUid, "ruleUid was not cleared after a delivered update");
}

extern "C" void run_device_update_rule_context_tests(void)
{
    printf("\n----- Running Device Update Rule Context Tests -----\n");

    RUN_TEST(test_an_undelivered_device_update_keeps_the_reason_the_device_switched_on);
    RUN_TEST(test_a_delivered_device_update_does_clear_the_reason);

    printf("\n----- Device Update Rule Context Tests Complete -----\n");
}
