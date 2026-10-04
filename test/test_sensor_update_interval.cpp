// SensorService: the 15 s update interval must count from the end of the send, not its start.
//
// This is the same defect the diagnostic timer had, in the path the control loop walks most
// often. SensorService::loop() sends an update through
// IHybridAPIService - a synchronous HTTP call the firmware allows up to ~90 s - and then stamps
// lastSensorUpdateTime with the time it read *before* the send. When a send outlasts the 15 s
// interval, the condition is true again on the very next iteration and the updates go out back
// to back with the control loop parked inside them.
//
// DeviceService::sendDeviceUpdate() already stamps after the call
// returns; this test holds SensorService to the same rule.
//
// Revert the fix in SensorService.cpp and this test reports a gap of a few milliseconds where
// fifteen seconds are required.

#include <unity.h>

#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"

#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"

#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimNetwork.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <vector>

using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimNetwork;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;
const uint32_t SENSOR_UPDATE_INTERVAL_MS = 15000; // SensorService::SENSOR_UPDATE_INTERVAL

// Same budget arithmetic as S1: a stalled POST costs three 30 s timeouts and three 200 ms pauses.
const uint32_t SLOW_SEND_MS = 90600;

// Two completed sends with the fix in place finish at 15 000 + 90 600 + 15 000 + 90 600.
const uint64_t OBSERVATION_WINDOW_MS = 230000;

class SlowHybridApiService : public MockHybridAPIService {
public:
    struct Send {
        uint64_t startedAtMs;
        uint64_t finishedAtMs;
    };

    SlowHybridApiService(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock)
    {
    }

    bool sendModuleUpdateData(const UpdateDto& updateData, ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        MockHybridAPIService::sendModuleUpdateData(updateData, priority);

        Send record;
        record.startedAtMs = _clock.nowMs();
        _scheduler.delayMs(SLOW_SEND_MS);
        record.finishedAtMs = _clock.nowMs();
        sends.push_back(record);
        return false;
    }

    std::vector<Send> sends;

private:
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
};

} // namespace

static void test_sensor_updates_do_not_run_back_to_back_when_the_server_is_slow(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    SimArduino arduino(clock, &scheduler);

    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = false;

    MockSettings settings;
    MockModuleConfig moduleConfig(&logger, &debugSystem, &settings);
    moduleConfig.setSettings(&settings);
    moduleConfig.setModuleUid("sensor-module");

    MockNTPTimeAdapter timeAdapter;
    // SensorService::loop() returns immediately when there are no active sensors, so the module
    // needs one for the update path to be reached at all.
    MockSensorRepository sensorRepository;
    SensorEntity sensor;
    sensor.uid = "84:F7:03:FE:88:2C.00";
    sensor.model = "ry_ws301";
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.isActive = true;
    sensor.isError = false;
    sensor.modbusDeviceAddress = 1;
    sensorRepository.sensors.push_back(sensor);
    MockSensorValueModel sensorValues;
    MockSensorReader sensorReader;
    MockMqttManager mqttManager;
    ModuleStructure moduleStructure;
    VpdChannelUseCase vpdChannelUseCase;
    VirtualSensorChannelsUseCase virtualChannels(&vpdChannelUseCase);

    SlowHybridApiService hybridApiService(scheduler, clock);

    SensorService sensorService(
        &moduleConfig,
        &moduleStructure,
        &sensorRepository,
        &sensorReader,
        &sensorValues,
        &timeAdapter,
        &arduino,
        &hybridApiService,
        &logger,
        &debugSystem,
        nullptr,  // udpManager - the UDP path is not what this test is about
        nullptr,  // jsonConverter
        nullptr,  // plausibility gate has its own suite
        &virtualChannels);

    uint64_t loopIterations = 0;
    scheduler.createTask("arduino-loop", 512 * 1024, 1, [&]() {
        while (true)
        {
            loopIterations++;
            sensorService.loop();
            scheduler.delayMs(CONTROL_PERIOD_MS);
        }
    });

    scheduler.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_FALSE_MESSAGE(scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(loopIterations > 100,
                             "the service loop barely ran - the harness, not the firmware, is at fault");
    TEST_ASSERT_TRUE_MESSAGE(hybridApiService.sends.size() >= 2,
                             "fewer than two updates completed - there is no interval to measure");

    for (size_t i = 1; i < hybridApiService.sends.size(); ++i)
    {
        const uint64_t idleMs = hybridApiService.sends[i].startedAtMs -
                                hybridApiService.sends[i - 1].finishedAtMs;

        char message[224];
        snprintf(message, sizeof(message),
                 "sensor update %u started %lu ms after update %u came back (interval is %lu ms): "
                 "lastSensorUpdateTime is stamped before the send, not after it",
                 (unsigned)i, (unsigned long)idleMs, (unsigned)(i - 1),
                 (unsigned long)SENSOR_UPDATE_INTERVAL_MS);

        TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(SENSOR_UPDATE_INTERVAL_MS,
                                                  (unsigned int)idleMs,
                                                  message);
    }
}

extern "C" void run_sensor_update_interval_tests(void)
{
    printf("\n----- Running Sensor Update Interval Tests -----\n");

    RUN_TEST(test_sensor_updates_do_not_run_back_to_back_when_the_server_is_slow);

    printf("\n----- Sensor Update Interval Tests Complete -----\n");
}
