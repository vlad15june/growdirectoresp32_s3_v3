// GDV1-3828: a module whose type has no local sensor pins has no consumer for the sensor type
// catalog, so it must not fetch, store or re-sync it. Every module of the current S3 fleet is
// such a module, and each of them paid for the catalog on every IP reconnect.
#include <unity.h>
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"

static MockHybridAPIService g_syncGateApiService;
static MockSensorTypeRepository g_syncGateRepository;
static MockArduinoAdapter g_syncGateArduinoAdapter;
static MockNTPTimeAdapter g_syncGateNtpTimeAdapter;
static JsonConverter g_syncGateJsonConverter;
static MockLogger g_syncGateLogger;
static MockDebugSystem g_syncGateDebugSystem;
static MockModuleConfig* g_syncGateModuleConfig = nullptr;
static SyncSensorTypesUseCase* g_syncGateUseCase = nullptr;

static SensorType syncGateCreateSensorType(const std::string& uid, const std::string& name) {
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " Display";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = "2026-02-11T09:01:35.44352+00:00";
    return sensorType;
}

// moduleType < 0 means "no module config injected at all" (pre-GDV1-3828 wiring)
static void sync_gate_setup(int moduleType) {
    g_syncGateApiService.reset();
    g_syncGateRepository.reset();
    g_syncGateArduinoAdapter.reset();
    g_syncGateNtpTimeAdapter.reset();
    g_syncGateLogger.reset();
    g_syncGateDebugSystem.reset();
    g_syncGateDebugSystem.featureEnabled = true;

    g_syncGateModuleConfig = nullptr;
    if (moduleType > 0) {
        g_syncGateModuleConfig = new MockModuleConfig();
        g_syncGateModuleConfig->setModuleType(moduleType);
    }

    std::vector<SensorType> serverTypes;
    serverTypes.push_back(syncGateCreateSensorType("gate-uid-1", "gate_sensor_1"));
    g_syncGateApiService.sensorTypesListReturnValue = serverTypes;

    g_syncGateUseCase = new SyncSensorTypesUseCase(
        &g_syncGateApiService,
        &g_syncGateRepository,
        &g_syncGateArduinoAdapter,
        &g_syncGateNtpTimeAdapter,
        &g_syncGateJsonConverter,
        &g_syncGateLogger,
        &g_syncGateDebugSystem,
        nullptr,
        g_syncGateModuleConfig);
}

static void sync_gate_teardown(void) {
    delete g_syncGateUseCase;
    g_syncGateUseCase = nullptr;
    delete g_syncGateModuleConfig;
    g_syncGateModuleConfig = nullptr;
}

// A drycontact module (MODULE_TYPE_DEVICE) neither fetches nor writes the catalog on forceSync().
void test_sync_gate_device_module_does_not_fetch_on_force_sync(void) {
    sync_gate_setup(MODULE_TYPE_DEVICE);

    bool result = g_syncGateUseCase->forceSync();

    TEST_ASSERT_TRUE(result); // skipping is not a failure
    TEST_ASSERT_FALSE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_FALSE(g_syncGateRepository.saveAllCalled);
    TEST_ASSERT_FALSE(g_syncGateRepository.getAllCalled);

    sync_gate_teardown();
}

// Same module, the periodic entry point: loop() must not fetch either.
void test_sync_gate_device_module_does_not_fetch_on_loop(void) {
    sync_gate_setup(MODULE_TYPE_DEVICE);

    // The constructor backdates _lastSyncTime by a full interval, so an ungated loop() syncs at once
    g_syncGateArduinoAdapter.setMillis(1000);
    g_syncGateUseCase->loop();

    TEST_ASSERT_FALSE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_FALSE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

// A dimmer module (MODULE_TYPE_ANALOG_INPUT_DEVICE) has no sensor pins either.
void test_sync_gate_analog_input_device_does_not_fetch(void) {
    sync_gate_setup(MODULE_TYPE_ANALOG_INPUT_DEVICE);

    g_syncGateUseCase->forceSync();

    TEST_ASSERT_FALSE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_FALSE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

// A sensor module does have local sensor pins: nothing changes for it.
void test_sync_gate_sensor_module_still_syncs(void) {
    sync_gate_setup(MODULE_TYPE_SENSOR);

    bool result = g_syncGateUseCase->forceSync();

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_TRUE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

// Hydro is the type that actually carries local sensors today: it must keep syncing.
void test_sync_gate_hydro_module_still_syncs(void) {
    sync_gate_setup(MODULE_TYPE_HYDRO);

    g_syncGateUseCase->forceSync();

    TEST_ASSERT_TRUE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_TRUE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

// Without a module config the type is unknown, so the use case behaves exactly as before the gate.
void test_sync_gate_without_module_config_still_syncs(void) {
    sync_gate_setup(-1);

    g_syncGateUseCase->forceSync();

    TEST_ASSERT_TRUE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_TRUE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

extern "C" int run_sensor_types_sync_module_type_gate_tests(void) {
    UNITY_BEGIN();

    RUN_TEST(test_sync_gate_device_module_does_not_fetch_on_force_sync);
    RUN_TEST(test_sync_gate_device_module_does_not_fetch_on_loop);
    RUN_TEST(test_sync_gate_analog_input_device_does_not_fetch);
    RUN_TEST(test_sync_gate_sensor_module_still_syncs);
    RUN_TEST(test_sync_gate_hydro_module_still_syncs);
    RUN_TEST(test_sync_gate_without_module_config_still_syncs);

    return UNITY_END();
}
