#include <unity.h>
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Data/Models/ModuleStructure.cpp"

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
    ModuleStructure ms;
    TEST_ASSERT_TRUE(ms.fromJson(test_json));
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30", ms.getGrowdirectorUid().c_str());
    TEST_ASSERT_EQUAL_STRING("1.9.30", ms.getFirmware().c_str());
    TEST_ASSERT_EQUAL_STRING("demo@demo.com", ms.getEmail().c_str());
    TEST_ASSERT_EQUAL_STRING("53444f91-e698-c0c7-caa2-dbc3bdbf93fc", ms.getAccountUid().c_str());
    TEST_ASSERT_EQUAL(1, ms.getNodes().size());
    TEST_ASSERT_EQUAL_FLOAT(32.73255, ms.getLatitude());
    TEST_ASSERT_EQUAL_FLOAT(35.19194, ms.getLongitude());

    const Node& node = ms.getNodes()[0];
    TEST_ASSERT_EQUAL_STRING("director_wifi", node.type.c_str());
    TEST_ASSERT_EQUAL_STRING("sensor", node.subtype.c_str());
    TEST_ASSERT_EQUAL_STRING("power-on", node.bootType.c_str());
    TEST_ASSERT_EQUAL_STRING("done", node.fileSystem.c_str());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30", node.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("1.9.30", node.firmware.c_str());
    TEST_ASSERT_EQUAL_STRING("Keenetic-3179", node.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.2.73", node.ip.c_str());

    TEST_ASSERT_EQUAL(2, node.sensors.size());
    TEST_ASSERT_EQUAL_STRING("air_temp", node.sensors[0].type.c_str());
    TEST_ASSERT_EQUAL(4, node.sensors[0].pin);

    TEST_ASSERT_EQUAL(1, node.relays.size());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30.00", node.relays[0].uid.c_str());
    TEST_ASSERT_EQUAL(1, node.relays[0].pin);

    TEST_ASSERT_EQUAL(1, node.pumps.size());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:30.pm.00", node.pumps[0].uid.c_str());
    TEST_ASSERT_EQUAL(2, node.pumps[0].pin);

    // Test serialization (round-trip)
    std::string jsonOut = ms.toJson();
    ModuleStructure ms2;
    TEST_ASSERT_TRUE(ms2.fromJson(jsonOut));
    TEST_ASSERT_EQUAL_STRING(ms.getGrowdirectorUid().c_str(), ms2.getGrowdirectorUid().c_str());
    TEST_ASSERT_EQUAL(ms.getNodes().size(), ms2.getNodes().size());
    TEST_ASSERT_EQUAL_FLOAT(ms.getLatitude(), ms2.getLatitude());
    TEST_ASSERT_EQUAL_FLOAT(ms.getLongitude(), ms2.getLongitude());
}

void test_module_structure_lookup(void) {
    ModuleStructure ms;
    ms.fromJson(test_json);

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
    ModuleStructure ms;
    ms.fromJson(test_json);
    TEST_ASSERT_EQUAL_STRING("sensor", ms.getDeviceType().c_str());
}

extern "C" void run_module_structure_tests(void) {
    RUN_TEST(test_module_structure_parse_and_serialize);
    RUN_TEST(test_module_structure_lookup);
    RUN_TEST(test_module_structure_device_type);
}
