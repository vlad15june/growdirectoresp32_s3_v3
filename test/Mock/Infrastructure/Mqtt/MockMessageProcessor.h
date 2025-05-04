#ifndef MOCK_MESSAGE_PROCESSOR_H
#define MOCK_MESSAGE_PROCESSOR_H

#include "Core/Domain/Interfaces/Mqtt/IMessageProcessor.h"
#include <vector>

class MockMessageProcessor : public IMessageProcessor {
public:
    // Track method calls
    bool processMessageCalled = false;
    int processMessageCallCount = 0;
    bool addTopicHandlerCalled = false;
    int addTopicHandlerCallCount = 0;
    bool removeTopicHandlerCalled = false;
    int removeTopicHandlerCallCount = 0;
    
    // Store last call parameters
    std::string lastTopic;
    std::vector<uint8_t> lastPayload;
    unsigned int lastPayloadLength = 0;
    ITopicHandler* lastAddedHandler = nullptr;
    ITopicHandler* lastRemovedHandler = nullptr;
    
    // Implement interface methods
    void processMessage(const char* topic, const uint8_t* payload, unsigned int length) override {
        processMessageCalled = true;
        processMessageCallCount++;
        
        if (topic) {
            lastTopic = topic;
        } else {
            lastTopic = "";
        }
        
        if (payload && length > 0) {
            lastPayload.resize(length);
            memcpy(lastPayload.data(), payload, length);
            lastPayloadLength = length;
        } else {
            lastPayload.clear();
            lastPayloadLength = 0;
        }
    }
    
    void addTopicHandler(ITopicHandler* handler) override {
        addTopicHandlerCalled = true;
        addTopicHandlerCallCount++;
        lastAddedHandler = handler;
    }
    
    void removeTopicHandler(ITopicHandler* handler) override {
        removeTopicHandlerCalled = true;
        removeTopicHandlerCallCount++;
        lastRemovedHandler = handler;
    }
    
    // Reset for test setup
    void reset() {
        processMessageCalled = false;
        processMessageCallCount = 0;
        addTopicHandlerCalled = false;
        addTopicHandlerCallCount = 0;
        removeTopicHandlerCalled = false;
        removeTopicHandlerCallCount = 0;
        lastTopic = "";
        lastPayload.clear();
        lastPayloadLength = 0;
        lastAddedHandler = nullptr;
        lastRemovedHandler = nullptr;
    }
    
    // Constructor
    MockMessageProcessor() {
        reset();
    }
};

#endif // MOCK_MESSAGE_PROCESSOR_H
