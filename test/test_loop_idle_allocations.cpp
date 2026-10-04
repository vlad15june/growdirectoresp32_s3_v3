// A settled idle iteration of the control loop must not allocate.
//
// The free-running loop iterates hundreds to thousands of times a second. Before this file's
// change, every one of those iterations deep-copied the whole sensor registry
// (SensorRepository::getActiveSensors() -> Repository::getAll(), two copies of every entity),
// copied the device list two to four times (DeviceModel::getAllDevices() returned the vector by
// value), and built a std::string signature of all device state by concatenation - churn that
// fragments the heap around the 16 KB TLS blocks the network task allocates next to it, for
// iterations in which nothing changed at all.
//
// The headline test wires the real SensorService and DeviceService - with the real
// SensorRepository over an in-memory DAO, and the real ProcessDeviceStateUseCase - runs them to
// a settled state, and then counts global operator new calls (Sim/HostAllocationCounter.h)
// across a stretch of idle iterations. The claim is exact: zero.
//
// The rest of the file is the other half of the bargain. The zero is bought with caches - a
// filtered view in the repository, a state snapshot in the device service - and a cache whose
// invalidation misses one change path makes the rules act on a dead list, which is worse than
// the copies ever were. So every change path gets its own test proving the next read sees the
// change.
//
// To see the headline red: make SensorRepository::getActiveSensors() rebuild its view on every
// call again, or have DeviceService call getAllDevices() (the by-value accessor) instead of
// allDevices(), or rebuild a signature string in DeviceService::loop() - the counter catches
// each one on its own.

#include <unity.h>

#include "Application/Services/Devices/DeviceService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCase.h"
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Mock/Application/MockOutbox.h"
#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorDao.h"

#include "Sim/HostAllocationCounter.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

// Realistic UIDs: backend UIDs are GUID-sized, far past the 15-character small-string buffer of
// std::string on this toolchain. A short UID would sit in the small-string buffer, allocate
// nothing when copied, and quietly turn the red test green without any fix being in place.
const char* kSensorUid1 = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";
const char* kSensorUid2 = "1b5d0e3f-64c2-5a7f-ad48-9e32f6b1c582";
const char* kDeviceUid1 = "2c6e1f40-75d3-6b80-be59-af43a7c2d693";
const char* kDeviceUid2 = "3d7f2a51-86e4-7c91-cf6a-ba54b8d3e7a4";
const char* kDeviceUid3 = "6a0c5d84-b9a7-af04-fc9d-ed87eba6bad7";
const char* kRuleUid1 = "5f9b4c73-a8f6-9eb3-eb8c-dc76daf5a9c6";

const uint32_t CONTROL_PERIOD_MS = 10;
const int WARMUP_ITERATIONS = 20;
const int MEASURED_ITERATIONS = 100;

SensorEntity makeSensor(const char* uid, int address)
{
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.sensorType = "temperature_and_humidity";
    sensor.model = "soil_moisture_temperature_ec_sensor";
    sensor.protocol = "ModBus";
    sensor.createdAt = "2026-08-22T10:00:00.0000000Z";
    sensor.pin = 4;
    sensor.isActive = true;
    sensor.isError = false;
    sensor.modbusDeviceAddress = address;

    ModbusRegisterConfig humidity;
    humidity.channel = 1;
    humidity.channelName = "soil_volumetric_water_content";
    humidity.sensorType = "soil_moisture_sensor_channel";
    sensor.modbusRegisterConfigs.push_back(humidity);

    ModbusRegisterConfig temperature;
    temperature.channel = 2;
    temperature.channelName = "soil_temperature_channel_name";
    temperature.sensorType = "soil_temperature_sensor_type";
    sensor.modbusRegisterConfigs.push_back(temperature);

    return sensor;
}

/** MockOutbox that also counts device updates, which the base mock does not record. */
class CountingOutbox : public MockOutbox {
public:
    uint32_t deviceUpdates = 0;

    uint32_t submitDeviceUpdate(const UpdateDto& update) override
    {
        deviceUpdates++;
        return MockOutbox::submitDeviceUpdate(update);
    }
};

/** The factory DeviceService::init() asks for the real state machine. */
class RealUseCaseFactory : public IProcessDeviceStateUseCaseFactory {
public:
    RealUseCaseFactory(ILogger* logger, IDebugSystem* debugSystem, IModuleConfig* moduleConfig,
                       IArduinoAdapter* arduinoAdapter, IDeviceModel* deviceModel)
        : _logger(logger), _debugSystem(debugSystem), _moduleConfig(moduleConfig),
          _arduinoAdapter(arduinoAdapter), _deviceModel(deviceModel)
    {
    }

    IProcessDeviceStateUseCase* create(int) override
    {
        return new ProcessDeviceStateUseCase(_logger, _debugSystem, _moduleConfig,
                                             _arduinoAdapter, _deviceModel);
    }

private:
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IModuleConfig* _moduleConfig;
    IArduinoAdapter* _arduinoAdapter;
    IDeviceModel* _deviceModel;
};

/**
 * The two services of the control loop these tests are about, wired as on the device: the real
 * SensorRepository (with its cache) over an in-memory DAO, the real ProcessDeviceStateUseCase
 * driving relays, and an outbox behind the device update path so the change-detection branch of
 * DeviceService::loop() actually runs.
 */
struct IdleLoopModule {
    VirtualClock clock;
    SimArduino arduino;
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;

    MockSpiffsSensorDao sensorDao;
    SensorRepository sensorRepository;
    MockSensorReader sensorReader;
    MockSensorValueModel sensorValueModel;
    SensorService sensorService;

    MockDeviceModel deviceModel;
    DeviceDigitalOut device1;
    DeviceDigitalOut device2;
    RealUseCaseFactory useCaseFactory;
    CountingOutbox outbox;
    DeviceService deviceService;

    IdleLoopModule()
        : arduino(clock),
          moduleConfig(&logger, &debugSystem, &settings),
          sensorRepository(&sensorDao),
          sensorService(&moduleConfig, nullptr, &sensorRepository, &sensorReader,
                        &sensorValueModel, &timeAdapter, &arduino, nullptr, &logger,
                        &debugSystem),
          // Value-initialized on purpose, mirroring `new DeviceDigitalOut()` on the device:
          // Device initializes almost none of its fields, and a fixture device left to default
          // initialization carries stack garbage in every field setUpDevice() does not name -
          // including pointers createDeviceUpdateDto() dereferences once workByRule is set.
          device1(),
          device2(),
          useCaseFactory(&logger, &debugSystem, &moduleConfig, &arduino, &deviceModel),
          deviceService(&moduleConfig, nullptr, &deviceModel, &timeAdapter, &arduino,
                        &useCaseFactory, nullptr, &logger, &debugSystem)
    {
        debugSystem.featureEnabled = false;

        settings.setUser("idle@example.com");
        settings.setAccountUid("idle-account");
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid("idle-module");
        moduleConfig.setDeviceCount(2);

        sensorDao.saveSensor(makeSensor(kSensorUid1, 1));
        sensorDao.saveSensor(makeSensor(kSensorUid2, 2));

        setUpDevice(device1, kDeviceUid1);
        setUpDevice(device2, kDeviceUid2);
        deviceModel.addDevice(&device1);
        deviceModel.addDevice(&device2);

        deviceService.setOutbox(&outbox);

        TEST_ASSERT_TRUE_MESSAGE(sensorService.init(), "sensor service failed to initialize");
        TEST_ASSERT_TRUE_MESSAGE(deviceService.init(), "device service failed to initialize");
    }

    static void setUpDevice(DeviceDigitalOut& device, const char* uid)
    {
        device.deviceUid = uid;
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = DEVICE_STATE_OFF;
        device.isError = false;
        device.isAuto = false;
        device.isManual = false;
        device.workByRule = false;
        device.workManual = false;
        device.needToSwitchBack = false;
        device.needReportingRuleUid = false;
        device.commandId = 0;
        device.ruleUid = nullptr;
        device.switchedAt = nullptr;
        // The fields createDeviceUpdateDto() reads once a device claims rule work. The devices
        // are value-initialized already; set explicitly so the test does not lean on that.
        device.ruleType = RuleType::SENSOR;
        device.ruleVersion = 0;
        device.workflowUid = nullptr;
        device.workflowVersion = 0;
        device.sensorUid = nullptr;
        device.ruleSensorStartAt = nullptr;
        device.ruleSensorStopAt = nullptr;
        device.ruleStopReason = nullptr;
        device.ruleScheduleStartAt = nullptr;
        device.isRuleTurnOnDevice = false;
    }

    /** One control-loop iteration the way main.cpp makes it, for the services under test. */
    void iterate()
    {
        sensorService.loop();
        deviceService.loop();
        clock.advanceMs(CONTROL_PERIOD_MS);
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// The headline: a settled idle iteration allocates nothing.
// --------------------------------------------------------------------------------------------

static void test_settled_idle_iteration_allocates_nothing(void)
{
    IdleLoopModule module;

    // Settle: first iterations load the repository cache, drive the relays to their levels and
    // submit the first device update. All of that may allocate - it happens once.
    for (int i = 0; i < WARMUP_ITERATIONS; i++)
    {
        module.iterate();
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.deviceUpdates,
                                     "warm-up should have produced exactly the initial device update");

    // The measured stretch stays inside the 15 s sensor tick and the 30 s device interval:
    // what runs here is exactly the iteration the loop makes thousands of times a second.
    // Trace is on for the stretch: a passing run prints nothing, a regression prints the
    // backtrace of every stray allocation instead of just a number.
    HostAllocationCounter::reset();
    HostAllocationCounter::setTrace(true);

    for (int i = 0; i < MEASURED_ITERATIONS; i++)
    {
        module.iterate();
    }

    HostAllocationCounter::setTrace(false);
    const unsigned long long allocations = HostAllocationCounter::newCount();

    char message[160];
    snprintf(message, sizeof(message),
             "%llu heap allocations across %d settled idle iterations (%llu per iteration) - "
             "an idle iteration must allocate nothing",
             (unsigned long long)allocations, MEASURED_ITERATIONS,
             (unsigned long long)(allocations / MEASURED_ITERATIONS));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)allocations, message);

    // The stretch measured was the idle path, not a stalled loop: nothing new was submitted and
    // the sensors are still there to be polled.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.outbox.deviceUpdates,
                                     "an idle stretch must not submit device updates");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)module.sensorRepository.getActiveSensors().size(),
                                  "the active sensors disappeared during the idle stretch");
}

// --------------------------------------------------------------------------------------------
// Invalidation: every path that changes the sensor registry is seen by the next read.
// The cached view the zero is bought with must die with the cache, on every change path.
// --------------------------------------------------------------------------------------------

static bool viewContains(ISensorRepository& repository, const char* uid)
{
    for (const auto& sensor : repository.getActiveSensors())
    {
        if (sensor.uid == uid)
        {
            return true;
        }
    }
    return false;
}

static void test_saving_a_new_sensor_is_seen_by_the_next_read(void)
{
    MockSpiffsSensorDao dao;
    SensorRepository repository(&dao);
    dao.saveSensor(makeSensor(kSensorUid1, 1));
    TEST_ASSERT_EQUAL_INT(1, (int)repository.getActiveSensors().size());

    repository.save(makeSensor(kSensorUid2, 2));

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)repository.getActiveSensors().size(),
                                  "a sensor saved through the repository must appear on the next read");
    TEST_ASSERT_TRUE(viewContains(repository, kSensorUid2));
}

static void test_deactivating_a_sensor_is_seen_by_the_next_read(void)
{
    MockSpiffsSensorDao dao;
    SensorRepository repository(&dao);
    dao.saveSensor(makeSensor(kSensorUid1, 1));
    dao.saveSensor(makeSensor(kSensorUid2, 2));
    TEST_ASSERT_EQUAL_INT(2, (int)repository.getActiveSensors().size());

    SensorEntity deactivated = makeSensor(kSensorUid2, 2);
    deactivated.isActive = false;
    repository.save(deactivated);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)repository.getActiveSensors().size(),
                                  "a deactivated sensor must leave the active view on the next read");
    TEST_ASSERT_FALSE(viewContains(repository, kSensorUid2));
}

static void test_a_status_update_is_seen_by_the_next_read(void)
{
    MockSpiffsSensorDao dao;
    SensorRepository repository(&dao);
    dao.saveSensor(makeSensor(kSensorUid1, 1));
    TEST_ASSERT_FALSE(repository.getActiveSensors()[0].isError);

    // The path SensorService takes when a read fails: get + save through the repository.
    TEST_ASSERT_TRUE(repository.updateSensorStatus(kSensorUid1, true, true));

    TEST_ASSERT_EQUAL_INT(1, (int)repository.getActiveSensors().size());
    TEST_ASSERT_TRUE_MESSAGE(repository.getActiveSensors()[0].isError,
                             "an error flag set through updateSensorStatus must be visible on the next read");
}

static void test_removing_a_sensor_is_seen_by_the_next_read(void)
{
    MockSpiffsSensorDao dao;
    SensorRepository repository(&dao);
    dao.saveSensor(makeSensor(kSensorUid1, 1));
    dao.saveSensor(makeSensor(kSensorUid2, 2));
    TEST_ASSERT_EQUAL_INT(2, (int)repository.getActiveSensors().size());

    TEST_ASSERT_TRUE(repository.remove(kSensorUid1));

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)repository.getActiveSensors().size(),
                                  "a removed sensor must leave the view on the next read");
    TEST_ASSERT_FALSE(viewContains(repository, kSensorUid1));
}

static void test_replacing_the_registry_is_seen_by_the_next_read(void)
{
    MockSpiffsSensorDao dao;
    SensorRepository repository(&dao);
    dao.saveSensor(makeSensor(kSensorUid1, 1));
    TEST_ASSERT_EQUAL_INT(1, (int)repository.getActiveSensors().size());

    // The path a sensor sync takes: the whole registry replaced in one call.
    std::vector<SensorEntity> replacement;
    replacement.push_back(makeSensor(kSensorUid2, 2));
    SensorEntity inactive = makeSensor("4e8a3b62-97f5-8da2-da7b-cb65c9e4f8b5", 3);
    inactive.isActive = false;
    replacement.push_back(inactive);
    TEST_ASSERT_EQUAL_INT(2, repository.saveAll(replacement));

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)repository.getActiveSensors().size(),
                                  "saveAll must replace the view on the next read");
    TEST_ASSERT_TRUE(viewContains(repository, kSensorUid2));
    TEST_ASSERT_FALSE(viewContains(repository, kSensorUid1));
}

static void test_clearing_the_registry_is_seen_by_the_next_read(void)
{
    MockSpiffsSensorDao dao;
    SensorRepository repository(&dao);
    dao.saveSensor(makeSensor(kSensorUid1, 1));
    TEST_ASSERT_EQUAL_INT(1, (int)repository.getActiveSensors().size());

    TEST_ASSERT_EQUAL_INT(1, repository.removeAll());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)repository.getActiveSensors().size(),
                                  "removeAll must empty the view on the next read");

    dao.saveSensor(makeSensor(kSensorUid1, 1));
    repository.refreshCache();
    TEST_ASSERT_EQUAL_INT(1, (int)repository.getActiveSensors().size());

    TEST_ASSERT_TRUE(repository.clear());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)repository.getActiveSensors().size(),
                                  "clear must empty the view on the next read");
}

static void test_a_refreshed_cache_is_seen_by_the_next_read(void)
{
    MockSpiffsSensorDao dao;
    SensorRepository repository(&dao);
    dao.saveSensor(makeSensor(kSensorUid1, 1));
    TEST_ASSERT_EQUAL_INT(1, (int)repository.getActiveSensors().size());

    // A change behind the repository's back, then the explicit refresh that is the documented
    // way to pick such a change up.
    dao.saveSensor(makeSensor(kSensorUid2, 2));
    repository.refreshCache();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)repository.getActiveSensors().size(),
                                  "refreshCache must rebuild the view from storage");
}

// --------------------------------------------------------------------------------------------
// Change detection: the snapshot that replaced the signature string must notice every change
// the signature noticed, and must not forget a change whose submission was refused.
// --------------------------------------------------------------------------------------------

static void test_a_device_state_change_is_reported_on_the_next_iteration(void)
{
    IdleLoopModule module;
    for (int i = 0; i < WARMUP_ITERATIONS; i++)
    {
        module.iterate();
    }
    TEST_ASSERT_EQUAL_UINT32(1, module.outbox.deviceUpdates);

    // A rule switches device 2 on. DEVICE_STATE_ON, not REQUEST_ON: the request states are
    // transitions the state machine would advance; ON is a settled state the reporter sees.
    module.device2.state = DEVICE_STATE_ON;
    module.iterate();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.outbox.deviceUpdates,
                                     "a changed device state must be submitted on the next iteration");

    // And it is a change detector, not a periodic timer wearing a new coat: nothing further
    // changed, so nothing further goes out.
    for (int i = 0; i < 10; i++)
    {
        module.iterate();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.outbox.deviceUpdates,
                                     "an unchanged state must not be submitted again");
}

static void test_a_flag_only_change_is_reported_on_the_next_iteration(void)
{
    // The signature covered more than the on/off state; the snapshot must too. workByRule and
    // ruleUid are the fields whose loss would leave the server with an unexplained device.
    IdleLoopModule module;
    for (int i = 0; i < WARMUP_ITERATIONS; i++)
    {
        module.iterate();
    }
    TEST_ASSERT_EQUAL_UINT32(1, module.outbox.deviceUpdates);

    module.device1.workByRule = true;
    module.device1.ruleUid = kRuleUid1;
    module.iterate();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.outbox.deviceUpdates,
                                     "a rule-context change with no state change must still be reported");
}

static void test_a_refused_submission_is_retried_on_the_next_iteration(void)
{
    // The old code stamped the signature only when the outbox accepted the update, so a refusal
    // meant "still changed" on the next iteration. The snapshot must keep that property, or a
    // change that hit a full outbox once is never reported at all.
    IdleLoopModule module;
    for (int i = 0; i < WARMUP_ITERATIONS; i++)
    {
        module.iterate();
    }
    TEST_ASSERT_EQUAL_UINT32(1, module.outbox.deviceUpdates);

    module.device2.state = DEVICE_STATE_ON;
    module.outbox.refusalsLeft = 1;
    module.iterate();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.outbox.deviceUpdates,
                                     "the refused submission attempt itself was expected");

    module.iterate();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, module.outbox.deviceUpdates,
                                     "a change refused by the outbox must be resubmitted, not forgotten");
}


static void test_every_reported_field_is_watched_by_the_change_detector(void)
{
    // The signature string this detector replaced carried ten fields; dropping any one of them
    // from the comparison must turn a test red, not just read wrong. Each step changes exactly
    // one reported field of a settled device and expects a submission on the next iteration
    // (plus quiet on the settle iterations after it). The steps run in an order where the state
    // machine leaves the mutated field alone: isAuto/isManual are reset every pass while the
    // device is OFF, so they are exercised with the device ON.
    IdleLoopModule module;
    for (int i = 0; i < WARMUP_ITERATIONS; i++)
    {
        module.iterate();
    }
    uint32_t expected = module.outbox.deviceUpdates;
    TEST_ASSERT_EQUAL_UINT32(1, expected);

    auto expectReportedAfter = [&](const char* field) {
        module.iterate();
        expected++;
        char message[96];
        snprintf(message, sizeof(message),
                 "a change of %s was not reported on the next iteration", field);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, module.outbox.deviceUpdates, message);
        for (int i = 0; i < 3; i++)
        {
            module.iterate();
        }
        snprintf(message, sizeof(message), "the report of %s did not settle", field);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, module.outbox.deviceUpdates, message);
    };

    module.device2.commandId = 42;
    expectReportedAfter("commandId");

    module.device2.workManual = true;
    expectReportedAfter("workManual");

    module.device2.ruleUid = kRuleUid1;
    expectReportedAfter("ruleUid");

    module.device2.switchedAt = "2026-08-22T12:00:00.0000000Z";
    expectReportedAfter("switchedAt");

    module.device2.workByRule = true;
    expectReportedAfter("workByRule");

    // The state change also stamps switchedAt on the same iteration - one submission carries
    // both, which is exactly what the signature did.
    module.device2.state = DEVICE_STATE_ON;
    expectReportedAfter("state");

    module.device2.isAuto = true;
    expectReportedAfter("isAuto");

    module.device2.isManual = true;
    expectReportedAfter("isManual");

    // timeOn/durationOn keep processOn() from switching the device back off; they are not
    // reported fields, and the settle assertions above have just shown that touching
    // non-reported fields alone submits nothing.
    module.device2.timeOn = (uint32_t)module.clock.nowMs();
    module.device2.durationOn = 3600u * 1000u;
    module.device2.needToSwitchBack = true;
    expectReportedAfter("needToSwitchBack");

    module.device2.deviceUid = kDeviceUid3;
    expectReportedAfter("uid");
}

static void test_switched_at_is_stamped_when_the_state_changes(void)
{
    IdleLoopModule module;
    for (int i = 0; i < WARMUP_ITERATIONS; i++)
    {
        module.iterate();
    }
    TEST_ASSERT_NULL_MESSAGE(module.device2.switchedAt,
                             "switchedAt must stay unset while the state never changed");

    module.device2.state = DEVICE_STATE_ON;
    module.iterate();

    TEST_ASSERT_NOT_NULL_MESSAGE(module.device2.switchedAt,
                                 "a state change must stamp the moment the device switched");
    TEST_ASSERT_EQUAL_STRING(module.timeAdapter.getFormattedTime(), module.device2.switchedAt);

    // Stamped on the change, not refreshed on every iteration afterwards.
    const std::string stampedAt = module.device2.switchedAt;
    for (int i = 0; i < 5; i++)
    {
        module.iterate();
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(stampedAt.c_str(), module.device2.switchedAt,
                                     "switchedAt must not move while the state holds");
}

extern "C" void run_loop_idle_allocation_tests(void)
{
    printf("\n----- Running Loop Idle Allocation Tests -----\n");

    RUN_TEST(test_settled_idle_iteration_allocates_nothing);

    RUN_TEST(test_saving_a_new_sensor_is_seen_by_the_next_read);
    RUN_TEST(test_deactivating_a_sensor_is_seen_by_the_next_read);
    RUN_TEST(test_a_status_update_is_seen_by_the_next_read);
    RUN_TEST(test_removing_a_sensor_is_seen_by_the_next_read);
    RUN_TEST(test_replacing_the_registry_is_seen_by_the_next_read);
    RUN_TEST(test_clearing_the_registry_is_seen_by_the_next_read);
    RUN_TEST(test_a_refreshed_cache_is_seen_by_the_next_read);

    RUN_TEST(test_a_device_state_change_is_reported_on_the_next_iteration);
    RUN_TEST(test_a_flag_only_change_is_reported_on_the_next_iteration);
    RUN_TEST(test_a_refused_submission_is_retried_on_the_next_iteration);
    RUN_TEST(test_every_reported_field_is_watched_by_the_change_detector);
    RUN_TEST(test_switched_at_is_stamped_when_the_state_changes);

    printf("\n----- Loop Idle Allocation Tests Complete -----\n");
}
