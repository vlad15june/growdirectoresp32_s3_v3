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
};

#endif // I_MQTT_MANAGER_H