#include <unity.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <set>
#include <string>

#include "Infrastructure/Adapters/Rs485/IModbusRS485.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"

namespace {

class FakeModbusRS485 : public IModbusRS485 {
public:
    struct Frame { std::vector<uint8_t> bytes; };
    struct WriteCall { bool multi; int slave; int reg; uint16_t value; };

    std::vector<Frame> sent;
    std::vector<WriteCall> writes;
    std::set<int> respondingAddresses;
    uint16_t readbackValue = 1;
    bool powered = false;

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
        if (respondingAddresses.count(addr) == 0) return 0;
        uint8_t resp[7] = {
            (uint8_t)addr, fc, 0x02,
            (uint8_t)(readbackValue >> 8), (uint8_t)(readbackValue & 0xFF),
            0x00, 0x00
        };
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

    bool powerOnPort() override { bool ch = !powered; powered = true; return ch; }
    bool powerOffPort() override { bool ch = powered; powered = false; return ch; }

    int countWritesToRegister(int reg) const {
        int c = 0;
        for (const auto &w : writes) if (w.reg == reg) c++;
        return c;
    }

    const WriteCall *firstWriteToRegister(int reg) const {
        for (const auto &w : writes) if (w.reg == reg) return &w;
        return nullptr;
    }

    const Frame *firstFc03FrameToRegister(int reg) const {
        for (const auto &f : sent) {
            if (f.bytes.size() >= 4 && f.bytes[1] == 0x03 &&
                ((f.bytes[2] << 8) | f.bytes[3]) == reg) {
                return &f;
            }
        }
        return nullptr;
    }
};

SensorEntity makeSensor(const std::string &model, int addr, int pin) {
    SensorEntity sensor;
    sensor.uid = "84:F7:03:FE:88:2C.0" + std::to_string(pin);
    sensor.protocol = "ModBus";
    sensor.model = model;
    sensor.pin = pin;
    sensor.modbusDeviceAddress = addr;
    sensor.baudRate = 9600;
    return sensor;
}

} // namespace

// INV-1: XF700A preparation writes 0x0107=5 (FC06) and reads it back (FC03).
void test_xf700a_writes_accum_time_and_verifies(void) {
    FakeModbusRS485 p0, p1, p2, p3;
    p0.respondingAddresses.insert(255);
    p0.readbackValue = 1;
    std::vector<IModbusRS485 *> ports{ &p0, &p1, &p2, &p3 };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbusReader(ports, arduino, nullptr, nullptr);
    SensorReader sensorReader(nullptr, &modbusReader, &arduino, nullptr, nullptr);

    std::vector<SensorEntity> sensors{ makeSensor("XF700A", 255, 0) };
    sensorReader.prepareSensorsForReading(sensors);

    const auto *w = p0.firstWriteToRegister(0x0107);
    TEST_ASSERT_NOT_NULL(w);
    TEST_ASSERT_FALSE(w->multi);
    TEST_ASSERT_EQUAL_INT(255, w->slave);
    TEST_ASSERT_EQUAL_UINT16(1, w->value);

    const auto *readFrame = p0.firstFc03FrameToRegister(0x0107);
    TEST_ASSERT_NOT_NULL(readFrame);
    TEST_ASSERT_EQUAL_UINT8(255, readFrame->bytes[0]);
}

// INV-2: a non-XF700A Modbus sensor never gets the 0x0107 write.
void test_non_xf700a_no_accum_write(void) {
    FakeModbusRS485 p0, p1, p2, p3;
    p0.respondingAddresses.insert(5);
    std::vector<IModbusRS485 *> ports{ &p0, &p1, &p2, &p3 };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbusReader(ports, arduino, nullptr, nullptr);
    SensorReader sensorReader(nullptr, &modbusReader, &arduino, nullptr, nullptr);

    std::vector<SensorEntity> sensors{ makeSensor("ry_ws12s", 5, 0) };
    sensorReader.prepareSensorsForReading(sensors);

    TEST_ASSERT_EQUAL_INT(0, p0.countWritesToRegister(0x0107));
}

// INV-2: the model gate is case-insensitive.
void test_xf700a_case_insensitive(void) {
    FakeModbusRS485 p0, p1, p2, p3;
    p0.respondingAddresses.insert(255);
    std::vector<IModbusRS485 *> ports{ &p0, &p1, &p2, &p3 };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbusReader(ports, arduino, nullptr, nullptr);
    SensorReader sensorReader(nullptr, &modbusReader, &arduino, nullptr, nullptr);

    std::vector<SensorEntity> sensors{ makeSensor("xf700a", 255, 0) };
    sensorReader.prepareSensorsForReading(sensors);

    TEST_ASSERT_EQUAL_INT(1, p0.countWritesToRegister(0x0107));
}

// INV-2/INV-3: address 0 is skipped, no write.
void test_xf700a_addr_zero_no_write(void) {
    FakeModbusRS485 p0, p1, p2, p3;
    std::vector<IModbusRS485 *> ports{ &p0, &p1, &p2, &p3 };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbusReader(ports, arduino, nullptr, nullptr);
    SensorReader sensorReader(nullptr, &modbusReader, &arduino, nullptr, nullptr);

    std::vector<SensorEntity> sensors{ makeSensor("XF700A", 0, 0) };
    sensorReader.prepareSensorsForReading(sensors);

    TEST_ASSERT_EQUAL_INT(0, p0.countWritesToRegister(0x0107));
}

// INV-4: re-running preparation writes the constant 5 again, no accumulation.
void test_xf700a_idempotent_two_passes(void) {
    FakeModbusRS485 p0, p1, p2, p3;
    p0.respondingAddresses.insert(255);
    std::vector<IModbusRS485 *> ports{ &p0, &p1, &p2, &p3 };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbusReader(ports, arduino, nullptr, nullptr);
    SensorReader sensorReader(nullptr, &modbusReader, &arduino, nullptr, nullptr);

    std::vector<SensorEntity> sensors{ makeSensor("XF700A", 255, 0) };
    sensorReader.prepareSensorsForReading(sensors);
    sensorReader.prepareSensorsForReading(sensors);

    TEST_ASSERT_EQUAL_INT(2, p0.countWritesToRegister(0x0107));
    for (const auto &w : p0.writes) {
        if (w.reg == 0x0107) TEST_ASSERT_EQUAL_UINT16(1, w.value);
    }
}

extern "C" int run_xf700a_rainfall_window_tests(void) {
    RUN_TEST(test_xf700a_writes_accum_time_and_verifies);
    RUN_TEST(test_non_xf700a_no_accum_write);
    RUN_TEST(test_xf700a_case_insensitive);
    RUN_TEST(test_xf700a_addr_zero_no_write);
    RUN_TEST(test_xf700a_idempotent_two_passes);
    return 0;
}
