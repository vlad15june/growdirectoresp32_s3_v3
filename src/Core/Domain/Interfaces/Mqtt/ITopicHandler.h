#ifndef I_TOPIC_HANDLERS_H
#define I_TOPIC_HANDLERS_H

#include <vector>
#include <cstdint>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class ITopicHandler {
public:
    virtual ~ITopicHandler() = default;
    
    virtual bool canHandle(const char* topic) = 0;
    
    virtual void handleMessage(const char* topic, const uint8_t* payload, unsigned int length) = 0;
    
    virtual const char* getTopicPattern() = 0;
};

#endif // I_TOPIC_HANDLERS_H