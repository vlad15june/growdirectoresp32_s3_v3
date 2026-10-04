#include <unity.h>
#include <string>
#include <vector>
#include "Infrastructure/Repositories/SensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorTypeDao.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"


// Test fixtures
static MockSpiffsSensorTypeDao* mockDao;
static SensorTypeRepository* repository;

// Helper function to create test sensor type
static SensorType createTestSensorType(const std::string& uid, const std::string& name, SensorProtocol protocol) {
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " Display";
    sensorType.protocol = protocol;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.id = 1; // Legacy compatibility
    
    // Add a sub-sensor type
    SubSensorType subType;
    subType.name = "test_sub";
    subType.displayName = "Test Sub Sensor";
    subType.canCalibrate = (protocol == SensorProtocol::ANALOG_PROTOCOL);
    subType.minValue = 0.0f;
    subType.maxValue = 100.0f;
    subType.unit = "test_unit";
    
    sensorType.sensorTypes.push_back(subType);
    
    return sensorType;
}

void setUp_sensor_type_repository(void) {
    mockDao = new MockSpiffsSensorTypeDao();
    repository = new SensorTypeRepository(mockDao);
}

void tearDown_sensor_type_repository(void) {
    delete repository;
    delete mockDao;
    repository = nullptr;
    mockDao = nullptr;
}

// Test get method with valid UID
void test_sensor_type_repository_get_valid_uid(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("test-uid-1", "test_sensor", SensorProtocol::MODBUS_PROTOCOL);
    mockDao->addToStorage(testSensorType);
    
    // Act
    auto result = repository->get("test-uid-1");
    
    // Assert
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL_STRING("test-uid-1", result.value().uid.c_str());
    TEST_ASSERT_EQUAL_STRING("test_sensor", result.value().name.c_str());
    TEST_ASSERT_EQUAL(SensorProtocol::MODBUS_PROTOCOL, result.value().protocol);
    
    tearDown_sensor_type_repository();
}

// Test get method with invalid UID
void test_sensor_type_repository_get_invalid_uid(void) {
    setUp_sensor_type_repository();
    
    // Act
    auto result = repository->get("non-existent-uid");
    
    // Assert
    TEST_ASSERT_FALSE(result.has_value());
    
    tearDown_sensor_type_repository();
}

// Test get method with null UID
void test_sensor_type_repository_get_null_uid(void) {
    setUp_sensor_type_repository();
    
    // Act
    auto result = repository->get(nullptr);
    
    // Assert
    TEST_ASSERT_FALSE(result.has_value());
    
    tearDown_sensor_type_repository();
}

// Test save method
void test_sensor_type_repository_save(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("save-test-uid", "save_test", SensorProtocol::I2C_PROTOCOL);
    
    // Act
    bool result = repository->save(testSensorType);
    
    // Assert
    TEST_ASSERT_TRUE(result);
    
    // Verify it was saved
    auto retrieved = repository->get("save-test-uid");
    TEST_ASSERT_TRUE(retrieved.has_value());
    TEST_ASSERT_EQUAL_STRING("save_test", retrieved.value().name.c_str());
    
    tearDown_sensor_type_repository();
}

// Test getAll method
void test_sensor_type_repository_get_all(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType sensor1 = createTestSensorType("uid-1", "sensor1", SensorProtocol::MODBUS_PROTOCOL);
    SensorType sensor2 = createTestSensorType("uid-2", "sensor2", SensorProtocol::ANALOG_PROTOCOL);
    SensorType sensor3 = createTestSensorType("uid-3", "sensor3", SensorProtocol::I2C_PROTOCOL);
    
    mockDao->addToStorage(sensor1);
    mockDao->addToStorage(sensor2);
    mockDao->addToStorage(sensor3);
    
    // Act
    std::vector<SensorType> result = repository->getAll();
    
    // Assert
    TEST_ASSERT_EQUAL(3, result.size());
    
    tearDown_sensor_type_repository();
}

// Test remove method
void test_sensor_type_repository_remove(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("remove-test-uid", "remove_test", SensorProtocol::MODBUS_PROTOCOL);
    mockDao->addToStorage(testSensorType);
    
    // Verify it exists first
    auto beforeRemove = repository->get("remove-test-uid");
    TEST_ASSERT_TRUE(beforeRemove.has_value());
    
    // Act
    bool result = repository->remove("remove-test-uid");
    
    // Assert
    TEST_ASSERT_TRUE(result);
    
    // Verify it was removed
    auto afterRemove = repository->get("remove-test-uid");
    TEST_ASSERT_FALSE(afterRemove.has_value());
    
    tearDown_sensor_type_repository();
}

// Test exists method
void test_sensor_type_repository_exists(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("exists-test-uid", "exists_test", SensorProtocol::MODBUS_PROTOCOL);
    mockDao->addToStorage(testSensorType);
    
    // Act & Assert
    TEST_ASSERT_TRUE(repository->exists("exists-test-uid"));
    TEST_ASSERT_FALSE(repository->exists("non-existent-uid"));
    TEST_ASSERT_FALSE(repository->exists(nullptr));
    
    tearDown_sensor_type_repository();
}

// Test getSensorTypesByProtocol method
void test_sensor_type_repository_get_by_protocol(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType modbusSensor1 = createTestSensorType("modbus-1", "modbus1", SensorProtocol::MODBUS_PROTOCOL);
    SensorType modbusSensor2 = createTestSensorType("modbus-2", "modbus2", SensorProtocol::MODBUS_PROTOCOL);
    SensorType analogSensor = createTestSensorType("analog-1", "analog1", SensorProtocol::ANALOG_PROTOCOL);
    
    mockDao->addToStorage(modbusSensor1);
    mockDao->addToStorage(modbusSensor2);
    mockDao->addToStorage(analogSensor);
    
    // Act
    std::vector<SensorType> modbusTypes = repository->getSensorTypesByProtocol(SensorProtocol::MODBUS_PROTOCOL);
    std::vector<SensorType> analogTypes = repository->getSensorTypesByProtocol(SensorProtocol::ANALOG_PROTOCOL);
    
    // Assert
    TEST_ASSERT_EQUAL(2, modbusTypes.size());
    TEST_ASSERT_EQUAL(1, analogTypes.size());
    TEST_ASSERT_EQUAL(SensorProtocol::MODBUS_PROTOCOL, modbusTypes[0].protocol);
    TEST_ASSERT_EQUAL(SensorProtocol::ANALOG_PROTOCOL, analogTypes[0].protocol);
    
    tearDown_sensor_type_repository();
}

// Test getSensorTypeByName method
void test_sensor_type_repository_get_by_name(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("name-test-uid", "unique_name", SensorProtocol::MODBUS_PROTOCOL);
    mockDao->addToStorage(testSensorType);
    
    // Act
    const SensorType* result = repository->getSensorTypeByName("unique_name");
    const SensorType* notFound = repository->getSensorTypeByName("non_existent_name");
    
    // Assert
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("unique_name", result->name.c_str());
    TEST_ASSERT_EQUAL_STRING("name-test-uid", result->uid.c_str());
    TEST_ASSERT_NULL(notFound);
    
    tearDown_sensor_type_repository();
}

// Test getCalibrableSensorTypes method
void test_sensor_type_repository_get_calibratable(void) {
    setUp_sensor_type_repository();
    
    // Arrange - create sensors with calibratable sub-types
    SensorType calibratableSensor = createTestSensorType("calib-uid", "calibratable", SensorProtocol::ANALOG_PROTOCOL);
    SensorType nonCalibratable = createTestSensorType("non-calib-uid", "non_calibratable", SensorProtocol::MODBUS_PROTOCOL);
    
    mockDao->addToStorage(calibratableSensor);
    mockDao->addToStorage(nonCalibratable);
    
    // Act
    std::vector<SensorType> calibratableTypes = repository->getCalibrableSensorTypes();
    
    // Assert
    TEST_ASSERT_EQUAL(1, calibratableTypes.size());
    TEST_ASSERT_EQUAL_STRING("calibratable", calibratableTypes[0].name.c_str());
    
    tearDown_sensor_type_repository();
}

// Test count method
void test_sensor_type_repository_count(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType sensor1 = createTestSensorType("count-1", "count1", SensorProtocol::MODBUS_PROTOCOL);
    SensorType sensor2 = createTestSensorType("count-2", "count2", SensorProtocol::ANALOG_PROTOCOL);
    
    mockDao->addToStorage(sensor1);
    mockDao->addToStorage(sensor2);
    
    // Act
    int count = repository->count();
    
    // Assert
    TEST_ASSERT_EQUAL(2, count);
    
    tearDown_sensor_type_repository();
}

// Test cache invalidation on save
void test_sensor_type_repository_cache_invalidation(void) {
    setUp_sensor_type_repository();
    
    // Arrange
    SensorType sensor1 = createTestSensorType("cache-1", "cache1", SensorProtocol::MODBUS_PROTOCOL);
    mockDao->addToStorage(sensor1);
    
    // Load cache
    int initialCount = repository->count();
    TEST_ASSERT_EQUAL(1, initialCount);
    
    // Add another sensor directly to mock storage (bypassing repository)
    SensorType sensor2 = createTestSensorType("cache-2", "cache2", SensorProtocol::ANALOG_PROTOCOL);
    mockDao->addToStorage(sensor2);
    
    // Cache should still show old count
    int cachedCount = repository->count();
    TEST_ASSERT_EQUAL(1, cachedCount);
    
    // Act - save something to invalidate cache
    SensorType sensor3 = createTestSensorType("cache-3", "cache3", SensorProtocol::I2C_PROTOCOL);
    repository->save(sensor3);
    
    // Assert - cache should be refreshed now
    int newCount = repository->count();
    TEST_ASSERT_EQUAL(3, newCount);
    
    tearDown_sensor_type_repository();
}

// Test clear method
void test_sensor_type_repository_clear(void) {
    setUp_sensor_type_repository();
    
    // Arrange - add multiple sensor types
    SensorType sensor1 = createTestSensorType("clear-1", "clear1", SensorProtocol::ANALOG_PROTOCOL);
    SensorType sensor2 = createTestSensorType("clear-2", "clear2", SensorProtocol::I2C_PROTOCOL);
    SensorType sensor3 = createTestSensorType("clear-3", "clear3", SensorProtocol::MODBUS_PROTOCOL);
    
    repository->save(sensor1);
    repository->save(sensor2);
    repository->save(sensor3);
    
    // Verify data exists
    TEST_ASSERT_EQUAL(3, repository->count());
    TEST_ASSERT_TRUE(repository->exists("clear-1"));
    
    // Act - clear repository
    bool clearResult = repository->clear();
    
    // Assert - all data should be removed
    TEST_ASSERT_TRUE(clearResult);
    TEST_ASSERT_EQUAL(0, repository->count());
    TEST_ASSERT_FALSE(repository->exists("clear-1"));
    TEST_ASSERT_FALSE(repository->exists("clear-2"));
    TEST_ASSERT_FALSE(repository->exists("clear-3"));
    
    // Verify getAll returns empty
    std::vector<SensorType> allTypes = repository->getAll();
    TEST_ASSERT_EQUAL(0, allTypes.size());
    
    tearDown_sensor_type_repository();
}

// Test clear invalidates cache
void test_sensor_type_repository_clear_invalidates_cache(void) {
    setUp_sensor_type_repository();
    
    // Arrange - add sensor types and build cache
    SensorType sensor1 = createTestSensorType("cache-clear-1", "cache1", SensorProtocol::ANALOG_PROTOCOL);
    repository->save(sensor1);
    repository->count(); // Build cache
    
    // Act - clear repository
    repository->clear();
    
    // Add new sensor directly to mock storage
    SensorType sensor2 = createTestSensorType("cache-clear-2", "cache2", SensorProtocol::I2C_PROTOCOL);
    mockDao->addToStorage(sensor2);
    
    // Assert - cache should be invalidated, count should reflect new state
    int count = repository->count();
    TEST_ASSERT_EQUAL(1, count);
    
    tearDown_sensor_type_repository();
}

// Export test runner function
extern "C" int run_sensor_type_repository_tests(void) {
    RUN_TEST(test_sensor_type_repository_get_valid_uid);
    RUN_TEST(test_sensor_type_repository_get_invalid_uid);
    RUN_TEST(test_sensor_type_repository_get_null_uid);
    RUN_TEST(test_sensor_type_repository_save);
    RUN_TEST(test_sensor_type_repository_get_all);
    RUN_TEST(test_sensor_type_repository_remove);
    RUN_TEST(test_sensor_type_repository_exists);
    RUN_TEST(test_sensor_type_repository_get_by_protocol);
    RUN_TEST(test_sensor_type_repository_get_by_name);
    RUN_TEST(test_sensor_type_repository_get_calibratable);
    RUN_TEST(test_sensor_type_repository_count);
    RUN_TEST(test_sensor_type_repository_cache_invalidation);
    RUN_TEST(test_sensor_type_repository_clear);
    RUN_TEST(test_sensor_type_repository_clear_invalidates_cache);
    return 0;
}