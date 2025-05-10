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
class MqttManager : public IMqttManager {
private:
    PubSubClient& _mqttClient;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IMessageProcessor* _messageProcessor;
    TopicHandlerFactory* _topicHandlerFactory;
    const char* _clientId;
    const char* _username;
    const char* _password;
    bool _cleanSession;
    uint32_t _lastReconnectAttempt;
    uint16_t _reconnectInterval;
    
    // Static instance pointer for callback access
    static MqttManager* _instance;
    
    // Logging method
    void log(const char* format, ...) {
        if (_logger != nullptr && _debugSystem != nullptr && _debugSystem->isFeatureEnabled(DebugFeatures::MQTT_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            _logger->println(buffer);
        }
    }
    
    // Setup topic handlers
    void setupMqttTopicHandlers() {
        if (_messageProcessor != nullptr && _topicHandlerFactory != nullptr) {
            log("Setting up MQTT topic handlers");
            auto handlers = _topicHandlerFactory->createHandlers();
            for (auto* handler : handlers) {
                // Add the handler to the message processor
                _messageProcessor->addTopicHandler(handler);
                
                // TODO: Check if need subscribe to the topic for this handler. Add flag to the handler
                // Subscribe to the handler's topic pattern
                const char* topicPattern = handler->getTopicPattern();
                if (subscribe(topicPattern)) {
                    log("Subscribed to topic pattern: %s", topicPattern);
                } else {
                    log("Failed to subscribe to topic pattern: %s", topicPattern);
                }
            }
            log("MQTT topic handlers setup completed");
        }
    }
    
    // Reconnect to MQTT broker
    bool reconnect() {
        // Log the last reconnect attempt time and reconnect interval
        log("Last reconnect attempt: %lu ms ago, Reconnect interval: %u ms, _lastReconnectAttempt: %lu ms", 
            millis() - _lastReconnectAttempt, _reconnectInterval, _lastReconnectAttempt);
            
        if (millis() - _lastReconnectAttempt < _reconnectInterval || _lastReconnectAttempt == 0) {
            log("Reconnect attempt too soon, waiting...");
            return false;
        }
        
        _lastReconnectAttempt = millis();
        
        log("Attempting MQTT connection...");
        
        bool connected = false;
        if (_username != nullptr && _password != nullptr) {
            connected = _mqttClient.connect(_clientId, _username, _password, nullptr, 0, _cleanSession, nullptr);
        } else {
            connected = _mqttClient.connect(_clientId);
        }
        
        if (connected) {
            log("MQTT connected");
            
            // Resubscribe to topics if needed
            // Implementation depends on how you want to handle topic subscriptions
        } else {
            char failReason[64];
            snprintf(failReason, sizeof(failReason), "Failed, rc=%d", _mqttClient.state());
            log("%s", failReason);
        }
        
        return connected;
    }
    
    // Static callback that will be passed to PubSubClient
    static void mqttCallback(char* topic, byte* payload, unsigned int length) {
        // Now we can access the instance through the static pointer
        if (_instance && _instance->_messageProcessor) {
            _instance->log("Received message on topic: %s", topic);
            _instance->_messageProcessor->processMessage(topic, payload, length);
        }
    }
    
public:
    MqttManager(
        PubSubClient& mqttClient,
        ILogger* logger,
        IDebugSystem* debugSystem,
        const char* clientId,
        const char* username = nullptr,
        const char* password = nullptr,
        bool cleanSession = true,
        IMessageProcessor* messageProcessor = nullptr,
        TopicHandlerFactory* topicHandlerFactory = nullptr
    ) : _mqttClient(mqttClient),
        _logger(logger),
        _debugSystem(debugSystem),
        _messageProcessor(messageProcessor),
        _topicHandlerFactory(topicHandlerFactory),
        _clientId(clientId),
        _username(username),
        _password(password),
        _cleanSession(cleanSession),
        _lastReconnectAttempt(0),
        _reconnectInterval(5000) {
            
        // Set this instance as the static instance
        _instance = this;
        
        // Set callback function
        _mqttClient.setCallback(mqttCallback);
        
        // Setup topic handlers if message processor and factory are provided
        if (_messageProcessor != nullptr && _topicHandlerFactory != nullptr) {
            setupMqttTopicHandlers();
        }
    }
    
    ~MqttManager() override {
        // Clear the static instance if this object is being destroyed
        if (_instance == this) {
            _instance = nullptr;
        }
    }
    
    bool connect() override {
        if (_mqttClient.connected()) {
            return true;
        }
        
        return reconnect();
    }
    
    void disconnect() override {
        if (_mqttClient.connected()) {
            _mqttClient.disconnect();
            log("MQTT disconnected");
        }
    }
    
    bool isConnected() override {
        return _mqttClient.connected();
    }
    
    bool subscribe(const char* topic) override {
        if (!_mqttClient.connected()) {
            if (!connect()) {
                return false;
            }
        }
        
        bool success = _mqttClient.subscribe(topic);
        if (success) {
            log("Subscribed to topic: %s", topic);
        } else {
            log("Failed to subscribe to topic: %s", topic);
        }
        
        return success;
    }
    
    bool unsubscribe(const char* topic) override {
        if (!_mqttClient.connected()) {
            return false;
        }
        
        bool success = _mqttClient.unsubscribe(topic);
        if (success) {
            log("Unsubscribed from topic: %s", topic);
        }
        
        return success;
    }
    
    bool publish(const char* topic, const char* payload) override {
        if (!_mqttClient.connected()) {
            if (!connect()) {
                return false;
            }
        }
        
        bool success = _mqttClient.publish(topic, payload);
        
        if (!success) {
            log("Failed to publish to topic: %s", topic);
        }
        
        return success;
    }
    
    void loop() override {
        if (_mqttClient.connected()) {
            _mqttClient.loop();
        } else {
            connect();
        }
    }
    
    void setMessageProcessor(IMessageProcessor* processor) override {
        _messageProcessor = processor;
        log("Message processor set");
        
        // Setup topic handlers if factory is available
        if (_topicHandlerFactory != nullptr) {
            setupMqttTopicHandlers();
        }
    }
    
    // Add a method to set the topic handler factory
    void setTopicHandlerFactory(TopicHandlerFactory* factory) {
        _topicHandlerFactory = factory;
        log("Topic handler factory set");
        
        // Setup topic handlers if message processor is available
        if (_messageProcessor != nullptr) {
            setupMqttTopicHandlers();
        }
    }
};

// Initialize the static instance pointer
MqttManager* MqttManager::_instance = nullptr;

#endif // MQTT_MANAGER_H
