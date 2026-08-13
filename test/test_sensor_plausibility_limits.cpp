// GDV1-3810: sensor plausibility limits (limitMinValue/limitMaxValue) must gate the reading path.
//
// The gate lives in SensorService::readSensor — one place for every sensor, before the value is
// stored in the value model and before it can reach the update DTO. A reading outside the
// physical range is dropped: no value stored, no fresh read time, sensor left in the error state
// so the rules fall back on sensor_unreachable.

#include <unity.h>
#include <map>
#include <string>
#include <vector>

#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/UseCases/Sensors/SensorPlausibilityUseCase.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"

namespace {

const std::string SENSOR_UID = "9C:13:9E:99:47:38.00";
const std::string SENSOR_TYPE_UID = "4719a986-a300-48d5-bd1f-e34a1329798a";
const std::string SOIL_SENSOR_TYPE_UID = "144f6c6d-d210-44bf-a9c3-23c5f26472fe";

// Air temperature as the server seeds it: recommended range 15..35, physical range -40..75
const float AIR_TEMP_LIMIT_MIN = -40.0f;
const float AIR_TEMP_LIMIT_MAX = 75.0f;

class FakeSensorReader : public ISensorReader {
public:
    float singleValue = 0.0f;
    bool singleReadOk = true;
    std::map<std::string, float> multiValues;
    bool multiReadOk = true;

    void init(unsigned long) override {}

    bool readSensor(const SensorEntity &, float &outValue) override {
        outValue = singleValue;
        return singleReadOk;
    }

    bool scanSensor(const SensorEntity &) override { return false; }

    SensorReadResult readSensorDetailed(const SensorEntity &) override {
        return SensorReadResult::createError("not used", SensorReadStatus::COMMUNICATION_ERROR);
    }

    bool readMultiChannelSensor(const SensorEntity &, std::map<std::string, float> &channelValues) override {
        channelValues = multiValues;
        return multiReadOk;
    }

    bool writeSensorAddress(const SensorEntity &, int) override { return false; }
    void prepareSensorsForReading(const std::vector<SensorEntity> &) override {}
    bool setModbusPortPower(int, bool) override { return true; }
};

SensorType makeAirTempSensorType(float limitMin, float limitMax) {
    SensorType sensorType;
    sensorType.uid = SENSOR_TYPE_UID;
    sensorType.name = "ry_ws301";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;

    SubSensorType airTemp;
    airTemp.name = "air_temp";
    airTemp.minValue = 15.0f;   // recommended range — must NOT gate
    airTemp.maxValue = 35.0f;
    airTemp.limitMinValue = limitMin;
    airTemp.limitMaxValue = limitMax;
    sensorType.sensorTypes.push_back(airTemp);

    return sensorType;
}

SensorEntity makeSingleChannelSensor() {
    SensorEntity sensor;
    sensor.uid = SENSOR_UID;
    sensor.sensorType = "air_temp";
    sensor.model = "ry_ws301";
    sensor.protocol = "ModBus";
    sensor.sensorTypeUid = SENSOR_TYPE_UID;
    sensor.isActive = true;
    sensor.pin = 0;
    return sensor;
}

// Everything SensorService::readSensor touches, wired around one sensor and one sensor type
struct Fixture {
    MockSensorRepository sensorRepository;
    MockSensorTypeRepository sensorTypeRepository;
    FakeSensorReader sensorReader;
    MockSensorValueModel sensorValueModel;
    MockArduinoAdapter arduinoAdapter;
    SensorService service;

    Fixture()
        : service(nullptr,
                  nullptr,
                  &sensorRepository,
                  &sensorReader,
                  &sensorValueModel,
                  nullptr,
                  &arduinoAdapter,
                  nullptr,
                  nullptr,
                  nullptr,
                  nullptr,
                  nullptr,
                  nullptr,
                  &sensorTypeRepository) {}
};

// -------- the gate itself: four cases required by the ticket --------

void test_value_below_limit_is_rejected(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(makeAirTempSensorType(AIR_TEMP_LIMIT_MIN, AIR_TEMP_LIMIT_MAX));
    f.sensorReader.singleValue = -273.0f;

    TEST_ASSERT_FALSE(f.service.readSensor(makeSingleChannelSensor()));

    // not stored as the last value and no fresh read time -> rules see a stale sensor
    TEST_ASSERT_EQUAL_INT(0, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_INT(0, f.sensorValueModel.setLastReadTimeCallCount);
    // sensor left in the error state
    TEST_ASSERT_TRUE(f.sensorRepository.lastStatusIsError);
}

void test_value_above_limit_is_rejected(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(makeAirTempSensorType(AIR_TEMP_LIMIT_MIN, AIR_TEMP_LIMIT_MAX));
    f.sensorReader.singleValue = 3801.0f;  // the GDV1-3161 field value

    TEST_ASSERT_FALSE(f.service.readSensor(makeSingleChannelSensor()));

    TEST_ASSERT_EQUAL_INT(0, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_INT(0, f.sensorValueModel.setLastReadTimeCallCount);
    TEST_ASSERT_TRUE(f.sensorRepository.lastStatusIsError);
}

void test_value_within_limits_is_accepted(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(makeAirTempSensorType(AIR_TEMP_LIMIT_MIN, AIR_TEMP_LIMIT_MAX));
    f.sensorReader.singleValue = 21.5f;

    TEST_ASSERT_TRUE(f.service.readSensor(makeSingleChannelSensor()));

    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, f.sensorValueModel.getSensorValue(SENSOR_UID));
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setLastReadTimeCallCount);
    TEST_ASSERT_FALSE(f.sensorRepository.lastStatusIsError);
}

// limitMinValue == limitMaxValue == 0 is the constructor default: the server sent no bounds
void test_no_limits_configured_accepts_everything(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(makeAirTempSensorType(0.0f, 0.0f));
    f.sensorReader.singleValue = 3801.0f;

    TEST_ASSERT_TRUE(f.service.readSensor(makeSingleChannelSensor()));

    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_FLOAT(3801.0f, f.sensorValueModel.getSensorValue(SENSOR_UID));
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setLastReadTimeCallCount);
}

// -------- boundaries and the pairs that must not gate --------

void test_values_exactly_on_the_limits_are_accepted(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(makeAirTempSensorType(AIR_TEMP_LIMIT_MIN, AIR_TEMP_LIMIT_MAX));

    f.sensorReader.singleValue = AIR_TEMP_LIMIT_MIN;
    TEST_ASSERT_TRUE(f.service.readSensor(makeSingleChannelSensor()));

    f.sensorReader.singleValue = AIR_TEMP_LIMIT_MAX;
    TEST_ASSERT_TRUE(f.service.readSensor(makeSingleChannelSensor()));

    TEST_ASSERT_EQUAL_INT(2, f.sensorValueModel.setSensorValueCallCount);
}

// minValue/maxValue are the recommended range (15..35 °C in the seed), not plausibility
void test_value_outside_recommended_range_is_accepted(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(makeAirTempSensorType(AIR_TEMP_LIMIT_MIN, AIR_TEMP_LIMIT_MAX));
    f.sensorReader.singleValue = -12.0f;  // winter, below minValue 15 but inside the physical range

    TEST_ASSERT_TRUE(f.service.readSensor(makeSingleChannelSensor()));

    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_FLOAT(-12.0f, f.sensorValueModel.getSensorValue(SENSOR_UID));
}

// No sensor type on the module at all -> nothing to gate on, behaviour unchanged
void test_unknown_sensor_type_accepts_reading(void) {
    Fixture f;
    f.sensorReader.singleValue = 3801.0f;

    TEST_ASSERT_TRUE(f.service.readSensor(makeSingleChannelSensor()));

    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setSensorValueCallCount);
}

// -------- multi-channel: the gate is per channel --------

SensorType makeTwoChannelSensorType() {
    SensorType sensorType;
    sensorType.uid = SENSOR_TYPE_UID;
    sensorType.name = "ry_ws301";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;

    SubSensorType airTemp;
    airTemp.name = "air_temp";
    airTemp.limitMinValue = AIR_TEMP_LIMIT_MIN;
    airTemp.limitMaxValue = AIR_TEMP_LIMIT_MAX;
    sensorType.sensorTypes.push_back(airTemp);

    SubSensorType humidity;
    humidity.name = "humidity";
    humidity.limitMinValue = 0.0f;
    humidity.limitMaxValue = 100.0f;
    sensorType.sensorTypes.push_back(humidity);

    return sensorType;
}

SensorEntity makeTwoChannelSensor() {
    SensorEntity sensor = makeSingleChannelSensor();
    sensor.sensorType = "";

    ModbusRegisterConfig temp;
    temp.channel = 1;
    temp.channelName = "air_temp";
    temp.sensorType = "air_temp";
    sensor.modbusRegisterConfigs.push_back(temp);

    ModbusRegisterConfig humidity;
    humidity.channel = 2;
    humidity.channelName = "humidity";
    humidity.sensorType = "humidity";
    sensor.modbusRegisterConfigs.push_back(humidity);

    return sensor;
}

// MP-508C as the server seeds it (main.cpp): soil moisture + soil temperature. A multi-channel
// sensor WITHOUT an air temperature / humidity pair, so it never grows a computed channel.
SensorType makeSoilSensorType() {
    SensorType sensorType;
    sensorType.uid = SOIL_SENSOR_TYPE_UID;
    sensorType.name = "mp-508c";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;

    SubSensorType soilMoisture;
    soilMoisture.name = "soil_moisture";
    soilMoisture.limitMinValue = 0.0f;
    soilMoisture.limitMaxValue = 100.0f;
    sensorType.sensorTypes.push_back(soilMoisture);

    SubSensorType soilTemp;
    soilTemp.name = "soil_temp";
    soilTemp.limitMinValue = -40.0f;
    soilTemp.limitMaxValue = 80.0f;
    sensorType.sensorTypes.push_back(soilTemp);

    return sensorType;
}

SensorEntity makeSoilSensor() {
    SensorEntity sensor = makeSingleChannelSensor();
    sensor.sensorType = "";
    sensor.model = "mp-508c";
    sensor.sensorTypeUid = SOIL_SENSOR_TYPE_UID;

    ModbusRegisterConfig soilMoisture;
    soilMoisture.channel = 1;
    soilMoisture.channelName = "soil_moisture";
    soilMoisture.sensorType = "soil_moisture";
    sensor.modbusRegisterConfigs.push_back(soilMoisture);

    ModbusRegisterConfig soilTemp;
    soilTemp.channel = 2;
    soilTemp.channelName = "soil_temp";
    soilTemp.sensorType = "soil_temp";
    sensor.modbusRegisterConfigs.push_back(soilTemp);

    return sensor;
}

// The gate decides per channel: the implausible channel is dropped, the healthy one is stored and
// the read still counts as successful.
//
// Stated by channel uid and not by "how many channels were stored": an air temperature + humidity
// sensor additionally publishes a COMPUTED VPD channel (GDV1-3782), and the accepted order
// (2026-08-13) is "VPD is computed from the raw readings and then goes through this very gate".
// The number of stored channels therefore grows with that feature, while what this test states -
// which register channel survives the gate - does not.
void test_multi_channel_drops_only_the_implausible_channel(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(makeTwoChannelSensorType());
    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 3801.0f;  // air_temp, implausible
    f.sensorReader.multiValues[SENSOR_UID + ".02"] = 55.0f;    // humidity, fine

    TEST_ASSERT_TRUE(f.service.readSensor(makeTwoChannelSensor()));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.sensorValueModel.getChannelValue(SENSOR_UID, SENSOR_UID + ".01"));
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensorValueModel.getChannelValue(SENSOR_UID, SENSOR_UID + ".02"));
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setLastReadTimeCallCount);
    TEST_ASSERT_FALSE(f.sensorRepository.lastStatusIsError);
}

// Nothing survived the gate -> the read counts as unsuccessful. Deliberately on the soil sensor:
// it has no air pair, so no computed channel can survive on its behalf and the case keeps talking
// about the gate alone, before and after GDV1-3782.
void test_multi_channel_all_channels_implausible_is_an_error(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(makeSoilSensorType());
    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 900.0f;   // soil_moisture, above 100
    f.sensorReader.multiValues[SENSOR_UID + ".02"] = 3801.0f;  // soil_temp, above 80

    TEST_ASSERT_FALSE(f.service.readSensor(makeSoilSensor()));

    TEST_ASSERT_EQUAL_INT(0, f.sensorValueModel.setChannelValueCallCount);
    TEST_ASSERT_EQUAL_INT(0, f.sensorValueModel.setLastReadTimeCallCount);
    TEST_ASSERT_TRUE(f.sensorRepository.lastStatusIsError);
}

// -------- the decision on its own --------

void test_limits_resolution_and_decision(void) {
    MockSensorTypeRepository repository;
    repository.addSensorType(makeAirTempSensorType(AIR_TEMP_LIMIT_MIN, AIR_TEMP_LIMIT_MAX));
    SensorPlausibilityUseCase plausibility(&repository);

    SensorPlausibilityUseCase::Limits limits = plausibility.getLimits(makeSingleChannelSensor(), "air_temp");
    TEST_ASSERT_TRUE(limits.defined);
    TEST_ASSERT_EQUAL_FLOAT(AIR_TEMP_LIMIT_MIN, limits.min);
    TEST_ASSERT_EQUAL_FLOAT(AIR_TEMP_LIMIT_MAX, limits.max);

    TEST_ASSERT_FALSE(SensorPlausibilityUseCase::isWithinLimits(-273.0f, limits));
    TEST_ASSERT_FALSE(SensorPlausibilityUseCase::isWithinLimits(3801.0f, limits));
    TEST_ASSERT_TRUE(SensorPlausibilityUseCase::isWithinLimits(21.5f, limits));

    SensorPlausibilityUseCase::Limits none;
    TEST_ASSERT_FALSE(none.defined);
    TEST_ASSERT_TRUE(SensorPlausibilityUseCase::isWithinLimits(3801.0f, none));
}

// Without a sensor type repository the gate is inert — the wiring is what turns it on
void test_gate_without_repository_accepts_everything(void) {
    SensorPlausibilityUseCase plausibility;
    SensorPlausibilityUseCase::Limits limits;

    TEST_ASSERT_TRUE(plausibility.isPlausible(makeSingleChannelSensor(), "air_temp", 3801.0f, limits));
    TEST_ASSERT_FALSE(limits.defined);
}

} // namespace

extern "C" int run_sensor_plausibility_limits_tests(void) {
    RUN_TEST(test_value_below_limit_is_rejected);
    RUN_TEST(test_value_above_limit_is_rejected);
    RUN_TEST(test_value_within_limits_is_accepted);
    RUN_TEST(test_no_limits_configured_accepts_everything);
    RUN_TEST(test_values_exactly_on_the_limits_are_accepted);
    RUN_TEST(test_value_outside_recommended_range_is_accepted);
    RUN_TEST(test_unknown_sensor_type_accepts_reading);
    RUN_TEST(test_multi_channel_drops_only_the_implausible_channel);
    RUN_TEST(test_multi_channel_all_channels_implausible_is_an_error);
    RUN_TEST(test_limits_resolution_and_decision);
    RUN_TEST(test_gate_without_repository_accepts_everything);
    return 0;
}
