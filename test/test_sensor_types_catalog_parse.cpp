#include <Arduino.h>

#include <unity.h>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"

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
    TEST_ASSERT_TRUE(converter.sensorTypesFromJson(json, parsed));
    TEST_ASSERT_EQUAL(3, static_cast<int>(parsed.size()));
    TEST_ASSERT_EQUAL_STRING("uid-a", parsed[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("uid-b", parsed[1].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("uid-c", parsed[2].uid.c_str());
}

static void assertBodyEndingInsideATypeIsRefused(IJsonConverter& converter) {
    std::string json = converter.sensorTypesToJson(threeTypes());
    TEST_ASSERT_FALSE(json.empty());

    std::vector<SensorType> parsed;
    TEST_ASSERT_FALSE(converter.sensorTypesFromJson(cutInsideTheLastType(json), parsed));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));
}

static void assertEmptyAndUnusableBodiesAreRefused(IJsonConverter& converter) {
    std::vector<SensorType> parsed;

    TEST_ASSERT_FALSE(converter.sensorTypesFromJson("", parsed));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));

    TEST_ASSERT_FALSE(converter.sensorTypesFromJson("[]", parsed));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));

    TEST_ASSERT_FALSE(converter.sensorTypesFromJson("<html>502 Bad Gateway</html>", parsed));
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

static void test_arduino_converter_still_refuses_empty_and_unusable_bodies(void) {
    ArduinoJsonConverter converter;
    assertEmptyAndUnusableBodiesAreRefused(converter);
}

static void test_arduino_converter_refuses_a_type_too_large_for_its_document(void) {
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
    TEST_ASSERT_FALSE(converter.sensorTypesFromJson(json, parsed));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));
}

static void test_json_converter_parses_a_whole_catalog(void) {
    JsonConverter converter;
    assertWholeCatalogIsParsed(converter);
}

static void test_json_converter_refuses_a_body_that_ends_inside_a_type(void) {
    JsonConverter converter;
    assertBodyEndingInsideATypeIsRefused(converter);
}

static void test_json_converter_still_refuses_empty_and_unusable_bodies(void) {
    JsonConverter converter;
    assertEmptyAndUnusableBodiesAreRefused(converter);
}

static void test_json_converter_refuses_a_type_it_cannot_read(void) {
    JsonConverter jsonConverter;
    IJsonConverter& converter = jsonConverter;

    std::vector<SensorType> types = threeTypes();
    types[1].name = "";

    std::string json = converter.sensorTypesToJson(types);
    TEST_ASSERT_FALSE(json.empty());

    std::vector<SensorType> parsed;
    TEST_ASSERT_FALSE(converter.sensorTypesFromJson(json, parsed));
    TEST_ASSERT_EQUAL(0, static_cast<int>(parsed.size()));
}

extern "C" int run_sensor_types_catalog_parse_tests(void) {
    UNITY_BEGIN();

    RUN_TEST(test_arduino_converter_parses_a_whole_catalog);
    RUN_TEST(test_arduino_converter_refuses_a_body_that_ends_inside_a_type);
    RUN_TEST(test_arduino_converter_refuses_a_type_too_large_for_its_document);
    RUN_TEST(test_arduino_converter_still_refuses_empty_and_unusable_bodies);
    RUN_TEST(test_json_converter_parses_a_whole_catalog);
    RUN_TEST(test_json_converter_refuses_a_body_that_ends_inside_a_type);
    RUN_TEST(test_json_converter_refuses_a_type_it_cannot_read);
    RUN_TEST(test_json_converter_still_refuses_empty_and_unusable_bodies);

    return UNITY_END();
}
