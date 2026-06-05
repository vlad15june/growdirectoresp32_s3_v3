#include <unity.h>
#include <string>

#include "Infrastructure/Adapters/Sensors/RemoteSensors/RemoteSensorClient.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Application/DTOs/UpdateDto.h"

#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Core/Domain/MockRemoteSensorReceiver.h"
#include "Mock/Infrastructure/Udp/MockUdpManager.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Mqtt/MockMessageProcessor.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Application/Services/CommandService.h"
#include "Application/DTOs/CommandDto.h"

struct ReceiverFixture {
    MockUdpManager udpManager;
    MockMqttManager mqttManager;
    MockMessageProcessor messageProcessor;
    MockSensorValueModel sensorValueModel;
    MockRuleRepository ruleRepository;
    MockSensorRepository sensorRepository;
    JsonConverter jsonConverter;
    MockArduinoAdapter arduinoAdapter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    RemoteSensorReceiver receiver;

    ReceiverFixture()
        : receiver(&udpManager, &mqttManager, &messageProcessor, &sensorValueModel,
                   &ruleRepository, &sensorRepository, &jsonConverter, &arduinoAdapter,
                   &logger, &debugSystem) {}

    void emitMqtt(const std::string& topic, const std::string& payload) {
        ITopicHandler* handler = messageProcessor.lastAddedHandler;
        TEST_ASSERT_NOT_NULL(handler);
        handler->handleMessage(topic.c_str(),
                               reinterpret_cast<const uint8_t*>(payload.c_str()),
                               static_cast<unsigned int>(payload.size()));
    }
};

static Rule makeRemoteSensorRule(const std::string& uid, const std::string& sensorUid) {
    Rule rule;
    rule.uid = uid;
    rule.deviceUid = "device-1";
    rule.sensorUid = sensorUid;
    rule.ruleType = RuleType::SENSOR;
    rule.mode = RuleMode::ON;
    rule.valueLow = 10.0;
    rule.valueHigh = 30.0;
    return rule;
}

static const char* kSingleSensorPayload =
    "{\"GrowdirectorUid\":\"84:F7:03:FE:88:2C\",\"email\":\"x\",\"millis\":1,"
    "\"devices\":[],\"sensors\":[{\"uid\":\"84:F7:03:FE:88:2C.00.01\",\"value\":25.40,\"state\":\"online\"}]}";

static void test_evaluator_reads_real_value(void) {
    MockSensorValueModel model;
    model.values["84:F7:03:FE:88:2C.00.01"] = 25.4f;
    MockRuleRepository ruleRepo;
    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    EvaluateRulesUseCase uc(nullptr, &ruleRepo, &arduino, nullptr, &model, &logger, &debug);

    Rule rule = makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01");
    RuleEvaluationResult result = uc.evaluateRule(rule);

    TEST_ASSERT_EQUAL_FLOAT(25.4f, result.sensorValue);
}

static void test_udp_write(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 5000;
    f.receiver.init();

    f.udpManager.controller.emitPacket(kSingleSensorPayload);

    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setLastReadTimeCallCount);
    TEST_ASSERT_EQUAL_FLOAT(25.4f, f.sensorValueModel.values["84:F7:03:FE:88:2C.00.01"]);
}

static void test_mqtt_write(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 5000;
    f.receiver.init();

    f.emitMqtt("hardware/update/84:F7:03:FE:88:2C", kSingleSensorPayload);

    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setLastReadTimeCallCount);
    TEST_ASSERT_EQUAL_FLOAT(25.4f, f.sensorValueModel.values["84:F7:03:FE:88:2C.00.01"]);
}

static void test_last_read_time_stamped(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 41230;
    f.receiver.init();

    f.udpManager.controller.emitPacket(kSingleSensorPayload);

    TEST_ASSERT_EQUAL_UINT32(41230, f.sensorValueModel.lastReadTimes["84:F7:03:FE:88:2C.00.01"]);
}

static void test_reconcile_subscribe_idempotent(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01"));
    f.receiver.init();

    TEST_ASSERT_EQUAL_INT(1, f.mqttManager.subscribeCallCount);
    TEST_ASSERT_EQUAL_STRING("hardware/update/84:F7:03:FE:88:2C", f.mqttManager.lastSubscribeTopic.c_str());

    f.receiver.onRuleSetChanged();
    TEST_ASSERT_EQUAL_INT(1, f.mqttManager.subscribeCallCount);
}

static void test_reconcile_unsubscribe(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01"));
    f.receiver.init();
    TEST_ASSERT_EQUAL_INT(1, f.mqttManager.subscribeCallCount);

    f.ruleRepository.rules.clear();
    f.receiver.onRuleSetChanged();

    TEST_ASSERT_EQUAL_INT(1, f.mqttManager.unsubscribeCallCount);
    TEST_ASSERT_EQUAL_STRING("hardware/update/84:F7:03:FE:88:2C", f.mqttManager.lastUnsubscribeTopic.c_str());
}

static void test_boot_delay_gate(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 5000;
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01"));
    f.receiver.init();
    TEST_ASSERT_EQUAL_INT(0, f.mqttManager.subscribeCallCount);

    f.arduinoAdapter.currentMillis = 15000;
    f.receiver.onRuleSetChanged();
    TEST_ASSERT_EQUAL_INT(1, f.mqttManager.subscribeCallCount);
}

static void test_local_sensor_not_subscribed(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    SensorEntity local;
    local.uid = "11:22:33:44:55:66.00";
    f.sensorRepository.sensors.push_back(local);
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "11:22:33:44:55:66.00.01"));
    f.receiver.init();

    TEST_ASSERT_EQUAL_INT(0, f.mqttManager.subscribeCallCount);
}

static void test_owner_derivation(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01"));
    f.receiver.init();

    TEST_ASSERT_EQUAL_STRING("hardware/update/84:F7:03:FE:88:2C", f.mqttManager.lastSubscribeTopic.c_str());
}

static void test_subscription_cap(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    for (int i = 0; i < 51; ++i) {
        char mac[32];
        snprintf(mac, sizeof(mac), "AA:BB:CC:DD:%02X:%02X.00.01", (i >> 8) & 0xFF, i & 0xFF);
        f.ruleRepository.rules.push_back(makeRemoteSensorRule("r" + std::to_string(i), mac));
    }
    f.receiver.init();

    TEST_ASSERT_EQUAL_INT(50, f.mqttManager.subscribeCallCount);
}

static void test_bad_payload_dropped(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 5000;
    f.receiver.init();

    f.udpManager.controller.emitPacket("this is not json");
    TEST_ASSERT_EQUAL_INT(0, f.sensorValueModel.setSensorValueCallCount);

    const char* mixed =
        "{\"sensors\":[{\"uid\":\"a\",\"state\":\"online\"},{\"uid\":\"b\",\"value\":2}]}";
    f.udpManager.controller.emitPacket(mixed);
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, f.sensorValueModel.values["b"]);
}

static void test_multi_channel_flattened(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 5000;
    f.receiver.init();

    const char* payload =
        "{\"sensors\":[{\"uid\":\"MAC.00.01\",\"value\":1,\"state\":\"online\"},"
        "{\"uid\":\"MAC.00.02\",\"value\":2,\"state\":\"online\"}]}";
    f.udpManager.controller.emitPacket(payload);

    TEST_ASSERT_EQUAL_INT(2, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, f.sensorValueModel.values["MAC.00.01"]);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, f.sensorValueModel.values["MAC.00.02"]);
}

static void test_parser_round_trip(void) {
    JsonConverter converterImpl;
    IJsonConverter& converter = converterImpl;

    UpdateDto dto;
    dto.growdirectorUid = "84:F7:03:FE:88:2C";
    SensorDto s1;
    s1.uid = "84:F7:03:FE:88:2C.00.01";
    s1.value = 25.4f;
    s1.state = "online";
    dto.sensors.push_back(s1);
    SensorDto s2;
    s2.uid = "84:F7:03:FE:88:2C.00.02";
    s2.value = 12.5f;
    s2.state = "online";
    dto.sensors.push_back(s2);

    std::string json = converter.updateToJson(dto);

    std::vector<std::pair<std::string, float>> pairs;
    bool ok = converter.sensorValuesFromUpdateJson(json, pairs);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(pairs.size()));
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:2C.00.01", pairs[0].first.c_str());
    TEST_ASSERT_EQUAL_FLOAT(25.4f, pairs[0].second);
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:2C.00.02", pairs[1].first.c_str());
    TEST_ASSERT_EQUAL_FLOAT(12.5f, pairs[1].second);
}

static void test_parser_rejects_non_sensor(void) {
    JsonConverter converterImpl;
    IJsonConverter& converter = converterImpl;
    std::vector<std::pair<std::string, float>> pairs;

    TEST_ASSERT_FALSE(converter.sensorValuesFromUpdateJson("{\"devices\":[]}", pairs));
    TEST_ASSERT_FALSE(converter.sensorValuesFromUpdateJson("not json", pairs));
    TEST_ASSERT_FALSE(converter.sensorValuesFromUpdateJson("{\"sensors\":[]}", pairs));
}

static void test_notification_hook(void) {
    MockRemoteSensorReceiver receiver;
    MockLogger logger;
    MockDebugSystem debug;
    CommandService commandService(nullptr, nullptr, nullptr, nullptr, nullptr,
                                  &logger, &debug, nullptr, nullptr);
    commandService.setRemoteSensorReceiver(&receiver);

    CommandDto command;
    command.type = "RuleCommandNotification";
    command.ruleUid = "rule-1";
    commandService.process(command);

    TEST_ASSERT_EQUAL_INT(1, receiver.onRuleSetChangedCallCount);
}

static void test_logging_records(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01"));
    f.receiver.init();

    TEST_ASSERT_TRUE(f.logger.hasMessageContaining("init udp="));
    TEST_ASSERT_TRUE(f.logger.hasMessageContaining("reconcile rules="));
    TEST_ASSERT_TRUE(f.logger.hasMessageContaining("subscribe topic="));

    f.udpManager.controller.emitPacket(kSingleSensorPayload);
    TEST_ASSERT_TRUE(f.logger.hasMessageContaining("udp_packet len="));
    TEST_ASSERT_TRUE(f.logger.hasMessageContaining("write sensorUid="));
}

extern "C" void run_remote_sensor_tests(void) {
    RUN_TEST(test_evaluator_reads_real_value);
    RUN_TEST(test_udp_write);
    RUN_TEST(test_mqtt_write);
    RUN_TEST(test_last_read_time_stamped);
    RUN_TEST(test_reconcile_subscribe_idempotent);
    RUN_TEST(test_reconcile_unsubscribe);
    RUN_TEST(test_boot_delay_gate);
    RUN_TEST(test_local_sensor_not_subscribed);
    RUN_TEST(test_owner_derivation);
    RUN_TEST(test_subscription_cap);
    RUN_TEST(test_bad_payload_dropped);
    RUN_TEST(test_multi_channel_flattened);
    RUN_TEST(test_parser_round_trip);
    RUN_TEST(test_parser_rejects_non_sensor);
    RUN_TEST(test_notification_hook);
    RUN_TEST(test_logging_records);
}
