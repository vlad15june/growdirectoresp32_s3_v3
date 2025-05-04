#ifndef I_TOPIC_HANDLERS_H
#define I_TOPIC_HANDLERS_H

#include <Arduino.h>
#include <vector>
#include <string>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

/**
 * Interface for MQTT Topic Handler
 */
class ITopicHandler {
public:
    virtual ~ITopicHandler() = default;
    
    // Check if this handler can handle the topic
    virtual bool canHandle(const char* topic) = 0;
    
    // Handle the incoming message
    virtual void handleMessage(const char* topic, const byte* payload, unsigned int length) = 0;
    
    // Get the topic pattern this handler is responsible for
    virtual const char* getTopicPattern() = 0;
};

#endif // I_TOPIC_HANDLERS_H