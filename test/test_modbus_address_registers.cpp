#include <unity.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <set>
#include <string>

#include "Infrastructure/Adapters/Rs485/IModbusRS485.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"

namespace {

class FakeModbusRS485 : public IModbusRS485 {
public:
    struct Frame { std::vector<uint8_t> bytes; };
    struct WriteCall { bool multi; int slave; int reg; uint16_t value; };

    std::vector<Frame> sent;
    std::vector<WriteCall> writes;
    std::set<int> respondingAddresses;
    int respondAddressAfterWrites = -1;
    size_t respondAfterWriteCount = 0;
    bool powered = false;
    int powerOnCount = 0;
    bool requiresSecondPowerOn = false;

    void begin(unsigned long) override {}

    void sendData(uint8_t *data, uint8_t length) override {
        Frame f;
        f.bytes.assign(data, data + length);
        sent.push_back(f);
    }

    int readAvailable(uint8_t *buffer, size_t maxLength) override {
        if (sent.empty()) return 0;
        const auto &last = sent.back().bytes;
        if (last.size() < 2) return 0;
        int addr = last[0];
        uint8_t fc = last[1];
        bool responds = respondingAddresses.count(addr) != 0;
        if (addr == respondAddressAfterWrites && writes.size() >= respondAfterWriteCount) {
            responds = true;
        }
        if (requiresSecondPowerOn && powerOnCount < 2) {
            responds = false;
        }
        if (!responds) return 0;
        uint8_t resp[7] = { (uint8_t)addr, fc, 0x02, 0x00, 0x00, 0x00, 0x00 };
        int n = 7;
        for (int i = 0; i < n && i < (int)maxLength; i++) buffer[i] = resp[i];
        return n;
    }

    int available() override { return 0; }
    const char *getPortName() const override { return "fake"; }
    uint16_t calculateCRC(uint8_t *, uint8_t) override { return 0; }

    bool writeRegister(int slave, int reg, uint16_t value) override {
        writes.push_back({ true, slave, reg, value });
        return true;
    }

    bool writeSingleRegister(int slave, int reg, uint16_t value) override {
        writes.push_back({ false, slave, reg, value });
        return true;
    }

    bool powerOnPort() override { bool ch = !powered; powered = true; ++powerOnCount; return ch; }
    bool powerOffPort() override { bool ch = powered; powered = false; return ch; }

    const Frame *firstFrameWithFunctionCode(uint8_t fc) const {
        for (const auto &f : sent) {
            if (f.bytes.size() >= 2 && f.bytes[1] == fc) return &f;
        }
        return nullptr;
    }
};

} // namespace

// INV-2 + INV-1: primary FC16 write targets the configured register on the
// device's current (non-broadcast) address, then verifies on the ping register.
void test_primary_write_fc16_uses_configured_register_and_current_address(void) {
    FakeModbusRS485 fake;
    fake.respondingAddresses.insert(22);
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fake.writes.size() >= 1);
    TEST_ASSERT_TRUE(fake.writes[0].multi);
    TEST_ASSERT_EQUAL_INT(10, fake.writes[0].slave);
    TEST_ASSERT_EQUAL_INT(0x0100, fake.writes[0].reg);
    TEST_ASSERT_EQUAL_UINT16(22, fake.writes[0].value);

    const auto *ping = fake.firstFrameWithFunctionCode(0x03);
    TEST_ASSERT_NOT_NULL(ping);
    TEST_ASSERT_EQUAL_UINT8(0x01, ping->bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, ping->bytes[3]);
}

// INV-2: primary FC06 write uses writeSingleRegister to the configured register.
void test_primary_write_fc06_uses_single_register(void) {
    FakeModbusRS485 fake;
    fake.respondingAddresses.insert(22);
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0007, 6, 0x0007);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fake.writes.size() >= 1);
    TEST_ASSERT_FALSE(fake.writes[0].multi);
    TEST_ASSERT_EQUAL_INT(10, fake.writes[0].slave);
    TEST_ASSERT_EQUAL_INT(0x0007, fake.writes[0].reg);
    TEST_ASSERT_EQUAL_UINT16(22, fake.writes[0].value);
}

// Primary write that fails at the known address retries the same configured
// register/FC as a broadcast before any legacy fallback.
void test_primary_write_falls_back_to_broadcast_at_configured_register(void) {
    FakeModbusRS485 fake;
    // The device only answers after the broadcast write (the 2nd write).
    fake.respondAddressAfterWrites = 22;
    fake.respondAfterWriteCount = 2;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fake.writes.size() >= 2);
    TEST_ASSERT_TRUE(fake.writes[0].multi);
    TEST_ASSERT_EQUAL_INT(10, fake.writes[0].slave);
    TEST_ASSERT_EQUAL_INT(0x0100, fake.writes[0].reg);
    TEST_ASSERT_TRUE(fake.writes[1].multi);
    TEST_ASSERT_EQUAL_INT(0x00, fake.writes[1].slave);
    TEST_ASSERT_EQUAL_INT(0x0100, fake.writes[1].reg);
    TEST_ASSERT_EQUAL_UINT16(22, fake.writes[1].value);
}

// INV-3 + INV-6: unconfirmed primary falls back to the legacy broadcast
// sequence (FC16->0x0001, then FC06->0x0000) and returns false when nothing
// confirms, leaving the caller's stored address untouched.
void test_unconfirmed_primary_falls_back_to_legacy_broadcast(void) {
    FakeModbusRS485 fake;
    // No address ever responds -> neither primary nor legacy confirms.
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_FALSE(ok);

    bool primaryWrite = false;
    bool legacyFc16 = false;
    bool legacyFc06 = false;
    for (const auto &w : fake.writes) {
        if (w.multi && w.slave == 10 && w.reg == 0x0100) primaryWrite = true;
        if (w.multi && w.slave == 0x00 && w.reg == 0x0001) legacyFc16 = true;
        if (!w.multi && w.slave == 0x00 && w.reg == 0x0000) legacyFc06 = true;
    }
    TEST_ASSERT_TRUE(primaryWrite);
    TEST_ASSERT_TRUE(legacyFc16);
    TEST_ASSERT_TRUE(legacyFc06);
}

// INV-4: fields left at 0 are used verbatim - no primary write to the current
// address; the legacy broadcast path runs and the ping uses register 0.
void test_zero_fields_skip_primary_and_use_legacy(void) {
    FakeModbusRS485 fake;
    fake.respondingAddresses.insert(22);
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    bool ok = reader.writeSensorAddress(10, 22, 0, 0, 0, 0);

    TEST_ASSERT_TRUE(ok);

    for (const auto &w : fake.writes) {
        TEST_ASSERT_NOT_EQUAL(10, w.slave);
    }
    TEST_ASSERT_TRUE(fake.writes.size() >= 1);
    TEST_ASSERT_FALSE(fake.writes[0].multi);
    TEST_ASSERT_EQUAL_INT(0x00, fake.writes[0].slave);
    TEST_ASSERT_EQUAL_INT(0x0000, fake.writes[0].reg);

    const FakeModbusRS485::Frame *ping = nullptr;
    for (const auto &f : fake.sent) {
        if (f.bytes.size() >= 2 && f.bytes[0] == 22 && f.bytes[1] == 0x03) {
            ping = &f;
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(ping);
    TEST_ASSERT_EQUAL_UINT8(0x00, ping->bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, ping->bytes[3]);
}

// INV-1: scan verification ping is built from modbusPingAddress.
void test_scan_ping_uses_configured_ping_register(void) {
    FakeModbusRS485 p0, p1, p2, p3;
    p0.respondingAddresses.insert(22);
    std::vector<IModbusRS485 *> ports{ &p0, &p1, &p2, &p3 };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbusReader(ports, arduino, nullptr, nullptr);
    SensorReader sensorReader(nullptr, &modbusReader, &arduino, nullptr, nullptr);

    SensorEntity sensor;
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.modbusDeviceAddress = 22;
    sensor.baudRate = 9600;
    sensor.modbusPingAddress = 0x0100;

    ModbusRegisterConfig cfg;
    cfg.functionCode = 0x03;
    cfg.startRegister = 0x0000;
    cfg.numRegisters = 1;
    cfg.multiplier = 1.0f;
    cfg.offset = 0.0f;
    cfg.channelName = "ch";
    cfg.channel = 0;
    cfg.canConnectMinValue = -1.0f;
    cfg.canConnectMaxValue = 100.0f;
    sensor.modbusRegisterConfigs.push_back(cfg);

    sensorReader.scanSensor(sensor);

    TEST_ASSERT_TRUE(p0.sent.size() >= 1);
    const auto &ping = p0.sent[0].bytes;
    TEST_ASSERT_EQUAL_UINT8(0x01, ping[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, ping[3]);
}

// INV-5: non-Modbus models never reach the Modbus write path.
void test_non_modbus_sensor_does_not_write_address(void) {
    FakeModbusRS485 fake;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbusReader(ports, arduino, nullptr, nullptr);
    SensorReader sensorReader(nullptr, &modbusReader, &arduino, nullptr, nullptr);

    SensorEntity sensor;
    sensor.protocol = "Analog";
    sensor.pin = 0;

    bool ok = sensorReader.writeSensorAddress(sensor, 22);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(0, (int)fake.writes.size());
    TEST_ASSERT_EQUAL_INT(0, (int)fake.sent.size());
}

// Reference sync round-trip: the three fields survive SensorType JSON, including 0.
void test_sensor_type_json_round_trip(void) {
    JsonConverter concrete;
    IJsonConverter &converter = concrete;

    std::string json =
        "{\"uid\":\"u1\",\"name\":\"ry_ws12s\",\"protocol\":\"ModBus\","
        "\"modbusDeviceAddress\":1,\"baudRate\":9600,"
        "\"modbusPingAddress\":256,\"registerToStoreModbusDeviceAddress\":256,"
        "\"writeRegistersCommand\":16}";

    SensorType parsed;
    TEST_ASSERT_TRUE(converter.sensorTypeFromJson(json, parsed));
    TEST_ASSERT_EQUAL_UINT16(256, parsed.modbusPingAddress);
    TEST_ASSERT_EQUAL_UINT16(256, parsed.registerToStoreModbusDeviceAddress);
    TEST_ASSERT_EQUAL_UINT8(16, parsed.writeRegistersCommand);

    std::string serialized = converter.sensorTypeToJson(parsed);
    SensorType reparsed;
    TEST_ASSERT_TRUE(converter.sensorTypeFromJson(serialized, reparsed));
    TEST_ASSERT_EQUAL_UINT16(256, reparsed.modbusPingAddress);
    TEST_ASSERT_EQUAL_UINT16(256, reparsed.registerToStoreModbusDeviceAddress);
    TEST_ASSERT_EQUAL_UINT8(16, reparsed.writeRegistersCommand);

    std::string missing = "{\"uid\":\"u2\",\"name\":\"legacy\",\"protocol\":\"ModBus\"}";
    SensorType legacy;
    TEST_ASSERT_TRUE(converter.sensorTypeFromJson(missing, legacy));
    TEST_ASSERT_EQUAL_UINT16(0, legacy.modbusPingAddress);
    TEST_ASSERT_EQUAL_UINT16(0, legacy.registerToStoreModbusDeviceAddress);
    TEST_ASSERT_EQUAL_UINT8(0, legacy.writeRegistersCommand);
}

// The fields persist on SensorEntity so the duplicate-resolution path keeps them
// across the repository round-trip.
void test_sensor_entity_json_round_trip(void) {
    JsonConverter concrete;
    IJsonConverter &converter = concrete;

    SensorEntity sensor;
    sensor.uid = "mod.00";
    sensor.protocol = "ModBus";
    sensor.modbusDeviceAddress = 5;
    sensor.modbusPingAddress = 0x0100;
    sensor.registerToStoreModbusDeviceAddress = 0x0100;
    sensor.writeRegistersCommand = 16;

    std::string json = converter.sensorToJson(sensor);
    SensorEntity parsed;
    TEST_ASSERT_TRUE(converter.sensorFromJson(json, parsed));
    TEST_ASSERT_EQUAL_UINT16(0x0100, parsed.modbusPingAddress);
    TEST_ASSERT_EQUAL_UINT16(0x0100, parsed.registerToStoreModbusDeviceAddress);
    TEST_ASSERT_EQUAL_UINT8(16, parsed.writeRegistersCommand);
}

// RK520-02 style signed 16-bit register: raw 0xFFCE with multiplier 0.1 -> -5.0.
void test_read_register_signed_int16(void) {
    class StaticResponseModbusRS485 : public IModbusRS485 {
    public:
        std::vector<uint8_t> response;
        bool powered = false;

        void begin(unsigned long) override {}
        void sendData(uint8_t *, uint8_t) override {}

        int readAvailable(uint8_t *buffer, size_t maxLength) override {
            size_t n = response.size();
            if (n > maxLength) n = maxLength;
            for (size_t i = 0; i < n; i++) buffer[i] = response[i];
            return (int)n;
        }

        int available() override { return 0; }
        const char *getPortName() const override { return "fake"; }
        uint16_t calculateCRC(uint8_t *, uint8_t) override { return 0; }
        bool writeRegister(int, int, uint16_t) override { return true; }
        bool writeSingleRegister(int, int, uint16_t) override { return true; }
        bool powerOnPort() override { bool ch = !powered; powered = true; return ch; }
        bool powerOffPort() override { bool ch = powered; powered = false; return ch; }
    };

    StaticResponseModbusRS485 fake;
    // 01 03 02 FF CE 00 00 - temperature register, raw -50 (0xFFCE), multiplier 0.1 -> -5.0
    fake.response = { 0x01, 0x03, 0x02, 0xFF, 0xCE, 0x00, 0x00 };

    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    ModbusRegisterConfig cfg;
    cfg.functionCode = 0x03;
    cfg.startRegister = 0x0000;
    cfg.numRegisters = 1;
    cfg.multiplier = 0.1f;
    cfg.offset = 0.0f;
    cfg.dataType = ModbusDataType::Int16;

    float value = 0.0f;
    bool ok = reader.readRegister(0x01, cfg, value, 0);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, value);
}

// Some sensors (e.g. RK520-02) only apply a new Modbus address after a power cycle.
void test_primary_write_confirms_after_power_cycle(void) {
    FakeModbusRS485 fake;
    fake.respondingAddresses.insert(22);
    fake.requiresSecondPowerOn = true;

    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0200, 6, 0x0000);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(2, fake.powerOnCount);
    TEST_ASSERT_TRUE(fake.writes.size() >= 1);
    TEST_ASSERT_FALSE(fake.writes[0].multi);
    TEST_ASSERT_EQUAL_INT(10, fake.writes[0].slave);
    TEST_ASSERT_EQUAL_INT(0x0200, fake.writes[0].reg);
    TEST_ASSERT_EQUAL_UINT16(22, fake.writes[0].value);
}

void test_sensor_entity_json_round_trip_with_data_type(void) {
    JsonConverter concrete;
    IJsonConverter &converter = concrete;

    SensorEntity sensor;
    sensor.uid = "mod.00";
    sensor.protocol = "ModBus";
    sensor.modbusDeviceAddress = 1;

    ModbusRegisterConfig cfg;
    cfg.functionCode = 0x03;
    cfg.startRegister = 0x0000;
    cfg.numRegisters = 1;
    cfg.multiplier = 0.1f;
    cfg.offset = 0.0f;
    cfg.channelName = "temp";
    cfg.channel = 0;
    cfg.sensorType = "soil_temp";
    cfg.canConnectMinValue = -30.0f;
    cfg.canConnectMaxValue = 70.0f;
    cfg.dataType = ModbusDataType::Int16;
    sensor.modbusRegisterConfigs.push_back(cfg);

    std::string json = converter.sensorToJson(sensor);
    SensorEntity parsed;
    TEST_ASSERT_TRUE(converter.sensorFromJson(json, parsed));
    TEST_ASSERT_EQUAL(1, (int)parsed.modbusRegisterConfigs.size());
    TEST_ASSERT_TRUE(parsed.modbusRegisterConfigs[0].dataType == ModbusDataType::Int16);
}

void test_sensor_entity_json_defaults_data_type_to_uint16(void) {
    JsonConverter concrete;
    IJsonConverter &converter = concrete;

    std::string json =
        "{\"uid\":\"mod.00\",\"protocol\":\"ModBus\","
        "\"modbusRegisterConfigs\":[{"
        "\"functionCode\":3,\"startRegister\":0,\"numRegisters\":1,"
        "\"channel_name\":\"x\",\"channel\":0,\"sensorType\":\"x\","
        "\"canConnectMinValue\":0,\"canConnectMaxValue\":100}]}";

    SensorEntity sensor;
    TEST_ASSERT_TRUE(converter.sensorFromJson(json, sensor));
    TEST_ASSERT_EQUAL(1, (int)sensor.modbusRegisterConfigs.size());
    TEST_ASSERT_TRUE(sensor.modbusRegisterConfigs[0].dataType == ModbusDataType::UInt16);
}

extern "C" int run_modbus_address_registers_tests(void) {
    RUN_TEST(test_primary_write_fc16_uses_configured_register_and_current_address);
    RUN_TEST(test_primary_write_fc06_uses_single_register);
    RUN_TEST(test_primary_write_falls_back_to_broadcast_at_configured_register);
    RUN_TEST(test_unconfirmed_primary_falls_back_to_legacy_broadcast);
    RUN_TEST(test_zero_fields_skip_primary_and_use_legacy);
    RUN_TEST(test_scan_ping_uses_configured_ping_register);
    RUN_TEST(test_non_modbus_sensor_does_not_write_address);
    RUN_TEST(test_sensor_type_json_round_trip);
    RUN_TEST(test_sensor_entity_json_round_trip);
    RUN_TEST(test_read_register_signed_int16);
    RUN_TEST(test_primary_write_confirms_after_power_cycle);
    RUN_TEST(test_sensor_entity_json_round_trip_with_data_type);
    RUN_TEST(test_sensor_entity_json_defaults_data_type_to_uint16);
    return 0;
}
