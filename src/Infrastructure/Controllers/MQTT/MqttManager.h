#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <IPAddress.h>
#include <set>
#include <vector>
#include <mutex>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Mqtt/IMessageProcessor.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttLinkMachine.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "MqttInboundSizeGuard.h"
#include "TopicHandlerFactory.h"
#include <cstdarg>
#include <cstdio>

/**
 * @brief The MQTT client of the module: subscriptions, inbound dispatch, and the reconnect.
 *
 * The reconnect is a state machine and not a task. Idle, Closing and Connecting are advanced by
 * whoever calls serviceLinkOnce() - on the device the outbox network task, between the REST calls
 * it makes - so that a broker socket is never rebuilt in the same seconds as a TLS handshake. The
 * machine keeps its own timers and backoff; the caller only lends it a task.
 */
class MqttManager : public IMqttManager, public IMqttLinkMachine
{
private:
    AsyncMqttClient &_mqttClient;
    ILogger *_logger;
    IDebugSystem *_debugSystem;
    IMessageProcessor *_messageProcessor;
    TopicHandlerFactory *_topicHandlerFactory;
    IWifiAdapter *_wifiAdapter;
    IPAddress _broker;
    uint16_t _port;
    std::string _clientId;
    const char *_username;
    const char *_password;
    uint32_t _lastReconnectAttempt;
    uint32_t _reconnectInterval;
    uint32_t _reconnectAt;
    int _mqttFails;
    int _consecutivePublishFails = 0;
    static const int PUBLISH_FAIL_RECONNECT_THRESHOLD = 10;

    /**
     * @brief The QoS every subscription of this module is taken with.
     *
     * A message is delivered with the lower of two guarantees: the one the publisher asked of the
     * broker, and the one the subscriber asked for in its SUBSCRIBE. At QoS 0 the broker writes
     * the message to whatever socket is open at that moment and forgets it - a module that is
     * offline, or whose link drops mid-transfer, never learns the message existed, and the server
     * cannot tell the difference. At QoS 1 the broker holds the message until this client
     * acknowledges it, and holds it for the length of an outage while the session is kept.
     *
     * The price is that a message can arrive more than once - at-least-once is what this level
     * means - so what a subscription feeds must tolerate a repeat. Both the command topic and the
     * peer sensor topic do: a command notification makes the module ask the server for the
     * authoritative list, and a rule snapshot is merged by version.
     */
    static const uint8_t SUBSCRIBE_QOS = 1;

    /**
     * @brief Whether the broker may throw this client's session away at CONNECT.
     *
     * It may not, and this is not a wiring option: a broker keeps the subscriptions and the
     * messages a client missed only for a session it was asked to keep. With a clean session the
     * queue the QoS above buys does not exist - everything published while the module was away is
     * discarded at the moment it reconnects, silently on both sides.
     *
     * The other half of it is the client id: the session is filed under that string, so the
     * guarantee only holds as long as the module presents the same id after a restart. See
     * ModuleSetupService::setupMqttConnection(), which composes it from the module UID.
     */
    static const bool CLEAN_SESSION = false;
    volatile bool _connected;
    bool _configured;
    std::set<String> _subscribedTopics;

    enum class ConnState { Idle, Closing, Connecting };
    ConnState _connState = ConnState::Idle;
    uint32_t _closeStartedAt = 0;
    uint32_t _connectStartedAt = 0;
    volatile bool _disconnectEvent = false;
    // Whether the client has been asked to dial since the boot: until then it has no socket to tear
    // down, and the reconnect goes straight to CONNECT.
    bool _connectIssued = false;

    std::recursive_mutex *_clientMutex;

    struct MqttMessage {
        String topic;
        std::vector<uint8_t> payload;
    };
    QueueHandle_t _messageQueue;

    static const size_t MAX_MESSAGE_SIZE = MqttInboundSizeGuard::MAX_INBOUND_MESSAGE_SIZE;
    String _assemblyTopic;
    std::vector<uint8_t> _assemblyBuf;
    size_t _assemblyTotal = 0;
    MqttInboundSizeGuard _inboundSizeGuard;

    struct OptLock {
        std::recursive_mutex *m;
        explicit OptLock(std::recursive_mutex *mm) : m(mm) { if (m) m->lock(); }
        ~OptLock() { if (m) m->unlock(); }
    };

    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr && _debugSystem->isFeatureEnabled(DebugFeatures::MQTT_DEBUG))
        {
            char buffer[160];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature *feature = _debugSystem->getFeature(DebugFeatures::MQTT_DEBUG);
            _logger->println(DebugFeatures::MQTT_DEBUG, buffer,
                             feature ? feature->getLogChannel() : MqttLoggerMode::SerialOnly);
        }
    }

    void configureClient()
    {
        if (_configured)
        {
            return;
        }

        _mqttClient.setServer(_broker, _port);
        if (_username != nullptr && _password != nullptr)
        {
            _mqttClient.setCredentials(_username, _password);
        }
        if (!_clientId.empty())
        {
            _mqttClient.setClientId(_clientId.c_str());
        }
        _mqttClient.setKeepAlive(30);
        _mqttClient.setCleanSession(CLEAN_SESSION);

        _mqttClient.onConnect([this](bool sessionPresent) { onMqttConnect(sessionPresent); });
        _mqttClient.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { onMqttDisconnect(reason); });
        _mqttClient.onMessage([this](char *topic, char *payload, AsyncMqttClientMessageProperties properties,
                                     size_t len, size_t index, size_t total) {
            onMqttMessage(topic, payload, len, index, total);
        });

        log("MQTT: client configured broker=%s:%u cleanSession=%d", _broker.toString().c_str(), _port, CLEAN_SESSION ? 1 : 0);
        _configured = true;
    }

    void onMqttConnect(bool sessionPresent)
    {
        _connected = true;
        _mqttFails = 0;
        _consecutivePublishFails = 0;
        _reconnectInterval = 5000;
        log("MQTT: connected (sessionPresent=%d), resubscribing %d topics", sessionPresent ? 1 : 0, static_cast<int>(_subscribedTopics.size()));

        OptLock lock(_clientMutex);
        for (const auto &topic : _subscribedTopics)
        {
            _mqttClient.subscribe(topic.c_str(), SUBSCRIBE_QOS);
        }
    }

    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
    {
        _connected = false;
        _mqttFails++;
        _disconnectEvent = true;
        if (_reconnectInterval < 30000)
        {
            _reconnectInterval = _reconnectInterval * 2;
            if (_reconnectInterval > 30000)
            {
                _reconnectInterval = 30000;
            }
        }
        _reconnectAt = millis() + _reconnectInterval;
        log("MQTT: disconnected (reason=%d), next try in %ums", static_cast<int>(reason), _reconnectInterval);
    }

    void enqueueMessage(const String &topic, const uint8_t *data, size_t len)
    {
        MqttMessage *msg = new MqttMessage();
        msg->topic = topic;
        msg->payload.assign(data, data + len);

        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE)
        {
            log("MQTT: inbound queue full, dropping topic=%s", topic.c_str());
            delete msg;
        }
    }

    // The debug line above only reaches Serial (the MQTT debug feature logs there), so on its
    // own the drop stays invisible to the server. Publish it as an error event on the module
    // log topic as well, so an undelivered message cannot look like a delivered one.
    void reportOversizedDrop(const char *topic, size_t total)
    {
        if (_logger == nullptr)
        {
            return;
        }

        char buffer[192];
        snprintf(buffer, sizeof(buffer),
                 "[error] MQTT: inbound message dropped (over size limit) topic=%s total=%u limit=%u drops=%u",
                 topic != nullptr ? topic : "",
                 static_cast<unsigned>(total),
                 static_cast<unsigned>(MAX_MESSAGE_SIZE),
                 static_cast<unsigned>(_inboundSizeGuard.drops));

        _logger->println(DebugFeatures::MQTT_DEBUG, MqttLoggerMode::MqttAndSerial, buffer);
    }

    void onMqttMessage(char *topic, char *payload, size_t len, size_t index, size_t total)
    {
        if (total == 0)
        {
            return;
        }

        const MqttInboundSizeVerdict verdict = _inboundSizeGuard.inspect(total, index, MAX_MESSAGE_SIZE);
        if (verdict != MqttInboundSizeVerdict::Accept)
        {
            // A fragmented oversized message is announced with the same `total` on every
            // fragment, so it is reported once - on the first one - and not once per fragment.
            if (verdict == MqttInboundSizeVerdict::DropReport)
            {
                log("MQTT: dropping oversized inbound topic=%s total=%u limit=%u drops=%u",
                    topic, static_cast<unsigned>(total), static_cast<unsigned>(MAX_MESSAGE_SIZE),
                    static_cast<unsigned>(_inboundSizeGuard.drops));
                reportOversizedDrop(topic, total);
            }
            return;
        }

        if (len == total)
        {
            enqueueMessage(String(topic), reinterpret_cast<uint8_t *>(payload), len);
            return;
        }

        if (index == 0)
        {
            _assemblyTopic = String(topic);
            _assemblyTotal = total;
            _assemblyBuf.clear();
            _assemblyBuf.reserve(total);
        }
        else if (_assemblyTotal != total || _assemblyTopic != String(topic))
        {
            log("MQTT: dropping out-of-order fragment topic=%s", topic);
            return;
        }

        _assemblyBuf.insert(_assemblyBuf.end(),
                            reinterpret_cast<uint8_t *>(payload),
                            reinterpret_cast<uint8_t *>(payload) + len);

        if (_assemblyBuf.size() >= _assemblyTotal)
        {
            enqueueMessage(_assemblyTopic, _assemblyBuf.data(), _assemblyBuf.size());
            _assemblyBuf.clear();
            _assemblyBuf.shrink_to_fit();
            _assemblyTotal = 0;
            _assemblyTopic = "";
        }
    }

    void serviceReconnect()
    {
        bool wifiOk = (_wifiAdapter != nullptr && _wifiAdapter->isConnected());

        if (_connected || !wifiOk)
        {
            _connState = ConnState::Idle;
            return;
        }

        uint32_t now = millis();

        switch (_connState)
        {
            case ConnState::Idle:
                if (now >= _reconnectAt && !_connectIssued)
                {
                    // The boot's first CONNECT: the client has never dialled, so there is no stale
                    // socket to close and no close to wait for.
                    log("MQTT: first connect of the boot, nothing to tear down");
                    if (!connect())
                    {
                        _reconnectAt = now + _reconnectInterval;
                        log("MQTT: connect refused (wifi down), back to idle");
                    }
                }
                else if (now >= _reconnectAt)
                {
                    _disconnectEvent = false;
                    {
                        OptLock lock(_clientMutex);
                        _mqttClient.disconnect(true);
                    }
                    _closeStartedAt = now;
                    _connState = ConnState::Closing;
                    log("MQTT: reconnect - tearing down stale socket");
                }
                break;

            case ConnState::Closing:
                if (_disconnectEvent || now - _closeStartedAt >= CLOSE_GRACE_MS)
                {
                    log("MQTT: close settled (event=%d), issuing connect", _disconnectEvent ? 1 : 0);
                    _disconnectEvent = false;
                    if (!connect())
                    {
                        _connState = ConnState::Idle;
                        _reconnectAt = now + _reconnectInterval;
                        log("MQTT: connect refused (wifi down), back to idle");
                    }
                }
                break;

            case ConnState::Connecting:
                if (_disconnectEvent)
                {
                    _disconnectEvent = false;
                    _connState = ConnState::Idle;
                    log("MQTT: connect attempt failed during CONNECTING, back to idle");
                }
                else if (now - _connectStartedAt >= CONNECT_TIMEOUT_MS)
                {
                    _connState = ConnState::Idle;
                    _reconnectAt = now + _reconnectInterval;
                    log("MQTT: connect timed out, will retry in %ums", _reconnectInterval);
                }
                break;
        }
    }

public:
    /**
     * @brief The two waits inside one connect act.
     *
     * Public because they are the act's length as the network task sees it: a stale socket is
     * given CLOSE_GRACE_MS to close when no disconnect event settles it, and a CONNECT is given
     * CONNECT_TIMEOUT_MS to be answered. Their sum is the longest a REST request waits behind a
     * broker that says nothing at all.
     */
    static const uint32_t CLOSE_GRACE_MS = 2000;
    static const uint32_t CONNECT_TIMEOUT_MS = 10000;

    MqttManager(
        AsyncMqttClient &mqttClient,
        ILogger *logger,
        IDebugSystem *debugSystem,
        const char *clientId,
        IPAddress broker,
        uint16_t port,
        const char *username = nullptr,
        const char *password = nullptr,
        IMessageProcessor *messageProcessor = nullptr,
        TopicHandlerFactory *topicHandlerFactory = nullptr,
        IWifiAdapter *wifiAdapter = nullptr,
        std::recursive_mutex *clientMutex = nullptr) : _mqttClient(mqttClient),
                                                       _logger(logger),
                                                       _debugSystem(debugSystem),
                                                       _messageProcessor(messageProcessor),
                                                       _topicHandlerFactory(topicHandlerFactory),
                                                       _wifiAdapter(wifiAdapter),
                                                       _broker(broker),
                                                       _port(port),
                                                       _clientId(clientId != nullptr ? clientId : ""),
                                                       _username(username),
                                                       _password(password),
                                                       _lastReconnectAttempt(0),
                                                       _reconnectInterval(5000),
                                                       _reconnectAt(0),
                                                       _mqttFails(0),
                                                       _connected(false),
                                                       _configured(false),
                                                       _subscribedTopics(),
                                                       _clientMutex(clientMutex)
    {
        _messageQueue = xQueueCreate(16, sizeof(MqttMessage *));
    }

    ~MqttManager() override = default;

    void setupMqttTopicHandlers() override
    {
        if (_messageProcessor != nullptr && _topicHandlerFactory != nullptr)
        {
            log("Setting up MQTT topic handlers");
            auto handlers = _topicHandlerFactory->createHandlers();
            for (auto *handler : handlers)
            {
                _messageProcessor->addTopicHandler(handler);
                const char *topicPattern = handler->getTopicPattern();
                if (subscribe(topicPattern))
                {
                    log("Subscribed to topic pattern: %s", topicPattern);
                }
                else
                {
                    log("Failed to subscribe to topic pattern: %s", topicPattern);
                }
            }
            log("MQTT topic handlers setup completed");
        }
    }

    bool connect() override
    {
        configureClient();
        log("MQTT: connect() entry wifi=%d state=%d fails=%d",
            (_wifiAdapter != nullptr) ? (_wifiAdapter->isConnected() ? 1 : 0) : -1, static_cast<int>(_connState), _mqttFails);

        if (_wifiAdapter != nullptr && !_wifiAdapter->isConnected())
        {
            log("MQTT: WiFi not connected, connect deferred");
            return false;
        }

        {
            OptLock lock(_clientMutex);
            _disconnectEvent = false;
            _connState = ConnState::Connecting;
            _connectStartedAt = millis();
            _lastReconnectAttempt = _connectStartedAt;
            _connectIssued = true;
            _mqttClient.connect();
        }
        log("MQTT: connect requested to %s:%u clientId=%s", _broker.toString().c_str(), _port, _clientId.c_str());
        return true;
    }

    bool serviceLinkOnce() override
    {
        // Nothing to keep alive before the boot has asked for the broker: configureClient() runs
        // first thing in connect(), and until then the client has no id to present - a CONNECT
        // made here would file the session under the wrong one.
        if (!_configured)
        {
            return false;
        }

        isConnected();
        serviceReconnect();
        return _connState != ConnState::Idle;
    }

    void disconnect() override
    {
        OptLock lock(_clientMutex);
        _mqttClient.disconnect();
        log("MQTT: disconnect requested");
    }

    bool isConnected() override
    {
        if (_connected)
        {
            bool clientConnected;
            {
                OptLock lock(_clientMutex);
                clientConnected = _mqttClient.connected();
            }
            if (!clientConnected)
            {
                _connected = false;
                log("MQTT: client reports disconnected, clearing connected flag");
            }
            else if (_wifiAdapter != nullptr && !_wifiAdapter->isConnected())
            {
                _connected = false;
                log("MQTT: connection dropped (WiFi down), clearing connected flag");
            }
        }
        return _connected;
    }

    bool subscribe(const char *topic) override
    {
        if (topic == nullptr)
        {
            return false;
        }

        _subscribedTopics.insert(String(topic));

        if (!_connected)
        {
            log("MQTT: subscribe buffered (not connected): %s", topic);
            return true;
        }

        OptLock lock(_clientMutex);
        uint16_t packetId = _mqttClient.subscribe(topic, SUBSCRIBE_QOS);
        if (packetId != 0)
        {
            log("Subscribed to topic: %s", topic);
            return true;
        }
        log("Failed to subscribe to topic: %s", topic);
        return false;
    }

    bool unsubscribe(const char *topic) override
    {
        if (topic == nullptr)
        {
            return false;
        }

        _subscribedTopics.erase(String(topic));

        if (!_connected)
        {
            return true;
        }

        OptLock lock(_clientMutex);
        uint16_t packetId = _mqttClient.unsubscribe(topic);
        if (packetId != 0)
        {
            log("Unsubscribed from topic: %s", topic);
            return true;
        }
        return false;
    }

    bool publish(const char *topic, const char *payload) override
    {
        if (!_connected)
        {
            log("MQTT: publish dropped (not connected): %s", topic);
            return false;
        }

        OptLock lock(_clientMutex);
        uint16_t packetId = _mqttClient.publish(topic, 0, false, payload);
        if (packetId == 0)
        {
            _consecutivePublishFails++;
            log("Failed to publish to topic: %s (consecutive fails: %d)", topic, _consecutivePublishFails);
            if (_consecutivePublishFails >= PUBLISH_FAIL_RECONNECT_THRESHOLD)
            {
                log("MQTT: %d consecutive publish failures, forcing reconnect", _consecutivePublishFails);
                _consecutivePublishFails = 0;
                _connected = false;
                _disconnectEvent = false;
                _reconnectAt = millis();
            }
            return false;
        }
        _consecutivePublishFails = 0;
        return true;
    }

    void loop() override
    {
        MqttMessage *msg = nullptr;
        while (_messageQueue != nullptr && xQueueReceive(_messageQueue, &msg, 0) == pdTRUE)
        {
            if (msg != nullptr)
            {
                log("MQTT: dispatching inbound topic=%s size=%u", msg->topic.c_str(), static_cast<unsigned>(msg->payload.size()));
                if (_messageProcessor != nullptr)
                {
                    _messageProcessor->processMessage(msg->topic.c_str(), msg->payload.data(), msg->payload.size());
                }
                delete msg;
            }
        }
    }

    void setMessageProcessor(IMessageProcessor *processor) override
    {
        _messageProcessor = processor;
        log("Message processor set");

        if (_topicHandlerFactory != nullptr)
        {
            setupMqttTopicHandlers();
        }
    }

    void setTopicHandlerFactory(TopicHandlerFactory *factory)
    {
        _topicHandlerFactory = factory;
        log("Topic handler factory set");

        if (_messageProcessor != nullptr)
        {
            setupMqttTopicHandlers();
        }
    }

    void setWifiAdapter(IWifiAdapter *wifiAdapter)
    {
        _wifiAdapter = wifiAdapter;
        log("WiFi adapter set for MQTT manager");
    }

    int getMqttFails() const override
    {
        return _mqttFails;
    }

    void resetMqttFails() override
    {
        _mqttFails = 0;
        log("MQTT failures counter reset");
    }

    uint32_t getInboundOversizeDrops() const override
    {
        return _inboundSizeGuard.drops;
    }

    const char *getClientId() const override
    {
        return _clientId.c_str();
    }

    void setClientId(const char *clientId) override
    {
        _clientId = (clientId != nullptr) ? clientId : "";
        if (_configured && !_clientId.empty())
        {
            _mqttClient.setClientId(_clientId.c_str());
        }
        log("MQTT: client ID set to: %s", _clientId.c_str());
    }
};

#endif // MQTT_MANAGER_H
