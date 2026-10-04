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
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Mock/Application/MockOutbox.h"
#include "Mock/Core/Domain/MockNetworkStatusProvider.h"

#include <cstdio>

// Global test variables
static MockHybridAPIService g_mockApiService;
static MockSensorTypeRepository g_mockRepository;
static MockArduinoAdapter g_mockArduinoAdapter;
static MockNTPTimeAdapter g_mockNtpTimeAdapter;
static ArduinoJsonConverter g_jsonConverter;
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

// Test: a module holding the catalog the server sent asks for nothing on the control loop.
// The loop is driven for a simulated day, which is four times the safety-net interval.
void test_sync_sensor_types_loop_does_not_poll_a_healthy_catalog(void) {
    sync_sensor_types_use_case_setup();

    g_mockArduinoAdapter.setMillis(0);
    g_syncUseCase->init();

    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid-1", "test_sensor_1"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    g_mockRepository.setShouldReturnEmpty(true);

    // The catalog the module has, fetched the way setup fetches it.
    TEST_ASSERT_TRUE(g_syncUseCase->syncNow());
    g_mockApiService.getSensorTypesListCallCount = 0;

    for (unsigned long minute = 1; minute <= 24 * 60; minute++) {
        g_mockArduinoAdapter.setMillis(minute * 60UL * 1000UL);
        g_syncUseCase->loop();
    }

    char message[160];
    snprintf(message, sizeof(message),
             "a module with the server's catalog made %d requests in a simulated day of control "
             "loop; a synchronization that succeeded re-arms nothing",
             g_mockApiService.getSensorTypesListCallCount);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockApiService.getSensorTypesListCallCount, message);

    sync_sensor_types_use_case_teardown();
}

// Test: a fetch that never reached the server is asked again once the safety-net interval passes,
// with no connectivity edge to trigger it - the outage the module never registered as one.
void test_sync_sensor_types_loop_rearms_a_failed_fetch_after_the_safety_interval(void) {
    sync_sensor_types_use_case_setup();

    g_mockArduinoAdapter.setMillis(0);
    g_syncUseCase->init();

    // The fetch setup makes, against a server that does not answer.
    g_mockApiService.getSensorTypesListHttpFailed = true;
    TEST_ASSERT_FALSE(g_syncUseCase->syncNow());
    g_mockApiService.getSensorTypesListCallCount = 0;

    // Five hours of control loop: under the interval, so nothing is asked yet.
    for (unsigned long minute = 1; minute <= 5 * 60; minute++) {
        g_mockArduinoAdapter.setMillis(minute * 60UL * 1000UL);
        g_syncUseCase->loop();
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockApiService.getSensorTypesListCallCount,
                                  "the safety net fired before its interval");

    // The server comes back and the seventh hour arrives.
    g_mockApiService.getSensorTypesListHttpFailed = false;
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid-1", "test_sensor_1"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    g_mockRepository.setShouldReturnEmpty(true);

    g_mockArduinoAdapter.setMillis(7UL * 60 * 60 * 1000);
    g_syncUseCase->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockApiService.getSensorTypesListCallCount,
                                  "the failed fetch was never asked again");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockRepository.saveCallCount,
                                  "the catalog the re-armed fetch brought back was not stored");
    TEST_ASSERT_TRUE(g_syncUseCase->getLastSyncStatus());

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

// --- the catalog fetch through the outbox ------------------------------------------------------
//
// The path the device actually takes. forceSync() does not call
// syncSensorTypesFromServer() on the caller's task - the caller is OnChangeLocalIpUseCase, on
// the control loop, and an unreachable server would cost the loop three 30 s GETs. The fetch is a
// request in the outbox and the answer arrives in onOutboxCompletion(), on the control loop.
//
// To see these reds: delete the `if (_outbox != nullptr)` branch from
// forceSync() in SyncSensorTypesUseCase, so it goes back to calling the API itself.

static SyncSensorTypesUseCase* makeOutboxWiredSync(MockOutbox& outbox) {
    SyncSensorTypesUseCase* useCase = new SyncSensorTypesUseCase(
        &g_mockApiService,
        &g_mockRepository,
        &g_mockArduinoAdapter,
        &g_mockNtpTimeAdapter,
        &g_jsonConverter,
        &g_mockLogger,
        &g_mockDebugSystem);
    useCase->setOutbox(&outbox);
    useCase->init();
    return useCase;
}

static void completeSensorTypes(SyncSensorTypesUseCase* useCase, uint32_t ticket, bool delivered) {
    OutboxCompletion completion;
    completion.kind = OutboxKind::SENSOR_TYPES_FETCH;
    completion.ticket = ticket;
    completion.delivered = delivered;
    completion.attempts = delivered ? 1 : 4;
    useCase->onOutboxCompletion(completion);
}

void test_sync_sensor_types_force_sync_goes_through_the_outbox(void) {
    sync_sensor_types_use_case_setup();

    MockOutbox outbox;
    SyncSensorTypesUseCase* useCase = makeOutboxWiredSync(outbox);

    useCase->forceSync();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, outbox.sensorTypesRequests,
                                     "the catalog was not requested through the outbox");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockApiService.getSensorTypesListCallCount,
                                  "forceSync() went to the network on the caller's task");

    delete useCase;
    sync_sensor_types_use_case_teardown();
}

// forceSync() is reached from OnChangeLocalIpUseCase::perform(), which runs every iteration while
// an address is unreported. One fetch may travel at a time.
void test_sync_sensor_types_one_fetch_travels_at_a_time(void) {
    sync_sensor_types_use_case_setup();

    MockOutbox outbox;
    SyncSensorTypesUseCase* useCase = makeOutboxWiredSync(outbox);

    for (int iteration = 0; iteration < 200; iteration++) {
        useCase->forceSync();
    }

    char message[160];
    snprintf(message, sizeof(message),
             "200 force requests produced %u catalog fetches; one may be travelling at a time",
             (unsigned)outbox.sensorTypesRequests);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, outbox.sensorTypesRequests, message);

    // Once the one in flight is answered, the next request is allowed again.
    completeSensorTypes(useCase, 1, false);
    useCase->forceSync();
    TEST_ASSERT_EQUAL_UINT32(2, outbox.sensorTypesRequests);

    delete useCase;
    sync_sensor_types_use_case_teardown();
}

// And an address change that lands while a fetch is travelling is held, not lost: the ask stays
// in the force flag and the next loop() hands it over. What it asks about is the address it was
// made under, which the answer already on its way says nothing about.
void test_sync_sensor_types_a_force_asked_mid_flight_is_handed_over_later(void) {
    sync_sensor_types_use_case_setup();

    MockOutbox outbox;
    g_mockArduinoAdapter.setMillis(0);
    SyncSensorTypesUseCase* useCase = makeOutboxWiredSync(outbox);
    g_mockRepository.setShouldReturnEmpty(true);

    // The first address change, and the fetch it sends out.
    useCase->forceSync();
    TEST_ASSERT_EQUAL_UINT32(1, outbox.sensorTypesRequests);

    // The second, while that fetch is still out: one travels at a time, so nothing goes yet.
    useCase->forceSync();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, outbox.sensorTypesRequests,
                                     "a second catalog fetch travelled while the first was out");

    // The first answer lands, and it is a synchronization that succeeded - so nothing that
    // follows can be a re-arm.
    outbox.sensorTypesResponse.push_back(createTestSensorType("uid-1", "sensor_1"));
    outbox.sensorTypesResponseReady = true;
    completeSensorTypes(useCase, 1, true);
    TEST_ASSERT_TRUE_MESSAGE(useCase->getLastSyncStatus(),
                             "the premise failed: the fetch this test lets through did not succeed");
    TEST_ASSERT_FALSE(useCase->isRearmPending());

    // A minute later: far inside the safety-net interval, and this use case has no network status
    // provider, so the held ask is the only thing left that can send a fetch.
    g_mockArduinoAdapter.setMillis(60000);
    useCase->loop();

    char message[192];
    snprintf(message, sizeof(message),
             "the module made %u catalog fetches for two address changes; the one asked for while "
             "a fetch was travelling was dropped where it stood",
             (unsigned)outbox.sensorTypesRequests);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, outbox.sensorTypesRequests, message);

    delete useCase;
    sync_sensor_types_use_case_teardown();
}

// A fetch that never reached the server says nothing about the catalog. Reporting it as an
// empty one would let a network outage be recorded as a successful sync against no types - and the
// module has to keep working from the types it already has.
void test_sync_sensor_types_a_failed_fetch_is_not_an_empty_catalog(void) {
    sync_sensor_types_use_case_setup();

    MockOutbox outbox;
    SyncSensorTypesUseCase* useCase = makeOutboxWiredSync(outbox);

    g_mockRepository.addSensorType(createTestSensorType("kept-uid-1", "kept_sensor_1"));
    g_mockRepository.addSensorType(createTestSensorType("kept-uid-2", "kept_sensor_2"));
    g_mockRepository.reset();
    g_mockRepository.addSensorType(createTestSensorType("kept-uid-1", "kept_sensor_1"));
    g_mockRepository.addSensorType(createTestSensorType("kept-uid-2", "kept_sensor_2"));

    useCase->forceSync();
    completeSensorTypes(useCase, 1, false);

    char message[176];
    snprintf(message, sizeof(message),
             "after a failed fetch the module held %d sensor types and had written %d times; a "
             "failed fetch must change neither",
             (int)g_mockRepository.getAll().size(), g_mockRepository.saveCallCount);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)g_mockRepository.getAll().size(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockRepository.saveCallCount, message);
    TEST_ASSERT_FALSE_MESSAGE(useCase->getLastSyncStatus(),
                              "a fetch that never reached the server was recorded as a successful "
                              "synchronization");

    delete useCase;
    sync_sensor_types_use_case_teardown();
}

// The other half of the same distinction: a server that answered "no sensor models" is a real
// answer. The module keeps its local catalog and counts the sync as done.
void test_sync_sensor_types_an_empty_answer_is_a_successful_sync(void) {
    sync_sensor_types_use_case_setup();

    MockOutbox outbox;
    SyncSensorTypesUseCase* useCase = makeOutboxWiredSync(outbox);

    g_mockRepository.addSensorType(createTestSensorType("kept-uid-1", "kept_sensor_1"));

    useCase->forceSync();
    outbox.sensorTypesResponse.clear();
    outbox.sensorTypesResponseReady = true;
    completeSensorTypes(useCase, 1, true);

    TEST_ASSERT_TRUE_MESSAGE(useCase->getLastSyncStatus(),
                             "an empty catalog from a server that answered was treated as a failure");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_mockRepository.getAll().size(),
                                  "an empty answer removed the types the module already had");
    TEST_ASSERT_EQUAL_INT(0, g_mockRepository.saveCallCount);

    delete useCase;
    sync_sensor_types_use_case_teardown();
}

// And a delivered catalog is applied - the same code path the synchronous version used, reached
// from the completion instead of from the return of a GET.
void test_sync_sensor_types_a_delivered_catalog_is_applied(void) {
    sync_sensor_types_use_case_setup();

    MockOutbox outbox;
    SyncSensorTypesUseCase* useCase = makeOutboxWiredSync(outbox);

    g_mockRepository.setShouldReturnEmpty(true);

    useCase->forceSync();
    outbox.sensorTypesResponse.push_back(createTestSensorType("uid-1", "sensor_1"));
    outbox.sensorTypesResponse.push_back(createTestSensorType("uid-2", "sensor_2"));
    outbox.sensorTypesResponseReady = true;
    completeSensorTypes(useCase, 1, true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, g_mockRepository.saveCallCount,
                                  "a delivered catalog was not written to storage");
    TEST_ASSERT_TRUE(useCase->getLastSyncStatus());

    delete useCase;
    sync_sensor_types_use_case_teardown();
}

// Somebody else's completion is not our answer.
void test_sync_sensor_types_another_tickets_completion_is_ignored(void) {
    sync_sensor_types_use_case_setup();

    MockOutbox outbox;
    SyncSensorTypesUseCase* useCase = makeOutboxWiredSync(outbox);

    g_mockRepository.setShouldReturnEmpty(true);

    useCase->forceSync();   // ticket 1
    outbox.sensorTypesResponse.push_back(createTestSensorType("uid-1", "sensor_1"));
    outbox.sensorTypesResponseReady = true;
    completeSensorTypes(useCase, 9, true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockRepository.saveCallCount,
                                  "a completion belonging to another request was applied");

    completeSensorTypes(useCase, 1, true);
    TEST_ASSERT_EQUAL_INT(1, g_mockRepository.saveCallCount);

    delete useCase;
    sync_sensor_types_use_case_teardown();
}

// syncNow() retries on httpFailed and on nothing else, so an account with no sensor models costs
// one request. Before the empty list became an outcome of its own, sensorTypesFromJson() answered
// false to `[]`, RestAPIService turned that into httpFailed, and this loop spent all three of its
// attempts on a server that had answered correctly the first time. The transport-level red is in
// test_rest_api_service.cpp; this is the retry count that came with it.
void test_sync_sensor_types_an_empty_catalog_costs_one_request_not_three(void) {
    sync_sensor_types_use_case_setup();

    g_syncUseCase->init();
    g_mockApiService.sensorTypesListReturnValue.clear();
    g_mockApiService.getSensorTypesListHttpFailed = false; // the server answered, with nothing
    g_mockRepository.addSensorType(createTestSensorType("kept-uid", "kept_sensor"));

    const bool result = g_syncUseCase->syncNow();

    char message[192];
    snprintf(message, sizeof(message),
             "an empty catalog from a server that answered cost %d request(s) and returned %d; it "
             "must cost one and count as a successful synchronization",
             g_mockApiService.getSensorTypesListCallCount, result ? 1 : 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockApiService.getSensorTypesListCallCount, message);
    TEST_ASSERT_TRUE_MESSAGE(result, message);
    TEST_ASSERT_TRUE_MESSAGE(g_syncUseCase->getLastSyncStatus(), message);

    // And handleEmptyServerResponse() did its unchanged job: local types kept, nothing written.
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_mockRepository.getAll().size(),
                                  "an empty answer removed the types the module already had");
    TEST_ASSERT_EQUAL_INT(0, g_mockRepository.saveCallCount);

    sync_sensor_types_use_case_teardown();
}

// --- re-arming a synchronization that did not succeed -------------------------------------------
//
// A catalog fetch is asked for at setup and when the local IP changes. Neither comes back, so
// what these cover is the module that failed its one fetch - the server unreachable while the
// network was still coming up - and what makes it ask again without a reboot.
//
// To see these reds: in SyncSensorTypesUseCase::loop(), replace the re-arm
// condition with the plain interval it stands in for
//
//     bool shouldSync = _forceSyncRequested || ((now - _lastSyncTime) >= 300000);
//
// and drop the `if (rearm)` block. The failed fetch is then never asked again on the edge, and
// what the interval produces is a poll of every module rather than a re-arm of the ones that
// failed - which is the other half of what these say.

// The module's own offline decision clears, and the fetch the outage cost it is asked again -
// well before the safety-net interval, and without a reboot.
void test_sync_sensor_types_connectivity_restored_rearms_a_failed_fetch(void) {
    sync_sensor_types_use_case_setup();

    MockNetworkStatusProvider networkStatus;
    g_mockArduinoAdapter.setMillis(0);
    g_syncUseCase->setNetworkStatusProvider(&networkStatus);
    g_syncUseCase->init();

    // The fetch setup makes, against a server the module cannot reach yet.
    g_mockApiService.getSensorTypesListHttpFailed = true;
    TEST_ASSERT_FALSE_MESSAGE(g_syncUseCase->syncNow(),
                              "the premise failed: the fetch this test re-arms did not fail");
    TEST_ASSERT_TRUE(g_syncUseCase->isRearmPending());
    g_mockApiService.getSensorTypesListCallCount = 0;

    // Ten minutes of control loop with the module knowing it is offline.
    networkStatus.offline = true;
    for (unsigned long second = 1; second <= 600; second++) {
        g_mockArduinoAdapter.setMillis(second * 1000UL);
        g_syncUseCase->loop();
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockApiService.getSensorTypesListCallCount,
                                  "the module spent an outage calling a server it knows is down");

    // The link comes back.
    g_mockApiService.getSensorTypesListHttpFailed = false;
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid-1", "test_sensor_1"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    g_mockRepository.setShouldReturnEmpty(true);

    networkStatus.offline = false;
    g_mockArduinoAdapter.setMillis(601000);
    g_syncUseCase->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockApiService.getSensorTypesListCallCount,
                                  "the failed fetch was not asked again when connectivity came "
                                  "back");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockRepository.saveCallCount,
                                  "the catalog the re-armed fetch brought back was not stored");
    TEST_ASSERT_TRUE(g_syncUseCase->getLastSyncStatus());
    TEST_ASSERT_FALSE_MESSAGE(g_syncUseCase->isRearmPending(),
                              "a synchronization that succeeded left the module still asking");

    sync_sensor_types_use_case_teardown();
}

// The window a module actually boots into: the network is still coming up, the boot fetch fails,
// and the link is up seconds later - inside the first cooldown, which nothing has spent yet. The
// cooldown is time since the last re-arm, so a module that has never re-armed must not be charged
// for one, and the constructor is where that is settled.
//
// To see the red: in the constructor, drop
//
//     _lastRearmTime = _arduinoAdapter->millis() - REARM_COOLDOWN_MS;
//
// leaving _lastRearmTime at 0. The module then owes a full five minutes from boot before it may
// ask again, and the edge this test rides is swallowed. The other re-arm tests do not see it:
// they let ten minutes pass before the link comes back.
void test_sync_sensor_types_the_first_rearm_is_not_charged_a_cooldown(void) {
    sync_sensor_types_use_case_setup();

    MockNetworkStatusProvider networkStatus;
    g_mockArduinoAdapter.setMillis(0);
    g_syncUseCase->setNetworkStatusProvider(&networkStatus);
    g_syncUseCase->init();

    // The fetch setup makes, against a server the module cannot reach yet.
    g_mockApiService.getSensorTypesListHttpFailed = true;
    TEST_ASSERT_FALSE_MESSAGE(g_syncUseCase->syncNow(),
                              "the premise failed: the boot fetch this test re-arms did not fail");
    TEST_ASSERT_TRUE(g_syncUseCase->isRearmPending());
    g_mockApiService.getSensorTypesListCallCount = 0;

    // Forty seconds of control loop with the module knowing it is offline - a fraction of the
    // five-minute cooldown, and the whole point of the test.
    networkStatus.offline = true;
    for (unsigned long second = 1; second <= 40; second++) {
        g_mockArduinoAdapter.setMillis(second * 1000UL);
        g_syncUseCase->loop();
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockApiService.getSensorTypesListCallCount,
                                  "the module spent the outage calling a server it knows is down");

    // The link comes up on the forty-first second, as it does on a module whose WiFi and DHCP
    // finish after the first catalog fetch has already been tried.
    g_mockApiService.getSensorTypesListHttpFailed = false;
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("test-uid-1", "test_sensor_1"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    g_mockRepository.setShouldReturnEmpty(true);

    networkStatus.offline = false;
    g_mockArduinoAdapter.setMillis(41000);
    g_syncUseCase->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockApiService.getSensorTypesListCallCount,
                                  "41 s after boot the link came back and the module asked for "
                                  "nothing: the first re-arm was charged a cooldown no re-arm had "
                                  "spent");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockRepository.saveCallCount,
                                  "the catalog the re-armed fetch brought back was not stored");
    TEST_ASSERT_TRUE(g_syncUseCase->getLastSyncStatus());
    TEST_ASSERT_FALSE_MESSAGE(g_syncUseCase->isRearmPending(),
                              "a synchronization that succeeded left the module still asking");

    sync_sensor_types_use_case_teardown();
}

// A link that flaps produces an offline->online edge per flap. The cooldown is what stands
// between that and a request per flap for as long as the server is down.
void test_sync_sensor_types_a_flapping_link_does_not_storm_the_server(void) {
    sync_sensor_types_use_case_setup();

    MockOutbox outbox;
    MockNetworkStatusProvider networkStatus;
    g_mockArduinoAdapter.setMillis(0);
    SyncSensorTypesUseCase* useCase = makeOutboxWiredSync(outbox);
    useCase->setNetworkStatusProvider(&networkStatus);

    // The fetch that failed, and the state it leaves behind.
    useCase->forceSync();
    completeSensorTypes(useCase, 1, false);
    TEST_ASSERT_EQUAL_UINT32(1, outbox.sensorTypesRequests);
    TEST_ASSERT_TRUE(useCase->isRearmPending());

    // An hour of control loop over a link that is up for five seconds and down for five, with the
    // server down throughout: every fetch the re-arm hands over comes back undelivered.
    unsigned long edges = 0;
    bool wasOffline = false;
    uint32_t answered = 1;
    for (unsigned long second = 1; second <= 3600; second++) {
        const bool offline = ((second / 5) % 2) == 0;
        edges += (wasOffline && !offline) ? 1 : 0;
        wasOffline = offline;

        networkStatus.offline = offline;
        g_mockArduinoAdapter.setMillis(second * 1000UL);
        useCase->loop();

        while (outbox.sensorTypesRequests > answered) {
            answered++;
            completeSensorTypes(useCase, answered, false);
        }
    }

    // One hour, a five-minute cooldown: twelve re-arms are all an hour has room for, plus the one
    // fetch that failed before it started.
    const uint32_t allowed = 1 + 3600 / 300;
    char message[224];
    snprintf(message, sizeof(message),
             "%lu offline->online edges in one hour produced %u catalog fetches; the cooldown "
             "allows %u",
             edges, (unsigned)outbox.sensorTypesRequests, (unsigned)allowed);
    TEST_ASSERT_TRUE_MESSAGE(edges >= 300, "the premise failed: the link did not flap");
    TEST_ASSERT_TRUE_MESSAGE(outbox.sensorTypesRequests <= allowed, message);
    TEST_ASSERT_TRUE_MESSAGE(outbox.sensorTypesRequests > 1,
                             "the cooldown swallowed the re-arm entirely - an hour of a link "
                             "coming back produced no fetch at all");

    delete useCase;
    sync_sensor_types_use_case_teardown();
}

// --- what counts as a synchronization -----------------------------------------------------------

// The ordinary state of a module in the field: the server publishes the catalog the module
// already holds. An exchange that found nothing to do completed - it is not a failure, and the
// time of it is when the module last knew its catalog was the server's.
//
// To see the red: in processServerSensorTypes(), take `syncSuccess` back to
// `(syncedCount > 0)`.
void test_sync_sensor_types_a_sync_with_nothing_to_do_is_a_success(void) {
    sync_sensor_types_use_case_setup();

    g_syncUseCase->init();

    const std::string updatedAt = "2026-02-11T09:01:35.44352+00:00";
    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("steady-uid", "steady_sensor", updatedAt));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    g_mockRepository.addSensorType(createTestSensorType("steady-uid", "steady_sensor", updatedAt));
    g_mockRepository.saveCallCount = 0;

    const time_t syncedAt = 1771000000;
    g_mockNtpTimeAdapter.setEpochTime(syncedAt);

    const bool result = g_syncUseCase->syncNow();

    char message[208];
    snprintf(message, sizeof(message),
             "a synchronization against a catalog the module already had returned %d and left "
             "getLastSyncTime() at %ld; the server answered and nothing needed changing",
             result ? 1 : 0, (long)g_syncUseCase->getLastSyncTime());
    TEST_ASSERT_TRUE_MESSAGE(result, message);
    TEST_ASSERT_TRUE_MESSAGE(g_syncUseCase->getLastSyncStatus(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)syncedAt, (int)g_syncUseCase->getLastSyncTime(), message);

    // And nothing was written for it, which is the whole reason the count was zero.
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockRepository.saveCallCount,
                                  "a catalog with nothing to change was written to flash anyway");
    TEST_ASSERT_FALSE_MESSAGE(g_syncUseCase->isRearmPending(),
                              "a completed synchronization left the module asking again, so every "
                              "module in the fleet would re-arm forever");

    sync_sensor_types_use_case_teardown();
}

// The other side of the same distinction: a catalog that was received and then refused by storage
// leaves the module holding something that is not the server's, and that is a failed sync.
void test_sync_sensor_types_a_refused_write_is_still_a_failure(void) {
    sync_sensor_types_use_case_setup();

    g_syncUseCase->init();

    std::vector<SensorType> serverTypes;
    serverTypes.push_back(createTestSensorType("new-uid", "new_sensor"));
    g_mockApiService.sensorTypesListReturnValue = serverTypes;
    g_mockRepository.setSaveAllShouldFail(true);

    const bool result = g_syncUseCase->syncNow();

    TEST_ASSERT_FALSE_MESSAGE(result, "a catalog storage refused was reported as synchronized");
    TEST_ASSERT_FALSE_MESSAGE(g_syncUseCase->getLastSyncStatus(),
                              "a catalog storage refused was reported as synchronized");
    TEST_ASSERT_TRUE_MESSAGE(g_syncUseCase->isRearmPending(),
                             "a synchronization that left the module with the wrong catalog was "
                             "not re-armed");

    sync_sensor_types_use_case_teardown();
}

// Main test runner function
extern "C" void run_sync_sensor_types_use_case_tests(void) {
    RUN_TEST(test_sync_sensor_types_init_success);
    RUN_TEST(test_sync_sensor_types_loop_does_not_poll_a_healthy_catalog);
    RUN_TEST(test_sync_sensor_types_loop_rearms_a_failed_fetch_after_the_safety_interval);
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
    RUN_TEST(test_sync_sensor_types_force_sync_goes_through_the_outbox);
    RUN_TEST(test_sync_sensor_types_one_fetch_travels_at_a_time);
    RUN_TEST(test_sync_sensor_types_a_force_asked_mid_flight_is_handed_over_later);
    RUN_TEST(test_sync_sensor_types_a_failed_fetch_is_not_an_empty_catalog);
    RUN_TEST(test_sync_sensor_types_an_empty_answer_is_a_successful_sync);
    RUN_TEST(test_sync_sensor_types_a_delivered_catalog_is_applied);
    RUN_TEST(test_sync_sensor_types_another_tickets_completion_is_ignored);
    RUN_TEST(test_sync_sensor_types_an_empty_catalog_costs_one_request_not_three);
    RUN_TEST(test_sync_sensor_types_connectivity_restored_rearms_a_failed_fetch);
    RUN_TEST(test_sync_sensor_types_the_first_rearm_is_not_charged_a_cooldown);
    RUN_TEST(test_sync_sensor_types_a_flapping_link_does_not_storm_the_server);
    RUN_TEST(test_sync_sensor_types_a_sync_with_nothing_to_do_is_a_success);
    RUN_TEST(test_sync_sensor_types_a_refused_write_is_still_a_failure);
}
