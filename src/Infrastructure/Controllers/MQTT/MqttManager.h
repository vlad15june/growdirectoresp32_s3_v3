#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <set>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Mqtt/IMessageProcessor.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
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
    IWifiAdapter *_wifiAdapter;
    const char *_clientId;
    const char *_username;
    const char *_password;
    bool _cleanSession;
    uint32_t _lastReconnectAttempt;
    uint16_t _reconnectInterval;
    int _mqttFails; // Track MQTT connection failures
    std::set<String> _subscribedTopics; // Track subscribed topics (auto-deduplication)

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

        log("MQTT: Attempting reconnect (interval: %dms, failures: %d)", _reconnectInterval, _mqttFails);
        
        bool connected = connect();
        if (!connected)
        {
            _mqttFails++; // Increment failures counter on reconnect failure
            log("MQTT: Reconnect failed, total failures: %d", _mqttFails);
            
            // Increase reconnect interval on repeated failures (exponential backoff)
            if (_mqttFails > 5 && _reconnectInterval < 30000)
            {
                _reconnectInterval = min(_reconnectInterval * 2, 30000);
                log("MQTT: Increased reconnect interval to %dms", _reconnectInterval);
            }
        }
        else
        {
            // Reset reconnect interval on successful connection
            if (_reconnectInterval != 5000)
            {
                _reconnectInterval = 5000;
                log("MQTT: Reset reconnect interval to 5000ms");
            }
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
        TopicHandlerFactory *topicHandlerFactory = nullptr,
        IWifiAdapter *wifiAdapter = nullptr) : _mqttClient(mqttClient),
                                                              _logger(logger),
                                                              _debugSystem(debugSystem),
                                                              _messageProcessor(messageProcessor),
                                                              _topicHandlerFactory(topicHandlerFactory),
                                                              _wifiAdapter(wifiAdapter),
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
        Serial.printf("MQTT Callback triggered for topic: %s", topic);
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
            log("MQTT already connected, skipping connect");
            return true;
        }

        _lastReconnectAttempt = millis();

        // Log detailed pre-connection information
        log("=== MQTT Connection Attempt ===");
        
        // Check WiFi status first
        if (_wifiAdapter != nullptr)
        {
            bool wifiConnected = _wifiAdapter->isConnected();
            int wifiStatus = _wifiAdapter->getStatus();
            int rssi = wifiConnected ? _wifiAdapter->getRSSI() : 0;
            const char* ip = wifiConnected ? _wifiAdapter->getLocalIP() : "N/A";
            
            log("MQTT: WiFi connected: %s", wifiConnected ? "YES" : "NO");
            log("MQTT: WiFi status code: %d", wifiStatus);
            log("MQTT: WiFi RSSI: %d dBm", rssi);
            log("MQTT: WiFi IP: %s", ip);
            
            if (!wifiConnected)
            {
                log("MQTT: ✗ Cannot connect - WiFi is not connected!");
                log("==============================");
                return false;
            }
            
            // Warn on weak signal
            if (rssi < -80)
            {
                log("MQTT: ⚠ WARNING - Weak WiFi signal (%d dBm), connection may be unstable", rssi);
            }
        }
        else
        {
            log("MQTT: WARNING - WiFi adapter not available, cannot check WiFi status");
        }
        
        log("MQTT: Client ID: %s", _clientId);
        log("MQTT: Username: %s", _username ? _username : "none");
        log("MQTT: Clean session: %s", _cleanSession ? "true" : "false");
        log("MQTT: Current state: %d", _mqttClient.state());
        log("MQTT: Buffer size: %d", _mqttClient.getBufferSize());

        bool connected = false;
        if (_username != nullptr && _password != nullptr)
        {
            log("MQTT: Connecting with credentials...");
            connected = _mqttClient.connect(_clientId, _username, _password, nullptr, 0, _cleanSession, nullptr);
        }
        else
        {
            log("MQTT: Connecting without credentials...");
            connected = _mqttClient.connect(_clientId);
        }

        if (connected)
        {
            log("MQTT: ✓ Connection successful!");
            log("MQTT: State after connect: %d", _mqttClient.state());

            // Resubscribe to topics if needed
            // Implementation depends on how you want to handle topic subscriptions
        }
        else
        {
            _mqttFails++; // Increment failures counter on connection failure
            int state = _mqttClient.state();
            
            // Detailed error logging
            log("MQTT: ✗ Connection failed!");
            log("MQTT: Error code (rc): %d", state);
            log("MQTT: Total failures: %d", _mqttFails);
            
            // Decode MQTT error codes
            switch (state)
            {
                case -4:
                    log("MQTT: Error detail: MQTT_CONNECTION_TIMEOUT");
                    log("MQTT: Possible causes:");
                    log("  - Broker unreachable (check IP/firewall)");
                    log("  - WiFi connection unstable");
                    log("  - Network latency too high");
                    break;
                case -3:
                    log("MQTT: Error detail: MQTT_CONNECTION_LOST");
                    break;
                case -2:
                    log("MQTT: Error detail: MQTT_CONNECT_FAILED");
                    log("MQTT: Possible causes:");
                    log("  - WiFi client not connected");
                    log("  - Socket creation failed");
                    break;
                case -1:
                    log("MQTT: Error detail: MQTT_DISCONNECTED");
                    break;
                case 1:
                    log("MQTT: Error detail: MQTT_CONNECT_BAD_PROTOCOL");
                    break;
                case 2:
                    log("MQTT: Error detail: MQTT_CONNECT_BAD_CLIENT_ID");
                    break;
                case 3:
                    log("MQTT: Error detail: MQTT_CONNECT_UNAVAILABLE");
                    break;
                case 4:
                    log("MQTT: Error detail: MQTT_CONNECT_BAD_CREDENTIALS");
                    break;
                case 5:
                    log("MQTT: Error detail: MQTT_CONNECT_UNAUTHORIZED");
                    break;
                default:
                    log("MQTT: Error detail: Unknown error code");
                    break;
            }
            
            log("==============================");
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

        // _mqttClient.subscribe("GrowDirectorMqttTopic/48:27:E2:E2:01:78");

        bool success = _mqttClient.subscribe(topic);
        if (success)
        {
            // Add topic to the set (automatically handles duplicates)
            _subscribedTopics.insert(String(topic));
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
            // Remove topic from the set
            _subscribedTopics.erase(String(topic));
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
        static uint32_t lastPrintTime = 0;
        static uint32_t lastHealthCheck = 0;
        static bool wasConnected = false;
        const uint32_t printInterval = 30000; // Print every 30 seconds
        const uint32_t healthCheckInterval = 5000; // Check health every 5 seconds
        
        bool isConnected = _mqttClient.connected();
        
        // Detect connection state changes
        if (isConnected && !wasConnected)
        {
            log("MQTT: ✓ Connection established");
            
            // Resubscribe to all topics after reconnection
            if (_subscribedTopics.size() > 0)
            {
                log("MQTT: Resubscribing to %d topics...", _subscribedTopics.size());
                for (const auto& topic : _subscribedTopics)
                {
                    bool success = _mqttClient.subscribe(topic.c_str());
                    if (success)
                    {
                        log("MQTT: ✓ Resubscribed to: %s", topic.c_str());
                    }
                    else
                    {
                        log("MQTT: ✗ Failed to resubscribe to: %s", topic.c_str());
                    }
                }
            }
        }
        else if (!isConnected && wasConnected)
        {
            log("MQTT: ✗ Connection lost - will attempt reconnect");
        }
        
        wasConnected = isConnected;
        
        if (isConnected)
        {
            _mqttClient.loop();
            
            // Active health check - verify connection is really alive
            if (millis() - lastHealthCheck >= healthCheckInterval)
            {
                lastHealthCheck = millis();
                
                // Check MQTT state - if not CONNECTED (0), force disconnect
                int state = _mqttClient.state();
                if (state != 0)
                {
                    log("MQTT: Health check failed - state: %d (expected 0)", state);
                    log("MQTT: Forcing disconnect to trigger clean reconnect");
                    _mqttClient.disconnect();
                    wasConnected = false;
                    return; // Will reconnect on next loop()
                }
            }
            
            // Periodically print subscribed topics
            if (millis() - lastPrintTime >= printInterval)
            {
                lastPrintTime = millis();
                
                if (_subscribedTopics.size() > 0)
                {
                    log("=== Subscribed Topics (%d) ===", _subscribedTopics.size());
                    for (const auto& topic : _subscribedTopics)
                    {
                        log("  - %s", topic.c_str());
                    }
                    log("=========================");
                }
                else
                {
                    log("No MQTT topics subscribed");
                }
            }
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

    // Add a method to set the WiFi adapter
    void setWifiAdapter(IWifiAdapter *wifiAdapter)
    {
        _wifiAdapter = wifiAdapter;
        log("WiFi adapter set for MQTT manager");
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

    // Client ID getter and setter methods
    const char* getClientId() const
    {
        return _clientId;
    }

    void setClientId(const char* clientId)
    {
        _clientId = clientId;
        log("MQTT client ID set to: %s", clientId);
    }
};

// Initialize the static instance pointer
MqttManager *MqttManager::_instance = nullptr;

#endif // MQTT_MANAGER_H
