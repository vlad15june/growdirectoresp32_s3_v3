#include <unity.h>
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IRepublishModuleStructureUseCase.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"

// Global test variables
static MockHybridAPIService g_mockApiService;
static MockSensorTypeRepository g_mockRepository;
static MockArduinoAdapter g_mockArduinoAdapter;
static MockNTPTimeAdapter g_mockNtpTimeAdapter;
static JsonConverter g_jsonConverter;
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static SyncSensorTypesUseCase* g_syncUseCase;

// Helper function to create test sensor type
static SensorType createTestSensorType(const std::string& uid, const std::string& name, 
                                      const std::string& updatedAt = "default") {
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " Display";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = (updatedAt == "default") ? "2026-02-11T09:01:35.44352+00:00" : updatedAt;
    
    SubSensorType subType;
    subType.name = "test_sensor";
    subType.displayName = "Test Sensor";
    subType.isAnalog = false;
    subType.canCalibrate = false;
    subType.minValue = 0.0f;
    subType.maxValue = 100.0f;
    subType.unit = "test";
    subType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    subType.updatedAt = "2026-02-11T09:01:35.44352+00:00";
    
    sensorType.sensorTypes.push_back(subType);
    
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = 0;
    config.numRegisters = 1;
    config.multiplier = 0.01f;
    config.offset = 0.0f;
    config.channelName = "test_channel";
    config.channel = 1;
    config.sensorType = "test_sensor";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    
    sensorType.modbusRegisterConfig.push_back(config);
    
    return sensorType;
}

// Test setup and teardown
void sync_sensor_types_use_case_setup(void) {
    g_mockApiService.reset();
    g_mockRepository.reset();
    g_mockArduinoAdapter.reset();
    g_mockNtpTimeAdapter.reset();
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    
    // Enable debug logging
    g_mockDebugSystem.featureEnabled = true;
    
    // Create the use case
    g_syncUseCase = new SyncSensorTypesUseCase(
        &g_mockApiService,
        &g_mockRepository,
        &g_mockArduinoAdapter,
        &g_mockNtpTimeAdapter,
        &g_jsonConverter,
        &g_mockLogger,
        &g_mockDebugSystem
    );
}

void sync_sensor_types_use_case_teardown(void) {
    delete g_syncUseCase;
    g_syncUseCase = nullptr;
}

// Test: init with all dependencies available
void test_sync_sensor_types_init_success(void) {
    sync_sensor_types_use_case_setup();
    
    // Act
    bool result = g_syncUseCase->init();
    
    // Assert
    TEST_ASSERT_TRUE(result);
    
    sync_sensor_types_use_case_teardown();
}

// Test: loop does not sync before interval
void test_sync_sensor_types_loop_no_sync_before_interval(void) {
    sync_sensor_types_use_case_setup();
    
    g_syncUseCase->init();
    
    // Prepare API response for first sync (constructor sets _lastSyncTime so first loop() will sync)
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid-1", "test_sensor_1"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // First loop call - will sync because constructor initializes _lastSyncTime to trigger sync
    g_mockArduinoAdapter.setMillis(1000);
    g_syncUseCase->loop();
    
    // Reset the flag
    g_mockApiService.getSensorTypesListCalled = false;
    
    // Advance time but not enough for next sync interval (less than 5 minutes)
    g_mockArduinoAdapter.setMillis(2000); // Only 1 second passed since last sync
    
    // Act - call loop again
    g_syncUseCase->loop();
    
    // Assert - no API call should be made because not enough time has elapsed since last sync
    TEST_ASSERT_FALSE(g_mockApiService.getSensorTypesListCalled);
    
    sync_sensor_types_use_case_teardown();
}

// Test: loop syncs after interval
void test_sync_sensor_types_loop_sync_after_interval(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_mockArduinoAdapter.setMillis(0);
    g_syncUseCase->init();
    
    // Advance time beyond sync interval (5 minutes = 300000 ms)
    g_mockArduinoAdapter.setMillis(300001);
    
    // Prepare API response
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid-1", "test_sensor_1"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Act
    g_syncUseCase->loop();
    
    // Assert
    TEST_ASSERT_TRUE(g_mockApiService.getSensorTypesListCalled);
    TEST_ASSERT_EQUAL(1, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: forceSync triggers immediate sync
void test_sync_sensor_types_force_sync(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_mockArduinoAdapter.setMillis(1000);
    g_syncUseCase->init();
    
    // Prepare API response
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid-1", "test_sensor_1"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert
    TEST_ASSERT_TRUE(g_mockApiService.getSensorTypesListCalled);
    
    sync_sensor_types_use_case_teardown();
}

// Test: new sensor type is added
void test_sync_sensor_types_adds_new_type(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare API response with new sensor type
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("new-uid", "new_sensor"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Repository returns empty (sensor type doesn't exist)
    g_mockRepository.setShouldReturnEmpty(true);
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert
    TEST_ASSERT_TRUE(g_mockRepository.saveCalled);
    TEST_ASSERT_EQUAL(1, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: existing sensor type with same updatedAt is not updated
void test_sync_sensor_types_same_updated_at_no_update(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    std::string updatedAt = "2026-02-11T09:01:35.44352+00:00";
    
    // Prepare API response
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("existing-uid", "existing_sensor", updatedAt));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Repository returns existing sensor type with same updatedAt
    SensorType existingType = createTestSensorType("existing-uid", "existing_sensor", updatedAt);
    g_mockRepository.addSensorType(existingType);
    
    // Reset save call count
    g_mockRepository.saveCallCount = 0;
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert - should NOT save because updatedAt is the same (already up to date)
    TEST_ASSERT_EQUAL(0, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: existing sensor type with different updatedAt is updated
void test_sync_sensor_types_different_updated_at_updates(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare API response with newer updatedAt
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("existing-uid", "existing_sensor", 
                                                "2026-02-16T03:27:11.960502+00:00"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Repository returns existing sensor type with older updatedAt
    SensorType existingType = createTestSensorType("existing-uid", "existing_sensor", 
                                                   "2026-02-11T09:01:35.44352+00:00");
    g_mockRepository.addSensorType(existingType);
    
    // Reset save call count
    g_mockRepository.saveCallCount = 0;
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert
    TEST_ASSERT_TRUE(g_mockRepository.saveCalled);
    TEST_ASSERT_EQUAL(1, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: sensor type with empty updatedAt in existing forces update
void test_sync_sensor_types_empty_updated_at_in_existing_forces_update(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare API response with valid updatedAt
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("existing-uid", "existing_sensor", 
                                                "2026-02-16T03:27:11.960502+00:00"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Repository returns existing sensor type with empty updatedAt
    SensorType existingType = createTestSensorType("existing-uid", "existing_sensor", "");
    g_mockRepository.addSensorType(existingType);
    
    // Reset save call count
    g_mockRepository.saveCallCount = 0;
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert
    TEST_ASSERT_TRUE(g_mockRepository.saveCalled);
    TEST_ASSERT_EQUAL(1, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: sensor type with empty updatedAt in new forces update
void test_sync_sensor_types_empty_updated_at_in_new_forces_update(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare API response with empty updatedAt
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("existing-uid", "existing_sensor", ""));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Repository returns existing sensor type with valid updatedAt
    SensorType existingType = createTestSensorType("existing-uid", "existing_sensor", 
                                                   "2026-02-11T09:01:35.44352+00:00");
    g_mockRepository.addSensorType(existingType);
    
    // Reset save call count
    g_mockRepository.saveCallCount = 0;
    g_mockRepository.saveCalled = false;
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert - empty updatedAt in new type should trigger update
    TEST_ASSERT_TRUE(g_mockRepository.saveCalled);
    TEST_ASSERT_EQUAL(1, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: empty server response returns success
void test_sync_sensor_types_empty_server_response(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare empty API response
    std::vector<SensorType> emptyTypes;
    g_mockApiService.sensorTypesListReturnValue = emptyTypes;
    
    // Add some existing types to repository
    g_mockRepository.addSensorType(createTestSensorType("existing-uid", "existing_sensor"));
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert - should return success and use local storage
    TEST_ASSERT_TRUE(g_mockApiService.getSensorTypesListCalled);
    // No save should be called when server returns empty
    TEST_ASSERT_EQUAL(0, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: invalid sensor type (missing uid) is not saved
void test_sync_sensor_types_invalid_type_missing_uid(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare API response with invalid sensor type (no uid)
    std::vector<SensorType> serverTypes;
    SensorType invalidType = createTestSensorType("", "test_sensor");
    invalidType.uid = ""; // Clear uid
    serverTypes.push_back(invalidType);
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert - invalid type should not be saved
    TEST_ASSERT_EQUAL(0, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: invalid sensor type (missing name) is not saved
void test_sync_sensor_types_invalid_type_missing_name(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare API response with invalid sensor type (no name)
    std::vector<SensorType> serverTypes;
    SensorType invalidType = createTestSensorType("test-uid", "");
    invalidType.name = ""; // Clear name
    serverTypes.push_back(invalidType);
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert - invalid type should not be saved
    TEST_ASSERT_EQUAL(0, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: multiple sensor types are synced correctly
void test_sync_sensor_types_multiple_types(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare API response with multiple sensor types
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("uid-1", "sensor_1"));
    serverTypes.push_back(createTestSensorType("uid-2", "sensor_2"));
    serverTypes.push_back(createTestSensorType("uid-3", "sensor_3"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    
    // Repository returns empty (all new)
    g_mockRepository.setShouldReturnEmpty(true);
    
    // Act
    g_syncUseCase->forceSync();
    
    // Assert
    TEST_ASSERT_EQUAL(3, g_mockRepository.saveCallCount);
    
    sync_sensor_types_use_case_teardown();
}

// Test: getLastSyncStatus returns correct value
void test_sync_sensor_types_get_last_sync_status(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    // Prepare successful sync
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid", "test_sensor"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    g_mockRepository.setShouldReturnEmpty(true);
    
    // Act
    g_syncUseCase->forceSync();
    bool status = g_syncUseCase->getLastSyncStatus();
    
    // Assert
    TEST_ASSERT_TRUE(status);
    
    sync_sensor_types_use_case_teardown();
}

// Test: getLastSyncTime returns correct value
void test_sync_sensor_types_get_last_sync_time(void) {
    sync_sensor_types_use_case_setup();
    
    // Arrange
    g_syncUseCase->init();
    
    time_t expectedTime = 1234567890;
    g_mockNtpTimeAdapter.setEpochTime(expectedTime);
    
    // Prepare successful sync
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid", "test_sensor"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    g_mockRepository.setShouldReturnEmpty(true);
    
    // Act
    g_syncUseCase->forceSync();
    time_t syncTime = g_syncUseCase->getLastSyncTime();
    
    // Assert
    TEST_ASSERT_EQUAL(expectedTime, syncTime);
    
    sync_sensor_types_use_case_teardown();
}


// --- GDV1-3782: the catalog moved the computed VPD channel -> log + one structure republish ---

static ModbusRegisterConfig makeVpdSyncConfig(uint8_t channel, const std::string& sensorType) {
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = 0;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channelName = sensorType;
    config.channel = channel;
    config.sensorType = sensorType;
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    return config;
}

static SensorEntity makeVpdSyncSensor(const std::string& uid, const std::string& typeUid) {
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.sensorTypeUid = typeUid;
    sensor.modbusRegisterConfigs.push_back(makeVpdSyncConfig(1, "air_temp"));
    sensor.modbusRegisterConfigs.push_back(makeVpdSyncConfig(2, "humidity"));
    return sensor;
}

static SensorType makeVpdSyncType(const std::string& typeUid, const std::vector<ModbusRegisterConfig>& configs) {
    SensorType sensorType;
    sensorType.uid = typeUid;
    sensorType.name = "ry_ws301";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.baudRate = 9600;
    sensorType.modbusRegisterConfig = configs;
    return sensorType;
}


// The sync is given IRepublishModuleStructureUseCase and IVirtualSensorChannelsUseCase (PR #17
// review): the test injects its own republish use case instead of hooking a callback.
class FakeRepublishModuleStructureUseCase : public IRepublishModuleStructureUseCase {
public:
    int calls = 0;
    std::string lastReason;

    bool execute(const char* reason) override {
        calls++;
        lastReason = reason ? reason : "";
        return true;
    }
};

// INV-4, INV-10: an extra register channel shifts max+1, so the structure must be resent once
void test_sync_sensor_types_vpd_shift_triggers_republish(void) {
    sync_sensor_types_use_case_setup();

    MockSensorRepository sensorRepository;
    sensorRepository.sensors.push_back(makeVpdSyncSensor("AA:BB:CC:DD:EE:FF.00", "type-uid"));
    sensorRepository.sensors.push_back(makeVpdSyncSensor("AA:BB:CC:DD:EE:FF.01", "type-uid"));

    SyncSensorTypesUseCase useCase(
        &g_mockApiService,
        &g_mockRepository,
        &g_mockArduinoAdapter,
        &g_mockNtpTimeAdapter,
        &g_jsonConverter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &sensorRepository);

    VpdChannelUseCase vpdChannelUseCase;
    VirtualSensorChannelsUseCase virtualChannels(&vpdChannelUseCase);
    FakeRepublishModuleStructureUseCase republish;
    useCase.setVirtualSensorChannelsUseCase(&virtualChannels);
    useCase.setRepublishModuleStructureUseCase(&republish);

    std::vector<ModbusRegisterConfig> grown;
    grown.push_back(makeVpdSyncConfig(1, "air_temp"));
    grown.push_back(makeVpdSyncConfig(2, "humidity"));
    grown.push_back(makeVpdSyncConfig(5, "co2"));

    std::vector<SensorType> changed;
    changed.push_back(makeVpdSyncType("type-uid", grown));

    g_mockApiService.sensorTypesListReturnValue = changed;
    g_mockRepository.setShouldReturnEmpty(true);
    useCase.forceSync();

    // One call per synchronization, not per sensor
    TEST_ASSERT_EQUAL(1, republish.calls);
    TEST_ASSERT_EQUAL_STRING("virtual_channel_shift", republish.lastReason.c_str());

    sync_sensor_types_use_case_teardown();
}

// The channel layout did not change -> no republish
void test_sync_sensor_types_no_republish_without_shift(void) {
    sync_sensor_types_use_case_setup();

    MockSensorRepository sensorRepository;
    sensorRepository.sensors.push_back(makeVpdSyncSensor("AA:BB:CC:DD:EE:FF.00", "type-uid"));

    SyncSensorTypesUseCase useCase(
        &g_mockApiService,
        &g_mockRepository,
        &g_mockArduinoAdapter,
        &g_mockNtpTimeAdapter,
        &g_jsonConverter,
        &g_mockLogger,
        &g_mockDebugSystem,
        &sensorRepository);

    VpdChannelUseCase vpdChannelUseCase;
    VirtualSensorChannelsUseCase virtualChannels(&vpdChannelUseCase);
    FakeRepublishModuleStructureUseCase republish;
    useCase.setVirtualSensorChannelsUseCase(&virtualChannels);
    useCase.setRepublishModuleStructureUseCase(&republish);

    std::vector<ModbusRegisterConfig> same;
    same.push_back(makeVpdSyncConfig(1, "air_temp"));
    same.push_back(makeVpdSyncConfig(2, "humidity"));

    std::vector<SensorType> changed;
    changed.push_back(makeVpdSyncType("type-uid", same));

    g_mockApiService.sensorTypesListReturnValue = changed;
    g_mockRepository.setShouldReturnEmpty(true);
    useCase.forceSync();

    TEST_ASSERT_EQUAL(0, republish.calls);

    sync_sensor_types_use_case_teardown();
}

// Main test runner function
extern "C" int run_sync_sensor_types_use_case_tests(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_sync_sensor_types_init_success);
    RUN_TEST(test_sync_sensor_types_loop_no_sync_before_interval);
    RUN_TEST(test_sync_sensor_types_loop_sync_after_interval);
    RUN_TEST(test_sync_sensor_types_force_sync);
    RUN_TEST(test_sync_sensor_types_adds_new_type);
    RUN_TEST(test_sync_sensor_types_same_updated_at_no_update);
    RUN_TEST(test_sync_sensor_types_different_updated_at_updates);
    RUN_TEST(test_sync_sensor_types_empty_updated_at_in_existing_forces_update);
    RUN_TEST(test_sync_sensor_types_empty_updated_at_in_new_forces_update);
    RUN_TEST(test_sync_sensor_types_empty_server_response);
    RUN_TEST(test_sync_sensor_types_invalid_type_missing_uid);
    RUN_TEST(test_sync_sensor_types_invalid_type_missing_name);
    RUN_TEST(test_sync_sensor_types_multiple_types);
    RUN_TEST(test_sync_sensor_types_get_last_sync_status);
    RUN_TEST(test_sync_sensor_types_get_last_sync_time);
    RUN_TEST(test_sync_sensor_types_vpd_shift_triggers_republish);
    RUN_TEST(test_sync_sensor_types_no_republish_without_shift);
    
    return UNITY_END();
}
