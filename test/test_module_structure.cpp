#include <unity.h>
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/UpdateDto.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"

static const char* test_json = R"({
    "GrowdirectorUid":"84:F7:03:FE:88:30",
    "firmware":"1.9.30",
    "email":"demo@demo.com",
    "accountUid":"53444f91-e698-c0c7-caa2-dbc3bdbf93fc",
    "lat":32.73255,
    "lng":35.19194,
    "nodes":[
        {
            "type":"director_wifi",
            "subtype":"sensor",
            "bootType":"power-on",
            "fileSystem":"done",
            "psram":"done",
            "uid":"84:F7:03:FE:88:30",
            "firmware":"1.9.30",
            "ssid":"Keenetic-3179",
            "ip":"192.168.2.73",
            "sensors":[
                {"uid":"84:F7:03:FE:88:30.00.01","type":"air_temp","pin":4},
                {"uid":"84:F7:03:FE:88:30.00.02","type":"humidity","pin":4}
            ],
            "relays":[
                {"uid":"84:F7:03:FE:88:30.00","pin":1}
            ],
            "pumps":[
                {"uid":"84:F7:03:FE:88:30.pm.00","pin":2}
            ]
        }
    ]
})";

void test_module_structure_parse_and_serialize(void) {
    ArduinoJsonConverter jsonConverter;
    ModuleStructure ms;
    TEST_ASSERT_TRUE(jsonConverter.moduleStructureFromJson(test_json, ms));
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30", ms.growdirectorUid.c_str());
    TEST_ASSERT_EQUAL_STRING("1.9.30", ms.firmware.c_str());
    TEST_ASSERT_EQUAL_STRING("demo@demo.com", ms.email.c_str());
    TEST_ASSERT_EQUAL_STRING("53444f91-e698-c0c7-caa2-dbc3bdbf93fc", ms.accountUid.c_str());
    TEST_ASSERT_EQUAL(1, ms.nodes.size());
    TEST_ASSERT_EQUAL_FLOAT(32.73255, ms.latitude);
    TEST_ASSERT_EQUAL_FLOAT(35.19194, ms.longitude);

    const Node& node = ms.nodes[0];
    TEST_ASSERT_EQUAL_STRING("director_wifi", node.type.c_str());
    TEST_ASSERT_EQUAL_STRING("sensor", node.subtype.c_str());
    TEST_ASSERT_EQUAL_STRING("power-on", node.bootType.c_str());
    TEST_ASSERT_EQUAL_STRING("done", node.fileSystem.c_str());
    TEST_ASSERT_EQUAL_STRING("done", node.psram.c_str());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30", node.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("1.9.30", node.firmware.c_str());
    TEST_ASSERT_EQUAL_STRING("Keenetic-3179", node.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.2.73", node.ip.c_str());

    TEST_ASSERT_EQUAL(2, node.sensors.size());
    TEST_ASSERT_EQUAL_STRING("air_temp", node.sensors[0].type.c_str());
    // pin 4 in JSON is converted to 0 internally by convertPinFromJson
    TEST_ASSERT_EQUAL(0, node.sensors[0].pin);

    TEST_ASSERT_EQUAL(1, node.relays.size());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30.00", node.relays[0].uid.c_str());
    TEST_ASSERT_EQUAL(1, node.relays[0].pin);

    TEST_ASSERT_EQUAL(1, node.pumps.size());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30.pm.00", node.pumps[0].uid.c_str());
    TEST_ASSERT_EQUAL(2, node.pumps[0].pin);

    // Test serialization (round-trip)
    std::string jsonOut = jsonConverter.moduleStructureToJson(ms);
    ModuleStructure ms2;
    TEST_ASSERT_TRUE(jsonConverter.moduleStructureFromJson(jsonOut, ms2));
    TEST_ASSERT_EQUAL_STRING(ms.growdirectorUid.c_str(), ms2.growdirectorUid.c_str());
    TEST_ASSERT_EQUAL(ms.nodes.size(), ms2.nodes.size());
    TEST_ASSERT_EQUAL_FLOAT(ms.latitude, ms2.latitude);
    TEST_ASSERT_EQUAL_FLOAT(ms.longitude, ms2.longitude);
    TEST_ASSERT_EQUAL_STRING("done", ms2.nodes[0].psram.c_str());
}

void test_module_structure_lookup(void) {
    ArduinoJsonConverter jsonConverter;
    ModuleStructure ms;
    jsonConverter.moduleStructureFromJson(test_json, ms);

    // Sensor lookup
    Sensor* s = ms.findSensorByUid("84:F7:03:FE:88:30.00.01");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING("air_temp", s->type.c_str());

    std::vector<Sensor*> sensors = ms.findSensorsByType("humidity");
    TEST_ASSERT_EQUAL(1, sensors.size());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30.00.02", sensors[0]->uid.c_str());

    // Relay lookup
    Relay* r = ms.findRelayByUid("84:F7:03:FE:88:30.00");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(1, r->pin);

    // Pump lookup
    Pump* p = ms.findPumpByUid("84:F7:03:FE:88:30.pm.00");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(2, p->pin);

    // All relays/pumps
    std::vector<Relay*> relays = ms.getAllRelays();
    TEST_ASSERT_EQUAL(1, relays.size());
    std::vector<Pump*> pumps = ms.getAllPumps();
    TEST_ASSERT_EQUAL(1, pumps.size());
}

void test_module_structure_device_type(void) {
    ArduinoJsonConverter jsonConverter;
    ModuleStructure ms;
    jsonConverter.moduleStructureFromJson(test_json, ms);
    TEST_ASSERT_EQUAL_STRING("sensor", ms.getDeviceType().c_str());
}

// --- the republish and its retry budget --------------------------------------------------------
//
// RepublishModuleStructureUseCase is the one caller of sendToServerModuleStructure() that the
// outbox does not drive.
// RestAPIService::sendToServerModuleStructure() makes a single attempt, because the outbox retries on its own and three
// attempts inside one outbox attempt would make that attempt 90 s long while it holds the API
// lock. This use case is not driven by the outbox, so it would go from three attempts to
// one without noticing.
//
// Both tests assert on how many requests went out, not on an internal counter.

namespace {

// Records nothing: what is under test is how many times the structure was offered, not what it
// contained. The real ISensorStructureUseCase is exercised by the setup tests.
class CountingSensorStructureUseCase : public ISensorStructureUseCase {
public:
    int addSensorCalls = 0;
    void addSensor(Node&, const SensorEntity&) override { addSensorCalls++; }
    void addMultiChannelModbusSensor(Node&, const SensorEntity&) override {}
    void addMultiChannelI2CSensor(Node&, const SensorEntity&) override {}
    void addSingleChannelSensor(Node&, const SensorEntity&) override {}
    void addVirtualChannelSensors(Node&, const SensorEntity&) override {}
};

struct RepublishUnderTest {
    ModuleStructure structure;
    MockSensorRepository sensorRepository;
    MockHybridAPIService api;
    CountingSensorStructureUseCase sensorStructure;
    RepublishModuleStructureUseCase useCase;

    RepublishUnderTest()
        : useCase(&structure, &sensorRepository, &api, &sensorStructure, nullptr, nullptr)
    {
        Node node;
        node.uid = "republish-node";
        structure.nodes.push_back(node);
    }
};

} // namespace

void test_republish_retries_the_structure_three_times_when_the_server_refuses(void) {
    RepublishUnderTest module;
    module.api.sendModuleStructureReturnValue = false;

    const bool sent = module.useCase.execute("virtual_channel_shift");

    char message[176];
    snprintf(message, sizeof(message),
             "a refused republish went out %d times; it had three attempts before the retry loop "
             "was taken out of the transport and it must still have three",
             module.api.sendToServerModuleStructureCallCount);
    TEST_ASSERT_FALSE(sent);
    TEST_ASSERT_EQUAL_INT_MESSAGE(RepublishModuleStructureUseCase::MAX_ATTEMPTS,
                                  module.api.sendToServerModuleStructureCallCount, message);
}

void test_republish_stops_at_the_first_accepted_structure(void) {
    RepublishUnderTest module;
    module.api.sendModuleStructureReturnValue = true;

    const bool sent = module.useCase.execute("virtual_channel_shift");

    TEST_ASSERT_TRUE(sent);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.api.sendToServerModuleStructureCallCount,
                                  "an accepted structure was sent more than once - the retry loop "
                                  "does not stop on success");
    TEST_ASSERT_EQUAL_MESSAGE(ApiPriority::BOTH, module.api.lastModuleStructurePriority,
                              "the republish stopped going out through both transports");
}

extern "C" void run_module_structure_tests(void) {
    RUN_TEST(test_module_structure_parse_and_serialize);
    RUN_TEST(test_module_structure_lookup);
    RUN_TEST(test_module_structure_device_type);
    RUN_TEST(test_republish_retries_the_structure_three_times_when_the_server_refuses);
    RUN_TEST(test_republish_stops_at_the_first_accepted_structure);
}
