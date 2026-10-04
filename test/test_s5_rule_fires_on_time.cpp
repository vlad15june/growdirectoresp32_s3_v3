// Scenario S5 - a rule due at moment T fires at moment T, even though a slow network send is
// in flight right then.
//
// This is the user-visible symptom stated as a test. S1 says the control loop keeps turning; S5
// says the user gets something out of that. Watering is scheduled for 08:00:00. At 07:59:30 the
// module starts its periodic diagnostic send and the server stalls, so the send occupies the
// next ninety seconds. The relay must still close at 08:00:00.
//
// The chain is traced to the end: the real EvaluateRulesUseCase evaluates a real SCHEDULE rule,
// sets the real DeviceDigitalOut to REQUEST_ON, the real ProcessDeviceStateUseCase turns that
// into digitalWrite(relayPin, HIGH) - and the assertion is on the timestamp of that GPIO write,
// not on an intention somewhere upstream.
//
// SCHEDULE was chosen on purpose. Four of the eight rule types the platform offers - ph, ec,
// dimmer_auto, dimmer_manual - are validated, reported synchronized and then never evaluated:
// the rule-type switch in EvaluateRulesUseCase has no case for them, so they land in its default
// branch, which logs "Unsupported rule type" and leaves the device off. A test built on one
// of those could not be late, because it would never fire at all, and would prove nothing about
// timing.
//
// Call diagnosticService.setOutbox(nullptr) below and the send runs inline again: the relay then
// closes 60 670 ms late (measured, not estimated - the rule is due at t = 60 000 ms and the GPIO
// write lands at t = 120 670 ms), which is what this test catches. That is two
// diagnostic sends' worth of stall, not one: the loop is parked inside a send when the rule
// becomes due and again before it gets round to evaluating rules.

#include <unity.h>

#include "Application/Services/DiagnosticService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/Outbox.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Application/Services/Rules/RuleService.h"

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCase.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"

#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimNetwork.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimNetwork;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// The send budget the firmware grants itself, same arithmetic as in the S1 file:
// a /TestConnection ping, then three POST attempts of setTimeout(30000) with delay(200) between
// them (RestApiService).
const uint32_t PING_MS = 50;
const uint32_t ATTEMPT_TIMEOUT_MS = 30000;
const uint32_t ATTEMPTS = 3;
const uint32_t PAUSE_BETWEEN_ATTEMPTS_MS = 200;

// Wall clock: virtual t = 0 is 07:59:00 UTC, so 08:00:00 falls at virtual t = 60 000 ms, in the
// middle of the diagnostic send that starts at t = 30 000 ms.
const unsigned long EPOCH_AT_T0 = 8 * 3600 - 60;
const uint64_t WATERING_DUE_AT_MS = 60000;
const uint32_t WATERING_DURATION_S = 900; // long enough that the rule stays on past any stall

// Rules are evaluated at most once a second (EvaluateRulesUseCase::EVALUATION_INTERVAL_MS), the
// loop turns every 10 ms, and the device pass runs before the rule pass, so the relay can be one
// further loop step behind. Anything beyond a second and a half is the module being late, not
// the design.
const uint64_t MAX_ACCEPTED_LATENESS_MS = 1500;

const uint64_t OBSERVATION_WINDOW_MS = 180000;

const uint8_t RELAY_CHANNEL = 0;
const uint8_t RELAY_PIN = 1; // MockModuleConfig::getRelayPins() -> {1, 2, 3, 4}

// --- stand-ins -------------------------------------------------------------------------------

// Wall-clock time that moves with the virtual clock, so "08:00:00" is a moment in the run and
// not a value someone has to remember to set.
class SimNtpTimeAdapter : public MockNTPTimeAdapter {
public:
    SimNtpTimeAdapter(VirtualClock& clock, unsigned long epochAtT0)
        : _clock(clock), _epochAtT0(epochAtT0)
    {
    }

    unsigned long getEpochTime() override { return _epochAtT0 + _clock.nowMs() / 1000; }

private:
    VirtualClock& _clock;
    unsigned long _epochAtT0;
};

class SingleDeviceModel : public IDeviceModel {
public:
    Device* device = nullptr;

    bool init() override { return true; }
    Device* getDeviceByUid(const char* uid) override
    {
        if (device && device->deviceUid && uid && std::string(device->deviceUid) == uid)
        {
            return device;
        }
        return nullptr;
    }
    std::vector<Device*> getAllDevices() override
    {
        std::vector<Device*> v;
        if (device) v.push_back(device);
        return v;
    }
    int getDeviceCount() const override { return device ? 1 : 0; }
    bool addDevice(Device*) override { return true; }
    bool removeDevice(const char*) override { return true; }
    void clearDevices() override {}
};

// The device half of the loop, doing exactly what DeviceService::loop() does with the state the
// rules produced: hand it to ProcessDeviceStateUseCase, which drives the relay. The network
// reporting DeviceService also performs is left out - it is not what S5 is about.
class RelayDrivingDeviceService : public IDeviceService {
public:
    explicit RelayDrivingDeviceService(ProcessDeviceStateUseCase* useCase) : _useCase(useCase) {}
    bool init() override { return true; }
    void loop() override { _useCase->loop(); }

private:
    ProcessDeviceStateUseCase* _useCase;
};

class CountingSensorService : public ISensorService {
public:
    int loopCount = 0;
    bool init() override { return true; }
    void loop() override { loopCount++; }
    bool readAllSensors() override { return true; }
    bool readSensor(SensorEntity) override { return true; }
};

class StallingServerRestApiService : public MockRestAPIService {
public:
    StallingServerRestApiService(SimNetwork& network, HostTaskScheduler& scheduler, VirtualClock& clock)
        : _network(network), _scheduler(scheduler), _clock(clock)
    {
    }

    bool sendDiagnosticData(const DiagnosticDto& diagnosticData) override
    {
        MockRestAPIService::sendDiagnosticData(diagnosticData);

        if (sendCount == 0)
        {
            firstSendStartedAtMs = _clock.nowMs();
        }
        sendCount++;

        _network.setResponseLatencyMs(PING_MS);
        _network.send("TestConnection", "");

        _network.setResponseLatencyMs(ATTEMPT_TIMEOUT_MS);
        for (uint32_t tries = 0; tries < ATTEMPTS; ++tries)
        {
            _network.send("diagnostic", "");
            _scheduler.delayMs(PAUSE_BETWEEN_ATTEMPTS_MS);
        }

        if (sendCount == 1)
        {
            firstSendFinishedAtMs = _clock.nowMs();
        }
        return false;
    }

    int sendCount = 0;
    uint64_t firstSendStartedAtMs = 0;
    uint64_t firstSendFinishedAtMs = 0;

private:
    SimNetwork& _network;
    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
};

Rule makeWateringScheduleRule()
{
    Rule r;
    r.uid = "watering-0800";
    r.version = 1;
    r.ruleType = RuleType::SCHEDULE;
    r.deviceUid = "dev-1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.schedule.push_back(ScheduleItem(8 * 3600, WATERING_DURATION_S));
    return r;
}

} // namespace

static void test_s5_scheduled_rule_fires_on_time_while_a_send_is_in_flight(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    SimArduino arduino(clock, &scheduler);
    SimNetwork network(clock, scheduler);

    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = false;

    MockSettings settings;
    settings.setUser("s5@example.com");
    settings.setAccountUid("s5-account");

    MockModuleConfig moduleConfig(&logger, &debugSystem, &settings);
    moduleConfig.setSettings(&settings);
    moduleConfig.setModuleUid("s5-module");
    moduleConfig.setFirmwareVersion("test");
    moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
    moduleConfig.setDeviceCount(1);

    MockWifiAdapter wifiAdapter;
    wifiAdapter.setConnected(true);

    MockMqttManager mqttManager;
    mqttManager.isConnectedReturnValue = true;

    SimNtpTimeAdapter ntp(clock, EPOCH_AT_T0);

    // --- the watering circuit ---------------------------------------------------------------
    DeviceDigitalOut device{};
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;

    SingleDeviceModel deviceModel;
    deviceModel.device = &device;

    MockRuleRepository ruleRepository;
    ruleRepository.rules.push_back(makeWateringScheduleRule());

    MockSensorValueModel sensorValues;

    EvaluateRulesUseCase evaluateRulesUseCase(&deviceModel, &ruleRepository, &arduino, &ntp, &sensorValues);
    RuleService ruleService(&evaluateRulesUseCase, nullptr, &deviceModel, &logger, &debugSystem);

    ProcessDeviceStateUseCase processDeviceState(&logger, &debugSystem, &moduleConfig, &arduino, &deviceModel);
    RelayDrivingDeviceService deviceService(&processDeviceState);

    CountingSensorService sensorService;

    // --- the network half --------------------------------------------------------------------
    StallingServerRestApiService restApiService(network, scheduler, clock);

    // REST_ONLY because this scenario has no MQTT server to stall; the device builds the same
    // object with ApiPriority::BOTH.
    HybridAPIService hybridApiService(&restApiService, nullptr, &moduleConfig, &logger,
                                      &debugSystem, ApiPriority::REST_ONLY);

    DiagnosticService diagnosticService(
        &restApiService,
        &moduleConfig,
        logger,
        debugSystem,
        nullptr,
        &wifiAdapter,
        nullptr,
        &arduino,
        &mqttManager,
        &hybridApiService);

    HostMessageQueue<OutboxCompletion> completions(8);
    Outbox outbox(&hybridApiService, &scheduler, &completions, &logger, &debugSystem);

    // --- the module --------------------------------------------------------------------------
    LoopServiceConfigureDone loopService;
    loopService.setLogging(&logger, &debugSystem);
    loopService.setModuleConfig(&moduleConfig);
    loopService.setArduinoAdapter(&arduino);
    loopService.setDeviceService(&deviceService);
    loopService.setSensorService(&sensorService);
    loopService.setRuleService(&ruleService);
    loopService.setMqttManager(&mqttManager);

    // The switch: with an outbox the loop only hands the snapshot over, without one it sends
    // inline and parks the whole module inside the call.
    loopService.setDiagnosticService(&diagnosticService);
    loopService.setOutbox(&outbox);
    diagnosticService.setOutbox(&outbox);

    uint64_t loopIterations = 0;
    scheduler.createTask("arduino-loop", 512 * 1024, 1, [&]() {
        while (true)
        {
            loopIterations++;
            loopService.loop();
            scheduler.delayMs(CONTROL_PERIOD_MS);
        }
    });

    scheduler.runForMs(OBSERVATION_WINDOW_MS);

    // --- the run has to be sound before its verdict means anything ---------------------------

    TEST_ASSERT_FALSE_MESSAGE(scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(loopIterations > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");

    TEST_ASSERT_TRUE_MESSAGE(restApiService.sendCount >= 1,
                             "no diagnostic send happened - nothing slow was exercised");

    // The whole scenario rests on the send being in flight exactly when the rule comes due.
    char overlapMessage[224];
    snprintf(overlapMessage, sizeof(overlapMessage),
             "the send ran from t=%lu ms to t=%lu ms and did not cover the moment the rule was "
             "due (t=%lu ms) - the scenario did not happen",
             (unsigned long)restApiService.firstSendStartedAtMs,
             (unsigned long)restApiService.firstSendFinishedAtMs,
             (unsigned long)WATERING_DUE_AT_MS);
    TEST_ASSERT_TRUE_MESSAGE(restApiService.firstSendStartedAtMs < WATERING_DUE_AT_MS &&
                                 restApiService.firstSendFinishedAtMs > WATERING_DUE_AT_MS,
                             overlapMessage);

    TEST_ASSERT_TRUE_MESSAGE(arduino.digitalWriteCounts[RELAY_PIN] >= 1,
                             "the relay was never driven at all - the watering did not happen");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(arduino.getDigitalHigh(), arduino.digitalOutputs[RELAY_PIN],
                                    "the relay was driven, but not to the on level");
    TEST_ASSERT_EQUAL_INT_MESSAGE(DEVICE_STATE_ON, device.state,
                                  "the device did not end up in the on state");

    // --- the actual claim of S5 ---------------------------------------------------------------
    const uint64_t relayClosedAtMs = arduino.lastWriteAtMs[RELAY_PIN];
    TEST_ASSERT_TRUE_MESSAGE(relayClosedAtMs >= WATERING_DUE_AT_MS,
                             "the relay closed before the schedule said it should");

    const uint64_t latenessMs = relayClosedAtMs - WATERING_DUE_AT_MS;

    char message[256];
    snprintf(message, sizeof(message),
             "watering was due at t=%lu ms and the relay closed at t=%lu ms, %lu ms late "
             "(limit %lu ms): the diagnostic send held the control loop across the moment the "
             "rule came due",
             (unsigned long)WATERING_DUE_AT_MS,
             (unsigned long)relayClosedAtMs,
             (unsigned long)latenessMs,
             (unsigned long)MAX_ACCEPTED_LATENESS_MS);

    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_LATENESS_MS,
                                           (unsigned int)latenessMs,
                                           message);
    (void)RELAY_CHANNEL;
}

extern "C" void run_s5_rule_fires_on_time_tests(void)
{
    printf("\n----- Running S5 Rule Timeliness Tests -----\n");

    RUN_TEST(test_s5_scheduled_rule_fires_on_time_while_a_send_is_in_flight);

    printf("\n----- S5 Rule Timeliness Tests Complete -----\n");
}
