// What makes two sensor types the same (SensorTypeContent::sameSensorType()).
//
// The catalog synchronization stores a published type again when it is not the one the module
// holds, and the comparison is field for field - so a field the comparison leaves out is an edit of
// that field that never reaches the module. These tests hold it to every field the catalog is read
// and stored with (the members SensorTypeStreamParser reads, the ones the converter writes for the
// stored catalog): each of them, changed alone, makes another type. Two things do not: the order the
// sub-sensor types are listed in, and a trip through the stored catalog. The order of the channels
// does.

#include <unity.h>

#include "Core/Domain/Data/Entities/Sensor/SensorTypeContent.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"

#include <functional>
#include <string>
#include <vector>

namespace {

SubSensorType subType(const char* name, float minValue, float maxValue)
{
    SubSensorType sub;
    sub.name = name;
    sub.displayName = std::string(name) + " display";
    sub.isAnalog = false;
    sub.canCalibrate = true;
    sub.validCalibrationPeriodDays = 30;
    sub.calibrationTimeInSeconds = 120;
    sub.minValue = minValue;
    sub.maxValue = maxValue;
    sub.limitMinValue = minValue + 1.0f;
    sub.limitMaxValue = maxValue - 1.0f;
    sub.unit = "u";
    sub.step = 0.1f;
    sub.precisionPercent = 2.0f;
    sub.precisionValue = 0.5f;
    sub.createdAt = "2026-04-21T08:05:59.388726Z";
    sub.updatedAt = "2026-05-02T10:00:00.000000Z";
    return sub;
}

ModbusRegisterConfig modbusChannel(uint8_t channel, uint16_t startRegister, const char* type)
{
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = startRegister;
    config.numRegisters = 1;
    config.multiplier = 0.01f;
    config.offset = -40.0f;
    config.channelName = std::string(type) + " name";
    config.channel = channel;
    config.sensorType = type;
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    config.dataType = ModbusDataType::S16;
    return config;
}

I2CRegisterConfig i2cChannel(uint16_t registerAddr)
{
    I2CRegisterConfig config;
    config.registerAddr = registerAddr;
    config.length = 2;
    config.multiplier = 0.1f;
    config.offset = 1.5f;
    return config;
}

/** @brief A type with every field set to something other than its default. */
SensorType everyFieldSet()
{
    SensorType type;
    type.uid = "type-uid-107";
    type.name = "model_107";
    type.displayName = "Model 107";
    type.canScan = false;
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.isSupported = false;
    type.createdAt = "2026-04-21T08:05:59.388726Z";
    type.updatedAt = "2026-07-23T14:04:41.000000Z";
    type.sensorTypes.push_back(subType("air_temp", -40.0f, 80.0f));
    type.sensorTypes.push_back(subType("humidity", 0.0f, 100.0f));
    type.i2cAddress = "0x44";
    type.chipType = "SHT31";
    type.i2cRegisterConfig.push_back(i2cChannel(0x24));
    type.i2cRegisterConfig.push_back(i2cChannel(0x26));
    type.modbusDeviceAddress = 17;
    type.baudRate = 19200;
    type.modbusPingAddress = 258;
    type.registerToStoreModbusDeviceAddress = 2000;
    type.writeRegistersCommand = 16;
    type.applyAddressAfterPowerCycle = true;
    type.modbusRegisterConfig.push_back(modbusChannel(0, 9, "air_temp"));
    type.modbusRegisterConfig.push_back(modbusChannel(1, 10, "humidity"));
    type.analogPinConfig.push_back(AnalogPinConfig());
    type.priorityForScan = 7;
    type.id = 42;
    return type;
}

struct Edit {
    const char* field;
    std::function<void(SensorType&)> apply;
};

std::vector<Edit> everyEdit()
{
    std::vector<Edit> edits;
    // The type's own fields.
    edits.push_back({"uid", [](SensorType& t) { t.uid = "other-uid"; }});
    edits.push_back({"name", [](SensorType& t) { t.name = "other"; }});
    edits.push_back({"displayName", [](SensorType& t) { t.displayName = "Other"; }});
    edits.push_back({"canScan", [](SensorType& t) { t.canScan = !t.canScan; }});
    edits.push_back({"protocol", [](SensorType& t) { t.protocol = SensorProtocol::I2C_PROTOCOL; }});
    edits.push_back({"isSupported", [](SensorType& t) { t.isSupported = !t.isSupported; }});
    edits.push_back({"createdAt", [](SensorType& t) { t.createdAt = "2026-01-01T00:00:00Z"; }});
    edits.push_back({"updatedAt", [](SensorType& t) { t.updatedAt = ""; }});
    edits.push_back({"i2cAddress", [](SensorType& t) { t.i2cAddress = "0x45"; }});
    edits.push_back({"chipType", [](SensorType& t) { t.chipType = "SHT40"; }});
    edits.push_back({"modbusDeviceAddress", [](SensorType& t) { t.modbusDeviceAddress = 18; }});
    edits.push_back({"baudRate", [](SensorType& t) { t.baudRate = 9600; }});
    edits.push_back({"modbusPingAddress", [](SensorType& t) { t.modbusPingAddress = 9; }});
    edits.push_back({"registerToStoreModbusDeviceAddress",
                     [](SensorType& t) { t.registerToStoreModbusDeviceAddress = 255; }});
    edits.push_back({"writeRegistersCommand", [](SensorType& t) { t.writeRegistersCommand = 6; }});
    edits.push_back({"applyAddressAfterPowerCycle", [](SensorType& t) { t.applyAddressAfterPowerCycle = false; }});
    edits.push_back({"priorityForScan", [](SensorType& t) { t.priorityForScan = 1; }});
    edits.push_back({"id", [](SensorType& t) { t.id = 0; }});
    // A sub-sensor type: each of its fields, and one more or one less of them.
    edits.push_back({"sub.name", [](SensorType& t) { t.sensorTypes[1].name = "humidity_dig"; }});
    edits.push_back({"sub.displayName", [](SensorType& t) { t.sensorTypes[1].displayName = "Other"; }});
    edits.push_back({"sub.isAnalog", [](SensorType& t) { t.sensorTypes[1].isAnalog = true; }});
    edits.push_back({"sub.canCalibrate", [](SensorType& t) { t.sensorTypes[1].canCalibrate = false; }});
    edits.push_back({"sub.validCalibrationPeriodDays",
                     [](SensorType& t) { t.sensorTypes[1].validCalibrationPeriodDays = 60; }});
    edits.push_back({"sub.calibrationTimeInSeconds",
                     [](SensorType& t) { t.sensorTypes[1].calibrationTimeInSeconds = 0; }});
    edits.push_back({"sub.minValue", [](SensorType& t) { t.sensorTypes[1].minValue = 5.0f; }});
    edits.push_back({"sub.maxValue", [](SensorType& t) { t.sensorTypes[1].maxValue = 95.0f; }});
    edits.push_back({"sub.limitMinValue", [](SensorType& t) { t.sensorTypes[1].limitMinValue = 30.0f; }});
    edits.push_back({"sub.limitMaxValue", [](SensorType& t) { t.sensorTypes[1].limitMaxValue = 1200.0f; }});
    edits.push_back({"sub.unit", [](SensorType& t) { t.sensorTypes[1].unit = "%"; }});
    edits.push_back({"sub.step", [](SensorType& t) { t.sensorTypes[1].step = 0.01f; }});
    edits.push_back({"sub.precisionPercent", [](SensorType& t) { t.sensorTypes[1].precisionPercent = 3.0f; }});
    edits.push_back({"sub.precisionValue", [](SensorType& t) { t.sensorTypes[1].precisionValue = 0.25f; }});
    edits.push_back({"sub.createdAt", [](SensorType& t) { t.sensorTypes[1].createdAt = ""; }});
    edits.push_back({"sub.updatedAt", [](SensorType& t) { t.sensorTypes[1].updatedAt = ""; }});
    edits.push_back({"a sub-type more", [](SensorType& t) { t.sensorTypes.push_back(subType("vpd", 0.0f, 5.0f)); }});
    edits.push_back({"a sub-type less", [](SensorType& t) { t.sensorTypes.pop_back(); }});
    // A Modbus channel: each of its fields, one more or one less, and the order.
    edits.push_back({"modbus.functionCode", [](SensorType& t) { t.modbusRegisterConfig[1].functionCode = 4; }});
    edits.push_back({"modbus.startRegister", [](SensorType& t) { t.modbusRegisterConfig[1].startRegister = 11; }});
    edits.push_back({"modbus.numRegisters", [](SensorType& t) { t.modbusRegisterConfig[1].numRegisters = 2; }});
    edits.push_back({"modbus.multiplier", [](SensorType& t) { t.modbusRegisterConfig[1].multiplier = 10.0f; }});
    edits.push_back({"modbus.offset", [](SensorType& t) { t.modbusRegisterConfig[1].offset = 0.0f; }});
    edits.push_back({"modbus.channelName", [](SensorType& t) { t.modbusRegisterConfig[1].channelName = "RH"; }});
    edits.push_back({"modbus.channel", [](SensorType& t) { t.modbusRegisterConfig[1].channel = 5; }});
    edits.push_back({"modbus.sensorType", [](SensorType& t) { t.modbusRegisterConfig[1].sensorType = "humidity_dig"; }});
    edits.push_back({"modbus.canConnectMinValue", [](SensorType& t) { t.modbusRegisterConfig[1].canConnectMinValue = -20.0f; }});
    edits.push_back({"modbus.canConnectMaxValue", [](SensorType& t) { t.modbusRegisterConfig[1].canConnectMaxValue = 4000.0f; }});
    edits.push_back({"modbus.dataType", [](SensorType& t) { t.modbusRegisterConfig[1].dataType = ModbusDataType::U16; }});
    edits.push_back({"a Modbus channel more", [](SensorType& t) { t.modbusRegisterConfig.push_back(modbusChannel(7, 16, "lux")); }});
    edits.push_back({"a Modbus channel less", [](SensorType& t) { t.modbusRegisterConfig.pop_back(); }});
    edits.push_back({"the order of the Modbus channels",
                     [](SensorType& t) { std::swap(t.modbusRegisterConfig[0], t.modbusRegisterConfig[1]); }});
    // An I2C channel: the same.
    edits.push_back({"i2c.registerAddr", [](SensorType& t) { t.i2cRegisterConfig[1].registerAddr = 0x27; }});
    edits.push_back({"i2c.length", [](SensorType& t) { t.i2cRegisterConfig[1].length = 1; }});
    edits.push_back({"i2c.multiplier", [](SensorType& t) { t.i2cRegisterConfig[1].multiplier = 1.0f; }});
    edits.push_back({"i2c.offset", [](SensorType& t) { t.i2cRegisterConfig[1].offset = 0.0f; }});
    edits.push_back({"i2c.channelName", [](SensorType& t) { t.i2cRegisterConfig[1].channelName = "t"; }});
    edits.push_back({"i2c.channel", [](SensorType& t) { t.i2cRegisterConfig[1].channel = 1; }});
    edits.push_back({"i2c.sensorType", [](SensorType& t) { t.i2cRegisterConfig[1].sensorType = "air_temp"; }});
    edits.push_back({"an I2C channel less", [](SensorType& t) { t.i2cRegisterConfig.pop_back(); }});
    // An analog pin: its fields and the count.
    edits.push_back({"analog.pin", [](SensorType& t) { t.analogPinConfig[0].pin = 2; }});
    edits.push_back({"analog.multiplier", [](SensorType& t) { t.analogPinConfig[0].multiplier = 2.0f; }});
    edits.push_back({"analog.offset", [](SensorType& t) { t.analogPinConfig[0].offset = 1.0f; }});
    edits.push_back({"analog.channelName", [](SensorType& t) { t.analogPinConfig[0].channelName = "a"; }});
    edits.push_back({"analog.channel", [](SensorType& t) { t.analogPinConfig[0].channel = 1; }});
    edits.push_back({"analog.sensorType", [](SensorType& t) { t.analogPinConfig[0].sensorType = "ph"; }});
    edits.push_back({"an analog pin more", [](SensorType& t) { t.analogPinConfig.push_back(AnalogPinConfig()); }});
    return edits;
}

} // namespace

static void test_a_type_is_the_same_as_itself(void)
{
    const SensorType type = everyFieldSet();
    TEST_ASSERT_TRUE(SensorTypeContent::sameSensorType(type, type));
    TEST_ASSERT_TRUE(SensorTypeContent::sameSensorType(type, SensorType(type)));
}

static void test_every_field_of_a_type_tells_two_types_apart(void)
{
    const SensorType type = everyFieldSet();
    const std::vector<Edit> edits = everyEdit();
    for (size_t i = 0; i < edits.size(); i++) {
        SensorType edited = type;
        edits[i].apply(edited);
        char message[160];
        snprintf(message, sizeof(message), "a type whose %s changed is taken for the type it was", edits[i].field);
        TEST_ASSERT_FALSE_MESSAGE(SensorTypeContent::sameSensorType(type, edited), message);
        TEST_ASSERT_FALSE_MESSAGE(SensorTypeContent::sameSensorType(edited, type), message);
    }
}

static void test_the_order_of_the_sub_types_does_not_tell_two_types_apart(void)
{
    const SensorType type = everyFieldSet();
    SensorType reordered = type;
    std::swap(reordered.sensorTypes[0], reordered.sensorTypes[1]);
    TEST_ASSERT_TRUE_MESSAGE(SensorTypeContent::sameSensorType(type, reordered),
                             "the same sub-types in another order made another type");

    // As a set and not as a count: two types holding as many sub-types are told apart by which.
    SensorType other = type;
    other.sensorTypes[0] = other.sensorTypes[1];
    TEST_ASSERT_FALSE(SensorTypeContent::sameSensorType(type, other));
    TEST_ASSERT_FALSE(SensorTypeContent::sameSensorType(other, type));
}

// The stored catalog is written by the converter and read by the reader the transport reads the
// server's answer with. A type that went through it is the type that went in - the comparison of
// what the module holds with what the server sends meets no difference the trip made up.
static void test_a_type_read_back_from_the_stored_catalog_is_the_same_type(void)
{
    ArduinoJsonConverter converter;
    const SensorType written = everyFieldSet();
    SensorType read;
    TEST_ASSERT_TRUE(converter.sensorTypeFromJson(converter.sensorTypeToJson(written), read));

    // The reader keeps no fields of an I2C channel but its register, length and scaling, and none of
    // an analog pin but that it is there: the stored catalog says no more of them than the server.
    SensorType expected = written;
    for (size_t i = 0; i < expected.i2cRegisterConfig.size(); i++) {
        I2CRegisterConfig config;
        config.registerAddr = expected.i2cRegisterConfig[i].registerAddr;
        config.length = expected.i2cRegisterConfig[i].length;
        config.multiplier = expected.i2cRegisterConfig[i].multiplier;
        config.offset = expected.i2cRegisterConfig[i].offset;
        expected.i2cRegisterConfig[i] = config;
    }
    for (size_t i = 0; i < expected.analogPinConfig.size(); i++) {
        expected.analogPinConfig[i] = AnalogPinConfig();
    }
    TEST_ASSERT_TRUE_MESSAGE(SensorTypeContent::sameSensorType(expected, read),
                             "a type read back from the stored catalog is not the type stored");

    // And read back once more, from what the module itself wrote.
    SensorType again;
    TEST_ASSERT_TRUE(converter.sensorTypeFromJson(converter.sensorTypeToJson(read), again));
    TEST_ASSERT_TRUE(SensorTypeContent::sameSensorType(read, again));
}

// A number between 1e-5 and 1 with more digits than nine places after the point keep is stored as
// another number, a few float steps away - and is the same number to the comparison all the same. A
// small number edited by a step the text does keep is another number.
static void test_a_number_the_stored_text_rounds_is_the_same_number(void)
{
    ArduinoJsonConverter converter;
    SensorType written = everyFieldSet();
    written.modbusRegisterConfig[0].multiplier = 0.000123456789f;
    written.sensorTypes[0].step = 0.0123456789f;
    SensorType read;
    TEST_ASSERT_TRUE(converter.sensorTypeFromJson(converter.sensorTypeToJson(written), read));

    TEST_ASSERT_TRUE_MESSAGE(read.modbusRegisterConfig[0].multiplier != written.modbusRegisterConfig[0].multiplier,
                             "the premise: the stored text gives the multiplier back as another float");
    TEST_ASSERT_TRUE_MESSAGE(SensorTypeContent::sameRegisterConfig(written.modbusRegisterConfig[0],
                                                                   read.modbusRegisterConfig[0]),
                             "a multiplier the stored text rounded is taken for another multiplier");
    TEST_ASSERT_TRUE(SensorTypeContent::sameSubSensorTypes(written.sensorTypes, read.sensorTypes));

    TEST_ASSERT_FALSE(SensorTypeContent::sameNumber(0.000123f, 0.000124f));
    TEST_ASSERT_FALSE(SensorTypeContent::sameNumber(0.00001f, 0.000011f));
    TEST_ASSERT_FALSE(SensorTypeContent::sameNumber(1000000.0f, 1000000.125f));
}

extern "C" void run_sensor_type_content_tests(void)
{
    RUN_TEST(test_a_type_is_the_same_as_itself);
    RUN_TEST(test_every_field_of_a_type_tells_two_types_apart);
    RUN_TEST(test_the_order_of_the_sub_types_does_not_tell_two_types_apart);
    RUN_TEST(test_a_type_read_back_from_the_stored_catalog_is_the_same_type);
    RUN_TEST(test_a_number_the_stored_text_rounds_is_the_same_number);
}
