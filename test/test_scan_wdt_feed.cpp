#include <unity.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

#include "Infrastructure/Adapters/Rs485/IModbusRS485.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Entities/Sensor/ModbusRegisterConfig.h"

namespace {

class SilentModbusRS485 : public IModbusRS485 {
public:
    bool powered = false;

    void begin(unsigned long) override {}
    void sendData(uint8_t *, uint8_t) override {}
    int readAvailable(uint8_t *, size_t) override { return 0; }
    int available() override { return 0; }
    const char *getPortName() const override { return "fake"; }
    uint16_t calculateCRC(uint8_t *, uint8_t) override { return 0; }
    bool writeRegister(int, int, uint16_t) override { return true; }
    bool writeSingleRegister(int, int, uint16_t) override { return true; }
    bool powerOnPort() override { bool ch = !powered; powered = true; return ch; }
    bool powerOffPort() override { bool ch = powered; powered = false; return ch; }
    // No clock: a port it has put up is up for good.
    PortPower powerRecord() const override { PortPower r; r.on = powered; return r; }
    // A wire of its own: nothing else is plugged into this stand's line.
    const void *wire() const override { return this; }
};

SensorEntity makeModbusSensor() {
    SensorEntity sensor;
    sensor.protocol = "ModBus";
    sensor.pin = 0;
    sensor.baudRate = 9600;
    sensor.modbusDeviceAddress = 1;
    sensor.modbusPingAddress = 0x0000;

    ModbusRegisterConfig cfg;
    cfg.functionCode = 0x03;
    cfg.startRegister = 0x0000;
    cfg.numRegisters = 1;
    cfg.channelName = "value";
    cfg.canConnectMinValue = 0.0f;
    cfg.canConnectMaxValue = 100.0f;
    sensor.modbusRegisterConfigs.push_back(cfg);

    return sensor;
}

} // namespace

void test_scan_modbus_feeds_task_watchdog(void) {
    SilentModbusRS485 fake;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbus(ports, arduino, nullptr, nullptr);
    SensorReader reader(nullptr, &modbus, &arduino, nullptr, nullptr);

    SensorEntity sensor = makeModbusSensor();
    reader.scanSensor(sensor);

    TEST_ASSERT_TRUE(arduino.feedTaskWatchdogCount >= 1);
}

void test_scan_modbus_feeds_watchdog_around_warmup_delay(void) {
    SilentModbusRS485 fake;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader modbus(ports, arduino, nullptr, nullptr);
    SensorReader reader(nullptr, &modbus, &arduino, nullptr, nullptr);

    SensorEntity sensor = makeModbusSensor();
    reader.scanSensor(sensor);

    TEST_ASSERT_TRUE(arduino.delayCount >= 1);
    TEST_ASSERT_TRUE(arduino.feedTaskWatchdogCount >= 2);
}

extern "C" int run_scan_wdt_feed_tests(void) {
    RUN_TEST(test_scan_modbus_feeds_task_watchdog);
    RUN_TEST(test_scan_modbus_feeds_watchdog_around_warmup_delay);
    return 0;
}
