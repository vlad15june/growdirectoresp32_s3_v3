#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Mqtt/IMessageProcessor.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "TopicHandlerFactory.h"

/**
 * MQTT Manager implementation
 */
class MqttManager : public IMqttManager
{
private:
    PubSubClient &_mqttClient;
    ILogger *_logger;
    IDebugSystem *_debugSystem;
    IMessageProcessor *_messageProcessor;
    TopicHandlerFactory *_topicHandlerFactory;
    const char *_clientId;
    const char *_username;
    const char *_password;
    bool _cleanSession;
    uint32_t _lastReconnectAttempt;
    uint16_t _reconnectInterval;
    int _mqttFails; // Track MQTT connection failures

    // Static instance pointer for callback access
    static MqttManager *_instance;

    // Logging method
    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr && _debugSystem->isFeatureEnabled(DebugFeatures::MQTT_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(buffer);
        }
    }

    bool reconnect()
    {
        if (millis() - _lastReconnectAttempt < _reconnectInterval)
        {
            return false;
        }

        bool connected = connect();
        if (!connected)
        {
            _mqttFails++; // Increment failures counter on reconnect failure
            log("MQTT reconnect failed, total failures: %d", _mqttFails);
        }

        return connected;
    }

public:
    MqttManager(
        PubSubClient &mqttClient,
        ILogger *logger,
        IDebugSystem *debugSystem,
        const char *clientId,
        const char *username = nullptr,
        const char *password = nullptr,
        bool cleanSession = true,
        IMessageProcessor *messageProcessor = nullptr,
        TopicHandlerFactory *topicHandlerFactory = nullptr) : _mqttClient(mqttClient),
                                                              _logger(logger),
                                                              _debugSystem(debugSystem),
                                                              _messageProcessor(messageProcessor),
                                                              _topicHandlerFactory(topicHandlerFactory),
                                                              _clientId(clientId),
                                                              _username(username),
                                                              _password(password),
                                                              _cleanSession(cleanSession),
                                                              _lastReconnectAttempt(0),
                                                              _reconnectInterval(5000),
                                                              _mqttFails(0)
    {

        _instance = this;

        _mqttClient.setCallback(mqttCallback);

        _mqttClient.setBufferSize(4096);
    }

    ~MqttManager() override
    {
        if (_instance == this)
        {
            _instance = nullptr;
        }
    }

    void setupMqttTopicHandlers() override
    {
        if (_messageProcessor != nullptr && _topicHandlerFactory != nullptr)
        {
            log("Setting up MQTT topic handlers");
            auto handlers = _topicHandlerFactory->createHandlers();
            for (auto *handler : handlers)
            {
                // Add the handler to the message processor
                _messageProcessor->addTopicHandler(handler);

                // TODO: Check if need subscribe to the topic for this handler. Add flag to the handler
                // Subscribe to the handler's topic pattern
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

    // Static callback that will be passed to PubSubClient
    static void mqttCallback(char *topic, byte *payload, unsigned int length)
    {
        // Now we can access the instance through the static pointer
        if (_instance && _instance->_messageProcessor)
        {
            // NOTE: Not use mqtt logger here to avoid corrupting the MQTT data
            // _instance->log("Received message on topic: %s", topic);
            _instance->_messageProcessor->processMessage(topic, payload, length);
        }
    }

    bool connect() override
    {
        if (_mqttClient.connected())
        {
            return true;
        }

        _lastReconnectAttempt = millis();

        log("Attempting MQTT connection...");

        bool connected = false;
        if (_username != nullptr && _password != nullptr)
        {
            connected = _mqttClient.connect(_clientId, _username, _password, nullptr, 0, _cleanSession, nullptr);
        }
        else
        {
            connected = _mqttClient.connect(_clientId);
        }

        if (connected)
        {
            log("MQTT connected");

            // Resubscribe to topics if needed
            // Implementation depends on how you want to handle topic subscriptions
        }
        else
        {
            _mqttFails++; // Increment failures counter on connection failure
            char failReason[64];
            snprintf(failReason, sizeof(failReason), "Failed, rc=%d, total failures: %d", _mqttClient.state(), _mqttFails);
            log("%s", failReason);
        }

        return connected;
    }

    void disconnect() override
    {
        if (_mqttClient.connected())
        {
            _mqttClient.disconnect();
            log("MQTT disconnected");
        }
    }

    bool isConnected() override
    {
        return _mqttClient.connected();
    }

    bool subscribe(const char *topic) override
    {
        if (!_mqttClient.connected())
        {
            if (!connect())
            {
                return false;
            }
        }

        bool success = _mqttClient.subscribe(topic);
        if (success)
        {
            log("Subscribed to topic: %s", topic);
        }
        else
        {
            log("Failed to subscribe to topic: %s", topic);
        }

        return success;
    }

    bool unsubscribe(const char *topic) override
    {
        if (!_mqttClient.connected())
        {
            return false;
        }

        bool success = _mqttClient.unsubscribe(topic);
        if (success)
        {
            log("Unsubscribed from topic: %s", topic);
        }

        return success;
    }

    bool publish(const char *topic, const char *payload) override
    {
        if (!_mqttClient.connected())
        {
            if (!connect())
            {
                return false;
            }
        }

        bool success = _mqttClient.publish(topic, payload);

        if (!success)
        {
            log("Failed to publish to topic: %s", topic);
        }

        return success;
    }

    void loop() override
    {
        if (_mqttClient.connected())
        {
            _mqttClient.loop();
        }
        else
        {
            reconnect();
        }
    }

    void setMessageProcessor(IMessageProcessor *processor) override
    {
        _messageProcessor = processor;
        log("Message processor set");

        // Setup topic handlers if factory is available
        if (_topicHandlerFactory != nullptr)
        {
            setupMqttTopicHandlers();
        }
    }

    // Add a method to set the topic handler factory
    void setTopicHandlerFactory(TopicHandlerFactory *factory)
    {
        _topicHandlerFactory = factory;
        log("Topic handler factory set");

        // Setup topic handlers if message processor is available
        if (_messageProcessor != nullptr)
        {
            setupMqttTopicHandlers();
        }
    }

    // Implementation of MQTT failures counter methods
    int getMqttFails() const override
    {
        return _mqttFails;
    }

    void resetMqttFails() override
    {
        _mqttFails = 0;
        log("MQTT failures counter reset");
    }
};

// Initialize the static instance pointer
MqttManager *MqttManager::_instance = nullptr;

#endif // MQTT_MANAGER_H
