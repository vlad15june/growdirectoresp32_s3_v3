#include <unity.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <set>
#include <map>
#include <utility>
#include <algorithm>

#include "Infrastructure/Adapters/Rs485/IModbusRS485.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

namespace {

class MockModbusRS485 : public IModbusRS485 {
public:
    struct Frame { std::vector<uint8_t> bytes; };
    struct WriteCall { bool multi; int slave; int reg; uint16_t value; };

    std::vector<Frame> sent;
    std::vector<WriteCall> writes;

    std::map<std::pair<int, uint8_t>, std::vector<uint8_t>> responses;

    // Responses delivered piece by piece: each readAvailable() call pops the
    // next chunk, mimicking a device that trickles the frame out over time.
    std::map<std::pair<int, uint8_t>, std::vector<std::vector<uint8_t>>> chunkedResponses;

    std::set<int> respondingAddresses;
    int respondAddressAfterWrites = -1;
    size_t respondAfterWriteCount = 0;
    bool powered = false;

    // Strict like the board: a UART nobody began sends nothing and receives nothing. Every
    // stand below begins it through reader.init(), as the boot does before the first frame.
    bool begun = false;
    int droppedBeforeBegin = 0;

    // A zero rate leaves the board's UART without a driver: the core's rate detection fails.
    void begin(unsigned long baud) override { begun = (baud != 0); }

    void sendData(uint8_t *data, uint8_t length) override {
        if (!begun) { droppedBeforeBegin++; return; }
        Frame f;
        f.bytes.assign(data, data + length);
        sent.push_back(f);
    }

    int readAvailable(uint8_t *buffer, size_t maxLength) override {
        if (!begun || sent.empty()) return 0;
        const auto &last = sent.back().bytes;
        if (last.size() < 2) return 0;
        int addr = last[0];
        uint8_t fc = last[1];

        auto it = responses.find({addr, fc});
        if (it != responses.end()) {
            const auto &bytes = it->second;
            size_t n = bytes.size();
            if (n > maxLength) n = maxLength;
            for (size_t i = 0; i < n; i++) buffer[i] = bytes[i];
            return (int)n;
        }

        auto cit = chunkedResponses.find({addr, fc});
        if (cit != chunkedResponses.end()) {
            if (cit->second.empty()) return 0;
            auto chunk = cit->second.front();
            cit->second.erase(cit->second.begin());
            size_t n = chunk.size();
            if (n > maxLength) n = maxLength;
            for (size_t i = 0; i < n; i++) buffer[i] = chunk[i];
            return (int)n;
        }

        bool responds = respondingAddresses.count(addr) != 0;
        if (addr == respondAddressAfterWrites && writes.size() >= respondAfterWriteCount) {
            responds = true;
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
        if (!begun) { droppedBeforeBegin++; return false; }
        writes.push_back({ true, slave, reg, value });
        return true;
    }

    bool writeSingleRegister(int slave, int reg, uint16_t value) override {
        if (!begun) { droppedBeforeBegin++; return false; }
        writes.push_back({ false, slave, reg, value });
        return true;
    }

    bool powerOnPort() override { bool ch = !powered; powered = true; return ch; }
    bool powerOffPort() override { bool ch = powered; powered = false; return ch; }
    // No clock: a port it has put up is up for good.
    PortPower powerRecord() const override { PortPower r; r.on = powered; return r; }
    // A wire of its own: nothing else is plugged into this stand's line.
    const void *wire() const override { return this; }

    bool anyFrameStartsWith(const std::vector<uint8_t> &prefix) const {
        for (const auto &f : sent) {
            if (f.bytes.size() < prefix.size()) continue;
            bool match = true;
            for (size_t i = 0; i < prefix.size(); i++) {
                if (f.bytes[i] != prefix[i]) { match = false; break; }
            }
            if (match) return true;
        }
        return false;
    }

    bool hasWildcardFrameWithFc(uint8_t fc) const {
        for (const auto &f : sent) {
            if (f.bytes.size() >= 2 && f.bytes[0] == 0xFF && f.bytes[1] == fc) return true;
        }
        return false;
    }

    bool hasWrite(bool multi, int slave, int reg, uint16_t value) const {
        for (const auto &w : writes) {
            if (w.multi == multi && w.slave == slave && w.reg == reg && w.value == value) return true;
        }
        return false;
    }

    bool hasWriteToSlaveReg(bool multi, int slave, int reg) const {
        for (const auto &w : writes) {
            if (w.multi == multi && w.slave == slave && w.reg == reg) return true;
        }
        return false;
    }
};

} // namespace

// INV-1: when the primary write confirms, the FF-wildcard stage must not run.
void test_primary_confirmed_skips_ff_fallback(void) {
    MockModbusRS485 fake;
    fake.respondingAddresses.insert(22);
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(fake.hasWildcardFrameWithFc(0x03));
    TEST_ASSERT_FALSE(fake.hasWildcardFrameWithFc(0x6E));
    TEST_ASSERT_FALSE(fake.hasWrite(false, 5, 0x0100, 22));
}

void test_ws12s_stage_query_write_ping_confirms(void) {
    MockModbusRS485 fake;
    fake.responses[{0xFF, 0x03}] = { 0xFF, 0x03, 0x02, 0x00, 0x05, 0x00, 0x00 };
    fake.respondAddressAfterWrites = 22;
    fake.respondAfterWriteCount = 3;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x03));
    TEST_ASSERT_TRUE(fake.hasWrite(false, 5, 0x0100, 22));
    TEST_ASSERT_FALSE(fake.hasWildcardFrameWithFc(0x6E));
}

void test_cwyw_stage_runs_after_ws12s_timeout_and_confirms(void) {
    MockModbusRS485 fake;
    fake.responses[{0xFF, 0x6E}] = { 0xFF, 0x6E, 0x02, 0x00, 0x07, 0x00, 0x00 };
    fake.respondAddressAfterWrites = 22;
    fake.respondAfterWriteCount = 3;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x03));
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x6E));
    TEST_ASSERT_TRUE(fake.hasWrite(true, 7, 0x0018, 22));
}

// Regression: the CWYW device trickles its 7-byte reply out over ~200 ms and
// each readAvailable() consumes what it returns. The reader must accumulate
// the partial chunks into a full frame instead of discarding them.
void test_cwyw_stage_accepts_partial_frame_chunks(void) {
    MockModbusRS485 fake;
    fake.chunkedResponses[{0xFF, 0x6E}] = {
        { 0x01, 0x6E },
        { 0x02, 0x00, 0x07 },
        { 0x00, 0x00 },
    };
    fake.respondAddressAfterWrites = 22;
    fake.respondAfterWriteCount = 3;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x6E));
    TEST_ASSERT_TRUE(fake.hasWrite(true, 7, 0x0018, 22));
}

// INV-5: both FF stages fail -> legacy broadcast loop runs as today, return false.
void test_both_stages_fail_then_legacy_loop_runs(void) {
    MockModbusRS485 fake;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x03));
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x6E));
    TEST_ASSERT_TRUE(fake.hasWriteToSlaveReg(false, 0x00, 0x0000));
    TEST_ASSERT_TRUE(fake.hasWriteToSlaveReg(true, 0x00, 0x0001));
}

// INV-7: FF query CRC mismatch -> stage rejects, no address-specific write at discovered addr.
void test_ff_query_crc_mismatch_skips_stage(void) {
    MockModbusRS485 fake;
    // Mock calculateCRC returns 0; trailing CRC bytes here are non-zero -> mismatch.
    fake.responses[{0xFF, 0x03}] = { 0x05, 0x03, 0x02, 0x00, 0xAB, 0xAA, 0xBB };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    for (const auto &w : fake.writes) {
        // No WS12S address-specific write should appear at the discovered addr.
        bool ws12sWrite = (!w.multi && w.slave == 5 && w.reg == 0x0100);
        TEST_ASSERT_FALSE(ws12sWrite);
    }
}

void test_ws12s_address_parsed_from_payload_not_slave_byte(void) {
    MockModbusRS485 fake;
    fake.responses[{0xFF, 0x03}] = { 0xFF, 0x03, 0x02, 0x00, 0x73, 0x00, 0x00 };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(fake.hasWrite(false, 0x73, 0x0100, 22));
    TEST_ASSERT_FALSE(fake.hasWrite(false, 0xFF, 0x0100, 22));
}

void test_ws12s_address_byte_zero_in_payload_skips_stage(void) {
    MockModbusRS485 fake;
    fake.responses[{0xFF, 0x03}] = { 0xFF, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00 };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    for (const auto &w : fake.writes) {
        bool badWs12sWrite = (!w.multi && w.reg == 0x0100 && w.slave != 0x00);
        TEST_ASSERT_FALSE(badWs12sWrite);
    }
}

void test_ws12s_address_byte_above_247_skips_stage(void) {
    MockModbusRS485 fake;
    fake.responses[{0xFF, 0x03}] = { 0xFF, 0x03, 0x02, 0x00, 0xFA, 0x00, 0x00 };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_FALSE(fake.hasWrite(false, 0xFA, 0x0100, 22));
}

// INV-7: FF query FC echo mismatch -> stage rejects.
void test_ff_query_fc_mismatch_skips_stage(void) {
    MockModbusRS485 fake;
    // Reply echoes FC06 instead of FC03.
    fake.responses[{0xFF, 0x03}] = { 0x05, 0x06, 0x02, 0x00, 0xAB, 0x00, 0x00 };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    for (const auto &w : fake.writes) {
        bool ws12sWrite = (!w.multi && w.slave == 5 && w.reg == 0x0100);
        TEST_ASSERT_FALSE(ws12sWrite);
    }
}

// INV-8: across all scenarios, no FF restore-factory frame must ever be emitted.
void test_no_restore_factory_frame_ever_emitted(void) {
    MockModbusRS485 fake;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    std::vector<uint8_t> banned = { 0xFF, 0x06, 0x10, 0x00, 0x00, 0xAA };
    TEST_ASSERT_FALSE(fake.anyFrameStartsWith(banned));
}

// Channels of other sensor types must keep using the server multiplier as-is.
void test_other_channels_keep_server_multiplier(void) {
    MockModbusRS485 fake;
    fake.responses[{0x01, 0x03}] = { 0x01, 0x03, 0x04, 0x00, 0x00, 0x08, 0x5A, 0x00, 0x00 };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);
    reader.init();

    ModbusRegisterConfig cfg;
    cfg.functionCode = 0x03;
    cfg.startRegister = 0x0004;
    cfg.numRegisters = 2;
    cfg.multiplier = 0.001f;
    cfg.offset = 0.0f;
    cfg.channelName = "pressure";
    cfg.channel = 3;
    cfg.sensorType = "pressure";
    cfg.canConnectMinValue = 0;
    cfg.canConnectMaxValue = 200000;

    float value = 0.0f;
    bool ok = reader.readRegister(0x01, cfg, value, 0);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.138f, value);
}

extern "C" int run_ff_wildcard_broadcast_fallback_tests(void) {
    RUN_TEST(test_primary_confirmed_skips_ff_fallback);
    RUN_TEST(test_ws12s_stage_query_write_ping_confirms);
    RUN_TEST(test_cwyw_stage_runs_after_ws12s_timeout_and_confirms);
    RUN_TEST(test_cwyw_stage_accepts_partial_frame_chunks);
    RUN_TEST(test_both_stages_fail_then_legacy_loop_runs);
    RUN_TEST(test_ff_query_crc_mismatch_skips_stage);
    RUN_TEST(test_ws12s_address_parsed_from_payload_not_slave_byte);
    RUN_TEST(test_ws12s_address_byte_zero_in_payload_skips_stage);
    RUN_TEST(test_ws12s_address_byte_above_247_skips_stage);
    RUN_TEST(test_ff_query_fc_mismatch_skips_stage);
    RUN_TEST(test_no_restore_factory_frame_ever_emitted);
    RUN_TEST(test_other_channels_keep_server_multiplier);
    return 0;
}
