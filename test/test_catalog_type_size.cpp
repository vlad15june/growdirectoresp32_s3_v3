// What one sensor model in the catalog may weigh, and what happens to the rest when one is broken.
//
// A sensor model is a few fixed fields and a few arrays whose length is the number of channels the
// model measures - roughly six hundred bytes of JSON per channel. Read into one document, the model
// with the most channels decides whether the catalog can be read at all: a model that does not fit
// the document does not parse, and a catalog holding one of those is a catalog the module refuses
// whole, from the network and from flash alike. The largest model in the field today is at about
// four fifths of that ceiling.
//
// So there are two claims here. That reading a model does not depend on how many channels it has -
// neither in whether it succeeds nor in what it costs while it runs - and that a model this module
// cannot read is left out of the catalog instead of taking the catalog with it.
//
// The second one has a boundary, and it is asserted as carefully as the claim itself: an answer
// that carried models and yielded none of them is an unreadable answer, never a server saying the
// account publishes no models. The two are the same empty vector and different instructions - the
// second retires everything the module holds.
//
// What runs is production code: the converter globals.h gives the device, its catalog reader, and
// the real transport with the socket replaced.

#include <unity.h>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Json/JsonMemoryPool.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"

#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/HostAllocationCounter.h"
#include "Sim/SimHttpClient.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::SimHttpClient;

namespace {

const char* const SERVER_URL = "http://gd.test";
const char* const TOKEN = "TOK123";
const char* const MODULE_UID = "UID-1";

const char* const CREATED_AT = "2025-09-22T07:04:26.803128+00:00";
const char* const UPDATED_AT = "2026-02-11T09:01:35.443520+00:00";

/** The channel counts the models in the field have, and the one the server is about to publish. */
const size_t NARROW_MODEL = 2;
const size_t WIDE_MODEL = 12;

/**
 * A model past the ceiling by a clear margin rather than by a hair.
 *
 * Six channels of this shape are about 4.4 KB of JSON and seven about 5.1 KB, and the size a model
 * stopped being readable at fell between the two - close enough that the length of the model's name
 * decided it. Eight leaves no room for that kind of doubt.
 */
const size_t OVER_THE_OLD_CEILING = 8;

/**
 * @brief A sensor model of the shape the backend publishes, with `channels` channels.
 *
 * One entry in `sensorTypes` and one Modbus register block per channel, which is how a multi-probe
 * model is described: SensorModelHwDto carries the two lists side by side.
 */
SensorType makeSensorModel(const std::string& uid, const std::string& name, size_t channels)
{
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " display name for the module structure";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.baudRate = 9600;
    sensorType.modbusDeviceAddress = 1;
    sensorType.createdAt = CREATED_AT;
    sensorType.updatedAt = UPDATED_AT;

    for (size_t channel = 1; channel <= channels; channel++)
    {
        const std::string channelName = name + "_channel_" + std::to_string(channel);

        SubSensorType subType;
        subType.name = channelName;
        subType.displayName = name + " channel " + std::to_string(channel);
        subType.minValue = 0.0f;
        subType.maxValue = 1000.0f;
        subType.unit = "unit_" + std::to_string(channel);
        subType.createdAt = CREATED_AT;
        subType.updatedAt = UPDATED_AT;
        sensorType.sensorTypes.push_back(subType);

        ModbusRegisterConfig config;
        config.functionCode = 3;
        config.startRegister = static_cast<int>(channel);
        config.numRegisters = 2;
        config.multiplier = 0.01f;
        config.channel = static_cast<int>(channel);
        config.channelName = channelName;
        config.sensorType = channelName;
        config.canConnectMinValue = 0.0f;
        config.canConnectMaxValue = 1000.0f;
        sensorType.modbusRegisterConfig.push_back(config);
    }

    return sensorType;
}

std::string modelJson(const std::string& uid, const std::string& name, size_t channels)
{
    ArduinoJsonConverter converter;
    const std::string json = converter.sensorTypeToJson(makeSensorModel(uid, name, channels));
    TEST_ASSERT_FALSE_MESSAGE(json.empty(), "the scenario could not build a sensor model");
    return json;
}

/** A complete object that is not a sensor model this module can read. */
const char* const UNREADABLE_TYPE = "{\"uid\":\"type-broken\",\"name\":\"broken\",\"displayName\":}";

/**
 * @brief A three-channel model whose middle channel is whatever text is given.
 *
 * Written out by hand rather than serialized, because what is being asked is what happens to an
 * array element the serializer would never produce.
 */
std::string modelWithMiddleChannel(const std::string& middleElement)
{
    std::string json = "{\"uid\":\"type-1\",\"name\":\"soil_probe\",\"displayName\":\"Soil probe\",";
    json += "\"protocol\":\"ModBus\",\"canScan\":true,\"isSupported\":true,\"baudRate\":9600,";
    json += "\"sensorTypes\":[";
    json += "{\"name\":\"ch1\",\"unit\":\"C\",\"limitMinValue\":-40,\"limitMaxValue\":80},";
    json += middleElement;
    json += ",{\"name\":\"ch3\",\"unit\":\"%\",\"limitMinValue\":0,\"limitMaxValue\":100}],";
    json += "\"modbusRegisterConfigs\":["
            "{\"functionCode\":3,\"startRegister\":1,\"channel\":1,\"channelName\":\"ch1\"},"
            "{\"functionCode\":3,\"startRegister\":2,\"channel\":2,\"channelName\":\"ch2\"},"
            "{\"functionCode\":3,\"startRegister\":3,\"channel\":3,\"channelName\":\"ch3\"}]}";
    return json;
}

std::string arrayOf(const std::string& a, const std::string& b = "", const std::string& c = "")
{
    std::string body = "[";
    body += a;
    if (!b.empty())
    {
        body += ",";
        body += b;
    }
    if (!c.empty())
    {
        body += ",";
        body += c;
    }
    body += "]";
    return body;
}

/** @brief The transport, with the socket replaced and nothing else. */
struct TransportFixture {
    MockModuleConfig moduleConfig;
    MockSettings settings;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ArduinoJsonConverter jsonConverter;
    SimHttpClient http;
    RestAPIService service;

    TransportFixture()
        : service(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter)
    {
        debugSystem.featureEnabled = true;
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        moduleConfig.setModuleUid(MODULE_UID);
        service.init();
        http.alwaysRespond(-1, "");
    }
};

// --- measuring what one parse needs -----------------------------------------------------------

/**
 * @brief What reading one model needed above the model it produced.
 *
 * `before` and `after` are the live balances the parse runs between; the model built out of the
 * text is the difference between them and is not what is being measured. `peak` is the largest
 * balance seen inside the parse, and peak minus the larger of the two ends is what the parse held
 * on top of its own result.
 */
struct ParseMeasurement {
    long long before = 0;
    long long after = 0;
    long long peak = 0;

    long long overhead() const
    {
        const long long floorBytes = (after > before) ? after : before;
        return peak - floorBytes;
    }
};

ParseMeasurement measureParse(const std::string& json, size_t expectedChannels,
                              bool holdACopyInside = false)
{
    ArduinoJsonConverter converter;

    {
        // One parse before the measured one: whatever a first parse takes once - the document pool
        // claims its slots on first use - belongs to neither model size.
        SensorType warmup;
        TEST_ASSERT_TRUE_MESSAGE(converter.sensorTypeFromJson(json, warmup),
                                 "the warm-up parse did not read the model");
    }

    SensorType sensorType;
    ParseMeasurement measurement;

    HostAllocationCounter::resetPeak();
    measurement.before = HostAllocationCounter::liveBytes();

    bool parsed = false;
    {
        // The control: one copy of the model's text, alive only inside the window - the shape of
        // transient this measurement exists to forbid. Off in the real measurement, on in the one
        // that checks the measurement can see such a thing at all.
        std::string transientCopy;
        if (holdACopyInside)
        {
            transientCopy = json;
        }

        parsed = converter.sensorTypeFromJson(json, sensorType);
        measurement.peak = HostAllocationCounter::peakBytes();
    }

    measurement.after = HostAllocationCounter::liveBytes();

    TEST_ASSERT_TRUE_MESSAGE(parsed, "the sensor model was not read - the scenario did not happen");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expectedChannels, sensorType.sensorTypes.size(),
                                     "the parse did not read every channel of the model");
    return measurement;
}

/** @brief Bytes one channel of a model occupies in RAM, measured rather than assumed. */
long long channelBytes()
{
    const long long before = HostAllocationCounter::liveBytes();
    std::vector<SensorType> models;
    models.push_back(makeSensorModel("uid-narrow", "sensor_model", NARROW_MODEL));
    const long long narrow = HostAllocationCounter::liveBytes() - before;
    models.push_back(makeSensorModel("uid-wide", "sensor_model", WIDE_MODEL));
    const long long wide = HostAllocationCounter::liveBytes() - before - narrow;
    return (wide - narrow) / static_cast<long long>(WIDE_MODEL - NARROW_MODEL);
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The model with the most channels may not decide whether the catalog can be read.
// --------------------------------------------------------------------------------------------

static void test_a_sensor_model_is_read_whatever_its_channel_count(void)
{
    // Up to twelve, and not further, because of a ceiling on the other side of the same entity:
    // sensorTypeToJson() builds a model in a document of 16 384 bytes, and past about fifteen
    // channels it silently stops adding them - so a wider model cannot be produced to read.
    const size_t channelCounts[] = {1, 2, 6, 7, 12};

    ArduinoJsonConverter converter;
    for (size_t i = 0; i < sizeof(channelCounts) / sizeof(channelCounts[0]); i++)
    {
        const size_t channels = channelCounts[i];
        const std::string json = modelJson("type-uid-1", "sensor_model", channels);
        printf("  a model with %u channels is %u bytes of JSON\n", (unsigned)channels,
               (unsigned)json.size());

        SensorType parsed;
        char message[224];
        snprintf(message, sizeof(message),
                 "a sensor model with %u channels (%u bytes of JSON) could not be read",
                 (unsigned)channels, (unsigned)json.size());
        TEST_ASSERT_TRUE_MESSAGE(converter.sensorTypeFromJson(json, parsed), message);

        TEST_ASSERT_EQUAL_size_t_MESSAGE(channels, parsed.sensorTypes.size(), message);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(channels, parsed.modbusRegisterConfig.size(), message);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("type-uid-1", parsed.uid.c_str(), message);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("sensor_model", parsed.name.c_str(), message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(9600, parsed.baudRate, message);
        TEST_ASSERT_TRUE_MESSAGE(parsed.protocol == SensorProtocol::MODBUS_PROTOCOL, message);

        // The last channel, because a parse that stops early stops there: a model read in part
        // would have its first channels and not its last.
        const std::string lastChannel = "sensor_model_channel_" + std::to_string(channels);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(lastChannel.c_str(),
                                         parsed.sensorTypes.back().name.c_str(), message);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(lastChannel.c_str(),
                                         parsed.modbusRegisterConfig.back().channelName.c_str(),
                                         message);
        TEST_ASSERT_EQUAL_FLOAT(1000.0f, parsed.sensorTypes.back().maxValue);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)channels, parsed.modbusRegisterConfig.back().channel,
                                      message);
    }
}

/**
 * And the catalog around it: a wide model in an answer is read alongside the narrow ones, through
 * the transport that reads the answer off the socket.
 */
static void test_the_transport_reads_a_catalog_holding_a_wide_model(void)
{
    TransportFixture f;
    const std::string body = arrayOf(modelJson("type-1", "narrow_model", NARROW_MODEL),
                                     modelJson("type-2", "wide_model", OVER_THE_OLD_CEILING),
                                     modelJson("type-3", "narrow_model_2", NARROW_MODEL));
    f.http.alwaysRespond(200, body.c_str());

    bool httpFailed = true;
    const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    char message[224];
    snprintf(message, sizeof(message),
             "a %u-byte catalog holding an eight-channel model was read as %u sensor types",
             (unsigned)body.size(), (unsigned)types.size());
    TEST_ASSERT_FALSE_MESSAGE(httpFailed, message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(3, types.size(), message);
    TEST_ASSERT_EQUAL_STRING("type-2", types[1].uid.c_str());
    TEST_ASSERT_EQUAL_size_t_MESSAGE(OVER_THE_OLD_CEILING, types[1].sensorTypes.size(),
                                     "the wide model lost channels on the way through the socket");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(OVER_THE_OLD_CEILING, types[1].modbusRegisterConfig.size(),
                                     "the wide model lost register blocks on the way");
}

/**
 * The catalog read back out of the stored file is the same reading: a model that a module received
 * over the network and wrote down has to be readable at the next boot, or the catalog is lost on a
 * restart instead of on a fetch.
 */
static void test_a_stored_catalog_holding_a_wide_model_is_read_back(void)
{
    ArduinoJsonConverter converter;
    std::vector<SensorType> written;
    written.push_back(makeSensorModel("type-1", "narrow_model", NARROW_MODEL));
    written.push_back(makeSensorModel("type-2", "wide_model", WIDE_MODEL));

    const std::string file = converter.sensorTypesToJson(written);
    TEST_ASSERT_FALSE_MESSAGE(file.empty(), "the catalog could not be serialized for storage");

    std::vector<SensorType> readBack;
    char message[224];
    snprintf(message, sizeof(message), "a stored catalog of %u bytes was not read back",
             (unsigned)file.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Ok),
                                  static_cast<int>(converter.sensorTypesFromJson(file, readBack)),
                                  message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, readBack.size(), message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(WIDE_MODEL, readBack[1].sensorTypes.size(),
                                     "the wide model lost channels on the way out of storage");
}

// --------------------------------------------------------------------------------------------
// 2. And what reading it costs, which may not follow the channel count either.
// --------------------------------------------------------------------------------------------

static void test_reading_a_model_costs_the_same_at_two_channels_and_at_twelve(void)
{
    const long long perChannel = channelBytes();

    const ParseMeasurement narrow =
        measureParse(modelJson("type-1", "sensor_model", NARROW_MODEL), NARROW_MODEL);
    const ParseMeasurement wide =
        measureParse(modelJson("type-1", "sensor_model", WIDE_MODEL), WIDE_MODEL);

    printf("  model parse: %u channels needed %lld bytes above the model, %u channels needed %lld "
           "(one channel is %lld bytes)\n",
           (unsigned)NARROW_MODEL, narrow.overhead(), (unsigned)WIDE_MODEL, wide.overhead(),
           perChannel);

    const long long growth = wide.overhead() - narrow.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "reading a %u-channel model needed %lld bytes above the model it built and reading a "
             "%u-channel one needed %lld - %lld bytes more for %u channels more, which is the "
             "model being held whole (one channel is %lld bytes)",
             (unsigned)NARROW_MODEL, narrow.overhead(), (unsigned)WIDE_MODEL, wide.overhead(),
             growth, (unsigned)(WIDE_MODEL - NARROW_MODEL), perChannel);
    TEST_ASSERT_TRUE_MESSAGE(growth <= perChannel, message);
}

/**
 * And the control on the measurement above: a window that cannot see the model held inside it
 * proves nothing by staying green.
 */
static void test_the_parse_window_sees_a_model_held_inside_it(void)
{
    const long long perChannel = channelBytes();

    const ParseMeasurement narrow =
        measureParse(modelJson("type-1", "sensor_model", NARROW_MODEL), NARROW_MODEL, true);
    const ParseMeasurement wide =
        measureParse(modelJson("type-1", "sensor_model", WIDE_MODEL), WIDE_MODEL, true);

    const long long growth = wide.overhead() - narrow.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "a copy of the model's text held inside the measured window moved it by %lld bytes at "
             "%u channels and %lld at %u - a growth of %lld against a plank of %lld, so the window "
             "cannot see a transient of that size and its verdict means nothing",
             narrow.overhead(), (unsigned)NARROW_MODEL, wide.overhead(), (unsigned)WIDE_MODEL,
             growth, perChannel);
    TEST_ASSERT_TRUE_MESSAGE(growth > perChannel, message);
}

/**
 * What the parse asks the document pool for, said the only way the pool can be asked: with the two
 * large slots taken by something else.
 *
 * The pool's slots are static memory, so no allocation counter sees a document at all - what a
 * parse needs is visible only in which slot it takes. A parse that needs a document the size of the
 * largest model has to have the 8 KB slot or the 16 KB one, and with both held it falls out of the
 * pool onto the heap, at the moment the module is least able to give it one.
 */
static void test_reading_a_wide_model_needs_none_of_the_large_documents(void)
{
    ArduinoJsonConverter converter;
    const std::string json = modelJson("type-1", "sensor_model", WIDE_MODEL);

    SensorType warmup;
    converter.sensorTypeFromJson(json, warmup);

    JsonMemoryPool& pool = JsonMemoryPool::instance();
    const uint32_t fallbacksBefore = pool.heapFallbacks();

    SensorType parsed;
    bool read = false;
    {
        // The two largest slots, held by the serialization and by another task's telemetry.
        PooledJsonDocument largest(16384);
        PooledJsonDocument large(8192);
        largest["uid"] = MODULE_UID;
        large["uid"] = MODULE_UID;

        read = converter.sensorTypeFromJson(json, parsed);
    }

    char message[288];
    snprintf(message, sizeof(message),
             "reading a %u-channel model (%u bytes of JSON) with the 8 KB and 16 KB slots held "
             "left the pool %u times for the heap",
             (unsigned)WIDE_MODEL, (unsigned)json.size(),
             (unsigned)(pool.heapFallbacks() - fallbacksBefore));
    TEST_ASSERT_TRUE_MESSAGE(read, message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(WIDE_MODEL, parsed.sensorTypes.size(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fallbacksBefore, pool.heapFallbacks(), message);
}

// --------------------------------------------------------------------------------------------
// 3. One model this module cannot read is one model, not the catalog.
// --------------------------------------------------------------------------------------------

static void test_a_model_that_cannot_be_read_leaves_the_rest_of_the_catalog(void)
{
    const std::string body = arrayOf(modelJson("type-1", "first_model", NARROW_MODEL),
                                     UNREADABLE_TYPE,
                                     modelJson("type-3", "third_model", NARROW_MODEL));

    ArduinoJsonConverter converter;
    std::vector<SensorType> types;
    const JsonListParse verdict = converter.sensorTypesFromJson(body, types);

    char message[224];
    snprintf(message, sizeof(message),
             "a catalog of three models, one of them unreadable, was read as %d with %u models",
             static_cast<int>(verdict), (unsigned)types.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Ok), static_cast<int>(verdict),
                                  message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, types.size(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("type-1", types[0].uid.c_str(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("type-3", types[1].uid.c_str(), message);
}

/** The same answer arriving over the socket, and the count of what was left out in the log. */
static void test_the_transport_reports_the_models_it_could_not_read(void)
{
    TransportFixture f;
    const std::string body = arrayOf(modelJson("type-1", "first_model", NARROW_MODEL),
                                     UNREADABLE_TYPE,
                                     modelJson("type-3", "third_model", NARROW_MODEL));
    f.http.alwaysRespond(200, body.c_str());

    bool httpFailed = true;
    const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    TEST_ASSERT_FALSE_MESSAGE(httpFailed,
                              "one unreadable model made the whole answer a transport failure");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, types.size(),
                                     "the models around the unreadable one were dropped too");
    TEST_ASSERT_EQUAL_STRING("type-1", types[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("type-3", types[1].uid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("could not be read and were skipped"),
                             "a catalog was quietly one model short");
}

/**
 * And the boundary of that. An answer that carried models and yielded none of them is unreadable,
 * not empty: read as empty it says the account publishes no sensor models, the merge retires
 * everything the module holds, and a module empties its catalog because it could not read it.
 */
static void test_an_answer_yielding_no_readable_model_is_not_an_empty_catalog(void)
{
    const std::string body = arrayOf(UNREADABLE_TYPE, UNREADABLE_TYPE);

    ArduinoJsonConverter converter;
    std::vector<SensorType> types;
    const JsonListParse verdict = converter.sensorTypesFromJson(body, types);

    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Failed),
                                  static_cast<int>(verdict),
                                  "a body of unreadable models was accepted as a catalog");
    TEST_ASSERT_EQUAL_size_t(0, types.size());

    TransportFixture f;
    f.http.alwaysRespond(200, body.c_str());
    bool httpFailed = false;
    const std::vector<SensorType> fetched = f.service.getSensorTypesList(&httpFailed);
    TEST_ASSERT_TRUE_MESSAGE(httpFailed,
                             "an answer with no readable model was reported as a read catalog");
    TEST_ASSERT_EQUAL_size_t(0, fetched.size());
}

/**
 * A body that stops in the middle is still the whole answer failing, and that has not changed: the
 * models before the cut are the beginning of a catalog, and handing them on as the catalog retires
 * every model that did not arrive.
 */
static void test_a_body_that_stops_in_the_middle_still_fails_whole(void)
{
    const std::string whole = arrayOf(modelJson("type-1", "first_model", NARROW_MODEL),
                                      modelJson("type-2", "second_model", NARROW_MODEL));

    ArduinoJsonConverter converter;
    std::vector<SensorType> types;
    const JsonListParse verdict =
        converter.sensorTypesFromJson(whole.substr(0, whole.size() - 200), types);

    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Failed),
                                  static_cast<int>(verdict),
                                  "a truncated catalog was read as a shorter one");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, types.size(),
                                     "the models read before the body stopped were kept");
}

// --------------------------------------------------------------------------------------------
// 4. What the reader must never do: hand up a model it did not finish reading.
// --------------------------------------------------------------------------------------------

/**
 * A member the entity has no field for is walked past rather than read, and walking past it counts
 * brackets. On a body whose brackets do not balance, that count can reach zero before the object
 * does - and the member walk would then be standing in the middle of the object believing it had
 * reached the end of it.
 *
 * What comes of that is the one outcome this reader may not produce: not a refusal, not a skipped
 * model, but a model. A real uid, a real name, and none of the channels that were still to come -
 * which the merge is happy to write over the good copy the module holds.
 *
 * The object's end belongs to the reader that cut it out. This asserts that the two agree.
 */
static void test_a_member_with_unbalanced_brackets_does_not_yield_a_model(void)
{
    // The whole model, with one unreadable member in front of its channels.
    std::string broken = modelJson("type-1", "soil_probe", 4);
    const std::string unbalanced = "{\"uid\":\"type-1\",\"extra\":{\"a\":]},";
    broken.replace(0, std::string("{\"uid\":\"type-1\",").size(), unbalanced);

    {
        TransportFixture f;
        f.http.alwaysRespond(200, arrayOf(broken).c_str());

        bool httpFailed = false;
        const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

        char message[288];
        snprintf(message, sizeof(message),
                 "a body whose only model stopped being readable in the middle was read as %u "
                 "models; the first of them has %u channels",
                 (unsigned)types.size(),
                 (unsigned)(types.empty() ? 0 : types[0].sensorTypes.size()));
        TEST_ASSERT_TRUE_MESSAGE(httpFailed, message);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(0, types.size(), message);
        TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("could not be read and were skipped"),
                                 "the model was dropped without being counted");
    }

    // And in company: the models around it are read, it is not.
    {
        TransportFixture f;
        const std::string body = arrayOf(modelJson("type-a", "first_model", NARROW_MODEL), broken,
                                         modelJson("type-c", "third_model", NARROW_MODEL));
        f.http.alwaysRespond(200, body.c_str());

        bool httpFailed = true;
        const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

        TEST_ASSERT_FALSE_MESSAGE(httpFailed, "one unreadable model failed the whole answer");
        TEST_ASSERT_EQUAL_size_t_MESSAGE(2, types.size(),
                                         "a model that was not finished was handed up anyway");
        TEST_ASSERT_EQUAL_STRING("type-a", types[0].uid.c_str());
        TEST_ASSERT_EQUAL_STRING("type-c", types[1].uid.c_str());
    }
}

// --------------------------------------------------------------------------------------------
// 5. What a member this firmware has no field for may cost: nothing.
// --------------------------------------------------------------------------------------------

/**
 * The backend adds a field to its sensor model from time to time, and the module has no say in
 * what it is called or how long it is. A field the entity does not carry may therefore not be able
 * to make the models unreadable - if it could, one server-side edit would freeze every module's
 * catalog at once, and the only trace would be a line under a debug feature.
 */
static void test_an_unknown_member_costs_the_model_nothing(void)
{
    const std::string longName(200, 'k');
    const std::string longValue(4000, 'v');

    std::string model = modelJson("type-1", "sensor_model", NARROW_MODEL);
    std::string additions = "{";
    additions += "\"" + longName + "\":\"" + longValue + "\",";
    additions += "\"description\":\"" + longValue + "\",";
    additions += "\"localization\":{\"en\":\"" + longValue + "\"},";
    additions += "\"tags\":[\"" + longValue + "\"],";
    model.replace(0, 1, additions);

    ArduinoJsonConverter converter;
    SensorType parsed;

    char message[288];
    snprintf(message, sizeof(message),
             "a model of %u bytes carrying four fields this firmware has no use for (one of them "
             "named with %u characters) could not be read",
             (unsigned)model.size(), (unsigned)longName.size());
    TEST_ASSERT_TRUE_MESSAGE(converter.sensorTypeFromJson(model, parsed), message);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("type-1", parsed.uid.c_str(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("sensor_model", parsed.name.c_str(), message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(NARROW_MODEL, parsed.sensorTypes.size(), message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(NARROW_MODEL, parsed.modbusRegisterConfig.size(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(9600, parsed.baudRate, message);
}

/**
 * And the boundary that is left: the fields the entity does carry. Their number is fixed by the
 * published shape and a real model spends about seven hundred bytes on them, so the cap is where a
 * model stops being a model - but it is a cap, and this is where it stands.
 */
static void test_a_field_the_entity_carries_is_capped(void)
{
    ArduinoJsonConverter converter;

    SensorType roomy = makeSensorModel("type-1", "sensor_model", NARROW_MODEL);
    roomy.displayName = std::string(1000, 'x');
    SensorType parsedRoomy;
    TEST_ASSERT_TRUE_MESSAGE(converter.sensorTypeFromJson(converter.sensorTypeToJson(roomy),
                                                          parsedRoomy),
                             "a model with a kilobyte of display name was refused");
    TEST_ASSERT_EQUAL_size_t(1000, parsedRoomy.displayName.size());

    SensorType oversized = makeSensorModel("type-1", "sensor_model", NARROW_MODEL);
    oversized.displayName = std::string(3000, 'x');
    SensorType parsedOversized;
    TEST_ASSERT_FALSE_MESSAGE(converter.sensorTypeFromJson(converter.sensorTypeToJson(oversized),
                                                           parsedOversized),
                              "a model past the cap on its own fields was read in part");
}

/**
 * Every field of the entity, read back after a round trip.
 *
 * The reader knows which members belong to this entity by name, and that list sits beside the code
 * that reads them. A field added to one and not to the other reads as its default for ever, in
 * silence - this is what turns that into a red test.
 */
static void test_every_field_the_entity_carries_survives_the_read(void)
{
    SensorType written = makeSensorModel("type-uid-9", "sensor_model", NARROW_MODEL);
    written.displayName = "Display name of the model";
    written.canScan = false;
    written.isSupported = false;
    written.protocol = SensorProtocol::I2C_PROTOCOL;
    written.i2cAddress = "0x44";
    written.chipType = "SHT31";
    written.modbusDeviceAddress = 17;
    written.baudRate = 19200;
    written.modbusPingAddress = 258;
    written.registerToStoreModbusDeviceAddress = 2000;
    written.writeRegistersCommand = 16;
    written.applyAddressAfterPowerCycle = true;
    written.priorityForScan = 7;
    written.id = 42;

    ArduinoJsonConverter converter;
    const std::string json = converter.sensorTypeToJson(written);
    TEST_ASSERT_FALSE(json.empty());

    SensorType read;
    TEST_ASSERT_TRUE_MESSAGE(converter.sensorTypeFromJson(json, read),
                             "a model with every field set could not be read back");

    TEST_ASSERT_EQUAL_STRING("type-uid-9", read.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("sensor_model", read.name.c_str());
    TEST_ASSERT_EQUAL_STRING("Display name of the model", read.displayName.c_str());
    TEST_ASSERT_FALSE_MESSAGE(read.canScan, "canScan did not survive the read");
    TEST_ASSERT_FALSE_MESSAGE(read.isSupported, "isSupported did not survive the read");
    TEST_ASSERT_TRUE_MESSAGE(read.protocol == SensorProtocol::I2C_PROTOCOL,
                             "protocol did not survive the read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(CREATED_AT, read.createdAt.c_str(), "createdAt");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(UPDATED_AT, read.updatedAt.c_str(), "updatedAt");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0x44", read.i2cAddress.c_str(), "i2cAddress");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("SHT31", read.chipType.c_str(), "chipType");
    TEST_ASSERT_EQUAL_INT_MESSAGE(17, read.modbusDeviceAddress, "modbusDeviceAddress");
    TEST_ASSERT_EQUAL_INT_MESSAGE(19200, read.baudRate, "baudRate");
    TEST_ASSERT_EQUAL_INT_MESSAGE(258, read.modbusPingAddress, "modbusPingAddress");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2000, read.registerToStoreModbusDeviceAddress,
                                  "registerToStoreModbusDeviceAddress");
    TEST_ASSERT_EQUAL_INT_MESSAGE(16, read.writeRegistersCommand, "writeRegistersCommand");
    TEST_ASSERT_TRUE_MESSAGE(read.applyAddressAfterPowerCycle, "applyAddressAfterPowerCycle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, read.priorityForScan, "priorityForScan");
    TEST_ASSERT_EQUAL_INT_MESSAGE(42, read.id, "id");
}

// --------------------------------------------------------------------------------------------
// 6. And the same accounting on the way out of storage.
// --------------------------------------------------------------------------------------------

/**
 * A stored catalog with one model in it that cannot be read comes back one model short, and that
 * has to be said out loud. The file is written once and read for the rest of the module's life; a
 * catalog quietly losing a model at every boot is a module whose sensors stop being recognized for
 * no reason anyone can see.
 */
static void test_the_stored_catalog_reports_the_models_it_could_not_read(void)
{
    MockSpiffsAdapter flash;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ArduinoJsonConverter converter;
    debugSystem.featureEnabled = true;

    const std::string stored = arrayOf(modelJson("type-1", "first_model", NARROW_MODEL),
                                       UNREADABLE_TYPE,
                                       modelJson("type-3", "third_model", NARROW_MODEL));
    TEST_ASSERT_TRUE(flash.rewriteToFile("/data/sensorTypes.json", stored.c_str()));

    SpiffsSensorTypeDao dao(&flash, &converter, &debugSystem, &logger);
    std::vector<SensorType> read;

    TEST_ASSERT_TRUE_MESSAGE(dao.getAll(read), "a stored catalog with one bad model was refused");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, read.size(), "the models around the bad one were dropped");
    TEST_ASSERT_EQUAL_STRING("type-1", read[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("type-3", read[1].uid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(logger.hasMessageContaining("could not be read and were skipped"),
                             "the stored catalog came back a model short and said nothing");
}

// --------------------------------------------------------------------------------------------
// 7. An array of objects that holds something else. A model may not come out of it shorter.
// --------------------------------------------------------------------------------------------

/**
 * A channel that is not an object costs the model that channel - and a channel lost is not a
 * cosmetic loss. Channels are matched to their limits by name
 * (SensorPlausibilityUseCase::subSensorTypeRange), so a reading whose channel has no entry left is
 * a reading with no plausibility limits at all; and where the loss brings a two-channel model down
 * to one, the "a single sub-type must be the one being read" shortcut hands the survivor another
 * channel's limits.
 *
 * Nothing about the JSON here is malformed - `[{..},null,{..}]` is a valid document - so no other
 * guard in this reader has anything to say about it. The same array is already refused when it
 * holds no object at all, and the same element is already refused when it is an object that cannot
 * be read; this is the one case in between, and it may not be the silent one.
 */
static void test_a_channel_that_is_not_an_object_does_not_shorten_the_model(void)
{
    const char* const elements[] = {"null", "\"ch2\"", "7", "[]", "true"};

    ArduinoJsonConverter converter;
    for (size_t i = 0; i < sizeof(elements) / sizeof(elements[0]); i++)
    {
        const std::string json = modelWithMiddleChannel(elements[i]);

        SensorType parsed;
        const bool read = converter.sensorTypeFromJson(json, parsed);

        char message[288];
        snprintf(message, sizeof(message),
                 "a model whose middle channel is %s was read with %u channels and %u register "
                 "blocks - the model the server sent has three of each",
                 elements[i], (unsigned)parsed.sensorTypes.size(),
                 (unsigned)parsed.modbusRegisterConfig.size());

        // Refused, and therefore counted as a model that could not be read - not quietly two
        // channels wide.
        TEST_ASSERT_FALSE_MESSAGE(read, message);

        std::vector<SensorType> types;
        size_t skipped = 0;
        const JsonListParse verdict =
            converter.sensorTypesFromJson(arrayOf(json), types, skipped);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Failed),
                                      static_cast<int>(verdict), message);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(0, types.size(), message);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, skipped, "the model was dropped without being counted");
    }

    // And in a catalog: the models around it are read, it is counted, and the transport says so.
    TransportFixture f;
    const std::string body = arrayOf(modelJson("type-a", "first_model", NARROW_MODEL),
                                     modelWithMiddleChannel("null"),
                                     modelJson("type-c", "third_model", NARROW_MODEL));
    f.http.alwaysRespond(200, body.c_str());

    bool httpFailed = true;
    const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    TEST_ASSERT_FALSE_MESSAGE(httpFailed, "one model with a bad channel failed the whole answer");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, types.size(),
                                     "a model was handed up with a channel missing");
    TEST_ASSERT_EQUAL_STRING("type-a", types[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("type-c", types[1].uid.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("could not be read and were skipped"),
                             "the model with a bad channel was dropped in silence");
}

/**
 * The same rule one level up. An entry of the catalog that is not an object is not a catalog with
 * one entry fewer: the merge retires every model the answer does not carry, so a body read that way
 * takes models off the module because the answer had a stray value in it.
 */
static void test_a_catalog_entry_that_is_not_an_object_is_not_a_shorter_catalog(void)
{
    ArduinoJsonConverter converter;

    const std::string body = "[" + modelJson("type-1", "first_model", NARROW_MODEL) + ",null," +
                             modelJson("type-3", "third_model", NARROW_MODEL) + "]";

    std::vector<SensorType> types;
    const JsonListParse verdict = converter.sensorTypesFromJson(body, types);

    char message[224];
    snprintf(message, sizeof(message),
             "a catalog of two models with a stray null between them was read as %d with %u models",
             static_cast<int>(verdict), (unsigned)types.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Failed),
                                  static_cast<int>(verdict), message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, types.size(), message);

    // Whatever is left over after an entry is not an entry either.
    std::vector<SensorType> trailing;
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        static_cast<int>(JsonListParse::Failed),
        static_cast<int>(converter.sensorTypesFromJson(
            "[" + modelJson("type-1", "first_model", NARROW_MODEL) + " junk]", trailing)),
        "an entry followed by something that is not one was read as a catalog");
    TEST_ASSERT_EQUAL_size_t(0, trailing.size());
}

extern "C" int run_catalog_type_size_tests(void)
{
    RUN_TEST(test_a_sensor_model_is_read_whatever_its_channel_count);
    RUN_TEST(test_the_transport_reads_a_catalog_holding_a_wide_model);
    RUN_TEST(test_a_stored_catalog_holding_a_wide_model_is_read_back);
    RUN_TEST(test_reading_a_model_costs_the_same_at_two_channels_and_at_twelve);
    RUN_TEST(test_the_parse_window_sees_a_model_held_inside_it);
    RUN_TEST(test_reading_a_wide_model_needs_none_of_the_large_documents);
    RUN_TEST(test_a_model_that_cannot_be_read_leaves_the_rest_of_the_catalog);
    RUN_TEST(test_the_transport_reports_the_models_it_could_not_read);
    RUN_TEST(test_an_answer_yielding_no_readable_model_is_not_an_empty_catalog);
    RUN_TEST(test_a_body_that_stops_in_the_middle_still_fails_whole);
    RUN_TEST(test_a_member_with_unbalanced_brackets_does_not_yield_a_model);
    RUN_TEST(test_an_unknown_member_costs_the_model_nothing);
    RUN_TEST(test_a_field_the_entity_carries_is_capped);
    RUN_TEST(test_every_field_the_entity_carries_survives_the_read);
    RUN_TEST(test_the_stored_catalog_reports_the_models_it_could_not_read);
    RUN_TEST(test_a_channel_that_is_not_an_object_does_not_shorten_the_model);
    RUN_TEST(test_a_catalog_entry_that_is_not_an_object_is_not_a_shorter_catalog);
    return 0;
}
