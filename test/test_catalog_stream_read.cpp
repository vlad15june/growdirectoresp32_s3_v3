// Reading the sensor type catalog off the socket instead of out of one buffer.
//
// The transport reads the body in the pieces the socket delivers, not as one String handed to the
// converter, and where a piece boundary
// falls is the socket's business: a reader that only works when an object arrives whole is a reader
// that works only in a test. So every body below is read at several piece sizes, including one byte,
// and read whole through the converter, and all of them have to come out the same - the same types,
// and the same verdict on a body that is empty, unreadable or cut in half.
//
// Those verdicts are not cosmetic. "The account has no sensor models" and "this answer could not be
// read" are answered differently by everything above the transport, and telling them apart is what
// SyncSensorTypesUseCase::handleEmptyServerResponse() exists for.
//
// The whole-body path is the same reader driven from a string rather than a second implementation
// of it - one reading of one format, because the stored file and the answer are that format. What
// the comparison still carries is the piece boundaries: the reader has to reach the same verdict
// whatever the socket hands it in.

#include <unity.h>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Json/SensorTypeStreamReader.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"

#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"

#include "Sim/SimHttpClient.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::SimHttpClient;

namespace {

const char* const SERVER_URL = "http://gd.test";
const char* const TOKEN = "TOK123";
const char* const MODULE_UID = "UID-1";

/** A complete sensor type, as the backend publishes one. */
const char* const ONE_TYPE =
    "{\"uid\":\"type-1\",\"name\":\"soil_probe\",\"displayName\":\"Soil probe\","
    "\"protocol\":\"ModBus\",\"canScan\":true,\"isSupported\":true,\"baudRate\":9600,"
    "\"updatedAt\":\"2026-02-11T09:01:35.443520+00:00\","
    "\"sensorTypes\":[{\"name\":\"moisture\",\"unit\":\"%\",\"minValue\":0,\"maxValue\":100}],"
    "\"modbusRegisterConfigs\":[{\"functionCode\":3,\"startRegister\":1,\"numRegisters\":2,"
    "\"multiplier\":0.1,\"channel\":1,\"channelName\":\"moisture\",\"sensorType\":\"moisture\","
    "\"dataType\":\"u16\"}]}";

/** And one whose text holds the characters the cutting has to survive: braces and escaped quotes. */
const char* const AWKWARD_TYPE =
    "{\"uid\":\"type-2\",\"name\":\"probe_{2}\",\"displayName\":\"a \\\"quoted\\\" }brace{ name\","
    "\"protocol\":\"ModBus\",\"canScan\":true,\"isSupported\":true,"
    "\"updatedAt\":\"2026-02-11T09:01:35.443520+00:00\","
    "\"sensorTypes\":[{\"name\":\"level\",\"unit\":\"m\"}],"
    "\"modbusRegisterConfigs\":[]}";

std::string arrayOf(const std::string& first, const std::string& second = "")
{
    std::string body = "[";
    body += first;
    if (!second.empty())
    {
        body += ",";
        body += second;
    }
    body += "]";
    return body;
}

/** @brief Read a body as a stream cut into pieces of `pieceBytes`. */
JsonListParse readStreamed(const std::string& body,
                           size_t pieceBytes,
                           std::vector<SensorType>& sensorTypes)
{
    SensorTypeStreamReader reader(sensorTypes);
    for (size_t offset = 0; offset < body.size(); offset += pieceBytes)
    {
        const size_t piece = (pieceBytes < body.size() - offset) ? pieceBytes : body.size() - offset;
        if (!reader.write(body.data() + offset, piece))
        {
            break;
        }
    }
    return reader.finish();
}

const char* verdictName(JsonListParse verdict)
{
    switch (verdict)
    {
    case JsonListParse::Ok:
        return "Ok";
    case JsonListParse::Empty:
        return "Empty";
    default:
        return "Failed";
    }
}

/**
 * @brief The same body, read whole and read as a stream, has to come out the same.
 *
 * Piece sizes: one byte (every boundary there is), a prime that lands inside keys and values, the
 * ~1.4 KB the framework reads a socket in, and the whole body at once.
 */
void assertStreamMatchesWholeBody(const std::string& body, const char* what)
{
    const size_t pieceSizes[] = {1, 7, 64, 1460, 65536};

    ArduinoJsonConverter deviceConverter;
    std::vector<SensorType> whole;
    const JsonListParse wholeVerdict = deviceConverter.sensorTypesFromJson(body, whole);

    for (size_t i = 0; i < sizeof(pieceSizes) / sizeof(pieceSizes[0]); i++)
    {
        std::vector<SensorType> streamed;
        const JsonListParse streamedVerdict =
            readStreamed(body, pieceSizes[i], streamed);

        char message[256];
        snprintf(message, sizeof(message),
                 "%s in %u-byte pieces was read as %s, the whole body was read as %s", what,
                 (unsigned)pieceSizes[i], verdictName(streamedVerdict), verdictName(wholeVerdict));
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(wholeVerdict),
                                      static_cast<int>(streamedVerdict), message);

        snprintf(message, sizeof(message),
                 "%s in %u-byte pieces yielded %u sensor types, the whole body yielded %u", what,
                 (unsigned)pieceSizes[i], (unsigned)streamed.size(), (unsigned)whole.size());
        TEST_ASSERT_EQUAL_size_t_MESSAGE(whole.size(), streamed.size(), message);

        for (size_t t = 0; t < whole.size(); t++)
        {
            snprintf(message, sizeof(message), "%s: type %u differs in %u-byte pieces", what,
                     (unsigned)t, (unsigned)pieceSizes[i]);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(whole[t].uid.c_str(), streamed[t].uid.c_str(), message);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(whole[t].name.c_str(), streamed[t].name.c_str(),
                                             message);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(whole[t].displayName.c_str(),
                                             streamed[t].displayName.c_str(), message);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(whole[t].sensorTypes.size(),
                                             streamed[t].sensorTypes.size(), message);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(whole[t].modbusRegisterConfig.size(),
                                             streamed[t].modbusRegisterConfig.size(), message);
        }
    }
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

} // namespace

// --- the same reading, whichever way the body arrives ------------------------------------------

static void test_a_streamed_catalog_reads_the_same_as_a_whole_one(void)
{
    assertStreamMatchesWholeBody(arrayOf(ONE_TYPE), "one sensor type");
    assertStreamMatchesWholeBody(arrayOf(ONE_TYPE, AWKWARD_TYPE), "two sensor types");
    assertStreamMatchesWholeBody("  " + arrayOf(ONE_TYPE) + "  ", "a body with blanks around it");
    assertStreamMatchesWholeBody("[ ]", "an empty catalog");
    assertStreamMatchesWholeBody("[]", "an empty catalog without a blank");
    assertStreamMatchesWholeBody("", "no body at all");
    assertStreamMatchesWholeBody("not json", "a body that is not JSON");
    assertStreamMatchesWholeBody("[1,2,3]", "an array holding no objects");
    // Separators and nothing else. The dangerous reading is Empty - it travels on as "the account
    // has no sensor models", the synchronization is called successful and the module stops asking.
    assertStreamMatchesWholeBody("[,]", "an array of one separator");
    assertStreamMatchesWholeBody("[ , , ]", "an array of separators and blanks");
    assertStreamMatchesWholeBody("{\"uid\":\"type-1\"}", "an object where a list was expected");
    assertStreamMatchesWholeBody(arrayOf(ONE_TYPE).substr(0, 40), "a body cut inside a type");
    assertStreamMatchesWholeBody("[" + std::string(ONE_TYPE), "an array that never closes");
    assertStreamMatchesWholeBody(arrayOf(ONE_TYPE) + " and then some",
                                 "a body that goes on after the array");
    assertStreamMatchesWholeBody(arrayOf(ONE_TYPE) + "\r\n", "a body ending in a line break");
}

/**
 * A body that stops inside a sensor type fails the whole answer. The types read before the cut are
 * the beginning of a catalog, and handing them on would hand the merge a catalog missing everything
 * that did not arrive - the account would lose sensor models because a connection dropped.
 *
 * A type that arrives whole and cannot be read is a different thing and is left out on its own;
 * test_catalog_type_size.cpp is where that lives.
 */
static void test_a_body_that_stops_inside_a_type_fails_the_whole_answer(void)
{
    const std::string body = arrayOf(ONE_TYPE, "{\"uid\":\"type-2\",\"name\":");

    std::vector<SensorType> streamed;
    const JsonListParse verdict = readStreamed(body, 13, streamed);

    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Failed),
                                  static_cast<int>(verdict),
                                  "a body that stopped inside a type was accepted");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, streamed.size(),
                                     "the types read before the body stopped were kept");
}

/**
 * How long one object may run before the body is called nonsense. Nothing accumulates the object -
 * its fields and its array elements are read as they go past - so this bounds a run of bytes rather
 * than a buffer, and reaching it fails the body: an object that never ends is not a shorter one.
 */
static void test_an_object_larger_than_the_cap_fails_the_body(void)
{
    // Long because of what it holds, not because of any one field of it: a run of ordinary
    // channels, each of them small enough to read. Nothing but the cap on the object itself can
    // end this one.
    std::string huge = "{\"uid\":\"type-1\",\"name\":\"pad\",\"sensorTypes\":[";
    while (huge.size() < SensorTypeStreamReader::MAX_TYPE_BYTES + 100)
    {
        if (huge[huge.size() - 1] != '[')
        {
            huge += ",";
        }
        huge += "{\"name\":\"channel\",\"unit\":\"C\",\"step\":1}";
    }
    huge += "]}";

    std::vector<SensorType> streamed;
    const JsonListParse verdict = readStreamed(arrayOf(huge), 256, streamed);

    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(JsonListParse::Failed),
                                  static_cast<int>(verdict),
                                  "an object past the cap was read anyway");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, streamed.size(), "a capped body still yielded types");
}

// --- and what the transport makes of it --------------------------------------------------------

static void test_the_transport_reports_a_catalog_it_read(void)
{
    TransportFixture f;
    f.http.alwaysRespond(200, arrayOf(ONE_TYPE, AWKWARD_TYPE).c_str());

    bool httpFailed = true;
    const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    TEST_ASSERT_FALSE_MESSAGE(httpFailed, "a catalog that was read was reported as a failure");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, types.size(), "the transport did not read both types");
    TEST_ASSERT_EQUAL_STRING("type-1", types[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("type-2", types[1].uid.c_str());
}

/**
 * An account with no sensor models is an answer, and the module acts on it - it keeps whatever it
 * has stored and calls the synchronization successful. Reported as a transport failure it would
 * cost a retry budget and a failed sync, every time, for ever.
 */
static void test_an_empty_catalog_is_an_answer_and_not_a_failure(void)
{
    TransportFixture f;
    f.http.alwaysRespond(200, "[]");

    bool httpFailed = true;
    const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    TEST_ASSERT_FALSE_MESSAGE(httpFailed, "an empty catalog was reported as a transport failure");
    TEST_ASSERT_EQUAL_size_t(0, types.size());
}

static void test_a_200_with_no_body_is_a_failure(void)
{
    TransportFixture f;
    f.http.alwaysRespond(200, "");

    bool httpFailed = false;
    const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    TEST_ASSERT_TRUE_MESSAGE(httpFailed, "a 200 with no body was taken for an empty catalog");
    TEST_ASSERT_EQUAL_size_t(0, types.size());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("200 with no usable body"),
                             "the empty answer was not reported as such in the log");
}

static void test_a_body_cut_in_half_is_a_failure(void)
{
    TransportFixture f;
    const std::string cut = arrayOf(ONE_TYPE, AWKWARD_TYPE).substr(0, 200);
    f.http.alwaysRespond(200, cut.c_str());

    bool httpFailed = false;
    const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    TEST_ASSERT_TRUE_MESSAGE(httpFailed, "a truncated catalog was accepted");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, types.size(),
                                     "the types read before the body stopped were kept");
}

static void test_a_server_that_did_not_answer_is_a_failure(void)
{
    TransportFixture f;
    f.http.alwaysRespond(500, "[]");

    bool httpFailed = false;
    const std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    TEST_ASSERT_TRUE_MESSAGE(httpFailed, "a 500 was not reported as a transport failure");
    TEST_ASSERT_EQUAL_size_t(0, types.size());
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, f.http.requestCount(),
                                     "the catalog fetch made more than one request");
}

/** The body is read before the connection is ended, and the connection is ended in every case. */
static void test_the_connection_is_closed_whatever_the_answer(void)
{
    TransportFixture f;
    f.http.alwaysRespond(200, arrayOf(ONE_TYPE).c_str());
    f.service.getSensorTypesList();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, f.http.endCount(), "a read catalog left the connection open");

    TransportFixture refused;
    refused.http.alwaysRespond(403, "");
    refused.service.getSensorTypesList();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, refused.http.endCount(),
                                  "a refused request left the connection open");
}

extern "C" int run_catalog_stream_read_tests(void)
{
    RUN_TEST(test_a_streamed_catalog_reads_the_same_as_a_whole_one);
    RUN_TEST(test_a_body_that_stops_inside_a_type_fails_the_whole_answer);
    RUN_TEST(test_an_object_larger_than_the_cap_fails_the_body);
    RUN_TEST(test_the_transport_reports_a_catalog_it_read);
    RUN_TEST(test_an_empty_catalog_is_an_answer_and_not_a_failure);
    RUN_TEST(test_a_200_with_no_body_is_a_failure);
    RUN_TEST(test_a_body_cut_in_half_is_a_failure);
    RUN_TEST(test_a_server_that_did_not_answer_is_a_failure);
    RUN_TEST(test_the_connection_is_closed_whatever_the_answer);
    return 0;
}
