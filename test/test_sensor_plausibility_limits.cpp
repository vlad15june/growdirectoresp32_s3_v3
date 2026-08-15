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
    SensorPlausibilityUseCase plausibility;
    SensorService service;

    Fixture()
        : plausibility(&sensorTypeRepository),
          service(nullptr,
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
                  &plausibility) {}
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

// -------- GDV1-3826: the range is the UNION of the sub type and the register config --------
//
// The sub sensor type alone is regularly the narrower of the two sources, and gating on it alone
// dropped healthy readings and reported live channels as disconnected. The register config of the
// very same channel already carries a range the scan accepts a sensor by (canConnect*), so the gate
// now rejects only what is outside BOTH.

const std::string WIDE_SENSOR_TYPE_UID = "e0d3b2a1-0000-4000-8000-000000000001";
const std::string LUX_SENSOR_TYPE_UID = "e0d3b2a1-0000-4000-8000-000000000002";
const std::string I2C_SENSOR_TYPE_UID = "e0d3b2a1-0000-4000-8000-000000000003";

// One channel, one sub sensor type, one register config — the shape of every case below
SensorType makeOneChannelSensorType(const std::string &uid,
                                    const std::string &model,
                                    const std::string &channel,
                                    float limitMin,
                                    float limitMax) {
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = model;
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;

    SubSensorType subSensorType;
    subSensorType.name = channel;
    subSensorType.limitMinValue = limitMin;
    subSensorType.limitMaxValue = limitMax;
    sensorType.sensorTypes.push_back(subSensorType);

    return sensorType;
}

SensorEntity makeRegisterChannelSensor(const std::string &uid,
                                       const std::string &model,
                                       const std::string &channel,
                                       float canConnectMin,
                                       float canConnectMax) {
    SensorEntity sensor = makeSingleChannelSensor();
    sensor.sensorType = "";
    sensor.model = model;
    sensor.sensorTypeUid = uid;

    ModbusRegisterConfig registerConfig;
    registerConfig.channel = 1;
    registerConfig.channelName = channel;
    registerConfig.sensorType = channel;
    registerConfig.canConnectMinValue = canConnectMin;
    registerConfig.canConnectMaxValue = canConnectMax;
    sensor.modbusRegisterConfigs.push_back(registerConfig);

    return sensor;
}

// Unconfigured wide channel: the catalogue says 0..100 while the register config declares the raw
// counts the sensor actually answers with. The scan accepted 6471 on the module of the ticket and
// the reading path then rejected 6502 as implausible — one firmware, two verdicts.
void test_register_config_widens_the_upper_bound(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(
        makeOneChannelSensorType(WIDE_SENSOR_TYPE_UID, "ry-cwyw485", "conductivity", 0.0f, 100.0f));
    SensorEntity sensor =
        makeRegisterChannelSensor(WIDE_SENSOR_TYPE_UID, "ry-cwyw485", "conductivity", 0.0f, 100000.0f);
    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 6500.0f;

    TEST_ASSERT_TRUE(f.service.readSensor(sensor));

    TEST_ASSERT_EQUAL_FLOAT(6500.0f, f.sensorValueModel.getChannelValue(SENSOR_UID, SENSOR_UID + ".01"));
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setLastReadTimeCallCount);
    TEST_ASSERT_FALSE(f.sensorRepository.lastStatusIsError);
}

// Lower bound, the class the ticket missed: lux is catalogued as 1..200000, so darkness (0 lux) was
// dropped every night. The register config starts at zero, so zero is a reading, not a fault.
void test_zero_lux_at_night_is_accepted(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(
        makeOneChannelSensorType(LUX_SENSOR_TYPE_UID, "XF700A", "lux", 1.0f, 200000.0f));
    SensorEntity sensor =
        makeRegisterChannelSensor(LUX_SENSOR_TYPE_UID, "XF700A", "lux", 0.0f, 200000.0f);
    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 0.0f;

    TEST_ASSERT_TRUE(f.service.readSensor(sensor));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.sensorValueModel.getChannelValue(SENSOR_UID, SENSOR_UID + ".01"));
    TEST_ASSERT_FALSE(f.sensorRepository.lastStatusIsError);
}

// The other direction, and the reason this is a union and not a replacement: mp-508c declares
// canConnect 10..40 for soil temperature while the catalogue allows 0..100. Swapping one range for
// the other would have narrowed acceptance and rejected cold soil.
void test_register_config_narrower_than_the_sub_type_does_not_narrow_the_gate(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(
        makeOneChannelSensorType(SOIL_SENSOR_TYPE_UID, "mp-508c", "soil_temp", 0.0f, 100.0f));
    SensorEntity sensor =
        makeRegisterChannelSensor(SOIL_SENSOR_TYPE_UID, "mp-508c", "soil_temp", 10.0f, 40.0f);

    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 5.0f;  // inside the sub type, below canConnect
    TEST_ASSERT_TRUE(f.service.readSensor(sensor));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, f.sensorValueModel.getChannelValue(SENSOR_UID, SENSOR_UID + ".01"));

    // outside both sources: still garbage, still dropped
    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 3801.0f;
    TEST_ASSERT_FALSE(f.service.readSensor(sensor));
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setChannelValueCallCount);
    TEST_ASSERT_TRUE(f.sensorRepository.lastStatusIsError);
}

// I2C register configs carry no canConnect fields at all, so for them the sub sensor type stays the
// only source and the behaviour is the one from GDV1-3810 — unchanged by this fix.
void test_i2c_channel_without_can_connect_keeps_the_sub_type_range(void) {
    Fixture f;
    f.sensorTypeRepository.addSensorType(
        makeOneChannelSensorType(I2C_SENSOR_TYPE_UID, "sht31", "humidity", 0.0f, 100.0f));

    SensorEntity sensor = makeSingleChannelSensor();
    sensor.sensorType = "";
    sensor.model = "sht31";
    sensor.protocol = "I2C";
    sensor.sensorTypeUid = I2C_SENSOR_TYPE_UID;

    I2CRegisterConfig registerConfig;
    registerConfig.channel = 1;
    registerConfig.channelName = "humidity";
    registerConfig.sensorType = "humidity";
    sensor.i2cRegisterConfigs.push_back(registerConfig);

    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 55.0f;
    TEST_ASSERT_TRUE(f.service.readSensor(sensor));
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensorValueModel.getChannelValue(SENSOR_UID, SENSOR_UID + ".01"));

    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 3801.0f;
    TEST_ASSERT_FALSE(f.service.readSensor(sensor));
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setChannelValueCallCount);
}

// The catalogue knows nothing about this channel: the register config must NOT start gating it on
// its own — that would make the gate stricter than before the fix, which is the defect itself.
void test_unknown_sub_type_is_not_gated_by_the_register_config(void) {
    Fixture f;
    SensorEntity sensor =
        makeRegisterChannelSensor(WIDE_SENSOR_TYPE_UID, "ry-cwyw485", "conductivity", 10.0f, 40.0f);
    f.sensorReader.multiValues[SENSOR_UID + ".01"] = 900.0f;

    TEST_ASSERT_TRUE(f.service.readSensor(sensor));

    TEST_ASSERT_EQUAL_FLOAT(900.0f, f.sensorValueModel.getChannelValue(SENSOR_UID, SENSOR_UID + ".01"));

    ISensorPlausibilityUseCase::Limits limits = f.plausibility.getLimits(sensor, "conductivity");
    TEST_ASSERT_FALSE(limits.defined);
}

// The resolved range itself, both directions in numbers
void test_limits_are_the_union_of_the_two_sources(void) {
    MockSensorTypeRepository repository;
    repository.addSensorType(
        makeOneChannelSensorType(WIDE_SENSOR_TYPE_UID, "ry-cwyw485", "conductivity", 0.0f, 100.0f));
    repository.addSensorType(
        makeOneChannelSensorType(SOIL_SENSOR_TYPE_UID, "mp-508c", "soil_temp", 0.0f, 100.0f));
    SensorPlausibilityUseCase plausibility(&repository);
    ISensorPlausibilityUseCase &gate = plausibility;

    // register config wider on top, sub type wider at the bottom
    ISensorPlausibilityUseCase::Limits wide = gate.getLimits(
        makeRegisterChannelSensor(WIDE_SENSOR_TYPE_UID, "ry-cwyw485", "conductivity", -50.0f, 100000.0f),
        "conductivity");
    TEST_ASSERT_TRUE(wide.defined);
    TEST_ASSERT_EQUAL_FLOAT(-50.0f, wide.min);
    TEST_ASSERT_EQUAL_FLOAT(100000.0f, wide.max);

    // register config entirely inside the sub type range: the union is the sub type range
    ISensorPlausibilityUseCase::Limits narrow = gate.getLimits(
        makeRegisterChannelSensor(SOIL_SENSOR_TYPE_UID, "mp-508c", "soil_temp", 10.0f, 40.0f),
        "soil_temp");
    TEST_ASSERT_TRUE(narrow.defined);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, narrow.min);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, narrow.max);

    // a config that declares nothing (both fields at their 0.0f default) contributes nothing
    ISensorPlausibilityUseCase::Limits declaresNothing = gate.getLimits(
        makeRegisterChannelSensor(SOIL_SENSOR_TYPE_UID, "mp-508c", "soil_temp", 0.0f, 0.0f),
        "soil_temp");
    TEST_ASSERT_TRUE(declaresNothing.defined);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, declaresNothing.min);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, declaresNothing.max);
}

// -------- the decision on its own --------

void test_limits_resolution_and_decision(void) {
    MockSensorTypeRepository repository;
    repository.addSensorType(makeAirTempSensorType(AIR_TEMP_LIMIT_MIN, AIR_TEMP_LIMIT_MAX));
    SensorPlausibilityUseCase plausibility(&repository);

    // through the interface: this is how SensorService sees the gate
    ISensorPlausibilityUseCase &gate = plausibility;

    ISensorPlausibilityUseCase::Limits limits = gate.getLimits(makeSingleChannelSensor(), "air_temp");
    TEST_ASSERT_TRUE(limits.defined);
    TEST_ASSERT_EQUAL_FLOAT(AIR_TEMP_LIMIT_MIN, limits.min);
    TEST_ASSERT_EQUAL_FLOAT(AIR_TEMP_LIMIT_MAX, limits.max);

    TEST_ASSERT_FALSE(gate.isWithinLimits(-273.0f, limits));
    TEST_ASSERT_FALSE(gate.isWithinLimits(3801.0f, limits));
    TEST_ASSERT_TRUE(gate.isWithinLimits(21.5f, limits));

    ISensorPlausibilityUseCase::Limits none;
    TEST_ASSERT_FALSE(none.defined);
    TEST_ASSERT_TRUE(gate.isWithinLimits(3801.0f, none));
}

// Without a sensor type repository the gate is inert — the wiring is what turns it on
void test_gate_without_repository_accepts_everything(void) {
    SensorPlausibilityUseCase plausibility;
    ISensorPlausibilityUseCase::Limits limits;

    TEST_ASSERT_TRUE(plausibility.isPlausible(makeSingleChannelSensor(), "air_temp", 3801.0f, limits));
    TEST_ASSERT_FALSE(limits.defined);
}

// The service depends on the interface, not on the sensor type catalog: a stub verdict is enough
// to drive the reading path, which is the point of injecting the gate (PR #18 review).
class AlwaysRejectingGate : public ISensorPlausibilityUseCase {
public:
    mutable int calls = 0;

    Limits getLimits(const SensorEntity &, const std::string &) const override {
        Limits limits;
        limits.min = 0.0f;
        limits.max = 1.0f;
        limits.defined = true;
        return limits;
    }

    bool isWithinLimits(float, const Limits &) const override { return false; }

    bool isPlausible(const SensorEntity &sensor,
                     const std::string &subSensorTypeName,
                     float,
                     Limits &outLimits) const override {
        calls++;
        outLimits = getLimits(sensor, subSensorTypeName);
        return false;
    }
};

void test_service_uses_the_injected_gate(void) {
    MockSensorRepository sensorRepository;
    FakeSensorReader sensorReader;
    MockSensorValueModel sensorValueModel;
    MockArduinoAdapter arduinoAdapter;
    AlwaysRejectingGate gate;
    SensorService service(nullptr, nullptr, &sensorRepository, &sensorReader, &sensorValueModel,
                          nullptr, &arduinoAdapter, nullptr, nullptr, nullptr, nullptr, nullptr,
                          nullptr, &gate);

    sensorReader.singleValue = 21.5f;  // a perfectly normal value: only the gate rejects it

    TEST_ASSERT_FALSE(service.readSensor(makeSingleChannelSensor()));
    TEST_ASSERT_EQUAL_INT(1, gate.calls);
    TEST_ASSERT_EQUAL_INT(0, sensorValueModel.setSensorValueCallCount);
}

// Nothing injected at all -> the gate is inert and the old behaviour stands
void test_service_without_a_gate_accepts_everything(void) {
    MockSensorRepository sensorRepository;
    FakeSensorReader sensorReader;
    MockSensorValueModel sensorValueModel;
    MockArduinoAdapter arduinoAdapter;
    SensorService service(nullptr, nullptr, &sensorRepository, &sensorReader, &sensorValueModel,
                          nullptr, &arduinoAdapter);

    sensorReader.singleValue = 3801.0f;

    TEST_ASSERT_TRUE(service.readSensor(makeSingleChannelSensor()));
    TEST_ASSERT_EQUAL_INT(1, sensorValueModel.setSensorValueCallCount);
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
    RUN_TEST(test_register_config_widens_the_upper_bound);
    RUN_TEST(test_zero_lux_at_night_is_accepted);
    RUN_TEST(test_register_config_narrower_than_the_sub_type_does_not_narrow_the_gate);
    RUN_TEST(test_i2c_channel_without_can_connect_keeps_the_sub_type_range);
    RUN_TEST(test_unknown_sub_type_is_not_gated_by_the_register_config);
    RUN_TEST(test_limits_are_the_union_of_the_two_sources);
    RUN_TEST(test_limits_resolution_and_decision);
    RUN_TEST(test_gate_without_repository_accepts_everything);
    RUN_TEST(test_service_uses_the_injected_gate);
    RUN_TEST(test_service_without_a_gate_accepts_everything);
    return 0;
}
