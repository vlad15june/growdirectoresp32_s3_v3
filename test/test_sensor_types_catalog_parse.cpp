#include <Arduino.h>

#include <unity.h>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"

static SensorType makeCatalogType(const std::string& uid, const std::string& name) {
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " Display";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = "2026-02-11T09:01:35.44352+00:00";

    SubSensorType subType;
    subType.name = name + "_channel";
    subType.displayName = "Channel";
    subType.unit = "test";
    subType.minValue = 0.0f;
    subType.maxValue = 100.0f;
    subType.createdAt = sensorType.createdAt;
    subType.updatedAt = sensorType.updatedAt;
    sensorType.sensorTypes.push_back(subType);

    return sensorType;
}

static std::vector<SensorType> threeTypes() {
    std::vector<SensorType> types;
    types.push_back(makeCatalogType("uid-a", "sensor_a"));
    types.push_back(makeCatalogType("uid-b", "sensor_b"));
    types.push_back(makeCatalogType("uid-c", "sensor_c"));
    return types;
}

static std::string cutInsideTheLastType(const std::string& json) {
    size_t lastObject = json.rfind('{');
    TEST_ASSERT_TRUE(lastObject != std::string::npos);
    return json.substr(0, lastObject + 8);
}

static void assertWholeCatalogIsParsed(IJsonConverter& converter) {
    std::string json = converter.sensorTypesToJson(threeTypes());
    TEST_ASSERT_FALSE(json.empty());

    std::vector<SensorType> parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(JsonListParse::Ok),
                      static_cast<int>(converter.sensorTypesFromJson(json, parsed)));
    TEST_ASSERT_EQUAL(3, static_cast<int>(parsed.size()));
    TEST_ASSERT_EQUAL_STRING("uid-a", parsed[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("uid-b", parsed[1].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("uid-c", parsed[2].uid.c_str());
}

static void assertBodyEndingInsideATypeIsRefused(IJsonConverter& converter) {
    std::string json = converter.sensorTypesToJson(threeTypes());
    TEST_ASSERT_FALSE(json.empty());

    std::vector<SensorType> parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(JsonListParse::Failed),
                      static_cast<int>(converter.sensorTypesFromJson(cutInsideTheLastType(json), parsed)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));
}

/**
 * An unreadable body and an empty catalog are different answers and stay different: this firmware
 * reports the empty one as Empty - the server saying it publishes nothing - and only Failed keeps
 * the catalog the module already has.
 */
static void assertAnEmptyCatalogIsToldFromAnUnreadableBody(IJsonConverter& converter) {
    std::vector<SensorType> parsed;

    TEST_ASSERT_EQUAL(static_cast<int>(JsonListParse::Failed),
                      static_cast<int>(converter.sensorTypesFromJson("", parsed)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));

    TEST_ASSERT_EQUAL(static_cast<int>(JsonListParse::Empty),
                      static_cast<int>(converter.sensorTypesFromJson("[]", parsed)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));

    TEST_ASSERT_EQUAL(static_cast<int>(JsonListParse::Failed),
                      static_cast<int>(converter.sensorTypesFromJson("<html>502 Bad Gateway</html>", parsed)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));
}

static void test_arduino_converter_parses_a_whole_catalog(void) {
    ArduinoJsonConverter converter;
    assertWholeCatalogIsParsed(converter);
}

static void test_arduino_converter_refuses_a_body_that_ends_inside_a_type(void) {
    ArduinoJsonConverter converter;
    assertBodyEndingInsideATypeIsRefused(converter);
}

static void test_arduino_converter_tells_an_empty_catalog_from_an_unreadable_body(void) {
    ArduinoJsonConverter converter;
    assertAnEmptyCatalogIsToldFromAnUnreadableBody(converter);
}

/**
 * A field larger than the reader will hold refuses the type - and the types around it in the
 * catalog are read anyway. A single unreadable model is one model missing, not a catalog missing.
 */
static void test_arduino_converter_leaves_out_a_type_too_large_to_read(void) {
    ArduinoJsonConverter arduinoConverter;
    IJsonConverter& converter = arduinoConverter;

    SensorType oversized = makeCatalogType("uid-oversized", "sensor_oversized");
    oversized.displayName = std::string(9000, 'x');

    std::string oversizedJson = converter.sensorTypeToJson(oversized);
    TEST_ASSERT_FALSE(oversizedJson.empty());
    SensorType single;
    TEST_ASSERT_FALSE(converter.sensorTypeFromJson(oversizedJson, single));

    std::vector<SensorType> types;
    types.push_back(makeCatalogType("uid-a", "sensor_a"));
    types.push_back(oversized);
    types.push_back(makeCatalogType("uid-c", "sensor_c"));

    std::string json = converter.sensorTypesToJson(types);
    TEST_ASSERT_FALSE(json.empty());

    std::vector<SensorType> parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(JsonListParse::Ok),
                      static_cast<int>(converter.sensorTypesFromJson(json, parsed)));
    TEST_ASSERT_EQUAL(2, static_cast<int>(parsed.size()));
    TEST_ASSERT_EQUAL_STRING("uid-a", parsed[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("uid-c", parsed[1].uid.c_str());
}

/**
 * A model the server sent without a name is kept, and so is the catalog around it.
 *
 * The alternative - refusing the body because one model in it is unusable - is worse than the
 * model: the catalog is what says which sensors can be scanned at all, it is fetched over the
 * network and then stored, and a module that answers "no catalog" to one bad record is a module
 * that cannot scan until the server sends a clean one.
 *
 * That the nameless model is kept rather than skipped is characterization and not a requirement:
 * it holds a slot in the catalog and matches no sensor. The backend does not send such a model
 * (SensorModelHwDto.Name comes from a non-null column), so this is the shape of the answer if it
 * ever did.
 */
static void test_a_type_whose_name_is_missing_does_not_cost_the_catalog(void) {
    ArduinoJsonConverter arduinoConverter;
    IJsonConverter& converter = arduinoConverter;

    std::vector<SensorType> types = threeTypes();
    types[1].name = "";

    std::string json = converter.sensorTypesToJson(types);
    TEST_ASSERT_FALSE(json.empty());

    std::vector<SensorType> parsed;
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(JsonListParse::Ok),
                              static_cast<int>(converter.sensorTypesFromJson(json, parsed)),
                              "one model without a name was read as no catalog at all");
    TEST_ASSERT_EQUAL(3, static_cast<int>(parsed.size()));
    TEST_ASSERT_EQUAL_STRING("uid-a", parsed[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("uid-c", parsed[2].uid.c_str());

    // The models beside it kept their names, so this is one damaged record and not a body read
    // wrong from that point on.
    TEST_ASSERT_EQUAL_STRING("sensor_a", parsed[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("", parsed[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("sensor_c", parsed[2].name.c_str());
}

extern "C" int run_sensor_types_catalog_parse_tests(void) {

    RUN_TEST(test_arduino_converter_parses_a_whole_catalog);
    RUN_TEST(test_arduino_converter_refuses_a_body_that_ends_inside_a_type);
    RUN_TEST(test_arduino_converter_leaves_out_a_type_too_large_to_read);
    RUN_TEST(test_arduino_converter_tells_an_empty_catalog_from_an_unreadable_body);
    RUN_TEST(test_a_type_whose_name_is_missing_does_not_cost_the_catalog);

    return 0;
}
