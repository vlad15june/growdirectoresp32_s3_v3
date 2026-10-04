// The catalog fetch that failed, and the control loop that asks again.
//
// A module asks the server for the sensor type catalog at setup, and again when its local IP
// changes. A first boot on a site where the network is still coming up spends that one fetch on a
// server it cannot reach; from then on it runs on whatever catalog is on its flash, because
// nothing in the firmware asks a second time. What closes that is the module's own offline
// decision: the moment it clears, the fetch the outage cost is handed over again.
//
// What is under test is production code: the real LoopServiceConfigureDone drives the real
// SyncSensorTypesUseCase over the real Outbox with its network task, into the real
// SensorTypeRepository and the real SpiffsSensorTypeDao. Replaced are the outside world - the
// server and its reachability, the flash device, the offline decision the diagnostics publish -
// and time.
//
// ### Reproducing the red
//
// Delete the `syncSensorTypesUseCase->loop()` call from
// LoopServiceConfigureDone::executeLoopImplementation(). Nothing then drives the use case's own
// loop, the failed fetch is never asked again, and the module finishes the run holding the boot
// catalog with the server reachable the whole second half.
//
// The dead call costs no virtual time here on purpose. What a request against a silent server
// costs the control loop is measured in test_s9_loop_with_ip_change.cpp; what this file is about
// is whether anything asks at all.

#include <unity.h>

#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/Outbox.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Mock/Core/Domain/MockNetworkStatusProvider.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// Ten minutes of a module that knows it is offline. Long enough that a re-arm keyed on anything
// other than the edge - a plain interval of minutes, say - would have fired inside it.
const uint64_t OFFLINE_WINDOW_MS = 10 * 60 * 1000;

// And one minute after the link comes back. One outbox round trip is milliseconds here, so a
// minute is two orders of magnitude of slack; what it must not be is so long that the safety-net
// interval could be what fired.
const uint64_t RECOVERY_WINDOW_MS = 60 * 1000;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "rearm-module";
const char* const BOOT_TYPE_UID = "boot-type-uid";
const char* const SERVER_TYPE_UID = "server-type-uid";

/** @brief The server, and whether the module can reach it at this moment. */
class ScriptedServer : public MockHybridAPIService {
public:
    /** Answers the catalog only while reachable; otherwise reports a request that never landed. */
    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        calls++;
        if (!reachable)
        {
            deadCalls++;
            if (httpFailed != nullptr)
            {
                *httpFailed = true;
            }
            return std::vector<SensorType>();
        }

        servedCalls++;
        if (httpFailed != nullptr)
        {
            *httpFailed = false;
        }
        return catalog;
    }

    bool reachable = false;
    std::vector<SensorType> catalog;
    uint32_t calls = 0;
    uint32_t deadCalls = 0;
    uint32_t servedCalls = 0;
};

SensorType makeType(const std::string& uid, const std::string& name)
{
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " Display";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = "2026-02-11T09:01:35.44352+00:00";

    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = 0;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channelName = "air_temp";
    config.channel = 1;
    config.sensorType = "air_temp";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    sensorType.modbusRegisterConfig.push_back(config);

    return sensorType;
}

struct ModuleUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    MockNetworkStatusProvider networkStatus;

    MockSpiffsAdapter flash;
    ArduinoJsonConverter jsonConverter;
    SpiffsSensorTypeDao sensorTypeDao;
    SensorTypeRepository sensorTypeRepository;

    ScriptedServer server;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    SyncSensorTypesUseCase syncSensorTypes;

    LoopServiceConfigureDone loopService;

    ModuleUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          sensorTypeDao(&flash, &jsonConverter, &debugSystem, &logger),
          sensorTypeRepository(&sensorTypeDao, &debugSystem, &logger),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          syncSensorTypes(&server, &sensorTypeRepository, &arduino, &timeAdapter, &jsonConverter,
                          &logger, &debugSystem, nullptr, &moduleConfig)
    {
        debugSystem.featureEnabled = false; // keep tens of thousands of loop lines out of the log
        timeAdapter.setEpochTime(1755690000);
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);

        server.catalog.push_back(makeType(SERVER_TYPE_UID, "server_sensor"));

        syncSensorTypes.setOutbox(&outbox);
        syncSensorTypes.setNetworkStatusProvider(&networkStatus);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setSyncSensorTypesUseCase(&syncSensorTypes);
    }

    /** The catalog the module had on flash when it booted. */
    void seedBootCatalog()
    {
        std::vector<SensorType> bootCatalog;
        bootCatalog.push_back(makeType(BOOT_TYPE_UID, "boot_sensor"));
        sensorTypeRepository.saveAll(bootCatalog);
        sensorTypeRepository.invalidateCache();
    }

    /** One task, mirroring main.cpp: the loop service, then one period of sleep. */
    void startArduinoLoopTask()
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this]() {
            while (true)
            {
                loopService.loop();
                loopIterations++;
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }

    /** What a module that rebooted now would read out of storage. */
    std::vector<SensorType> storedCatalog()
    {
        SpiffsSensorTypeDao daoAfterBoot(&flash, &jsonConverter, &debugSystem, &logger);
        std::vector<SensorType> stored;
        daoAfterBoot.getAll(stored);
        return stored;
    }

    bool storedCatalogHolds(const char* uid)
    {
        const std::vector<SensorType> stored = storedCatalog();
        for (size_t i = 0; i < stored.size(); i++)
        {
            if (stored[i].uid == uid)
            {
                return true;
            }
        }
        return false;
    }

    uint64_t loopIterations = 0;
};

} // namespace

// --- the claim -----------------------------------------------------------------------------

static void test_a_failed_boot_fetch_is_asked_again_when_connectivity_returns(void)
{
    ModuleUnderTest module;
    module.seedBootCatalog();

    // Setup, on a site where the network is still coming up: the one fetch a module makes before
    // its control loop exists, against a server it cannot reach.
    TEST_ASSERT_TRUE(module.syncSensorTypes.init());
    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.syncNow(),
                              "the premise failed: the boot fetch this test re-arms succeeded");
    TEST_ASSERT_TRUE_MESSAGE(module.server.deadCalls >= 1,
                             "the premise failed: the boot fetch never reached the transport");
    TEST_ASSERT_TRUE_MESSAGE(module.storedCatalogHolds(BOOT_TYPE_UID),
                             "a failed fetch changed the catalog on flash");
    module.server.calls = 0;

    // The module now knows it is offline, and the control loop runs.
    module.networkStatus.offline = true;
    module.startArduinoLoopTask();
    module.scheduler.runForMs(OFFLINE_WINDOW_MS);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.loopIterations > 1000,
                             "the control loop barely ran - the harness, not the firmware, is what "
                             "this run would be measuring");
    char offlineMessage[208];
    snprintf(offlineMessage, sizeof(offlineMessage),
             "the module made %u catalog requests in %lu ms of an outage it knows about; it must "
             "wait for the link, not poll through it",
             (unsigned)module.server.calls, (unsigned long)OFFLINE_WINDOW_MS);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.server.calls, offlineMessage);

    // The link comes back, and the server with it.
    module.server.reachable = true;
    module.networkStatus.offline = false;
    module.scheduler.runForMs(RECOVERY_WINDOW_MS);

    char message[288];
    snprintf(message, sizeof(message),
             "%lu ms after connectivity returned the module had made %u catalog requests (%u "
             "answered) and holds %u type(s) on flash; the boot fetch failed and nothing asked "
             "again",
             (unsigned long)RECOVERY_WINDOW_MS, (unsigned)module.server.calls,
             (unsigned)module.server.servedCalls, (unsigned)module.storedCatalog().size());

    TEST_ASSERT_TRUE_MESSAGE(module.server.servedCalls >= 1, message);
    TEST_ASSERT_TRUE_MESSAGE(module.storedCatalogHolds(SERVER_TYPE_UID), message);
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the catalog was fetched and stored but the synchronization was not "
                             "reported as successful");
    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.isRearmPending(),
                              "the module kept asking after the catalog arrived");

    // The type the server no longer publishes went with it: the re-armed fetch is the ordinary
    // catalog path and not a second, simpler one.
    TEST_ASSERT_FALSE_MESSAGE(module.storedCatalogHolds(BOOT_TYPE_UID),
                              "the retired type survived the re-armed synchronization");

    // One fetch, not a burst: the re-arm is armed by a failure and disarmed by the answer.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.syncSensorTypes.requestCount(),
                                     "the re-arm handed the outbox more than one catalog fetch for "
                                     "one restored link");
}

extern "C" int run_catalog_fetch_rearm_tests(void)
{
    RUN_TEST(test_a_failed_boot_fetch_is_asked_again_when_connectivity_returns);

    return 0;
}
