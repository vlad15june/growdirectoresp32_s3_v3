// The sensor registry, read and written without a document sized for the whole of it.
//
// The registry is one JSON array on flash, and reading it by parsing that array
// into a single fixed document would be a wall, not a limit: the array either fits or nothing
// comes out of it. Measured on the converter the device runs, the wall stands at 22 sensors with no
// register configs, 10 with two channels and 6 with four - and one sensor past it the module comes
// up with an empty registry, which the first write after that makes permanent. The same document
// bounded the write, so a module holding more sensors than fit stored a shorter array without
// saying so.
//
// What this file pins:
//
//   1. a registry longer than that document is read whole, and written whole;
//   2. a record the parser cannot read costs that record and not the registry;
//   3. a registry past the module's declared capacity is read up to it, with the surplus refused
//      and counted - the capacity is a hard cap, and the reading of it is what enforces it;
//   4. what reading costs does not follow how many sensors there are;
//   5. the bytes on flash did not move: a registry captured from the previous build is read into
//      the same sensors and written back byte for byte.
//
// What runs is production code - ArduinoJsonConverter, which the device runs, over the production
// SpiffsSensorDao. Replaced is the flash device (Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h).

#include <unity.h>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"

#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/HostAllocationCounter.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* const REGISTRY_FILE = "/data/sensors.json";

/**
 * @brief A registry that fits a single fixed document holding the whole array.
 *
 * Five sensors of four channels each is ~6.8 KB of JSON - inside the ~9.5 KB of text that document
 * actually holds, since a parsed array costs more than its text.
 */
const size_t SMALL_REGISTRY = 5;

/**
 * @brief Three times that, and past the document - but still inside what the module keeps.
 *
 * Both figures are below SensorLimits::MAX_SENSORS_PER_MODULE on purpose: what is measured between
 * them is the reading of the array, not the capacity.
 */
const size_t LARGE_REGISTRY = 3 * SMALL_REGISTRY;

/** @brief And a registry three times the module's capacity, for the cost of refusing. */
const size_t REGISTRY_PAST_THE_CAP = 3 * SensorLimits::MAX_SENSORS_PER_MODULE;

/** @brief The shape the field has: a Modbus sensor with four channels. */
const int FIELD_CHANNELS = 4;

// --- the registry the scenarios run on --------------------------------------------------------

SensorEntity makeModbusSensor(size_t index, int channels)
{
    SensorEntity sensor;
    char uid[32];
    snprintf(uid, sizeof(uid), "sensor-uid-%02u", (unsigned)index);
    sensor.uid = uid;
    sensor.sensorType = "air_temperature";
    sensor.model = "rk330_01";
    sensor.protocol = "ModBus";
    sensor.createdAt = "2026-01-04T10:00:00.000000+00:00";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.status = SENSOR_STATUS_ADDED;
    sensor.pin = (int)(index % 4);
    sensor.modbusDeviceAddress = MODBUS_ADDRESS_MIN + (int)(index % 4);
    sensor.baudRate = 9600;
    sensor.modbusPingAddress = 0x0100;
    sensor.registerToStoreModbusDeviceAddress = 0x0200;
    sensor.writeRegistersCommand = 6;
    sensor.applyAddressAfterPowerCycle = false;
    sensor.currentValue = 21.5f;
    sensor.lastReadTime = 1767520800u;
    sensor.sensorTypeId = 42;
    sensor.sensorTypeUid = "type-uid-0007";

    for (int channel = 1; channel <= channels; channel++)
    {
        ModbusRegisterConfig config;
        config.functionCode = 3;
        config.startRegister = (uint16_t)channel;
        config.numRegisters = 2;
        config.multiplier = 0.01f;
        config.offset = 0.0f;
        char name[32];
        snprintf(name, sizeof(name), "channel_%d", channel);
        config.channelName = name;
        config.channel = (uint8_t)channel;
        config.sensorType = name;
        config.canConnectMinValue = 0.0f;
        config.canConnectMaxValue = 1000.0f;
        config.dataType = ModbusDataType::U16;
        sensor.modbusRegisterConfigs.push_back(config);
    }

    return sensor;
}

std::vector<SensorEntity> makeRegistry(size_t count, int channels)
{
    std::vector<SensorEntity> registry;
    for (size_t i = 0; i < count; i++)
    {
        registry.push_back(makeModbusSensor(i, channels));
    }
    return registry;
}

/**
 * @brief The registry as it lies on flash: the array of the module's own per-sensor records.
 *
 * Built out of sensorToJson(), which is the one record format there is, so a scenario can put a
 * registry of any length on the flash stand-in without going through the writer under test.
 */
std::string registryText(IJsonConverter& converter, const std::vector<SensorEntity>& sensors)
{
    std::string text = "[";
    for (size_t i = 0; i < sensors.size(); i++)
    {
        if (i > 0)
        {
            text += ",";
        }
        text += converter.sensorToJson(sensors[i]);
    }
    text += "]";
    return text;
}

/** @brief A fixture whose flash device already holds this registry. */
struct RegistryFixture {
    MockSpiffsAdapter flash;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsSensorDao dao;

    RegistryFixture() : dao(&flash, &converter, &debugSystem, &logger) {}

    void store(const std::string& text)
    {
        TEST_ASSERT_TRUE_MESSAGE(flash.writeToFile(REGISTRY_FILE, text.c_str()),
                                 "the scenario could not put a registry on flash");
    }
};

/** @brief Uids of the sensors, in the order they came out. */
std::string uidsOf(const std::vector<SensorEntity>& sensors)
{
    std::string joined;
    for (size_t i = 0; i < sensors.size(); i++)
    {
        if (i > 0)
        {
            joined += ",";
        }
        joined += sensors[i].uid;
    }
    return joined;
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. A registry longer than one document.
// --------------------------------------------------------------------------------------------

/**
 * The registry a restart reads. Fifteen sensors of four channels is ~20 KB of stored JSON, twice
 * what the array document ever held - and a module that reads it as empty has forgotten every
 * sensor it has.
 */
static void test_a_registry_larger_than_one_document_is_read_whole(void)
{
    RegistryFixture fixture;
    const std::vector<SensorEntity> stored = makeRegistry(LARGE_REGISTRY, FIELD_CHANNELS);
    const std::string text = registryText(fixture.converter, stored);
    fixture.store(text);

    std::vector<SensorEntity> loaded;
    const bool read = fixture.dao.getSensors(loaded);

    char message[256];
    snprintf(message, sizeof(message),
             "a stored registry of %u sensors (%u bytes of JSON) came back with %u of them",
             (unsigned)LARGE_REGISTRY, (unsigned)text.size(), (unsigned)loaded.size());
    TEST_ASSERT_TRUE_MESSAGE(read, message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(LARGE_REGISTRY, loaded.size(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(uidsOf(stored).c_str(), uidsOf(loaded).c_str(),
                                     "the registry came back in a different shape");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(FIELD_CHANNELS, loaded.back().modbusRegisterConfigs.size(),
                                     "the last sensor came back without its channels");
    TEST_ASSERT_EQUAL_STRING("channel_4", loaded.back().modbusRegisterConfigs[3].channelName.c_str());
}

/**
 * And the write, which the same document bounded: a module holding more sensors than fit stored a
 * shorter array and reported the write a success.
 */
static void test_a_registry_larger_than_one_document_is_written_whole(void)
{
    RegistryFixture fixture;
    const std::vector<SensorEntity> holding = makeRegistry(LARGE_REGISTRY, FIELD_CHANNELS);

    TEST_ASSERT_TRUE_MESSAGE(fixture.dao.saveSensors(holding), "the registry was not written");

    const std::string onFlash = fixture.flash.getFileContent(REGISTRY_FILE);
    const std::string expected = registryText(fixture.converter, holding);

    char message[256];
    snprintf(message, sizeof(message),
             "writing %u sensors put %u bytes on flash where the records come to %u",
             (unsigned)LARGE_REGISTRY, (unsigned)onFlash.size(), (unsigned)expected.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.c_str(), onFlash.c_str(), message);

    // And what a module that restarts on that file gets back.
    std::vector<SensorEntity> loaded;
    TEST_ASSERT_TRUE(fixture.dao.getSensors(loaded));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(LARGE_REGISTRY, loaded.size(),
                                     "the registry the module wrote is not the one it reads back");
    TEST_ASSERT_EQUAL_STRING(uidsOf(holding).c_str(), uidsOf(loaded).c_str());
}

// --------------------------------------------------------------------------------------------
// 2. One damaged record.
// --------------------------------------------------------------------------------------------

/**
 * A record the parser cannot read, in the middle of the file. Everything else in that file is
 * intact and has to come back; one damaged entry costing the module every sensor it has is the
 * difference between a module missing a sensor and a module missing all of them.
 */
static void test_a_record_the_module_cannot_read_is_skipped_and_the_rest_stand(void)
{
    RegistryFixture fixture;
    const std::vector<SensorEntity> stored = makeRegistry(SMALL_REGISTRY, FIELD_CHANNELS);

    // The damage: a number that is no longer a number, in the third record. The braces still
    // balance, so the array is still an array - which is what a byte going bad in a value looks
    // like, and the only kind of damage a reader can step over at all.
    std::string text = "[";
    for (size_t i = 0; i < stored.size(); i++)
    {
        if (i > 0)
        {
            text += ",";
        }
        std::string record = fixture.converter.sensorToJson(stored[i]);
        if (i == 2)
        {
            const std::string good = "\"baudRate\":9600";
            const size_t at = record.find(good);
            TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos,
                                     "the scenario could not find the field it damages");
            record.replace(at, good.size(), "\"baudRate\":96x00");
        }
        text += record;
    }
    text += "]";
    fixture.store(text);

    std::vector<SensorEntity> loaded;
    const bool read = fixture.dao.getSensors(loaded);

    char message[256];
    snprintf(message, sizeof(message),
             "one damaged record out of %u left the module with %u sensors",
             (unsigned)SMALL_REGISTRY, (unsigned)loaded.size());
    TEST_ASSERT_TRUE_MESSAGE(read, message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(SMALL_REGISTRY - 1, loaded.size(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "sensor-uid-00,sensor-uid-01,sensor-uid-03,sensor-uid-04", uidsOf(loaded).c_str(),
        "the sensors that survived are not the ones that were intact");

    TEST_ASSERT_TRUE_MESSAGE(
        fixture.logger.hasMessageContaining("could not be read and were skipped"),
        "the module came up one sensor short and said nothing about it");
}

// --------------------------------------------------------------------------------------------
// 3. The capacity of the registry.
// --------------------------------------------------------------------------------------------

/**
 * A registry longer than the module's declared capacity. The cap is hard - the sensors past it are
 * refused and counted - and what it must not do is cost the module the sensors it does have room
 * for.
 */
static void test_a_registry_past_the_capacity_keeps_what_fits_and_refuses_the_rest(void)
{
    RegistryFixture fixture;
    const size_t surplus = 3;
    const size_t storedCount = SensorLimits::MAX_SENSORS_PER_MODULE + surplus;
    // No register configs, so the whole registry fits the document the base build reads it into:
    // what this scenario is about is the capacity, not the box.
    const std::vector<SensorEntity> stored = makeRegistry(storedCount, 0);
    fixture.store(registryText(fixture.converter, stored));

    std::vector<SensorEntity> loaded;
    const bool read = fixture.dao.getSensors(loaded);

    char message[256];
    snprintf(message, sizeof(message),
             "a stored registry of %u sensors against a capacity of %u came back with %u",
             (unsigned)storedCount, (unsigned)SensorLimits::MAX_SENSORS_PER_MODULE,
             (unsigned)loaded.size());
    TEST_ASSERT_TRUE_MESSAGE(read, message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(SensorLimits::MAX_SENSORS_PER_MODULE, loaded.size(), message);

    // The ones it kept are the ones the file names first, not an arbitrary subset.
    std::vector<SensorEntity> expected(stored.begin(),
                                       stored.begin() + SensorLimits::MAX_SENSORS_PER_MODULE);
    TEST_ASSERT_EQUAL_STRING(uidsOf(expected).c_str(), uidsOf(loaded).c_str());

    TEST_ASSERT_TRUE_MESSAGE(
        fixture.logger.hasMessageContaining("past the capacity"),
        "sensors were dropped at the capacity and nothing was said about it");
}

// --------------------------------------------------------------------------------------------
// 4. The bytes on flash.
// --------------------------------------------------------------------------------------------

/**
 * @brief A registry file as the installed fleet has it, byte for byte.
 *
 * Three sensors, covering the three shapes a record takes: Modbus channels, I2C channels, and a
 * sensor with neither (where both arrays are left out of the record). Captured
 * by serializing that registry with ArduinoJsonConverter, so what it pins is not
 * this build's opinion of the format.
 *
 * The fleet reads and writes this file, and a module that writes a shape the previous firmware
 * cannot read is a module that cannot be rolled back.
 */
const char* const STORED_REGISTRY_OF_THE_PREVIOUS_BUILD =
    "[{\"uid\":\"sensor-uid-00\",\"sensorType\":\"air_temperature\",\"model\":\"rk330_01\",\"protocol\":\"ModBus\",\"c"
    "reatedAt\":\"2026-01-04T10:00:00.000000+00:00\",\"isActive\":true,\"isError\":false,\"status\":\"added\",\"p"
    "in\":0,\"modbusDeviceAddress\":112,\"baudRate\":9600,\"modbusPingAddress\":256,\"registerToStoreModbusDe"
    "viceAddress\":512,\"writeRegistersCommand\":6,\"applyAddressAfterPowerCycle\":false,\"i2cAddress\":\"\",\""
    "chipType\":\"\",\"currentValue\":21.5,\"lastReadTime\":1767520800,\"sensorTypeId\":42,\"sensorTypeUid\":\"ty"
    "pe-uid-0007\",\"modbusRegisterConfigs\":[{\"functionCode\":3,\"startRegister\":1,\"numRegisters\":2,\"mult"
    "iplier\":0.01,\"offset\":0,\"channel_name\":\"channel_1\",\"channel\":1,\"sensorType\":\"channel_1\",\"canConn"
    "ectMinValue\":0,\"canConnectMaxValue\":1000,\"dataType\":\"u16\"},{\"functionCode\":3,\"startRegister\":2,\""
    "numRegisters\":2,\"multiplier\":0.01,\"offset\":0,\"channel_name\":\"channel_2\",\"channel\":2,\"sensorType\""
    ":\"channel_2\",\"canConnectMinValue\":0,\"canConnectMaxValue\":1000,\"dataType\":\"u16\"}]},{\"uid\":\"sensor"
    "-uid-01\",\"sensorType\":\"co2\",\"model\":\"t6703\",\"protocol\":\"I2C\",\"createdAt\":\"2026-01-05T11:30:00.00"
    "0000+00:00\",\"isActive\":true,\"isError\":true,\"status\":\"disconnected\",\"pin\":2,\"modbusDeviceAddress\""
    ":1,\"baudRate\":9600,\"modbusPingAddress\":0,\"registerToStoreModbusDeviceAddress\":0,\"writeRegistersC"
    "ommand\":0,\"applyAddressAfterPowerCycle\":false,\"i2cAddress\":\"0x15\",\"chipType\":\"T6703\",\"currentVal"
    "ue\":812,\"lastReadTime\":1767524400,\"sensorTypeId\":7,\"sensorTypeUid\":\"type-uid-0011\",\"i2cRegisterC"
    "onfigs\":[{\"register\":4999,\"length\":2,\"multiplier\":1,\"offset\":0,\"channel_name\":\"co2_ppm\",\"channel"
    "\":1,\"sensorType\":\"co2\"}]},{\"uid\":\"sensor-uid-02\",\"sensorType\":\"air_temperature\",\"model\":\"rk330_0"
    "1\",\"protocol\":\"ModBus\",\"createdAt\":\"2026-01-04T10:00:00.000000+00:00\",\"isActive\":true,\"isError\":"
    "false,\"status\":\"added\",\"pin\":2,\"modbusDeviceAddress\":114,\"baudRate\":9600,\"modbusPingAddress\":256"
    ",\"registerToStoreModbusDeviceAddress\":512,\"writeRegistersCommand\":6,\"applyAddressAfterPowerCycle"
    "\":false,\"i2cAddress\":\"\",\"chipType\":\"\",\"currentValue\":21.5,\"lastReadTime\":1767520800,\"sensorTypeI"
    "d\":42,\"sensorTypeUid\":\"type-uid-0007\"}]";

static void test_the_stored_registry_format_did_not_move(void)
{
    RegistryFixture fixture;
    fixture.store(STORED_REGISTRY_OF_THE_PREVIOUS_BUILD);

    std::vector<SensorEntity> loaded;
    TEST_ASSERT_TRUE_MESSAGE(fixture.dao.getSensors(loaded),
                             "a registry file of the previous build could not be read");
    TEST_ASSERT_EQUAL_size_t(3, loaded.size());
    TEST_ASSERT_EQUAL_STRING("sensor-uid-00,sensor-uid-01,sensor-uid-02", uidsOf(loaded).c_str());
    TEST_ASSERT_EQUAL_size_t(2, loaded[0].modbusRegisterConfigs.size());
    TEST_ASSERT_EQUAL_size_t(1, loaded[1].i2cRegisterConfigs.size());
    TEST_ASSERT_EQUAL_STRING("0x15", loaded[1].i2cAddress.c_str());
    TEST_ASSERT_TRUE(loaded[2].modbusRegisterConfigs.empty());

    // And back out again: the same file, to the byte. A field that moved, a separator that changed
    // or a value formatted differently is a file the previous firmware would read as something
    // else.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        STORED_REGISTRY_OF_THE_PREVIOUS_BUILD, fixture.converter.sensorsToJson(loaded).c_str(),
        "the registry this build writes is not the registry the previous build wrote");

    // Through the storage path as well, so the bytes that reach flash are the ones compared above.
    TEST_ASSERT_TRUE(fixture.dao.saveSensors(loaded));
    TEST_ASSERT_EQUAL_STRING(STORED_REGISTRY_OF_THE_PREVIOUS_BUILD,
                             fixture.flash.getFileContent(REGISTRY_FILE).c_str());
}

// --------------------------------------------------------------------------------------------
// 5. What reading a registry costs.
//
// Measured with Sim/HostAllocationCounter.h, which counts every operator new the test binary makes
// and samples the live balance at each of them. The registry text is alive before the measured
// window, because the file is read into one string by the storage adapter and that string is not
// what this measures: what it measures is what building the sensors out of that text needs on top
// of the sensors themselves. Each scenario is measured at five sensors and at fifteen, and the
// verdict is the difference between the two - so nothing here rests on a magic number.
// --------------------------------------------------------------------------------------------

namespace {

struct PeakMeasurement {
    long long before = 0;
    long long after = 0;
    long long peak = 0;
    size_t sensorCount = 0;
    /** Bytes of one stored sensor as this module holds it, for the plank. */
    long long sensorBytes = 0;

    long long overhead() const
    {
        const long long floorBytes = (after > before) ? after : before;
        return peak - floorBytes;
    }
};

/** @brief Bytes a registry of `count` sensors occupies in RAM, measured rather than assumed. */
long long registryBytesFor(size_t count)
{
    const long long before = HostAllocationCounter::liveBytes();
    std::vector<SensorEntity> registry = makeRegistry(count, FIELD_CHANNELS);
    const long long after = HostAllocationCounter::liveBytes();
    return after - before;
}

/**
 * @brief What building a list of `count` sensors costs above holding it.
 *
 * A vector filled one sensor at a time reallocates its array as it grows, and both arrays are alive
 * for the length of that copy. Nobody can know the length of a registry before reading it, so this
 * is the floor under any reader that answers with a vector - and it belongs in the plank rather
 * than in the verdict, measured on the same registry the scenario runs on rather than guessed at.
 */
long long listGrowthBytesFor(size_t count)
{
    HostAllocationCounter::resetPeak();
    const long long before = HostAllocationCounter::liveBytes();
    std::vector<SensorEntity> registry;
    for (size_t i = 0; i < count; i++)
    {
        registry.push_back(makeModbusSensor(i, FIELD_CHANNELS));
    }
    const long long peak = HostAllocationCounter::peakBytes();
    const long long after = HostAllocationCounter::liveBytes();
    return peak - ((after > before) ? after : before);
}

/**
 * @brief Read a stored registry of `count` sensors and measure what it needed.
 *
 * @param holdOneRegistryInside the control: one copy of the registry alive inside the window and
 *        released before it closes - the shape of transient this measurement exists to forbid.
 */
PeakMeasurement measureRegistryRead(size_t count, bool holdOneRegistryInside = false)
{
    ArduinoJsonConverter converter;
    const std::string text = registryText(converter, makeRegistry(count, FIELD_CHANNELS));

    {
        // One read before the measured one. Whatever a first parse takes once - the document pool
        // claims its slots on first use - belongs to neither registry size and would be read as a
        // cost of the smaller one.
        std::vector<SensorEntity> warmup;
        converter.sensorsFromJson(text, warmup);
    }

    PeakMeasurement measurement;
    measurement.sensorBytes = registryBytesFor(1);

    std::vector<SensorEntity> sensors;

    HostAllocationCounter::resetPeak();
    measurement.before = HostAllocationCounter::liveBytes();
    {
        std::vector<SensorEntity> transientCopy;
        if (holdOneRegistryInside)
        {
            transientCopy = makeRegistry(count, FIELD_CHANNELS);
        }

        converter.sensorsFromJson(text, sensors);
        measurement.peak = HostAllocationCounter::peakBytes();
    }
    measurement.after = HostAllocationCounter::liveBytes();
    measurement.sensorCount = sensors.size();

    const size_t expected = count < SensorLimits::MAX_SENSORS_PER_MODULE
                                ? count
                                : SensorLimits::MAX_SENSORS_PER_MODULE;
    char message[224];
    snprintf(message, sizeof(message),
             "reading a stored registry of %u sensors (%u bytes) built %u of them, not %u - the "
             "measurement is of a read that did not happen",
             (unsigned)count, (unsigned)text.size(), (unsigned)sensors.size(), (unsigned)expected);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expected, sensors.size(), message);
    return measurement;
}

/**
 * @brief How much more the read may need at fifteen sensors than at five.
 *
 * One sensor - not one per sensor, one in total - plus the growth of the vector the sensors are
 * handed back in, which is measured rather than allowed for. What this refuses is anything else
 * proportional to the registry: the text held a second time, a document over the whole array, a
 * second copy of the sensors. Any of those is many kilobytes at the sizes measured here.
 */
long long growthBudget(long long sensorBytes)
{
    return sensorBytes + (listGrowthBytesFor(LARGE_REGISTRY) - listGrowthBytesFor(SMALL_REGISTRY));
}

void report(const char* what, const PeakMeasurement& small, const PeakMeasurement& large)
{
    printf("  registry %s: %u sensors needed %lld bytes above what they came to, %u sensors needed "
           "%lld (one stored sensor is %lld bytes)\n",
           what, (unsigned)small.sensorCount, small.overhead(), (unsigned)large.sensorCount,
           large.overhead(), large.sensorBytes);
}

} // namespace

static void test_reading_a_registry_costs_one_sensor_and_not_the_registry(void)
{
    const PeakMeasurement small = measureRegistryRead(SMALL_REGISTRY);
    const PeakMeasurement large = measureRegistryRead(LARGE_REGISTRY);
    report("read", small, large);

    const long long growth = large.overhead() - small.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "reading %u sensors needed %lld bytes above the sensors it built and reading %u needed "
             "%lld - %lld bytes more for %u sensors more, against a plank of %lld (one stored "
             "sensor is %lld bytes)",
             (unsigned)SMALL_REGISTRY, small.overhead(), (unsigned)LARGE_REGISTRY, large.overhead(),
             growth, (unsigned)(LARGE_REGISTRY - SMALL_REGISTRY), growthBudget(large.sensorBytes),
             large.sensorBytes);
    TEST_ASSERT_TRUE_MESSAGE(growth <= growthBudget(large.sensorBytes), message);
}

/**
 * And the measurement that is not about the box: a registry three times the module's capacity.
 * Nothing above the sensors it keeps may follow the length of the file - not the text, not a
 * document over it, and not the sensors past the cap, which are refused before they are built.
 */
static void test_reading_a_registry_past_the_capacity_costs_no_more_than_the_capacity(void)
{
    const PeakMeasurement small = measureRegistryRead(SMALL_REGISTRY);
    const PeakMeasurement huge = measureRegistryRead(REGISTRY_PAST_THE_CAP);
    report("read past the capacity", small, huge);

    const long long growth = huge.overhead() - small.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "a stored registry of %u sensors built %u of them and needed %lld bytes above them, "
             "against %lld for %u - %lld bytes more against a plank of %lld",
             (unsigned)REGISTRY_PAST_THE_CAP, (unsigned)huge.sensorCount, huge.overhead(),
             small.overhead(), (unsigned)SMALL_REGISTRY, growth, growthBudget(huge.sensorBytes));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(SensorLimits::MAX_SENSORS_PER_MODULE, huge.sensorCount,
                                     "the module built more sensors than it keeps");
    TEST_ASSERT_TRUE_MESSAGE(growth <= growthBudget(huge.sensorBytes), message);
}

/**
 * And the control on the two above: a window that cannot see a copy of the registry inside it
 * proves nothing by staying green.
 */
static void test_the_read_window_sees_a_registry_held_inside_it(void)
{
    const PeakMeasurement small = measureRegistryRead(SMALL_REGISTRY, true);
    const PeakMeasurement large = measureRegistryRead(LARGE_REGISTRY, true);
    report("read with one registry held inside the window", small, large);

    const long long growth = large.overhead() - small.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "a copy of the registry held inside the measured window moved it by %lld bytes at %u "
             "sensors and %lld at %u - a growth of %lld against a plank of %lld, so the window "
             "cannot see a transient of that size and its verdict means nothing",
             small.overhead(), (unsigned)SMALL_REGISTRY, large.overhead(), (unsigned)LARGE_REGISTRY,
             growth, growthBudget(large.sensorBytes));
    TEST_ASSERT_TRUE_MESSAGE(growth > growthBudget(large.sensorBytes), message);
}

extern "C" void run_sensor_registry_stream_read_tests(void)
{
    RUN_TEST(test_a_registry_larger_than_one_document_is_read_whole);
    RUN_TEST(test_a_registry_larger_than_one_document_is_written_whole);
    RUN_TEST(test_a_record_the_module_cannot_read_is_skipped_and_the_rest_stand);
    RUN_TEST(test_a_registry_past_the_capacity_keeps_what_fits_and_refuses_the_rest);
    RUN_TEST(test_the_stored_registry_format_did_not_move);
    RUN_TEST(test_reading_a_registry_costs_one_sensor_and_not_the_registry);
    RUN_TEST(test_reading_a_registry_past_the_capacity_costs_no_more_than_the_capacity);
    RUN_TEST(test_the_read_window_sees_a_registry_held_inside_it);
}
