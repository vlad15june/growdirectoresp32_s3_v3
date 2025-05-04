#ifndef I_MESSAGE_PROCESSOR_H
#define I_MESSAGE_PROCESSOR_H

#include <Arduino.h>

// Forward declaration
class ITopicHandler;

/**
 * Interface for MQTT Message Processor
 */
class IMessageProcessor {
public:
    virtual ~IMessageProcessor() = default;
    
    // Process an incoming MQTT message
    virtual void processMessage(const char* topic, const byte* payload, unsigned int length) = 0;
    
    // Add a topic handler
    virtual void addTopicHandler(ITopicHandler* handler) = 0;
    
    // Remove a topic handler
    virtual void removeTopicHandler(ITopicHandler* handler) = 0;
};

#endif // I_MESSAGE_PROCESSOR_H
