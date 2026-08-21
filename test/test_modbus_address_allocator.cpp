#include <unity.h>
#include "Mock/ArduinoMock.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "constants.h"

/**
 * @brief Build one ModBus entity for the repository of the allocator tests
 * @param uid Uid of the entity
 * @param pin Port the entity sits on
 * @param address ModBus address the entity holds
 * @param status Status of the entity; an entity in ERROR still owns its address
 */
static SensorEntity makeModbusEntity(const char *uid, int pin, int address, SensorStatusEnum status)
{
    SensorEntity entity;
    entity.uid = uid;
    entity.pin = pin;
    entity.protocol = "ModBus";
    entity.modbusDeviceAddress = address;
    entity.status = status;
    return entity;
}

static void test_occupancy_counts_only_modbus_entities_inside_the_range(void)
{
    MockSensorRepository repository;
    repository.sensors.push_back(makeModbusEntity("mod.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    repository.sensors.push_back(makeModbusEntity("mod.01", 1, MODBUS_ADDRESS_MAX + 1, SENSOR_STATUS_ADDED));

    SensorEntity i2cEntity;
    i2cEntity.uid = "i2c.02";
    i2cEntity.pin = 2;
    i2cEntity.protocol = "I2C";
    i2cEntity.modbusDeviceAddress = MODBUS_ADDRESS_MIN + 2;
    repository.sensors.push_back(i2cEntity);

    ModbusAddressAllocator allocator(&repository);
    auto occupied = allocator.occupiedAddresses("");

    TEST_ASSERT_EQUAL(1, static_cast<int>(occupied.size()));
    TEST_ASSERT_TRUE(allocator.isAddressTaken(occupied, MODBUS_ADDRESS_MIN));
    TEST_ASSERT_FALSE(allocator.isAddressTaken(occupied, MODBUS_ADDRESS_MIN + 2));
    TEST_ASSERT_EQUAL_STRING("mod.00", allocator.addressHolder(occupied, MODBUS_ADDRESS_MIN).c_str());
}

static void test_occupancy_keeps_an_entity_in_error_state(void)
{
    MockSensorRepository repository;
    repository.sensors.push_back(makeModbusEntity("mod.02", 2, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_ERROR));

    ModbusAddressAllocator allocator(&repository);
    auto occupied = allocator.occupiedAddresses("");

    TEST_ASSERT_TRUE(allocator.isAddressTaken(occupied, MODBUS_ADDRESS_MIN + 2));
    TEST_ASSERT_EQUAL_STRING("mod.02", allocator.addressHolder(occupied, MODBUS_ADDRESS_MIN + 2).c_str());
}

static void test_occupancy_excludes_the_named_uid(void)
{
    MockSensorRepository repository;
    repository.sensors.push_back(makeModbusEntity("mod.03", 3, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_ADDED));

    ModbusAddressAllocator allocator(&repository);

    TEST_ASSERT_TRUE(allocator.isAddressTaken(allocator.occupiedAddresses(""), MODBUS_ADDRESS_MIN + 2));
    TEST_ASSERT_FALSE(allocator.isAddressTaken(allocator.occupiedAddresses("mod.03"), MODBUS_ADDRESS_MIN + 2));
}

static void test_next_free_address_honours_preferred_then_ascends(void)
{
    MockSensorRepository repository;
    repository.sensors.push_back(makeModbusEntity("mod.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    repository.sensors.push_back(makeModbusEntity("mod.02", 2, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_ADDED));

    ModbusAddressAllocator allocator(&repository);
    auto occupied = allocator.occupiedAddresses("");

    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 3, allocator.nextFreeAddress(occupied, MODBUS_ADDRESS_MIN + 3));
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 1, allocator.nextFreeAddress(occupied, MODBUS_ADDRESS_MIN + 2));
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN + 1, allocator.nextFreeAddress(occupied, 0x01));
}

static void test_next_free_address_returns_minus_one_when_the_range_is_full(void)
{
    MockSensorRepository repository;
    for (int pin = 0; pin < 4; pin++)
    {
        repository.sensors.push_back(makeModbusEntity(("mod.0" + std::to_string(pin)).c_str(), pin,
                                                      MODBUS_ADDRESS_MIN + pin, SENSOR_STATUS_ADDED));
    }

    ModbusAddressAllocator allocator(&repository);
    auto occupied = allocator.occupiedAddresses("");

    TEST_ASSERT_EQUAL(-1, allocator.nextFreeAddress(occupied, MODBUS_ADDRESS_MIN + 1));
}

static void test_allocator_without_repository_sees_every_address_free(void)
{
    ModbusAddressAllocator allocator(nullptr);
    auto occupied = allocator.occupiedAddresses("");

    TEST_ASSERT_TRUE(occupied.empty());
    TEST_ASSERT_EQUAL(MODBUS_ADDRESS_MIN, allocator.nextFreeAddress(occupied, MODBUS_ADDRESS_MIN));
    TEST_ASSERT_EQUAL_STRING("", allocator.addressHolder(occupied, MODBUS_ADDRESS_MIN).c_str());
}

extern "C" int run_modbus_address_allocator_tests(void)
{
    RUN_TEST(test_occupancy_counts_only_modbus_entities_inside_the_range);
    RUN_TEST(test_occupancy_keeps_an_entity_in_error_state);
    RUN_TEST(test_occupancy_excludes_the_named_uid);
    RUN_TEST(test_next_free_address_honours_preferred_then_ascends);
    RUN_TEST(test_next_free_address_returns_minus_one_when_the_range_is_full);
    RUN_TEST(test_allocator_without_repository_sees_every_address_free);
    return 0;
}
