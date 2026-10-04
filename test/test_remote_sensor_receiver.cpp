#include <unity.h>
#include <string>

#include "Infrastructure/Adapters/Sensors/RemoteSensors/RemoteSensorClient.h"
#include "Infrastructure/Controllers/UDP/UdpInboundDispatcher.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Application/DTOs/UpdateDto.h"

#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Application/MockOutbox.h"
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
    ArduinoJsonConverter jsonConverter;
    MockArduinoAdapter arduinoAdapter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    UdpInboundDispatcher dispatcher;
    RemoteSensorReceiver receiver;

    ReceiverFixture()
        : dispatcher(&udpManager, &logger, &debugSystem),
          receiver(&udpManager, &dispatcher, &mqttManager, &messageProcessor, &sensorValueModel,
                   &ruleRepository, &sensorRepository, &jsonConverter, &arduinoAdapter,
                   &logger, &debugSystem) {
        dispatcher.init();
    }

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

static Rule makeConditionRule(const std::string& uid, RuleType type,
                              const std::vector<std::string>& conditionSensorUids) {
    Rule rule;
    rule.uid = uid;
    rule.deviceUid = "device-1";
    rule.ruleType = type;
    rule.mode = RuleMode::ON;
    for (const auto& sensorUid : conditionSensorUids) {
        RuleCondition cond;
        cond.sensorUids.push_back(sensorUid);
        cond.valueLow = 10.0;
        cond.valueHigh = 30.0;
        rule.conditions.push_back(cond);
    }
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

static void test_disconnected_sensor_skipped(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 5000;
    f.receiver.init();

    const char* payload =
        "{\"GrowdirectorUid\":\"84:F7:03:FE:88:2C\",\"email\":\"x\",\"millis\":1,"
        "\"devices\":[],\"sensors\":["
        "{\"uid\":\"84:F7:03:FE:88:2C.00.01\",\"value\":0,\"state\":\"disconnected\"},"
        "{\"uid\":\"84:F7:03:FE:88:2C.00.02\",\"value\":12.5,\"state\":\"online\"}]}";

    f.emitMqtt("hardware/update/84:F7:03:FE:88:2C", payload);

    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_INT(1, f.sensorValueModel.setLastReadTimeCallCount);
    TEST_ASSERT_EQUAL_FLOAT(12.5f, f.sensorValueModel.values["84:F7:03:FE:88:2C.00.02"]);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.sensorValueModel.values.count("84:F7:03:FE:88:2C.00.01")));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.sensorValueModel.lastReadTimes.count("84:F7:03:FE:88:2C.00.01")));
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
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

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
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK
    TEST_ASSERT_EQUAL_INT(1, f.mqttManager.subscribeCallCount);

    f.ruleRepository.rules.clear();
    f.receiver.onRuleSetChanged();

    TEST_ASSERT_EQUAL_INT(1, f.mqttManager.unsubscribeCallCount);
    TEST_ASSERT_EQUAL_STRING("hardware/update/84:F7:03:FE:88:2C", f.mqttManager.lastUnsubscribeTopic.c_str());
}

// The subscriptions are reconciled when the broker first answers, not by init() and not on a timer:
// MqttManager keeps a topic asked for before the CONNACK and subscribes it on the CONNACK, so a
// reconcile asked for at any uptime takes effect.
static void test_init_leaves_the_reconcile_to_the_first_connack_and_any_uptime_will_do(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 5000;
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01"));
    f.receiver.init();
    TEST_ASSERT_EQUAL_INT(0, f.mqttManager.subscribeCallCount);

    f.receiver.onRuleSetChanged();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, f.mqttManager.subscribeCallCount,
                                  "a reconcile five seconds into the boot subscribed to nothing");
}

static void test_local_sensor_not_subscribed(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    SensorEntity local;
    local.uid = "11:22:33:44:55:66.00";
    f.sensorRepository.sensors.push_back(local);
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "11:22:33:44:55:66.00.01"));
    f.receiver.init();
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

    TEST_ASSERT_EQUAL_INT(0, f.mqttManager.subscribeCallCount);
}

static void test_owner_derivation(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01"));
    f.receiver.init();
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

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
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

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
    ArduinoJsonConverter converterImpl;
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

    std::vector<RemoteSensorValue> pairs;
    bool ok = converter.sensorValuesFromUpdateJson(json, pairs);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(pairs.size()));
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:2C.00.01", pairs[0].uid.c_str());
    TEST_ASSERT_EQUAL_FLOAT(25.4f, pairs[0].value);
    TEST_ASSERT_EQUAL_STRING("online", pairs[0].state.c_str());
    TEST_ASSERT_EQUAL_STRING("84:F7:03:FE:88:2C.00.02", pairs[1].uid.c_str());
    TEST_ASSERT_EQUAL_FLOAT(12.5f, pairs[1].value);
}

static void test_parser_rejects_non_sensor(void) {
    ArduinoJsonConverter converterImpl;
    IJsonConverter& converter = converterImpl;
    std::vector<RemoteSensorValue> pairs;

    TEST_ASSERT_FALSE(converter.sensorValuesFromUpdateJson("{\"devices\":[]}", pairs));
    TEST_ASSERT_FALSE(converter.sensorValuesFromUpdateJson("not json", pairs));
    TEST_ASSERT_FALSE(converter.sensorValuesFromUpdateJson("{\"sensors\":[]}", pairs));
}

static void test_notification_hook(void) {
    MockRemoteSensorReceiver receiver;
    MockLogger logger;
    MockDebugSystem debug;
    ArduinoJsonConverter converter;
    MockRuleRepository ruleRepo;
    ruleRepo.synchronizeReturnValue = 1;
    MockOutbox outbox;
    CommandService commandService(nullptr, nullptr, nullptr, nullptr,
                                  &logger, &debug, nullptr, &converter, &ruleRepo);
    commandService.setOutbox(&outbox);
    commandService.setRemoteSensorReceiver(&receiver);

    CommandDto command;
    command.type = "RuleCommandNotification";
    command.ruleUid = "rule-1";
    command.rulesPayload =
        R"([{"uid":"rule-1","version":1,"deviceUid":"dev-1","ruleType":"sensor","sensorUid":"sensor-1","valueLow":10,"valueHigh":20,"direction":"down","mode":"Off","state":"Synchronized","daysOfWeek":[],"daysOfMonth":[],"conditions":[]}])";
    commandService.process(command);

    // Phase 2: the reconcile hook fires once after a push is applied.
    TEST_ASSERT_EQUAL_INT(1, receiver.onRuleSetChangedCallCount);
}

static void test_condition_sensor_subscribed_non_average(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(
        makeConditionRule("r1", RuleType::SENSOR, {"84:F7:03:FE:88:2C.00.01"}));
    f.receiver.init();
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

    TEST_ASSERT_EQUAL_INT(1, f.mqttManager.subscribeCallCount);
    TEST_ASSERT_EQUAL_STRING("hardware/update/84:F7:03:FE:88:2C", f.mqttManager.lastSubscribeTopic.c_str());
}

static void test_multiple_conditions_distinct_modules(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(
        makeConditionRule("r1", RuleType::STEP,
                          {"84:F7:03:FE:88:2C.00.01", "11:22:33:44:55:66.00.02"}));
    f.receiver.init();
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

    TEST_ASSERT_EQUAL_INT(2, f.mqttManager.subscribeCallCount);
}

static void test_average_sensor_reads_all_conditions(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(
        makeConditionRule("r1", RuleType::AVERAGE_SENSOR,
                          {"84:F7:03:FE:88:2C.00.01", "11:22:33:44:55:66.00.02"}));
    f.receiver.init();
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

    TEST_ASSERT_EQUAL_INT(2, f.mqttManager.subscribeCallCount);
}

static void test_local_condition_sensor_not_subscribed(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    SensorEntity local;
    local.uid = "11:22:33:44:55:66.00";
    f.sensorRepository.sensors.push_back(local);
    f.ruleRepository.rules.push_back(
        makeConditionRule("r1", RuleType::SENSOR, {"11:22:33:44:55:66.00.02"}));
    f.receiver.init();
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

    TEST_ASSERT_EQUAL_INT(0, f.mqttManager.subscribeCallCount);
}

static void test_logging_records(void) {
    ReceiverFixture f;
    f.arduinoAdapter.currentMillis = 20000;
    f.ruleRepository.rules.push_back(makeRemoteSensorRule("r1", "84:F7:03:FE:88:2C.00.01"));
    f.receiver.init();
    f.receiver.onRuleSetChanged(); // the broker's first CONNACK

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
    RUN_TEST(test_disconnected_sensor_skipped);
    RUN_TEST(test_last_read_time_stamped);
    RUN_TEST(test_reconcile_subscribe_idempotent);
    RUN_TEST(test_reconcile_unsubscribe);
    RUN_TEST(test_init_leaves_the_reconcile_to_the_first_connack_and_any_uptime_will_do);
    RUN_TEST(test_local_sensor_not_subscribed);
    RUN_TEST(test_owner_derivation);
    RUN_TEST(test_condition_sensor_subscribed_non_average);
    RUN_TEST(test_multiple_conditions_distinct_modules);
    RUN_TEST(test_average_sensor_reads_all_conditions);
    RUN_TEST(test_local_condition_sensor_not_subscribed);
    RUN_TEST(test_subscription_cap);
    RUN_TEST(test_bad_payload_dropped);
    RUN_TEST(test_multi_channel_flattened);
    RUN_TEST(test_parser_round_trip);
    RUN_TEST(test_parser_rejects_non_sensor);
    RUN_TEST(test_notification_hook);
    RUN_TEST(test_logging_records);
}
