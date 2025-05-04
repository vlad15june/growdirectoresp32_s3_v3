#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "IMqttManager.h"
#include "MessageProcessor.h"

/**
 * MQTT Manager implementation
 */
class MqttManager : public IMqttManager {
private:
    PubSubClient& _mqttClient;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IMessageProcessor* _messageProcessor;
    const char* _clientId;
    const char* _username;
    const char* _password;
    bool _cleanSession;
    uint32_t _lastReconnectAttempt;
    uint16_t _reconnectInterval;
    
    // Reconnect to MQTT broker
    bool reconnect() {
        if (millis() - _lastReconnectAttempt < _reconnectInterval) {
            return false;
        }
        
        _lastReconnectAttempt = millis();
        
        _logger->log(LogLevel::Info, "Attempting MQTT connection...");
        
        bool connected = false;
        if (_username != nullptr && _password != nullptr) {
            connected = _mqttClient.connect(_clientId, _username, _password, nullptr, 0, _cleanSession, nullptr);
        } else {
            connected = _mqttClient.connect(_clientId);
        }
        
        if (connected) {
            _logger->log(LogLevel::Info, "MQTT connected");
            
            // Resubscribe to topics if needed
            // Implementation depends on how you want to handle topic subscriptions
        } else {
            String failReason = "Failed, rc=" + String(_mqttClient.state());
            _logger->log(LogLevel::Error, failReason.c_str());
        }
        
        return connected;
    }
    
    // Static callback that will be passed to PubSubClient
    static void mqttCallback(char* topic, byte* payload, unsigned int length) {
        // This is a static method, so we need to get the instance from somewhere
        // Since this is just an example implementation, we'll leave this incomplete
        // In a real implementation, you would need to store a reference to the manager
        // in a static variable or use other techniques to access the instance
        
        // Assuming we have access to the instance somehow:
        // mqttManagerInstance->_messageProcessor->processMessage(topic, payload, length);
    }
    
public:
    MqttManager(
        PubSubClient& mqttClient,
        ILogger* logger,
        IDebugSystem* debugSystem,
        const char* clientId,
        const char* username = nullptr,
        const char* password = nullptr,
        bool cleanSession = true
    ) : _mqttClient(mqttClient),
        _logger(logger),
        _debugSystem(debugSystem),
        _messageProcessor(nullptr),
        _clientId(clientId),
        _username(username),
        _password(password),
        _cleanSession(cleanSession),
        _lastReconnectAttempt(0),
        _reconnectInterval(5000) {
            
        // Set callback function
        _mqttClient.setCallback(mqttCallback);
    }
    
    ~MqttManager() override = default;
    
    bool connect() override {
        if (_mqttClient.connected()) {
            return true;
        }
        
        return reconnect();
    }
    
    void disconnect() override {
        if (_mqttClient.connected()) {
            _mqttClient.disconnect();
            _logger->log(LogLevel::Info, "MQTT disconnected");
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
            String msg = "Subscribed to topic: ";
            msg += topic;
            _logger->log(LogLevel::Info, msg.c_str());
        } else {
            String msg = "Failed to subscribe to topic: ";
            msg += topic;
            _logger->log(LogLevel::Error, msg.c_str());
        }
        
        return success;
    }
    
    bool unsubscribe(const char* topic) override {
        if (!_mqttClient.connected()) {
            return false;
        }
        
        bool success = _mqttClient.unsubscribe(topic);
        if (success) {
            String msg = "Unsubscribed from topic: ";
            msg += topic;
            _logger->log(LogLevel::Info, msg.c_str());
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
            String msg = "Failed to publish to topic: ";
            msg += topic;
            _logger->log(LogLevel::Error, msg.c_str());
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
    }
};

#endif // MQTT_MANAGER_H
