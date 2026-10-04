// A number the module stored, or the backend sent, that is not a number when it is read back.
//
// The registry and the rule list are read on the way up, before anything else the module does, and
// a value in them is text until someone parses it. One field of that text holding "-nan", a word or
// a lone minus decides between three outcomes: the process ends on an exception nobody catches, the
// field is read as some other field's number, or the field is read as its default and counted.
//
// What this file pins:
//
//   1. a stored register config whose multiplier is a word is read with the identity multiplier,
//      and the sensor around it stands;
//   2. a field that is not a number does not take the value of the next field that is;
//   3. a rule body from the backend whose valueTarget is not a number leaves a rule with its
//      default, not a module that stops, and the substitution is counted;
//   4. a healthy registry still reads and writes byte for byte - the stored format did not move;
//   5. what a file holding "-nan" costs on the board: text that is not JSON at all refuses the
//      record whole, so one damaged number costs the sensor rather than the boot - and the module
//      cannot write such a file in the first place, because a non-finite number goes out as JSON's
//      own null;
//   6. that the substitutions are counted, and that neither a healthy file nor a key an older
//      build never wrote is counted;
//   7. and where the "-nan" comes from: a register config, Modbus or I2C, built on a used
//      stack.
//
// What runs is production code - ArduinoJsonConverter over the production SpiffsSensorDao.
// Replaced is the flash device (Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h).

#include <unity.h>

#include "Core/Domain/Diagnostics/MalformedJsonNumberCounter.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"

#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

const char* const REGISTRY_FILE = "/data/sensors.json";

/**
 * @brief The registry as the abort was taken off it: one sensor, two channels, and a multiplier
 *        that was never initialised before it was written out.
 *
 * "-nan" is what a float holding whatever the stack left there serializes as. It is also not valid
 * JSON, which is why the record around it is refused whole rather than read with a default: the
 * damage is not in one field's value, it is in the text the document is made of.
 */
const char* const REGISTRY_WITH_A_NAN_MULTIPLIER =
    "[{\"uid\":\"sensor-uid-00\",\"sensorType\":\"air_temperature\",\"model\":\"rk330_01\","
    "\"protocol\":\"ModBus\",\"createdAt\":\"2026-01-04T10:00:00.000000+00:00\",\"isActive\":true,"
    "\"isError\":false,\"status\":\"added\",\"pin\":1,\"modbusDeviceAddress\":112,\"baudRate\":9600,"
    "\"modbusPingAddress\":256,\"registerToStoreModbusDeviceAddress\":512,"
    "\"writeRegistersCommand\":6,\"applyAddressAfterPowerCycle\":false,\"i2cAddress\":\"\","
    "\"chipType\":\"\",\"currentValue\":21.5,\"lastReadTime\":1767520800,\"sensorTypeId\":42,"
    "\"sensorTypeUid\":\"type-uid-0007\",\"modbusRegisterConfigs\":["
    "{\"functionCode\":3,\"startRegister\":1,\"numRegisters\":2,\"multiplier\":-nan,\"offset\":0,"
    "\"channel_name\":\"channel_1\",\"channel\":1,\"sensorType\":\"channel_1\","
    "\"canConnectMinValue\":0,\"canConnectMaxValue\":1000,\"dataType\":\"u16\"},"
    "{\"functionCode\":3,\"startRegister\":2,\"numRegisters\":2,\"multiplier\":0.01,\"offset\":0,"
    "\"channel_name\":\"channel_2\",\"channel\":2,\"sensorType\":\"channel_2\","
    "\"canConnectMinValue\":0,\"canConnectMaxValue\":1000,\"dataType\":\"u16\"}]}]";

/**
 * @brief The same registry with a multiplier of letters and an offset of its own behind it.
 *
 * A word in a number's place has no digits to read; the question is whether the reader then walks
 * on until it finds some, and comes back with the offset that follows.
 */
const char* const REGISTRY_WITH_A_WORD_FOR_A_MULTIPLIER =
    "[{\"uid\":\"sensor-uid-00\",\"sensorType\":\"air_temperature\",\"model\":\"rk330_01\","
    "\"protocol\":\"ModBus\",\"createdAt\":\"2026-01-04T10:00:00.000000+00:00\",\"isActive\":true,"
    "\"isError\":false,\"status\":\"added\",\"pin\":1,\"modbusDeviceAddress\":112,\"baudRate\":9600,"
    "\"modbusPingAddress\":256,\"registerToStoreModbusDeviceAddress\":512,"
    "\"writeRegistersCommand\":6,\"applyAddressAfterPowerCycle\":false,\"i2cAddress\":\"\","
    "\"chipType\":\"\",\"currentValue\":21.5,\"lastReadTime\":1767520800,\"sensorTypeId\":42,"
    "\"sensorTypeUid\":\"type-uid-0007\",\"modbusRegisterConfigs\":["
    "{\"functionCode\":3,\"startRegister\":1,\"numRegisters\":2,\"multiplier\":\"abc\","
    "\"offset\":0.25,\"channel_name\":\"channel_1\",\"channel\":1,\"sensorType\":\"channel_1\","
    "\"canConnectMinValue\":0,\"canConnectMaxValue\":1000,\"dataType\":\"u16\"}]}]";

/**
 * @brief A healthy registry of the shape the module writes, captured from the converter itself.
 *
 * The round trip below compares against these bytes, so a value read differently or written
 * differently is a file the fleet's other firmware reads as something else.
 */
const char* const HEALTHY_REGISTRY =
    "[{\"uid\":\"sensor-uid-00\",\"sensorType\":\"air_temperature\",\"model\":\"rk330_01\","
    "\"protocol\":\"ModBus\",\"createdAt\":\"2026-01-04T10:00:00.000000+00:00\",\"isActive\":true,"
    "\"isError\":false,\"status\":\"added\",\"pin\":1,\"modbusDeviceAddress\":112,\"baudRate\":9600,"
    "\"modbusPingAddress\":256,\"registerToStoreModbusDeviceAddress\":512,"
    "\"writeRegistersCommand\":6,\"applyAddressAfterPowerCycle\":false,\"i2cAddress\":\"\","
    "\"chipType\":\"\",\"currentValue\":21.5,\"lastReadTime\":1767520800,\"sensorTypeId\":42,"
    "\"sensorTypeUid\":\"type-uid-0007\",\"modbusRegisterConfigs\":["
    "{\"functionCode\":3,\"startRegister\":1,\"numRegisters\":2,\"multiplier\":0.01,\"offset\":0,"
    "\"channel_name\":\"channel_1\",\"channel\":1,\"sensorType\":\"channel_1\","
    "\"canConnectMinValue\":0,\"canConnectMaxValue\":1000,\"dataType\":\"u16\"},"
    "{\"functionCode\":3,\"startRegister\":2,\"numRegisters\":2,\"multiplier\":0.01,\"offset\":0,"
    "\"channel_name\":\"channel_2\",\"channel\":2,\"sensorType\":\"channel_2\","
    "\"canConnectMinValue\":0,\"canConnectMaxValue\":1000,\"dataType\":\"u16\"}]}]";

/** @brief A rule snapshot as RULE_PUSH delivers it, with a target of a single minus sign. */
const char* const RULES_WITH_A_LONE_MINUS_FOR_A_TARGET =
    "[{\"uid\":\"rule-uid-0001\",\"version\":3,\"deviceUid\":\"7C:DF:A1:96:B4:94.01\","
    "\"ruleType\":\"step\",\"sensorUid\":\"84:F7:03:D7:8E:60.00.01\",\"valueTarget\":\"-\","
    "\"direction\":\"down\",\"workingTime\":60,\"waitingTime\":600,\"isRuleOn\":false,"
    "\"mode\":\"Off\",\"state\":\"Synchronized\",\"daysOfWeek\":[],\"daysOfMonth\":[]}]";

/** @brief And one whose target is a word, with the rule's version the next number behind it. */
const char* const RULES_WITH_A_WORD_FOR_A_TARGET =
    "[{\"uid\":\"rule-uid-0002\",\"ruleType\":\"step\","
    "\"sensorUid\":\"84:F7:03:D7:8E:60.00.01\",\"valueTarget\":\"n/a\",\"direction\":\"down\","
    "\"version\":7,\"workingTime\":60,\"waitingTime\":600,\"isRuleOn\":false,\"mode\":\"Off\","
    "\"state\":\"Synchronized\",\"daysOfWeek\":[],\"daysOfMonth\":[]}]";

/**
 * @brief Fill the frame the next call builds its locals in with a pattern that reads as a NaN
 *        float and as 0xFF in every integer.
 *
 * A field with no initialiser holds whatever was there before, and on a quiet stack that is
 * usually zero - which reads like a value somebody chose. This makes the difference visible: after
 * this, a number that was never initialised is one nothing can mistake for a configured value. If
 * some future compiler lays the two frames out apart the scenario stops proving anything and stays
 * green; it cannot fail for that reason.
 */
void poisonTheFrame()
{
    volatile unsigned char pattern[sizeof(ModbusRegisterConfig) * 4];
    for (size_t i = 0; i < sizeof(pattern); i++)
    {
        pattern[i] = 0xFF;
    }
}

/** @brief One register config as the module makes them, written out the way the registry stores it. */
std::string writeADefaultConfig(IJsonConverter& converter)
{
    ModbusRegisterConfig config;
    config.channelName = "channel_1";
    return converter.modbusRegisterConfigToJson(config);
}

/** @brief The same for the I2C config, which shares the stored record with the Modbus one. */
std::string writeADefaultI2cConfig(IJsonConverter& converter)
{
    I2CRegisterConfig config;
    config.channelName = "channel_1";
    return converter.i2cRegisterConfigToJson(config);
}

/** @brief A fixture whose flash device is read through the converter the caller names. */
struct RegistryFixture {
    MockSpiffsAdapter flash;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsSensorDao dao;

    explicit RegistryFixture(IJsonConverter& converter)
        : dao(&flash, &converter, &debugSystem, &logger) {}

    void store(const char* text)
    {
        TEST_ASSERT_TRUE_MESSAGE(flash.writeToFile(REGISTRY_FILE, text),
                                 "the scenario could not put a registry on flash");
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The file the module wrote and cannot read.
// --------------------------------------------------------------------------------------------

/**
 * A field with no number in it must not be answered with the first number anywhere after it: this
 * record would come back with its offset for a multiplier - not a crash, a channel quietly scaled by the
 * wrong constant. A word is valid JSON in a number's place, so the record stands and the field
 * takes its own default.
 */
static void test_a_number_that_is_not_there_is_not_taken_from_the_next_field(void)
{
    ArduinoJsonConverter converter;
    RegistryFixture fixture(converter);
    fixture.store(REGISTRY_WITH_A_WORD_FOR_A_MULTIPLIER);

    std::vector<SensorEntity> loaded;
    TEST_ASSERT_TRUE(fixture.dao.getSensors(loaded));
    TEST_ASSERT_EQUAL_size_t(1, loaded.size());
    TEST_ASSERT_EQUAL_size_t(1, loaded[0].modbusRegisterConfigs.size());

    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(
        1.0f, loaded[0].modbusRegisterConfigs[0].multiplier,
        "the multiplier was read out of the field behind it instead of defaulted");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.25f, loaded[0].modbusRegisterConfigs[0].offset,
                                    "the field that does hold a number was read wrong");
}

// --------------------------------------------------------------------------------------------
// 2. The same text, arriving from the backend.
// --------------------------------------------------------------------------------------------

/**
 * RULE_PUSH hands the payload to rulesFromJson as it arrived. A target of one minus sign is text
 * std::stod refuses, and the module ended there - a rule body from the backend taking the module
 * down, remotely and repeatably. The rule stands with its target defaulted instead, and the
 * reading is counted.
 */
static void test_a_rule_target_that_is_not_a_number_is_read_with_a_default(void)
{
    ArduinoJsonConverter converter;
    const uint32_t countedBefore = MalformedJsonNumberCounter::count();

    std::vector<Rule> rules;
    const JsonListParse parsed = converter.rulesFromJson(RULES_WITH_A_LONE_MINUS_FOR_A_TARGET, rules);

    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(JsonListParse::Ok), static_cast<int>(parsed),
                              "a rule list holding one damaged number was read as no list");
    TEST_ASSERT_EQUAL_size_t(1, rules.size());
    TEST_ASSERT_EQUAL_STRING("rule-uid-0001", rules[0].uid.c_str());
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, rules[0].valueTarget,
                                    "a target that is not a number has to read as the default");
    TEST_ASSERT_TRUE_MESSAGE(MalformedJsonNumberCounter::count() > countedBefore,
                             "the module read a rule target it could not read and said nothing");
}

/**
 * And a target of letters does not become the rule's version, which is the next number in the body.
 * A rule that fires at a threshold it was never given is worse than one that does not fire.
 */
static void test_a_rule_target_is_not_taken_from_the_next_field(void)
{
    ArduinoJsonConverter converter;

    std::vector<Rule> rules;
    TEST_ASSERT_EQUAL(static_cast<int>(JsonListParse::Ok),
                      static_cast<int>(converter.rulesFromJson(RULES_WITH_A_WORD_FOR_A_TARGET, rules)));
    TEST_ASSERT_EQUAL_size_t(1, rules.size());

    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, rules[0].valueTarget,
                                    "the target was read out of the field behind it");
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, rules[0].version,
                                  "the field that does hold a number was read wrong");
}

// --------------------------------------------------------------------------------------------
// 3. The healthy file is untouched.
// --------------------------------------------------------------------------------------------

/**
 * The control on all of the above: reading numbers more strictly must not read a healthy file
 * differently. The registry goes in as bytes, comes back as sensors, and is written back as the
 * same bytes.
 */
static void test_a_healthy_registry_is_read_and_written_byte_for_byte(void)
{
    ArduinoJsonConverter converter;
    RegistryFixture fixture(converter);
    fixture.store(HEALTHY_REGISTRY);

    std::vector<SensorEntity> loaded;
    TEST_ASSERT_TRUE(fixture.dao.getSensors(loaded));
    TEST_ASSERT_EQUAL_size_t(1, loaded.size());
    TEST_ASSERT_EQUAL_size_t(2, loaded[0].modbusRegisterConfigs.size());
    TEST_ASSERT_EQUAL_FLOAT(0.01f, loaded[0].modbusRegisterConfigs[0].multiplier);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loaded[0].modbusRegisterConfigs[0].offset);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, loaded[0].modbusRegisterConfigs[0].canConnectMaxValue);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, loaded[0].currentValue);

    TEST_ASSERT_TRUE(fixture.dao.saveSensors(loaded));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(HEALTHY_REGISTRY,
                                     fixture.flash.getFileContent(REGISTRY_FILE).c_str(),
                                     "the registry this build writes is not the one it read");
}

// --------------------------------------------------------------------------------------------
// 4. Text that is not JSON at all, and what it costs.
//
// The two above are damaged values inside a readable document. "-nan" is not that: it is text no
// JSON parser accepts, so the document ends there. What matters is where the cost stops - the
// record, not the boot: this is the file a module wrote for itself, read on the way up, and a
// reader that threw here ended every start on the same file.
// --------------------------------------------------------------------------------------------

static void test_the_device_converter_loses_the_record_it_cannot_read(void)
{
    ArduinoJsonConverter converter;
    RegistryFixture fixture(converter);
    fixture.store(REGISTRY_WITH_A_NAN_MULTIPLIER);

    std::vector<SensorEntity> loaded;
    fixture.dao.getSensors(loaded);

    char message[256];
    snprintf(message, sizeof(message),
             "the device converter read %u sensors out of a registry whose only record holds "
             "\"-nan\" - if this is 0, one damaged number costs the whole sensor, and the first "
             "write after that makes it permanent",
             (unsigned)loaded.size());
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, loaded.size(), message);
}

/**
 * The stored side of the same trace: a multiplier of letters costs the channel its scale, the
 * sensor stands, and the substitution is counted. A channel scaled by a number nobody configured
 * is worth a line in the log.
 */
static void test_the_device_converter_counts_a_stored_multiplier_it_could_not_read(void)
{
    ArduinoJsonConverter converter;
    RegistryFixture fixture(converter);
    fixture.store(REGISTRY_WITH_A_WORD_FOR_A_MULTIPLIER);
    const uint32_t countedBefore = MalformedJsonNumberCounter::count();

    std::vector<SensorEntity> loaded;
    TEST_ASSERT_TRUE(fixture.dao.getSensors(loaded));
    TEST_ASSERT_EQUAL_size_t(1, loaded.size());
    TEST_ASSERT_TRUE_MESSAGE(MalformedJsonNumberCounter::count() > countedBefore,
                             "the module read a stored multiplier it could not read and said "
                             "nothing");
}

/**
 * The control that keeps the number above worth reading. A registry whose every number is a number
 * must not move the counter, or the column says "damaged" about every healthy boot and stops
 * meaning anything.
 */
static void test_a_healthy_registry_costs_the_device_converter_no_count(void)
{
    ArduinoJsonConverter converter;
    RegistryFixture fixture(converter);
    fixture.store(HEALTHY_REGISTRY);
    const uint32_t countedBefore = MalformedJsonNumberCounter::count();

    std::vector<SensorEntity> loaded;
    TEST_ASSERT_TRUE(fixture.dao.getSensors(loaded));
    TEST_ASSERT_EQUAL_size_t(1, loaded.size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(countedBefore, MalformedJsonNumberCounter::count(),
                                     "a registry with nothing wrong in it was counted as damaged");
}

/**
 * And the other half of that control: a key the record simply does not carry. A sensor written by
 * an older build has no text in that field to be wrong, so its default is the answer and not an
 * event - otherwise every such record would be reported as damaged for as long as it is stored.
 */
static void test_a_key_a_record_does_not_carry_is_not_counted(void)
{
    ArduinoJsonConverter converter;
    const uint32_t countedBefore = MalformedJsonNumberCounter::count();

    SensorEntity sensor;
    TEST_ASSERT_TRUE(converter.sensorFromJson(
        "{\"uid\":\"sensor-uid-00\",\"sensorType\":\"air_temperature\",\"protocol\":\"ModBus\"}",
        sensor));

    TEST_ASSERT_EQUAL_INT_MESSAGE(9600, sensor.baudRate,
                                  "a record without a baud rate has to read as the default one");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(countedBefore, MalformedJsonNumberCounter::count(),
                                     "a key that is absent was counted as a damaged number");
}

/**
 * And the other direction: what the device writes when a number it holds is not finite. ArduinoJson
 * writes null rather than "nan", so the file stays valid JSON and reads back as the field's
 * default - the board cannot write a registry it is unable to read.
 */
static void test_the_device_converter_writes_a_non_finite_number_as_null(void)
{
    ArduinoJsonConverter converter;

    SensorEntity sensor;
    sensor.uid = "sensor-uid-00";
    sensor.sensorType = "air_temperature";
    sensor.protocol = "ModBus";
    sensor.pin = 1;
    ModbusRegisterConfig config;
    config.channelName = "channel_1";
    config.channel = 1;
    config.multiplier = std::numeric_limits<float>::quiet_NaN();
    sensor.modbusRegisterConfigs.push_back(config);

    const std::string written = converter.sensorToJson(sensor);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(written.c_str(), "\"multiplier\":null"),
                                 "a non-finite multiplier did not reach the file as null");

    SensorEntity readBack;
    TEST_ASSERT_TRUE_MESSAGE(converter.sensorFromJson(written, readBack),
                             "the device converter could not read the record it just wrote");
    TEST_ASSERT_EQUAL_size_t(1, readBack.modbusRegisterConfigs.size());
    TEST_ASSERT_TRUE_MESSAGE(std::isfinite(readBack.modbusRegisterConfigs[0].multiplier),
                             "the multiplier came back non-finite");
}

// --------------------------------------------------------------------------------------------
// 5. Where a "-nan" on flash comes from in the first place.
// --------------------------------------------------------------------------------------------

/**
 * The other end of the same defect. A register config is built by the module in several places -
 * the probes the address write and the ping use among them - and a numeric field with no
 * initialiser starts out as whatever the frame held. Written out, such a field is JSON's null,
 * which reads back as the field's default: a record that looks intact and carries a number nobody
 * chose. So the check is on the values, and on there being no null in the record at all.
 */
static void test_a_register_config_is_born_with_numbers_and_not_with_the_stack(void)
{
    ArduinoJsonConverter converter;

    poisonTheFrame();
    const std::string written = writeADefaultConfig(converter);

    char message[384];
    snprintf(message, sizeof(message),
             "a register config built on a used stack was written as %s - a record whose numbers "
             "are whatever the frame held before it",
             written.c_str());
    TEST_ASSERT_NULL_MESSAGE(strstr(written.c_str(), "null"), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(written.c_str(), "\"multiplier\":1"), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(written.c_str(), "\"offset\":0"), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(written.c_str(), "\"functionCode\":3"), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(written.c_str(), "\"numRegisters\":1"), message);
}

/**
 * The I2C config shares the stored sensor record with the Modbus one and is written by the same
 * serializer, so it reaches flash the same way and has to start from numbers the same way.
 */
static void test_an_i2c_register_config_is_born_with_numbers_and_not_with_the_stack(void)
{
    ArduinoJsonConverter converter;

    poisonTheFrame();
    const std::string written = writeADefaultI2cConfig(converter);

    char message[384];
    snprintf(message, sizeof(message),
             "an I2C register config built on a used stack was written as %s - a record whose "
             "numbers are whatever the frame held before it",
             written.c_str());
    TEST_ASSERT_NULL_MESSAGE(strstr(written.c_str(), "null"), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(written.c_str(), "\"multiplier\":1"), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(written.c_str(), "\"offset\":0"), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(written.c_str(), "\"length\":1"), message);
}

extern "C" void run_json_number_fault_tests(void)
{
    RUN_TEST(test_a_number_that_is_not_there_is_not_taken_from_the_next_field);
    RUN_TEST(test_a_rule_target_that_is_not_a_number_is_read_with_a_default);
    RUN_TEST(test_a_rule_target_is_not_taken_from_the_next_field);
    RUN_TEST(test_a_healthy_registry_is_read_and_written_byte_for_byte);
    RUN_TEST(test_the_device_converter_loses_the_record_it_cannot_read);
    RUN_TEST(test_the_device_converter_counts_a_stored_multiplier_it_could_not_read);
    RUN_TEST(test_a_healthy_registry_costs_the_device_converter_no_count);
    RUN_TEST(test_a_key_a_record_does_not_carry_is_not_counted);
    RUN_TEST(test_the_device_converter_writes_a_non_finite_number_as_null);
    RUN_TEST(test_a_register_config_is_born_with_numbers_and_not_with_the_stack);
    RUN_TEST(test_an_i2c_register_config_is_born_with_numbers_and_not_with_the_stack);
}
