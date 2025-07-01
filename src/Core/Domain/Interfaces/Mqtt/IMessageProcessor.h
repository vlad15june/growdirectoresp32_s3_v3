#ifndef I_MESSAGE_PROCESSOR_H
#define I_MESSAGE_PROCESSOR_H

#include "ITopicHandler.h"

class IMessageProcessor {
public:
    virtual ~IMessageProcessor() = default;
    
    virtual void processMessage(const char* topic, const uint8_t* payload, unsigned int length) = 0;
    
    virtual void addTopicHandler(ITopicHandler* handler) = 0;
    
    virtual void removeTopicHandler(ITopicHandler* handler) = 0;
};

#endif // I_MESSAGE_PROCESSOR_H
