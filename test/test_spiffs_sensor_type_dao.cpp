#include <unity.h>
#include <string>
#include <vector>
#include <map>
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"



// Test fixtures
static MockSpiffsAdapter* mockSpiffsAdapter;
static JsonConverter* jsonConverter;
static MockLogger* mockLogger;
static MockDebugSystem* mockDebugSystem;
static SpiffsSensorTypeDao* dao;

// Helper function to create test sensor type
static SensorType createTestSensorType(const std::string& uid, const std::string& name, SensorProtocol protocol) {
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " Display";
    sensorType.protocol = protocol;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.id = 1;
    
    // Add a sub-sensor type for calibration tests
    if (protocol == SensorProtocol::ANALOG_PROTOCOL) {
        SubSensorType subType;
        subType.name = "test_sub";
        subType.displayName = "Test Sub Sensor";
        subType.canCalibrate = true;
        subType.minValue = 0.0f;
        subType.maxValue = 100.0f;
        subType.unit = "test_unit";
        sensorType.sensorTypes.push_back(subType);
    }
    
    return sensorType;
}

void setUp_spiffs_sensor_type_dao(void) {
    mockSpiffsAdapter = new MockSpiffsAdapter();
    jsonConverter = new JsonConverter();
    mockLogger = new MockLogger();
    mockDebugSystem = new MockDebugSystem();
    
    dao = new SpiffsSensorTypeDao(mockSpiffsAdapter, jsonConverter, mockDebugSystem, mockLogger);
}

void tearDown_spiffs_sensor_type_dao(void) {
    delete dao;
    delete mockDebugSystem;
    delete mockLogger;
    delete jsonConverter;
    delete mockSpiffsAdapter;
    
    dao = nullptr;
    mockDebugSystem = nullptr;
    mockLogger = nullptr;
    jsonConverter = nullptr;
    mockSpiffsAdapter = nullptr;
}

// Test save method with new UID
void test_spiffs_sensor_type_dao_save_new(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("save-new-uid", "save_new", SensorProtocol::MODBUS_PROTOCOL);
    
    // Act
    bool result = dao->save(testSensorType, "save-new-uid");
    
    // Assert
    TEST_ASSERT_TRUE(result);
    
    // Verify it was saved by trying to get it
    SensorType retrieved;
    bool getResult = dao->getById("save-new-uid", retrieved);
    TEST_ASSERT_TRUE(getResult);
    TEST_ASSERT_EQUAL_STRING("save-new-uid", retrieved.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("save_new", retrieved.name.c_str());
    
    tearDown_spiffs_sensor_type_dao();
}

// Test save method updating existing UID
void test_spiffs_sensor_type_dao_save_existing(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange - save initial sensor type
    SensorType originalSensorType = createTestSensorType("update-uid", "original_name", SensorProtocol::MODBUS_PROTOCOL);
    dao->save(originalSensorType, "update-uid");
    
    // Create updated sensor type
    SensorType updatedSensorType = createTestSensorType("update-uid", "updated_name", SensorProtocol::I2C_PROTOCOL);
    
    // Act
    bool result = dao->save(updatedSensorType, "update-uid");
    
    // Assert
    TEST_ASSERT_TRUE(result);
    
    // Verify it was updated
    SensorType retrieved;
    bool getResult = dao->getById("update-uid", retrieved);
    TEST_ASSERT_TRUE(getResult);
    TEST_ASSERT_EQUAL_STRING("updated_name", retrieved.name.c_str());
    TEST_ASSERT_EQUAL(SensorProtocol::I2C_PROTOCOL, retrieved.protocol);
    
    tearDown_spiffs_sensor_type_dao();
}

// Test getById method
void test_spiffs_sensor_type_dao_get_by_id(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("get-by-id-uid", "get_by_id", SensorProtocol::ANALOG_PROTOCOL);
    dao->save(testSensorType, "get-by-id-uid");
    
    // Act
    SensorType retrieved;
    bool result = dao->getById("get-by-id-uid", retrieved);
    
    // Assert
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("get-by-id-uid", retrieved.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("get_by_id", retrieved.name.c_str());
    TEST_ASSERT_EQUAL(SensorProtocol::ANALOG_PROTOCOL, retrieved.protocol);
    
    tearDown_spiffs_sensor_type_dao();
}

// Test getById method with non-existent UID
void test_spiffs_sensor_type_dao_get_by_id_not_found(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Act
    SensorType retrieved;
    bool result = dao->getById("non-existent-uid", retrieved);
    
    // Assert
    TEST_ASSERT_FALSE(result);
    
    tearDown_spiffs_sensor_type_dao();
}

// Test remove method
void test_spiffs_sensor_type_dao_remove(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("remove-uid", "remove_test", SensorProtocol::MODBUS_PROTOCOL);
    dao->save(testSensorType, "remove-uid");
    
    // Verify it exists
    SensorType beforeRemove;
    TEST_ASSERT_TRUE(dao->getById("remove-uid", beforeRemove));
    
    // Act
    bool result = dao->remove("remove-uid");
    
    // Assert
    TEST_ASSERT_TRUE(result);
    
    // Verify it was removed
    SensorType afterRemove;
    TEST_ASSERT_FALSE(dao->getById("remove-uid", afterRemove));
    
    tearDown_spiffs_sensor_type_dao();
}

// Test getAll method
void test_spiffs_sensor_type_dao_get_all(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange
    SensorType sensor1 = createTestSensorType("all-1", "sensor1", SensorProtocol::MODBUS_PROTOCOL);
    SensorType sensor2 = createTestSensorType("all-2", "sensor2", SensorProtocol::ANALOG_PROTOCOL);
    SensorType sensor3 = createTestSensorType("all-3", "sensor3", SensorProtocol::I2C_PROTOCOL);
    
    std::vector<SensorType> sensors = {sensor1, sensor2, sensor3};
    dao->saveAll(sensors);
    
    // Act
    std::vector<SensorType> retrieved;
    bool result = dao->getAll(retrieved);
    
    // Assert
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(3, retrieved.size());
    
    tearDown_spiffs_sensor_type_dao();
}

// Test getSensorTypesByProtocol method
void test_spiffs_sensor_type_dao_get_by_protocol(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange
    SensorType modbusSensor1 = createTestSensorType("modbus-1", "modbus1", SensorProtocol::MODBUS_PROTOCOL);
    SensorType modbusSensor2 = createTestSensorType("modbus-2", "modbus2", SensorProtocol::MODBUS_PROTOCOL);
    SensorType analogSensor = createTestSensorType("analog-1", "analog1", SensorProtocol::ANALOG_PROTOCOL);
    
    std::vector<SensorType> sensors = {modbusSensor1, modbusSensor2, analogSensor};
    dao->saveAll(sensors);
    
    // Act
    std::vector<SensorType> modbusTypes;
    std::vector<SensorType> analogTypes;
    bool modbusResult = dao->getSensorTypesByProtocol(SensorProtocol::MODBUS_PROTOCOL, modbusTypes);
    bool analogResult = dao->getSensorTypesByProtocol(SensorProtocol::ANALOG_PROTOCOL, analogTypes);
    
    // Assert
    TEST_ASSERT_TRUE(modbusResult);
    TEST_ASSERT_TRUE(analogResult);
    TEST_ASSERT_EQUAL(2, modbusTypes.size());
    TEST_ASSERT_EQUAL(1, analogTypes.size());
    TEST_ASSERT_EQUAL(SensorProtocol::MODBUS_PROTOCOL, modbusTypes[0].protocol);
    TEST_ASSERT_EQUAL(SensorProtocol::ANALOG_PROTOCOL, analogTypes[0].protocol);
    
    tearDown_spiffs_sensor_type_dao();
}

// Test getCalibrableSensorTypes method
void test_spiffs_sensor_type_dao_get_calibratable(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange
    SensorType calibratableSensor = createTestSensorType("calib-1", "calibratable", SensorProtocol::ANALOG_PROTOCOL);
    SensorType nonCalibratable = createTestSensorType("non-calib-1", "non_calibratable", SensorProtocol::MODBUS_PROTOCOL);
    
    // Debug: Check if the sensor has calibratable types before saving
    TEST_ASSERT_TRUE(calibratableSensor.hasCalibratableSensorTypes());
    TEST_ASSERT_FALSE(nonCalibratable.hasCalibratableSensorTypes());
    
    std::vector<SensorType> sensors = {calibratableSensor, nonCalibratable};
    dao->saveAll(sensors);
    
    // Debug: Retrieve and check if calibratable properties are preserved
    std::vector<SensorType> allSensors;
    dao->getAll(allSensors);
    TEST_ASSERT_EQUAL(2, allSensors.size());
    
    // Find the calibratable sensor and check if it still has calibratable sub-types
    SensorType* retrievedCalibratable = nullptr;
    for (auto& sensor : allSensors) {
        if (sensor.name == "calibratable") {
            retrievedCalibratable = &sensor;
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(retrievedCalibratable);
    TEST_ASSERT_TRUE(retrievedCalibratable->hasCalibratableSensorTypes());
    
    // Act
    std::vector<SensorType> calibratableTypes;
    bool result = dao->getCalibrableSensorTypes(calibratableTypes);
    
    // Assert
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, calibratableTypes.size());
    TEST_ASSERT_EQUAL_STRING("calibratable", calibratableTypes[0].name.c_str());
    
    tearDown_spiffs_sensor_type_dao();
}

// Test getSensorTypeByName method
void test_spiffs_sensor_type_dao_get_by_name(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange
    SensorType testSensorType = createTestSensorType("name-search-uid", "unique_name", SensorProtocol::MODBUS_PROTOCOL);
    dao->save(testSensorType, "name-search-uid");
    
    // Act
    SensorType found;
    SensorType notFound;
    bool foundResult = dao->getSensorTypeByName("unique_name", found);
    bool notFoundResult = dao->getSensorTypeByName("non_existent", notFound);
    
    // Assert
    TEST_ASSERT_TRUE(foundResult);
    TEST_ASSERT_FALSE(notFoundResult);
    TEST_ASSERT_EQUAL_STRING("unique_name", found.name.c_str());
    TEST_ASSERT_EQUAL_STRING("name-search-uid", found.uid.c_str());
    
    tearDown_spiffs_sensor_type_dao();
}

// Test clear method
void test_spiffs_sensor_type_dao_clear(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange - add multiple sensor types
    SensorType sensor1 = createTestSensorType("clear-1", "clear1", SensorProtocol::ANALOG_PROTOCOL);
    SensorType sensor2 = createTestSensorType("clear-2", "clear2", SensorProtocol::I2C_PROTOCOL);
    SensorType sensor3 = createTestSensorType("clear-3", "clear3", SensorProtocol::MODBUS_PROTOCOL);
    
    dao->save(sensor1, "clear-1");
    dao->save(sensor2, "clear-2");
    dao->save(sensor3, "clear-3");
    
    // Verify data exists
    std::vector<SensorType> beforeClear;
    dao->getAll(beforeClear);
    TEST_ASSERT_EQUAL(3, beforeClear.size());
    
    // Act - clear DAO
    bool clearResult = dao->clear();
    
    // Assert - all data should be removed
    TEST_ASSERT_TRUE(clearResult);
    
    std::vector<SensorType> afterClear;
    dao->getAll(afterClear);
    TEST_ASSERT_EQUAL(0, afterClear.size());
    
    // Verify getById returns false
    SensorType retrieved;
    TEST_ASSERT_FALSE(dao->getById("clear-1", retrieved));
    
    tearDown_spiffs_sensor_type_dao();
}

// Test clear with empty storage
void test_spiffs_sensor_type_dao_clear_empty(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Act - clear empty DAO
    bool clearResult = dao->clear();
    
    // Assert - should succeed
    TEST_ASSERT_TRUE(clearResult);
    
    std::vector<SensorType> afterClear;
    dao->getAll(afterClear);
    TEST_ASSERT_EQUAL(0, afterClear.size());
    
    tearDown_spiffs_sensor_type_dao();
}

// Test error handling - SPIFFS failure
void test_spiffs_sensor_type_dao_spiffs_failure(void) {
    setUp_spiffs_sensor_type_dao();
    
    // Arrange
    mockSpiffsAdapter->setShouldFail(true);
    SensorType testSensorType = createTestSensorType("fail-uid", "fail_test", SensorProtocol::MODBUS_PROTOCOL);
    
    // Act & Assert
    std::vector<SensorType> entities;
    TEST_ASSERT_FALSE(dao->getAll(entities));
    
    SensorType retrieved;
    TEST_ASSERT_FALSE(dao->getById("fail-uid", retrieved));
    
    tearDown_spiffs_sensor_type_dao();
}



// Export test runner function
extern "C" int run_spiffs_sensor_type_dao_tests(void) {
    RUN_TEST(test_spiffs_sensor_type_dao_save_new);
    RUN_TEST(test_spiffs_sensor_type_dao_save_existing);
    RUN_TEST(test_spiffs_sensor_type_dao_get_by_id);
    RUN_TEST(test_spiffs_sensor_type_dao_get_by_id_not_found);
    RUN_TEST(test_spiffs_sensor_type_dao_remove);
    RUN_TEST(test_spiffs_sensor_type_dao_get_all);
    RUN_TEST(test_spiffs_sensor_type_dao_get_by_protocol);
    RUN_TEST(test_spiffs_sensor_type_dao_get_calibratable);
    RUN_TEST(test_spiffs_sensor_type_dao_get_by_name);
    RUN_TEST(test_spiffs_sensor_type_dao_clear);
    RUN_TEST(test_spiffs_sensor_type_dao_clear_empty);
    RUN_TEST(test_spiffs_sensor_type_dao_spiffs_failure);
    return 0;
}