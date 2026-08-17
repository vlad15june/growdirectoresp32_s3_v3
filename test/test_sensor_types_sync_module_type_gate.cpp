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

/**
 * Build the use case under test for a given module type.
 *
 * @param moduleType Module type to report through the injected module config; a value below 1
 *        leaves the module config unset, the way the use case was wired before GDV1-3828.
 */
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

void test_sync_gate_device_module_does_not_fetch_on_force_sync(void) {
    sync_gate_setup(MODULE_TYPE_DEVICE);

    bool result = g_syncGateUseCase->forceSync();

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FALSE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_FALSE(g_syncGateRepository.saveAllCalled);
    TEST_ASSERT_FALSE(g_syncGateRepository.getAllCalled);

    sync_gate_teardown();
}

void test_sync_gate_device_module_does_not_fetch_on_loop(void) {
    sync_gate_setup(MODULE_TYPE_DEVICE);

    g_syncGateArduinoAdapter.setMillis(1000);
    g_syncGateUseCase->loop();

    TEST_ASSERT_FALSE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_FALSE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

void test_sync_gate_analog_input_device_does_not_fetch(void) {
    sync_gate_setup(MODULE_TYPE_ANALOG_INPUT_DEVICE);

    g_syncGateUseCase->forceSync();

    TEST_ASSERT_FALSE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_FALSE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

void test_sync_gate_sensor_module_still_syncs(void) {
    sync_gate_setup(MODULE_TYPE_SENSOR);

    bool result = g_syncGateUseCase->forceSync();

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_TRUE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

void test_sync_gate_hydro_module_still_syncs(void) {
    sync_gate_setup(MODULE_TYPE_HYDRO);

    g_syncGateUseCase->forceSync();

    TEST_ASSERT_TRUE(g_syncGateApiService.getSensorTypesListCalled);
    TEST_ASSERT_TRUE(g_syncGateRepository.saveAllCalled);

    sync_gate_teardown();
}

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
