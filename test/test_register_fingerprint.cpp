// SensorEntity::registerFingerprint(): the registers a sensor is read by, as one number.
//
// A reading carries the number of the registers it was polled by, and the control loop drops one
// whose number is not the sensor's (SensorService::applyReading()); the catalog synchronization
// drops a sensor's readings when it stores the sensor again under another number
// (SyncSensorTypesUseCase). So the number has to move with everything that changes what a register
// reads - where a register is read, how its value is scaled, the channel it is kept under - and with
// nothing else: a renamed channel, a changed discovery range or another address would otherwise take
// a sensor's readings away for nothing. A channel's type is in the second group for the number: it
// decides the computed channels instead, which the synchronization compares on their own.

#include <unity.h>

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"

#include <cstdio>
#include <functional>
#include <string>

namespace {

ModbusRegisterConfig registerAt(uint8_t channel, uint16_t startRegister, const char* type)
{
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = startRegister;
    config.numRegisters = 1;
    config.multiplier = 0.01f;
    config.offset = 0.0f;
    config.channel = channel;
    config.channelName = type;
    config.sensorType = type;
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    return config;
}

I2CRegisterConfig i2cRegisterAt(uint8_t channel, uint16_t registerAddr, const char* type)
{
    I2CRegisterConfig config;
    config.registerAddr = registerAddr;
    config.length = 2;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channel = channel;
    config.channelName = type;
    config.sensorType = type;
    return config;
}

SensorEntity sensorWithRegisters()
{
    SensorEntity sensor;
    sensor.uid = "fp-module.01";
    sensor.pin = 1;
    sensor.model = "ry_ws301";
    sensor.protocol = "ModBus";
    sensor.modbusDeviceAddress = 0x71;
    sensor.modbusRegisterConfigs.push_back(registerAt(1, 0, "air_temp"));
    sensor.modbusRegisterConfigs.push_back(registerAt(2, 1, "humidity"));
    sensor.i2cRegisterConfigs.push_back(i2cRegisterAt(1, 0x20, "lux"));
    return sensor;
}

void assertMoves(const char* what, const std::function<void(SensorEntity&)>& change)
{
    const SensorEntity before = sensorWithRegisters();
    SensorEntity after = sensorWithRegisters();
    change(after);
    char message[160];
    snprintf(message, sizeof(message), "%s did not change the register fingerprint", what);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(before.registerFingerprint(), after.registerFingerprint(), message);
}

void assertStays(const char* what, const std::function<void(SensorEntity&)>& change)
{
    const SensorEntity before = sensorWithRegisters();
    SensorEntity after = sensorWithRegisters();
    change(after);
    char message[160];
    snprintf(message, sizeof(message), "%s changed the register fingerprint", what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.registerFingerprint(), after.registerFingerprint(), message);
}

} // namespace

static void test_the_register_fingerprint_is_the_same_for_the_same_registers(void)
{
    TEST_ASSERT_EQUAL_UINT32(sensorWithRegisters().registerFingerprint(),
                             sensorWithRegisters().registerFingerprint());
    SensorEntity none;
    TEST_ASSERT_EQUAL_UINT32(SensorEntity().registerFingerprint(), none.registerFingerprint());
}

static void test_the_register_fingerprint_moves_with_what_a_reading_depends_on(void)
{
    assertMoves("the function code", [](SensorEntity& s) { s.modbusRegisterConfigs[1].functionCode = 4; });
    assertMoves("the first register", [](SensorEntity& s) { s.modbusRegisterConfigs[1].startRegister = 10; });
    assertMoves("the register count", [](SensorEntity& s) { s.modbusRegisterConfigs[1].numRegisters = 2; });
    assertMoves("the data type", [](SensorEntity& s) { s.modbusRegisterConfigs[1].dataType = ModbusDataType::S16; });
    assertMoves("the multiplier", [](SensorEntity& s) { s.modbusRegisterConfigs[1].multiplier = 0.1f; });
    assertMoves("the offset", [](SensorEntity& s) { s.modbusRegisterConfigs[1].offset = -40.0f; });
    assertMoves("the channel number", [](SensorEntity& s) { s.modbusRegisterConfigs[1].channel = 3; });
    assertMoves("a channel taken out", [](SensorEntity& s) { s.modbusRegisterConfigs.pop_back(); });
    assertMoves("a channel added", [](SensorEntity& s) {
        s.modbusRegisterConfigs.push_back(registerAt(3, 2, "co2"));
    });
    assertMoves("the order of the channels", [](SensorEntity& s) {
        std::swap(s.modbusRegisterConfigs[0], s.modbusRegisterConfigs[1]);
    });
    assertMoves("the I2C register", [](SensorEntity& s) { s.i2cRegisterConfigs[0].registerAddr = 0x21; });
    assertMoves("the I2C length", [](SensorEntity& s) { s.i2cRegisterConfigs[0].length = 1; });
    assertMoves("the I2C multiplier", [](SensorEntity& s) { s.i2cRegisterConfigs[0].multiplier = 1.0f; });
    assertMoves("the I2C offset", [](SensorEntity& s) { s.i2cRegisterConfigs[0].offset = 5.0f; });
    assertMoves("the I2C channel", [](SensorEntity& s) { s.i2cRegisterConfigs[0].channel = 2; });
    assertMoves("an I2C channel taken out", [](SensorEntity& s) { s.i2cRegisterConfigs.clear(); });
}

static void test_the_register_fingerprint_stays_with_what_a_register_read_does_not_depend_on(void)
{
    assertStays("a channel's name", [](SensorEntity& s) { s.modbusRegisterConfigs[1].channelName = "Relative Humidity"; });
    assertStays("a channel's type", [](SensorEntity& s) { s.modbusRegisterConfigs[1].sensorType = "humidity_dig"; });
    assertStays("the discovery range", [](SensorEntity& s) {
        s.modbusRegisterConfigs[1].canConnectMinValue = 5.0f;
        s.modbusRegisterConfigs[1].canConnectMaxValue = 95.0f;
    });
    assertStays("an I2C channel's name and type", [](SensorEntity& s) {
        s.i2cRegisterConfigs[0].channelName = "Illuminance";
        s.i2cRegisterConfigs[0].sensorType = "lux_dig";
    });
    assertStays("the baud rate", [](SensorEntity& s) { s.baudRate = 4800; });
    assertStays("the address", [](SensorEntity& s) { s.modbusDeviceAddress = 0x72; });
    assertStays("the model", [](SensorEntity& s) { s.model = "mp-508c"; });
    assertStays("the error flag", [](SensorEntity& s) { s.isError = true; });
}

// Where the Modbus list ends and the I2C list begins is part of the registers. A Modbus channel gives
// the fingerprint 15 bytes of fields and an I2C channel 12, so four Modbus channels and five I2C
// channels whose fields are all zero are the same 60 bytes - and other registers.
static void test_the_register_fingerprint_tells_the_two_lists_apart(void)
{
    ModbusRegisterConfig modbusZero;
    modbusZero.functionCode = 0;
    modbusZero.startRegister = 0;
    modbusZero.numRegisters = 0;
    modbusZero.dataType = ModbusDataType::U16;
    modbusZero.multiplier = 0.0f;
    modbusZero.offset = 0.0f;
    modbusZero.channel = 0;

    I2CRegisterConfig i2cZero;
    i2cZero.registerAddr = 0;
    i2cZero.length = 0;
    i2cZero.multiplier = 0.0f;
    i2cZero.offset = 0.0f;
    i2cZero.channel = 0;

    SensorEntity modbus;
    modbus.modbusRegisterConfigs.assign(4, modbusZero);
    SensorEntity i2c;
    i2c.i2cRegisterConfigs.assign(5, i2cZero);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(modbus.registerFingerprint(), i2c.registerFingerprint(),
                                  "four Modbus channels took the fingerprint of five I2C channels");
}

extern "C" void run_register_fingerprint_tests(void)
{
    RUN_TEST(test_the_register_fingerprint_is_the_same_for_the_same_registers);
    RUN_TEST(test_the_register_fingerprint_moves_with_what_a_reading_depends_on);
    RUN_TEST(test_the_register_fingerprint_stays_with_what_a_register_read_does_not_depend_on);
    RUN_TEST(test_the_register_fingerprint_tells_the_two_lists_apart);
}
