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

    std::set<int> respondingAddresses;
    int respondAddressAfterWrites = -1;
    size_t respondAfterWriteCount = 0;
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

        auto it = responses.find({addr, fc});
        if (it != responses.end()) {
            const auto &bytes = it->second;
            size_t n = bytes.size();
            if (n > maxLength) n = maxLength;
            for (size_t i = 0; i < n; i++) buffer[i] = bytes[i];
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
        writes.push_back({ true, slave, reg, value });
        return true;
    }

    bool writeSingleRegister(int slave, int reg, uint16_t value) override {
        writes.push_back({ false, slave, reg, value });
        return true;
    }

    bool powerOnPort() override { bool ch = !powered; powered = true; return ch; }
    bool powerOffPort() override { bool ch = powered; powered = false; return ch; }

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

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(fake.hasWildcardFrameWithFc(0x03));
    TEST_ASSERT_FALSE(fake.hasWildcardFrameWithFc(0x6E));
    TEST_ASSERT_FALSE(fake.hasWrite(false, 5, 0x0100, 22));
}

// INV-2: WS12S stage discovers, writes via FC06, ping confirms, returns true.
void test_ws12s_stage_query_write_ping_confirms(void) {
    MockModbusRS485 fake;
    // WS12S query reply: discovered addr=5, fc echo=0x03, valid (mock CRC=0).
    fake.responses[{0xFF, 0x03}] = { 0x05, 0x03, 0x02, 0x00, 0xAB, 0x00, 0x00 };
    // Address 22 starts responding only after 3 writes:
    // #1 primary FC16 at 10, #2 primary broadcast FC16 at 0, #3 WS12S FC06 at 5.
    fake.respondAddressAfterWrites = 22;
    fake.respondAfterWriteCount = 3;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x03));
    TEST_ASSERT_TRUE(fake.hasWrite(false, 5, 0x0100, 22));
    TEST_ASSERT_FALSE(fake.hasWildcardFrameWithFc(0x6E));
}

// INV-3: CWYW stage runs when WS12S query times out; discovered addr written via FC16; ping confirms.
void test_cwyw_stage_runs_after_ws12s_timeout_and_confirms(void) {
    MockModbusRS485 fake;
    // WS12S query: no scripted response -> times out.
    // CWYW query reply: discovered addr=7, fc echo=0x6E, valid (mock CRC=0).
    fake.responses[{0xFF, 0x6E}] = { 0x07, 0x6E, 0x02, 0x00, 0xAB, 0x00, 0x00 };
    // Address 22 responds after 3 writes:
    // #1 primary FC16 at 10, #2 primary broadcast FC16 at 0, #3 CWYW FC16 at 7.
    fake.respondAddressAfterWrites = 22;
    fake.respondAfterWriteCount = 3;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    bool ok = reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x03));
    TEST_ASSERT_TRUE(fake.hasWildcardFrameWithFc(0x6E));
    TEST_ASSERT_TRUE(fake.hasWrite(true, 7, 0x0018, 22));
}

// INV-5: both FF stages fail -> legacy broadcast loop runs as today, return false.
void test_both_stages_fail_then_legacy_loop_runs(void) {
    MockModbusRS485 fake;
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

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

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    for (const auto &w : fake.writes) {
        // No WS12S address-specific write should appear at the discovered addr.
        bool ws12sWrite = (!w.multi && w.slave == 5 && w.reg == 0x0100);
        TEST_ASSERT_FALSE(ws12sWrite);
    }
}

// INV-7: FF query slave-byte 0xFF -> stage rejects, never writes to 0xFF.
void test_ff_query_bad_slave_ff_skips_stage(void) {
    MockModbusRS485 fake;
    fake.responses[{0xFF, 0x03}] = { 0xFF, 0x03, 0x02, 0x00, 0xAB, 0x00, 0x00 };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    for (const auto &w : fake.writes) {
        TEST_ASSERT_NOT_EQUAL(0xFF, w.slave);
    }
}

// INV-7: FF query slave-byte 0x00 -> stage rejects, no WS12S addr-specific write at slave=0.
void test_ff_query_bad_slave_zero_skips_stage(void) {
    MockModbusRS485 fake;
    fake.responses[{0xFF, 0x03}] = { 0x00, 0x03, 0x02, 0x00, 0xAB, 0x00, 0x00 };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    // No WS12S FC06 single-register write at register 0x0100 to slave 0x00 must occur.
    for (const auto &w : fake.writes) {
        bool badWs12sWrite = (!w.multi && w.slave == 0x00 && w.reg == 0x0100);
        TEST_ASSERT_FALSE(badWs12sWrite);
    }
}

// INV-7: FF query FC echo mismatch -> stage rejects.
void test_ff_query_fc_mismatch_skips_stage(void) {
    MockModbusRS485 fake;
    // Reply echoes FC06 instead of FC03.
    fake.responses[{0xFF, 0x03}] = { 0x05, 0x06, 0x02, 0x00, 0xAB, 0x00, 0x00 };
    std::vector<IModbusRS485 *> ports{ &fake };
    MockArduinoAdapter arduino;
    ModbusSensorReader reader(ports, arduino, nullptr, nullptr);

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

    reader.writeSensorAddress(10, 22, 0, 0x0100, 16, 0x0100);

    std::vector<uint8_t> banned = { 0xFF, 0x06, 0x10, 0x00, 0x00, 0xAA };
    TEST_ASSERT_FALSE(fake.anyFrameStartsWith(banned));
}

extern "C" int run_ff_wildcard_broadcast_fallback_tests(void) {
    RUN_TEST(test_primary_confirmed_skips_ff_fallback);
    RUN_TEST(test_ws12s_stage_query_write_ping_confirms);
    RUN_TEST(test_cwyw_stage_runs_after_ws12s_timeout_and_confirms);
    RUN_TEST(test_both_stages_fail_then_legacy_loop_runs);
    RUN_TEST(test_ff_query_crc_mismatch_skips_stage);
    RUN_TEST(test_ff_query_bad_slave_ff_skips_stage);
    RUN_TEST(test_ff_query_bad_slave_zero_skips_stage);
    RUN_TEST(test_ff_query_fc_mismatch_skips_stage);
    RUN_TEST(test_no_restore_factory_frame_ever_emitted);
    return 0;
}
