// JsonMemoryPool: which documents the reserved slots actually serve.
//
// The pool exists so that the blocks a conversion needs do not come out of the heap. Slots that
// started at 4 KB would leave every document below that - and most of the converter's documents are
// below it - a plain malloc/free pair of a size that lands between the large blocks the pool
// does hold. This file pins where each capacity comes from, that the deepest real conversion
// chain still finds a free slot for every document it opens, that a request the pool cannot
// serve is counted instead of disappearing, and that the hot path prints nothing.
//
// The claims are made against ArduinoJsonConverter - the converter globals.h gives the device -
// so what is measured is the traffic the board produces, not a test-only allocation.

#include <unity.h>

// Arduino.h stand-in first: ArduinoJsonConverter.h pulls in headers that expect it (env:native).
#include "Mock/ArduinoMock.h"

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Json/JsonMemoryPool.h"

#include <string>
#include <vector>

namespace
{

JsonMemoryPool &pool()
{
    return JsonMemoryPool::instance();
}

struct PoolCounters
{
    uint32_t pooledServed;
    uint32_t heapBypasses;
    uint32_t heapFallbacks;
};

PoolCounters readCounters()
{
    PoolCounters c;
    c.pooledServed = pool().pooledServed();
    c.heapBypasses = pool().heapBypasses();
    c.heapFallbacks = pool().heapFallbacks();
    return c;
}

// A module command that carries an options object: the converter opens a second, small document
// for the options while the outer one is still alive.
const char *kModuleCommandJson = R"({
    "growDirectorUid":"58:CF:79:A4:77:32",
    "type":"ModuleCommandNotification",
    "commandUid":"7f0a1e9c-2b44-4f0e-9d3a-11c2f0a4bb01",
    "createdAt":"2026-08-22T10:15:00.000Z",
    "command":"ModbusRead",
    "channel":1,
    "address":17,
    "function":3,
    "register":100,
    "count":2
})";

const char *kDeviceCommandJson = R"({
    "id":42,
    "uid":"58:CF:79:A4:77:32.01",
    "requestedState":"on",
    "status":"Created",
    "switchType":"manual",
    "createdDate":"2026-08-22T10:15:00.000Z"
})";

const char *kModbusRegisterConfigJson = R"({
    "functionCode":4,
    "startRegister":12,
    "numRegisters":2,
    "multiplier":0.1,
    "channel_name":"temperature",
    "channel":1,
    "sensorType":"temp"
})";

const char *kI2cRegisterConfigJson = R"({
    "register":16,
    "length":2,
    "multiplier":0.01,
    "channel_name":"humidity",
    "channel":2,
    "sensorType":"hum"
})";

const char *kTwoRulesJson = R"([
    {
        "uid":"ff60a9e6-4dc7-4d7e-a659-55fb6d9f98db",
        "version":3,
        "deviceUid":"7C:DF:A1:96:B4:94.01",
        "sensorUid":"84:F7:03:D7:8E:60.00.01",
        "ruleType":"step",
        "valueTarget":50,
        "direction":"down",
        "workingTime":60,
        "waitingTime":600,
        "isRuleOn":false,
        "mode":"Off",
        "state":"Synchronized",
        "conditions":[{"sensorUids":["84:F7:03:D7:8E:60.00.01"],"valueTarget":50,"direction":"down"}],
        "daysOfWeek":[],
        "daysOfMonth":[]
    },
    {
        "uid":"aa11bb22-cc33-dd44-ee55-ff6677889900",
        "version":1,
        "deviceUid":"7C:DF:A1:96:B4:94.02",
        "sensorUid":"84:F7:03:D7:8E:60.01.01",
        "ruleType":"step",
        "valueTarget":30,
        "direction":"up",
        "workingTime":30,
        "waitingTime":300,
        "isRuleOn":false,
        "mode":"Off",
        "state":"Synchronized",
        "conditions":[{"sensorUids":["84:F7:03:D7:8E:60.01.01"],"valueTarget":30,"direction":"up"}],
        "daysOfWeek":[],
        "daysOfMonth":[]
    }
])";

// Every capacity the converters ask for below the old 4 KB floor.
void test_every_small_capacity_is_served_from_a_slot(void)
{
    const size_t capacities[] = {256, 512, 1024, 2048};

    for (size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i)
    {
        const PoolCounters before = readCounters();
        {
            PooledJsonDocument doc(capacities[i]);
            doc["k"] = "v";
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.pooledServed + 1, pool().pooledServed(),
                                             "document of this capacity was not served from a slot");
        }
        const PoolCounters after = readCounters();
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapBypasses, after.heapBypasses,
                                         "document of this capacity went to the heap");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapFallbacks, after.heapFallbacks,
                                         "no slot was free for this capacity");
    }
}

// The same claim, made where it matters: through the converter the device runs.
void test_small_converters_take_their_documents_from_the_pool(void)
{
    ArduinoJsonConverter converter;
    const PoolCounters before = readCounters();

    // 256: the module-to-module command envelope.
    std::string command;
    std::string targetUid;
    const std::string envelope = converter.remoteModuleCommandToJson("reboot", "58:CF:79:A4:77:32");
    TEST_ASSERT_TRUE(converter.remoteModuleCommandFromJson(envelope, command, targetUid));
    TEST_ASSERT_EQUAL_STRING("reboot", command.c_str());

    // 512: an I2C register configuration.
    I2CRegisterConfig i2cConfig;
    TEST_ASSERT_TRUE(converter.i2cRegisterConfigFromJson(kI2cRegisterConfigJson, i2cConfig));

    // 1024: a Modbus register configuration.
    ModbusRegisterConfig modbusConfig;
    TEST_ASSERT_TRUE(converter.modbusRegisterConfigFromJson(kModbusRegisterConfigJson, modbusConfig));

    // 2048: a device command.
    DeviceCommandDto deviceCommand;
    TEST_ASSERT_TRUE(converter.deviceCommandFromJson(kDeviceCommandJson, deviceCommand));

    const PoolCounters after = readCounters();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapBypasses, after.heapBypasses,
                                     "a converter document below 4 KB still went to the heap");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapFallbacks, after.heapFallbacks,
                                     "a converter document found no free slot");
    // Five documents: the envelope is built and parsed, then three configurations.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.pooledServed + 5, after.pooledServed,
                                     "not every converter document came from a slot");
}

// A small document opened while its parent is still alive needs a slot of its own.
void test_nested_options_document_shares_the_pool_with_its_parent(void)
{
    ArduinoJsonConverter converter;
    const PoolCounters before = readCounters();

    CommandDto commandDto;
    TEST_ASSERT_TRUE(converter.commandFromJson(kModuleCommandJson, commandDto));
    TEST_ASSERT_EQUAL_STRING("ModbusRead", commandDto.command.c_str());
    TEST_ASSERT_FALSE(commandDto.options.empty());

    const PoolCounters after = readCounters();
    // The outer 4 KB document and the 512 B options document.
    TEST_ASSERT_EQUAL_UINT32(before.pooledServed + 2, after.pooledServed);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapBypasses, after.heapBypasses,
                                     "the options document was not pooled");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapFallbacks, after.heapFallbacks,
                                     "the options document found no free slot");
}

// Reading a rule list, with another task's document held across it and a small conversion
// interleaved. Nothing may reach the heap - and the list holds one document, whatever its length:
// the array is cut into its rules as it goes past and each rule is parsed on its own, so there is
// no document sized for the whole answer to find a slot for.
void test_deepest_chain_with_a_concurrent_holder_stays_inside_the_pool(void)
{
    ArduinoJsonConverter converter;
    pool().resetPeakSlotsInUse();
    const PoolCounters before = readCounters();

    {
        // Stand-in for the other task: a telemetry document alive across the whole conversion.
        PooledJsonDocument held(8192);
        held["uid"] = "58:CF:79:A4:77:32";

        std::vector<Rule> rules;
        TEST_ASSERT_TRUE(converter.rulesFromJson(kTwoRulesJson, rules) == JsonListParse::Ok);
        TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)rules.size());
    }

    const PoolCounters after = readCounters();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapFallbacks, after.heapFallbacks,
                                     "the conversion chain ran out of slots");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapBypasses, after.heapBypasses,
                                     "the conversion chain put a document on the heap");
    // What the geometry has to cover here: the document one rule is parsed into, and the held one.
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, pool().peakSlotsInUse(),
                                  "reading a rule list takes a different number of slots than the "
                                  "one document a single rule needs");
}

// The occupancy the small tier is sized for: the deepest chain and the other task hold every
// large slot, and a small conversion still has to find a slot of its own.
void test_small_conversions_survive_the_large_slots_being_taken(void)
{
    ArduinoJsonConverter converter;
    pool().resetPeakSlotsInUse();
    const PoolCounters before = readCounters();

    {
        // The large slots taken: a document of every large capacity the converter asks for, plus
        // another task's telemetry document.
        PooledJsonDocument arrayDoc(16384);
        PooledJsonDocument elementDoc(4096);
        PooledJsonDocument reparsedDoc(4096);
        PooledJsonDocument otherTask(8192);

        std::string command;
        std::string targetUid;
        const std::string envelope = converter.remoteModuleCommandToJson("reboot", "58:CF:79:A4:77:32");
        TEST_ASSERT_TRUE(converter.remoteModuleCommandFromJson(envelope, command, targetUid));

        I2CRegisterConfig i2cConfig;
        TEST_ASSERT_TRUE(converter.i2cRegisterConfigFromJson(kI2cRegisterConfigJson, i2cConfig));

        ModbusRegisterConfig modbusConfig;
        TEST_ASSERT_TRUE(converter.modbusRegisterConfigFromJson(kModbusRegisterConfigJson, modbusConfig));

        DeviceCommandDto deviceCommand;
        TEST_ASSERT_TRUE(converter.deviceCommandFromJson(kDeviceCommandJson, deviceCommand));
    }

    const PoolCounters after = readCounters();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapFallbacks, after.heapFallbacks,
                                     "a small conversion found no slot while the large ones were taken");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapBypasses, after.heapBypasses,
                                     "a small conversion went to the heap");
    // Four large documents held plus the one small document open at a time.
    TEST_ASSERT_EQUAL_INT(5, pool().peakSlotsInUse());
}

// What the doubled tiers buy: two documents of the same small capacity alive at once - one per
// task - are both served from the tier, and the 4 KB slots the deepest chain needs are still
// free. Without the second 2048 and the second 512, the overlapping documents would take those
// slots instead and the chain behind them would fall back.
//
// This pins the capacity, not the need for it: nothing here shows that two tasks really do
// convert at the same moment. Three of them can (control loop, outbox, network commands), which
// is why the tiers are doubled, but that is a reading of the task graph rather than a measurement.
void test_the_small_tier_carries_two_documents_of_a_capacity_at_once(void)
{
    ArduinoJsonConverter converter;
    pool().resetPeakSlotsInUse();
    const PoolCounters before = readCounters();

    {
        PooledJsonDocument firstTask2048(2048);
        PooledJsonDocument secondTask2048(2048);
        PooledJsonDocument firstTask512(512);
        PooledJsonDocument secondTask512(512);
        PooledJsonDocument someTask1024(1024);
        PooledJsonDocument someTask256(256);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.pooledServed + 6, pool().pooledServed(),
                                         "the small tier did not carry the overlap");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapFallbacks, pool().heapFallbacks(),
                                         "an overlapping small document found no slot");

        // The large slots were not consumed by the overlap: the chain behind them still fits.
        std::vector<Rule> rules;
        TEST_ASSERT_TRUE(converter.rulesFromJson(kTwoRulesJson, rules) == JsonListParse::Ok);
    }

    const PoolCounters after = readCounters();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapFallbacks, after.heapFallbacks,
                                     "the chain behind the overlap fell back to the heap");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.heapBypasses, after.heapBypasses,
                                     "a document went to the heap");
    // Six small documents held plus the one a rule list opens.
    TEST_ASSERT_EQUAL_INT(7, pool().peakSlotsInUse());
}

// A request the pool cannot serve still gets memory - and is counted, so a pool that stopped
// covering its traffic is visible instead of quietly turning back into malloc.
void test_a_request_the_pool_cannot_serve_is_counted(void)
{
    const PoolCounters before = readCounters();

    // Two 512 slots, then the third 512 B document has to take a larger slot.
    PooledJsonDocument first(512);
    PooledJsonDocument second(512);
    PooledJsonDocument third(512);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.pooledServed + 3, pool().pooledServed(),
                                     "a larger free slot should still serve a 512 B document");

    // Take every slot the 256 B one: nothing is left for another small document.
    PooledJsonDocument fill16384(16384);
    PooledJsonDocument fill8192(8192);
    PooledJsonDocument fill4096a(4096);
    PooledJsonDocument fill4096b(4096);
    PooledJsonDocument fill2048a(2048);
    PooledJsonDocument fill2048b(2048);

    const uint32_t fallbacksBefore = pool().heapFallbacks();
    PooledJsonDocument overflow(2048);
    overflow["k"] = "v";
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fallbacksBefore + 1, pool().heapFallbacks(),
                                     "a pool-full request was served by the heap without being counted");
    TEST_ASSERT_EQUAL_STRING("v", overflow["k"] | "");
}

// The pool is on the path of every conversion, including the ones the control loop makes: a
// release build must not print per allocation.
void test_pooled_traffic_is_silent(void)
{
    Serial.printfCalls = 0;
    {
        PooledJsonDocument doc(512);
        doc["k"] = "v";
    }
    {
        PooledJsonDocument doc(16384);
        doc["k"] = "v";
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printfCalls,
                                  "the pool printed on the allocation path");
}

} // namespace

extern "C" void run_json_pool_small_document_tests(void)
{
    RUN_TEST(test_every_small_capacity_is_served_from_a_slot);
    RUN_TEST(test_small_converters_take_their_documents_from_the_pool);
    RUN_TEST(test_nested_options_document_shares_the_pool_with_its_parent);
    RUN_TEST(test_deepest_chain_with_a_concurrent_holder_stays_inside_the_pool);
    RUN_TEST(test_small_conversions_survive_the_large_slots_being_taken);
    RUN_TEST(test_the_small_tier_carries_two_documents_of_a_capacity_at_once);
    RUN_TEST(test_a_request_the_pool_cannot_serve_is_counted);
    RUN_TEST(test_pooled_traffic_is_silent);
}
