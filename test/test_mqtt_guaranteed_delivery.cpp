// The module's half of a guaranteed delivery: what the broker is asked for, and what survives an
// outage because of it.
//
// The server's half is already paid for: the backend publishes every push with
// WithExactlyOnceQoS() and retain (MqttService.PublishMessageAsync), and the broker keeps sessions
// for clients that ask. A message is delivered with the lower of the two guarantees, so a
// subscription at QoS 0 reduced all of that to fire-and-forget: whatever the server sent while the
// module was offline was gone, and neither side could tell.
//
// What is under test is production code. The real MqttManager takes the connect options and the
// subscriptions, the real MessageProcessor dispatches, the real TopicHandlerFactory builds the
// real CommandTopicHandler, the real ArduinoJsonConverter parses, the real CommandService applies
// a rule push through the real RuleRepository and hands a module command to the real
// ModuleCommandExecutionUseCase, and the real RemoteSensorReceiver takes a peer module's sensor
// update. Replaced are the far end - Sim/SimMqttBroker.h answers as a 3.1.1 broker must - and the
// device headers MqttManager names (Mock/native/AsyncMqttClient.h, the FreeRTOS stand-ins),
// without which no test reached this class at all.
//
// ### Reproducing the red
//
// Two constants in MqttManager carry the behaviour, and putting either back reproduces the
// failure:
//
//   * SUBSCRIBE_QOS = 0  - test_a_subscription_taken_while_connected_asks_for_at_least_once,
//     ..._replayed_after_a_reconnect_..., the offline-push tests, the PUBACK tests and the
//     redelivery test fail;
//   * CLEAN_SESSION = true - the offline-push tests and the session tests fail.
//
// The module's reconnect state machine is not what reconnects here: on the device the outbox
// network task beats it between requests, and this bench has no such task, so it calls connect()
// where a beat would have. What that leaves unproven is the cadence of the reconnect, not the
// delivery; the cadence is test_network_offboard.cpp's.

#include <unity.h>

#include "Application/Services/CommandService.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Sensors/RemoteSensors/RemoteSensorClient.h"
#include "Infrastructure/Controllers/MQTT/MessageProcessor.h"
#include "Infrastructure/Controllers/MQTT/MqttManager.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Application/MockOutbox.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/SimMqttBroker.h"

#include <mutex>
#include <string>
#include <vector>

using hostsim::SimMqttBroker;

namespace {

// A MAC, because that is what the module's UID is: ModuleConfig::getModuleUid() returns
// WifiAdapter::getMACAddress().
const char *const MODULE_UID = "AA:BB:CC:DD:EE:01";
const char *const PEER_MODULE_UID = "AA:BB:CC:DD:EE:02";

// globals.h: the prefix every module of the fleet carries.
const char *const CLIENT_ID_PREFIX = "GrowDirectorWifiClient";

std::string clientId() { return std::string(CLIENT_ID_PREFIX) + MODULE_UID; }
std::string commandTopic() { return std::string("GrowDirectorMqttTopic/") + MODULE_UID; }
std::string peerUpdateTopic() { return std::string("hardware/update/") + PEER_MODULE_UID; }

/** JSON string literal: the rules of a push travel as a string inside the notification. */
std::string asJsonString(const std::string &text)
{
    std::string quoted = "\"";
    for (char character : text)
    {
        if (character == '"' || character == '\\')
        {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

std::string ruleJson(const std::string &uid, int version, const std::string &mode)
{
    return std::string("[{\"uid\":\"") + uid + "\",\"version\":" + std::to_string(version) +
           ",\"deviceUid\":\"dev-1\",\"ruleType\":\"sensor\",\"sensorUid\":\"sensor-1\"," +
           "\"valueLow\":10,\"valueHigh\":20,\"direction\":\"down\",\"mode\":\"" + mode +
           "\",\"state\":\"Synchronized\",\"daysOfWeek\":[],\"daysOfMonth\":[],\"conditions\":[]}]";
}

/** A rule push, exactly as MqttApplicationService.SendRuleCommandNotificationAsync builds it. */
std::string rulePush(const std::string &ruleUid, int version, const std::string &mode = "Off")
{
    return std::string("{\"growDirectorUid\":\"") + MODULE_UID + "\",\"ruleUid\":\"" + ruleUid +
           "\",\"type\":\"RuleCommandNotification\",\"rulesPayload\":" +
           asJsonString(ruleJson(ruleUid, version, mode)) + "}";
}

/** A device command notification, as SendDeviceCommandNotificationAsync builds it. */
std::string deviceCommandPush(int commandId, const std::string &state)
{
    return std::string("{\"growDirectorUid\":\"") + MODULE_UID +
           "\",\"deviceUid\":\"relay-0\",\"commandId\":" + std::to_string(commandId) +
           ",\"requestedState\":\"" + state +
           "\",\"analogControlPinValue\":0,\"status\":\"new\",\"needToSwitchBack\":false," +
           "\"secondsToSwitchBack\":0,\"switchType\":\"\",\"createdDate\":\"2026-09-13T10:00:00\"," +
           "\"type\":\"DeviceCommandNotification\"}";
}

/**
 * @brief A module command notification, as MqttApplicationService.SendModuleCommandAsync builds it.
 *
 * The whole body: the module's uid, the type, the command name and whatever parameters the command
 * takes, each of them a string. There is no timestamp in it and no command uid - nothing that says
 * when the command was issued or which issue of it this is.
 */
std::string moduleCommandPush(const std::string &command, const std::string &parameters = "")
{
    return std::string("{\"growDirectorUid\":\"") + MODULE_UID +
           "\",\"type\":\"ModuleCommandNotification\",\"command\":\"" + command + "\"" + parameters +
           "}";
}

/** What a peer module publishes on hardware/update/<its uid>. */
std::string peerSensorUpdate(double value)
{
    return std::string("{\"sensors\":[{\"uid\":\"") + PEER_MODULE_UID + ".1\",\"value\":" +
           std::to_string(value) + ",\"state\":\"ok\"}]}";
}

/** Records what the module would have done to a relay, in place of the GPIO writer. */
class RecordingDeviceCommands : public IDeviceCommandExecutionUseCase
{
public:
    struct Execution
    {
        std::string deviceUid;
        std::string requestedState;
        int commandId;
    };

    std::vector<Execution> executions;

    bool execute(const std::string &deviceUid, const std::string &requestedState, int commandId,
                 bool /*needToSwitchBack*/ = false, int /*secondsToSwitchBack*/ = 0,
                 const std::string & /*switchedAt*/ = "") override
    {
        executions.push_back({deviceUid, requestedState, commandId});
        return true;
    }
};

/**
 * @brief Counts the restarts, where the reboot handler ends: IArduinoAdapter::restart().
 *
 * The board reboots there; on the host the call only returns, so a count is what a test can read.
 */
class RestartCountingArduino : public MockArduinoAdapter
{
public:
    uint32_t restartCount = 0;

    void restart() override
    {
        restartCount++;
        MockArduinoAdapter::restart();
    }
};

/**
 * @brief The module as the MQTT leg sees it: the manager, the dispatcher and what they feed.
 *
 * Assembled the way globals.h assembles it and started the way
 * ModuleSetupService::setupMqttConnection() starts it - client id first, then connect, then the
 * topic handlers - so the order the board takes is the order under test.
 */
struct ModuleBench
{
    MockLogger logger;
    MockDebugSystem debug;
    MockWifiAdapter wifi;
    RestartCountingArduino arduino;
    MockModuleConfig config;
    MockNTPTimeAdapter time;
    MockSettings settings;
    MockOutbox outbox;
    MockSpiffsRuleDao ruleDao;
    MockSensorRepository sensors;
    MockSensorValueModel sensorValues;

    ArduinoJsonConverter json;
    RuleRepository rules;
    RecordingDeviceCommands deviceCommands;
    ModuleCommandExecutionUseCase moduleCommands;
    CommandService commands;
    MessageProcessor processor;
    TopicHandlerFactory handlerFactory;

    AsyncMqttClient client;
    std::recursive_mutex clientMutex;
    MqttManager mqtt;

    RemoteSensorReceiver remoteSensors;

    ModuleBench()
        : rules(&ruleDao),
          moduleCommands(&logger, &debug, &time, nullptr, &arduino, &sensors, nullptr, nullptr,
                         &config, &settings),
          commands(&deviceCommands, &moduleCommands, nullptr, nullptr, &logger, &debug, &time,
                   &json, &rules),
          processor(&logger, &debug),
          handlerFactory(&logger, &debug, &config, &commands, &json),
          mqtt(client, &logger, &debug, CLIENT_ID_PREFIX, IPAddress(116, 203, 235, 144), 1883,
               "GrowDirectorMqtt", "secret", &processor, &handlerFactory, &wifi, &clientMutex),
          remoteSensors(nullptr, nullptr, &mqtt, &processor, &sensorValues, &rules, &sensors,
                        &json, &arduino, &logger, &debug)
    {
        config.setModuleUid(MODULE_UID);
        wifi.setConnected(true);
        commands.setOutbox(&outbox);
        // Past RemoteSensorReceiver's boot delay, so a rule set change reconciles subscriptions.
        arduino.setMillis(60000);
    }

    void attachTo(SimMqttBroker &broker) { client.attachBroker(&broker); }

    /**
     * @brief The boot, in the order ModuleSetupService::setupMqttConnection() takes it.
     *
     * The client id is composed here rather than by that service, which would drag thirty
     * dependencies into this file for one string; the composition itself - and the fact that a
     * second pass produces the same string - is proven where it lives, in
     * test_module_setup_service.cpp. What this bench needs of it is only that the same id comes
     * back after a restart, which is what the broker keys the session on.
     */
    void boot()
    {
        std::string id = std::string(mqtt.getClientId()) + MODULE_UID;
        mqtt.setClientId(id.c_str());
        mqtt.connect();
        mqtt.setupMqttTopicHandlers();
    }

    /** The link goes away and comes back, as a reconnect or a restart does. */
    void reconnect()
    {
        mqtt.disconnect();
        mqtt.connect();
    }

    /** Hand what the network task queued to the control loop, where the handlers run. */
    void pump() { mqtt.loop(); }

    /**
     * @brief Answer every device command fetch the signals produced with a failure.
     *
     * What the module does then is its fallback: it executes the command out of the MQTT payload
     * that announced it, because the server it would have asked is not answering.
     */
    void serverDoesNotAnswerTheFetches()
    {
        while (_answeredFetches < outbox.deviceCommandFetches)
        {
            OutboxCompletion completion;
            completion.kind = OutboxKind::DEVICE_COMMAND_FETCH;
            completion.ticket = ++_answeredFetches;
            completion.delivered = false;
            completion.attempts = 3;
            commands.onOutboxCompletion(completion);
        }
    }

    /** The rule as it is stored, or an empty uid when there is none. */
    Rule storedRule(const std::string &uid)
    {
        std::vector<Rule> all = rules.getAll();
        for (const Rule &rule : all)
        {
            if (rule.uid == uid)
            {
                return rule;
            }
        }
        return Rule();
    }

private:
    uint32_t _answeredFetches = 0;
};

/** A rule that makes the receiver want the peer's topic, stored before the bench subscribes. */
void seedRemoteSensorRule(ModuleBench &bench)
{
    Rule rule;
    rule.uid = "remote-rule";
    rule.version = 1;
    rule.ruleType = RuleType::SENSOR;
    rule.deviceUid = "dev-1";
    rule.sensorUid = std::string(PEER_MODULE_UID) + ".1";
    rule.mode = RuleMode::OFF;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.direction = RuleDirection::UP;
    rule.state = RuleState::SYNCHRONIZED;
    bench.rules.save(rule);
}

// --- what the module asks the broker for --------------------------------------------------------

void test_a_subscription_taken_while_connected_asks_for_at_least_once(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();

    TEST_ASSERT_EQUAL_INT(1, broker.subscriptionQosOf(clientId(), commandTopic()));
}

void test_a_subscription_replayed_after_a_reconnect_asks_for_at_least_once(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    // Subscriptions taken while the module is offline are buffered and replayed from
    // onMqttConnect() - the second place the QoS is spelled.
    bench.mqtt.setupMqttTopicHandlers();
    TEST_ASSERT_EQUAL_INT(-1, broker.subscriptionQosOf(clientId(), commandTopic()));

    bench.boot();

    TEST_ASSERT_EQUAL_INT(1, broker.subscriptionQosOf(clientId(), commandTopic()));
}

void test_the_peer_sensor_subscription_asks_for_at_least_once_too(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);
    seedRemoteSensorRule(bench);

    bench.boot();
    bench.remoteSensors.init();
    bench.remoteSensors.onRuleSetChanged(); // the boot sequence, on the first CONNACK

    TEST_ASSERT_EQUAL_INT(1, broker.subscriptionQosOf(clientId(), peerUpdateTopic()));
}

// A boot with no link: setup configures the client and registers the handlers, and dials nothing.
// The first CONNECT is the state machine's, on its first beat once the link is up - without closing
// a socket the client never opened - and the command topic the handlers asked for is subscribed on
// that CONNACK, at least once.
void test_a_boot_without_a_link_connects_on_the_first_beat_and_subscribes_the_commands(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);
    bench.wifi.setConnected(false);
    hostUptimeMs = 100000;

    bench.boot();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bench.client.connectAttempts, "the client dialled with no link");
    TEST_ASSERT_FALSE(bench.mqtt.serviceLinkOnce());

    bench.wifi.setConnected(true);
    bench.mqtt.serviceLinkOnce();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bench.client.disconnectAttempts,
                                  "the first connect of the boot closed a socket the client never opened");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, bench.client.connectAttempts,
                                  "the first beat with the link up did not dial the broker");
    TEST_ASSERT_TRUE(bench.mqtt.isConnected());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, broker.subscriptionQosOf(clientId(), commandTopic()),
                                  "the command topic was not subscribed on the first CONNACK");
}

// Once the client has dialled, every later dial closes first: a CONNECT that went unanswered may
// have left a socket behind.
void test_a_client_that_dialled_once_closes_before_it_dials_again(void)
{
    ModuleBench bench;
    bench.wifi.setConnected(false);
    hostUptimeMs = 100000;
    bench.boot();

    // No broker behind the client: the CONNECT goes unanswered.
    bench.wifi.setConnected(true);
    bench.mqtt.serviceLinkOnce();
    TEST_ASSERT_EQUAL_INT(1, bench.client.connectAttempts);

    for (int beat = 0; beat < 400 && bench.client.connectAttempts < 2; beat++)
    {
        hostUptimeAdvance(200);
        bench.mqtt.serviceLinkOnce();
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, bench.client.connectAttempts, "the client never dialled again");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, bench.client.disconnectAttempts,
                                  "the second dial did not close what the first one may have left");
}

void test_the_module_asks_the_broker_to_keep_its_session(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();
    TEST_ASSERT_FALSE(bench.client.cleanSession());

    // The session outlives the connection: that is what a queue for an absent client needs.
    bench.mqtt.disconnect();
    TEST_ASSERT_TRUE(broker.hasSession(clientId()));
}

void test_the_broker_recognises_the_module_after_a_restart(void)
{
    SimMqttBroker broker;

    {
        ModuleBench first;
        first.attachTo(broker);
        first.boot();
        first.mqtt.disconnect();
    }

    // A restart: a new object, the same chip - so the same client id, composed the same way.
    ModuleBench second;
    second.attachTo(broker);
    second.boot();

    TEST_ASSERT_EQUAL_STRING(clientId().c_str(), second.client.clientId());
    // Session Present in the CONNACK: the broker recognised this client and still has what it
    // kept for it. This is the flag a probe read off the production broker.
    TEST_ASSERT_TRUE(second.client.lastSessionPresent);
    // And it took nothing over from a live connection - the id is this module's alone.
    TEST_ASSERT_EQUAL_UINT32(0, broker.takenOverCount);
}

// --- the push that waited ------------------------------------------------------------------------

void test_a_rule_pushed_while_the_module_was_away_is_applied_when_it_returns(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();
    bench.mqtt.disconnect();

    // Published without the retain flag on purpose: a retained message would be handed over again
    // by the resubscribe alone, and then the test would not say whether the session queue works.
    // This is the message that exists only in the queue the session buys.
    broker.publishFromServer(commandTopic(), rulePush("rule-1", 7), 2, false);

    // Nothing reached the module, and nothing was thrown away either.
    TEST_ASSERT_EQUAL_UINT32(1, broker.queuedFor(clientId()));
    TEST_ASSERT_EQUAL_UINT32(0, broker.droppedQos0OfflineCount);

    bench.mqtt.connect();
    bench.pump();

    // To the end of the path: the rule is in the repository and in the file the DAO stands for.
    TEST_ASSERT_EQUAL_STRING("rule-1", bench.storedRule("rule-1").uid.c_str());
    TEST_ASSERT_EQUAL_INT(7, bench.storedRule("rule-1").version);
    TEST_ASSERT_EQUAL_INT(1, (int)bench.ruleDao.getStorage().size());
    TEST_ASSERT_EQUAL_STRING("rule-1", bench.ruleDao.getStorage()[0].uid.c_str());
}

void test_a_device_command_pushed_while_the_module_was_away_reaches_the_command_service(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();
    bench.mqtt.disconnect();

    // Not retained, so the queue of the kept session is the only way in (see the rule test above).
    broker.publishFromServer(commandTopic(), deviceCommandPush(11, "on"), 2, false);
    TEST_ASSERT_EQUAL_UINT32(0, bench.outbox.deviceCommandFetches);

    bench.mqtt.connect();
    bench.pump();

    // The signal's whole job: ask the server for the authoritative list.
    TEST_ASSERT_EQUAL_UINT32(1, bench.outbox.deviceCommandFetches);
}

void test_a_peer_sensor_update_while_the_module_was_away_is_not_kept(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);
    seedRemoteSensorRule(bench);

    bench.boot();
    bench.remoteSensors.init();
    bench.remoteSensors.onRuleSetChanged(); // the boot sequence, on the first CONNACK
    bench.mqtt.disconnect();

    broker.publishFromPeerModule(peerUpdateTopic(), peerSensorUpdate(21.5));

    // The subscription asks for at-least-once, but the delivery is the lower of the two ends and
    // a module publishes at QoS 0 (MqttManager::publish). Between modules nothing is guaranteed,
    // and a sensor reading from the time the module was away would be stale anyway.
    TEST_ASSERT_EQUAL_UINT32(0, broker.queuedFor(clientId()));
    TEST_ASSERT_EQUAL_UINT32(1, broker.droppedQos0OfflineCount);

    bench.mqtt.connect();
    bench.pump();

    TEST_ASSERT_EQUAL_INT(0, bench.sensorValues.setSensorValueCallCount);
}

// --- at-least-once means the same message can come twice -----------------------------------------

void test_a_redelivered_rule_push_leaves_the_stored_rule_as_it_was(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();

    // The push arrives and is applied, but the acknowledgement is lost on the way back - so the
    // broker still owes it and sends it again, with DUP, on the next connection. Not retained:
    // the repeat under test is the one the missing PUBACK causes, not the resubscribe's copy.
    bench.client.suppressAcks = true;
    broker.publishFromServer(commandTopic(), rulePush("rule-1", 7), 2, false);
    bench.pump();

    const Rule afterFirst = bench.storedRule("rule-1");
    TEST_ASSERT_EQUAL_INT(7, afterFirst.version);
    TEST_ASSERT_EQUAL_UINT32(1, broker.inFlightFor(clientId()));

    bench.client.suppressAcks = false;
    bench.reconnect();
    bench.pump();

    TEST_ASSERT_EQUAL_UINT32(1, broker.redeliveredCount);
    const Rule afterSecond = bench.storedRule("rule-1");
    TEST_ASSERT_EQUAL_INT(1, (int)bench.rules.getAll().size());
    TEST_ASSERT_EQUAL_INT(afterFirst.version, afterSecond.version);
    TEST_ASSERT_EQUAL_INT((int)afterFirst.mode, (int)afterSecond.mode);
    TEST_ASSERT_EQUAL_INT((int)afterFirst.state, (int)afterSecond.state);
    TEST_ASSERT_EQUAL_FLOAT(afterFirst.valueLow, afterSecond.valueLow);
    TEST_ASSERT_EQUAL_FLOAT(afterFirst.valueHigh, afterSecond.valueHigh);
}

void test_a_redelivered_device_command_asks_the_server_again_and_decides_nothing_itself(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();

    bench.client.suppressAcks = true;
    broker.publishFromServer(commandTopic(), deviceCommandPush(11, "on"), 2, false);
    bench.pump();
    TEST_ASSERT_EQUAL_UINT32(1, bench.outbox.deviceCommandFetches);

    bench.client.suppressAcks = false;
    bench.reconnect();
    bench.pump();

    // The duplicate costs one more GET and nothing else: what is executed comes from the list the
    // server answers with, and a command the module already ran is not in it any more.
    TEST_ASSERT_EQUAL_UINT32(1, broker.redeliveredCount);
    TEST_ASSERT_EQUAL_UINT32(2, bench.outbox.deviceCommandFetches);
    TEST_ASSERT_EQUAL_INT(0, (int)bench.outbox.deviceCommandResults.size());
}

void test_a_repeated_peer_sensor_update_writes_the_same_value(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);
    seedRemoteSensorRule(bench);

    bench.boot();
    bench.remoteSensors.init();
    bench.remoteSensors.onRuleSetChanged(); // the boot sequence, on the first CONNACK

    broker.publishFromPeerModule(peerUpdateTopic(), peerSensorUpdate(21.5));
    bench.pump();
    const float afterFirst = bench.sensorValues.getSensorValue(std::string(PEER_MODULE_UID) + ".1");

    broker.publishFromPeerModule(peerUpdateTopic(), peerSensorUpdate(21.5));
    bench.pump();

    TEST_ASSERT_EQUAL_INT(2, bench.sensorValues.setSensorValueCallCount);
    TEST_ASSERT_EQUAL_FLOAT(afterFirst,
                            bench.sensorValues.getSensorValue(std::string(PEER_MODULE_UID) + ".1"));
}

void test_a_repeated_device_command_run_from_the_payload_asks_for_the_same_state_twice(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();

    // The server is unreachable, so the module falls back to the payload of the push - the one
    // path where a duplicate reaches a relay rather than a GET.
    bench.client.suppressAcks = true;
    broker.publishFromServer(commandTopic(), deviceCommandPush(11, "on"), 2, false);
    bench.pump();
    bench.serverDoesNotAnswerTheFetches();

    bench.client.suppressAcks = false;
    bench.reconnect();
    bench.pump();
    bench.serverDoesNotAnswerTheFetches();

    // Twice, and both times the same command: the device ends in the state the command asked for,
    // which is what "does not break the state" means for a command that carries its own payload.
    TEST_ASSERT_EQUAL_INT(2, (int)bench.deviceCommands.executions.size());
    TEST_ASSERT_EQUAL_STRING("relay-0", bench.deviceCommands.executions[1].deviceUid.c_str());
    TEST_ASSERT_EQUAL_STRING("on", bench.deviceCommands.executions[1].requestedState.c_str());
    TEST_ASSERT_EQUAL_INT(bench.deviceCommands.executions[0].commandId,
                          bench.deviceCommands.executions[1].commandId);
}

void test_a_retained_device_command_too_old_to_run_is_refused_rather_than_executed(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    // The retained slot of the topic holds a command from before the module went down; the server
    // is not answering, so nothing but the payload says what to do.
    broker.publishFromServer(commandTopic(), deviceCommandPush(11, "on"));
    bench.time.setCommandTimeValid(false);

    bench.boot();
    bench.pump();
    bench.serverDoesNotAnswerTheFetches();

    TEST_ASSERT_EQUAL_INT(0, (int)bench.deviceCommands.executions.size());
    TEST_ASSERT_EQUAL_INT(1, (int)bench.outbox.deviceCommandResults.size());
    TEST_ASSERT_EQUAL_STRING("error", bench.outbox.deviceCommandResults[0].status.c_str());
}

// --- the module command that carries no date ------------------------------------------------------
//
// The two tests below state what the command topic does with a ModuleCommandNotification today;
// neither is a property worth having. A module command that is not one of the six complex ones
// (CommandService::isComplexModuleCommand) is not a signal to fetch anything: CommandService hands
// it to ModuleCommandExecutionUseCase::execute(), which runs BlinkingStart, BlinkingStop and Reboot
// straight out of the payload. The age filter of this class - checkCommandTime(), 300 seconds -
// lives in executeCommands(), the path a fetched list takes; execute() does not call it, and the
// body has no date in it to check anyway.

void test_a_reboot_pushed_while_the_module_was_away_restarts_it_when_it_returns(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();
    bench.mqtt.disconnect();

    // Published without the retain flag, as the backend publishes it: Reboot is not one of the
    // commands whose status it tracks (ModuleCommandConst.CommandsRequiringStatusTracking), and
    // only those are retained. So the session queue is the only way in, and the only reason this
    // message is still deliverable at all.
    broker.publishFromServer(commandTopic(),
                             moduleCommandPush("Reboot", ",\"noScanSensors\":\"true\""), 2, false);
    TEST_ASSERT_EQUAL_UINT32(1, broker.queuedFor(clientId()));
    TEST_ASSERT_EQUAL_UINT32(0, bench.arduino.restartCount);

    // However long the outage was: a command of this age is one the module would call expired, if
    // it asked. Nothing on this path asks.
    bench.time.setCommandTimeValid(false);

    bench.mqtt.connect();
    bench.pump();

    TEST_ASSERT_EQUAL_UINT32(0, bench.outbox.moduleCommandFetches);
    TEST_ASSERT_FALSE(bench.time.wasCommandTimeValidCalled());

    // To the end of the path: the two settings the handler writes before it restarts the board,
    // and the restart itself.
    TEST_ASSERT_EQUAL_UINT8(1, bench.settings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_USER, bench.settings.getBootType());
    TEST_ASSERT_EQUAL_UINT32(1, bench.arduino.restartCount);
}

void test_a_redelivered_reboot_restarts_the_module_a_second_time(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();

    // The push arrives and the module restarts, but the acknowledgement is lost on the way back -
    // so the broker still owes the message and sends it again, with DUP, on the next connection.
    // The host adapter records the restart rather than performing it, so the same bench stands for
    // the module that came back: what this states is that the repeat reaches restart() a second
    // time, not that a boot ran in between.
    bench.client.suppressAcks = true;
    broker.publishFromServer(commandTopic(), moduleCommandPush("Reboot"), 2, false);
    bench.pump();
    TEST_ASSERT_EQUAL_UINT32(1, bench.arduino.restartCount);
    TEST_ASSERT_EQUAL_UINT32(1, broker.inFlightFor(clientId()));

    bench.client.suppressAcks = false;
    bench.reconnect();
    bench.pump();

    TEST_ASSERT_EQUAL_UINT32(1, broker.redeliveredCount);
    TEST_ASSERT_EQUAL_UINT32(2, bench.arduino.restartCount);
}

// --- retained: the last push is handed over again on every subscription ---------------------------

void test_a_retained_push_is_delivered_again_on_every_resubscribe(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();
    broker.publishFromServer(commandTopic(), deviceCommandPush(11, "on"));
    bench.pump();
    TEST_ASSERT_EQUAL_UINT32(1, bench.outbox.deviceCommandFetches);

    // Nothing new was published; the module simply came back, and onMqttConnect() subscribes to
    // every topic again - which is what makes the broker hand the retained message over once more.
    bench.reconnect();
    bench.pump();

    TEST_ASSERT_TRUE(broker.hasRetained(commandTopic()));
    TEST_ASSERT_EQUAL_UINT32(2, bench.outbox.deviceCommandFetches);
}

void test_a_retained_push_older_than_the_stored_rule_does_not_replace_it(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    // What the module holds after its restart: version 9, read from the file at boot.
    Rule stored;
    stored.uid = "rule-1";
    stored.version = 9;
    stored.ruleType = RuleType::SENSOR;
    stored.deviceUid = "dev-1";
    stored.sensorUid = "sensor-1";
    stored.mode = RuleMode::OFF;
    stored.valueLow = 40;
    stored.valueHigh = 50;
    stored.direction = RuleDirection::DOWN;
    stored.state = RuleState::SYNCHRONIZED;
    bench.rules.save(stored);

    // What the broker still holds on the topic: the push that carried version 7.
    broker.publishFromServer(commandTopic(), rulePush("rule-1", 7));

    bench.boot();
    bench.pump();

    const Rule after = bench.storedRule("rule-1");
    TEST_ASSERT_EQUAL_INT(9, after.version);
    TEST_ASSERT_EQUAL_FLOAT(40, after.valueLow);
    TEST_ASSERT_EQUAL_FLOAT(50, after.valueHigh);
}

void test_a_retained_push_whose_mode_differs_replaces_the_newer_stored_rule(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    // The same situation as above, except that the mode of the stored rule has moved on since.
    // RuleRepository::synchronizeRules() compares versions first but takes any incoming rule whose
    // mode differs, whatever its version - so the older push wins. This test states the boundary
    // of the guarantee rather than a property worth having: a retained message is as old as the
    // last push, and it is applied on every reconnect.
    Rule stored;
    stored.uid = "rule-1";
    stored.version = 9;
    stored.ruleType = RuleType::SENSOR;
    stored.deviceUid = "dev-1";
    stored.sensorUid = "sensor-1";
    stored.mode = RuleMode::ON;
    stored.valueLow = 40;
    stored.valueHigh = 50;
    stored.direction = RuleDirection::DOWN;
    stored.state = RuleState::SYNCHRONIZED;
    bench.rules.save(stored);

    broker.publishFromServer(commandTopic(), rulePush("rule-1", 7, "Off"));

    bench.boot();
    bench.pump();

    const Rule after = bench.storedRule("rule-1");
    TEST_ASSERT_EQUAL_INT(7, after.version);
    TEST_ASSERT_EQUAL_INT((int)RuleMode::OFF, (int)after.mode);
}

// --- the acknowledgement ---------------------------------------------------------------------------

void test_the_broker_keeps_a_delivery_until_the_module_acknowledges_it(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();

    bench.client.suppressAcks = true;
    broker.publishFromServer(commandTopic(), rulePush("rule-1", 7), 2, false);
    bench.pump();

    // Handled by the module, still owed as far as the broker is concerned.
    TEST_ASSERT_EQUAL_INT(7, bench.storedRule("rule-1").version);
    TEST_ASSERT_EQUAL_UINT32(1, broker.inFlightFor(clientId()));
    TEST_ASSERT_EQUAL_UINT32(0, broker.pubackCount);

    bench.client.suppressAcks = false;
    bench.reconnect();
    bench.pump();

    TEST_ASSERT_EQUAL_UINT32(1, broker.pubackCount);
    TEST_ASSERT_EQUAL_UINT32(0, broker.inFlightFor(clientId()));
}

void test_a_push_lost_on_the_way_is_sent_again_after_the_reconnect(void)
{
    SimMqttBroker broker;
    ModuleBench bench;
    bench.attachTo(broker);

    bench.boot();

    broker.loseNextDelivery = true;
    broker.publishFromServer(commandTopic(), rulePush("rule-1", 7), 2, false);
    bench.pump();

    TEST_ASSERT_EQUAL_UINT32(1, broker.lostInTransitCount);
    TEST_ASSERT_EQUAL_STRING("", bench.storedRule("rule-1").uid.c_str());

    bench.reconnect();
    bench.pump();

    TEST_ASSERT_EQUAL_INT(7, bench.storedRule("rule-1").version);
}

} // namespace

extern "C" void run_mqtt_guaranteed_delivery_tests(void)
{
    RUN_TEST(test_a_subscription_taken_while_connected_asks_for_at_least_once);
    RUN_TEST(test_a_subscription_replayed_after_a_reconnect_asks_for_at_least_once);
    RUN_TEST(test_the_peer_sensor_subscription_asks_for_at_least_once_too);
    RUN_TEST(test_a_boot_without_a_link_connects_on_the_first_beat_and_subscribes_the_commands);
    RUN_TEST(test_a_client_that_dialled_once_closes_before_it_dials_again);
    RUN_TEST(test_the_module_asks_the_broker_to_keep_its_session);
    RUN_TEST(test_the_broker_recognises_the_module_after_a_restart);

    RUN_TEST(test_a_rule_pushed_while_the_module_was_away_is_applied_when_it_returns);
    RUN_TEST(test_a_device_command_pushed_while_the_module_was_away_reaches_the_command_service);
    RUN_TEST(test_a_peer_sensor_update_while_the_module_was_away_is_not_kept);

    RUN_TEST(test_a_redelivered_rule_push_leaves_the_stored_rule_as_it_was);
    RUN_TEST(test_a_redelivered_device_command_asks_the_server_again_and_decides_nothing_itself);
    RUN_TEST(test_a_repeated_peer_sensor_update_writes_the_same_value);
    RUN_TEST(test_a_repeated_device_command_run_from_the_payload_asks_for_the_same_state_twice);
    RUN_TEST(test_a_retained_device_command_too_old_to_run_is_refused_rather_than_executed);

    RUN_TEST(test_a_reboot_pushed_while_the_module_was_away_restarts_it_when_it_returns);
    RUN_TEST(test_a_redelivered_reboot_restarts_the_module_a_second_time);

    RUN_TEST(test_a_retained_push_is_delivered_again_on_every_resubscribe);
    RUN_TEST(test_a_retained_push_older_than_the_stored_rule_does_not_replace_it);
    RUN_TEST(test_a_retained_push_whose_mode_differs_replaces_the_newer_stored_rule);

    RUN_TEST(test_the_broker_keeps_a_delivery_until_the_module_acknowledges_it);
    RUN_TEST(test_a_push_lost_on_the_way_is_sent_again_after_the_reconnect);
}
