#ifndef I_MQTT_MANAGER_H
#define I_MQTT_MANAGER_H

#include "IMessageProcessor.h"

class IMqttManager
{
public:
    virtual ~IMqttManager() = default;

    // Connect to MQTT broker
    virtual bool connect() = 0;

    // Disconnect from MQTT broker
    virtual void disconnect() = 0;

    // Check if connected to MQTT broker
    virtual bool isConnected() = 0;

    // Subscribe to a topic
    virtual bool subscribe(const char *topic) = 0;

    // Unsubscribe from a topic
    virtual bool unsubscribe(const char *topic) = 0;

    // Publish a message to a topic
    virtual bool publish(const char *topic, const char *payload) = 0;

    // Process MQTT messages and maintain connection
    virtual void loop() = 0;

    // Set message processor
    virtual void setMessageProcessor(IMessageProcessor *processor) = 0;

    // Setup MQTT topic handlers
    virtual void setupMqttTopicHandlers() = 0;
    
    // Get number of MQTT connection failures
    virtual int getMqttFails() const = 0;
    
    // Reset MQTT failures counter
    virtual void resetMqttFails() = 0;

    // Get MQTT client ID
    virtual const char* getClientId() const = 0;

    // Set MQTT client ID
    virtual void setClientId(const char* clientId) = 0;
};

#endif // I_MQTT_MANAGER_H