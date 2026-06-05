#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <IPAddress.h>
#include <set>
#include <vector>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Mqtt/IMessageProcessor.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "TopicHandlerFactory.h"

class MqttManager : public IMqttManager
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
    const char *_clientId;
    const char *_username;
    const char *_password;
    bool _cleanSession;
    uint32_t _lastReconnectAttempt;
    uint32_t _reconnectInterval;
    uint32_t _reconnectAt;
    int _mqttFails;
    volatile bool _connected;
    bool _configured;
    std::set<String> _subscribedTopics;

    std::recursive_mutex *_clientMutex;

    struct MqttMessage {
        String topic;
        std::vector<uint8_t> payload;
    };
    QueueHandle_t _messageQueue;

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

            _logger->println(buffer);
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
        if (_clientId != nullptr)
        {
            _mqttClient.setClientId(_clientId);
        }
        _mqttClient.setKeepAlive(30);
        _mqttClient.setCleanSession(_cleanSession);

        _mqttClient.onConnect([this](bool sessionPresent) { onMqttConnect(sessionPresent); });
        _mqttClient.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { onMqttDisconnect(reason); });
        _mqttClient.onMessage([this](char *topic, char *payload, AsyncMqttClientMessageProperties properties,
                                     size_t len, size_t index, size_t total) {
            onMqttMessage(topic, payload, len, index, total);
        });

        _configured = true;
    }

    void onMqttConnect(bool sessionPresent)
    {
        _connected = true;
        _mqttFails = 0;
        _reconnectInterval = 5000;
        log("MQTT: connected (sessionPresent=%d)", sessionPresent ? 1 : 0);

        OptLock lock(_clientMutex);
        for (const auto &topic : _subscribedTopics)
        {
            _mqttClient.subscribe(topic.c_str(), 0);
        }
    }

    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
    {
        _connected = false;
        _mqttFails++;
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

    void onMqttMessage(char *topic, char *payload, size_t len, size_t index, size_t total)
    {
        if (index != 0 || len != total)
        {
            return;
        }

        MqttMessage *msg = new MqttMessage();
        msg->topic = String(topic);
        msg->payload.assign(reinterpret_cast<uint8_t *>(payload), reinterpret_cast<uint8_t *>(payload) + len);

        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE)
        {
            delete msg;
        }
    }

public:
    MqttManager(
        AsyncMqttClient &mqttClient,
        ILogger *logger,
        IDebugSystem *debugSystem,
        const char *clientId,
        IPAddress broker,
        uint16_t port,
        const char *username = nullptr,
        const char *password = nullptr,
        bool cleanSession = true,
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
                                                       _clientId(clientId),
                                                       _username(username),
                                                       _password(password),
                                                       _cleanSession(cleanSession),
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

        if (_wifiAdapter != nullptr && !_wifiAdapter->isConnected())
        {
            log("MQTT: WiFi not connected, connect deferred");
            return false;
        }

        OptLock lock(_clientMutex);
        _mqttClient.connect();
        _lastReconnectAttempt = millis();
        log("MQTT: connect requested");
        return true;
    }

    void disconnect() override
    {
        OptLock lock(_clientMutex);
        _mqttClient.disconnect();
        log("MQTT: disconnect requested");
    }

    bool isConnected() override
    {
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
        uint16_t packetId = _mqttClient.subscribe(topic, 0);
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
            return false;
        }

        OptLock lock(_clientMutex);
        uint16_t packetId = _mqttClient.publish(topic, 0, false, payload);
        if (packetId == 0)
        {
            log("Failed to publish to topic: %s", topic);
            return false;
        }
        return true;
    }

    void loop() override
    {
        MqttMessage *msg = nullptr;
        while (_messageQueue != nullptr && xQueueReceive(_messageQueue, &msg, 0) == pdTRUE)
        {
            if (msg != nullptr)
            {
                if (_messageProcessor != nullptr)
                {
                    _messageProcessor->processMessage(msg->topic.c_str(), msg->payload.data(), msg->payload.size());
                }
                delete msg;
            }
        }

        if (!_connected && millis() >= _reconnectAt &&
            (_wifiAdapter == nullptr || _wifiAdapter->isConnected()))
        {
            connect();
            _reconnectAt = millis() + _reconnectInterval;
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

    const char *getClientId() const override
    {
        return _clientId;
    }

    void setClientId(const char *clientId) override
    {
        _clientId = clientId;
        if (_configured && _clientId != nullptr)
        {
            _mqttClient.setClientId(_clientId);
        }
        log("MQTT client ID set to: %s", clientId);
    }
};

#endif // MQTT_MANAGER_H
