#ifndef MOCK_MQTT_MANAGER_H
#define MOCK_MQTT_MANAGER_H

#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"

class MockMqttManager : public IMqttManager {
public:
    // Track method calls
    bool connectCalled = false;
    int connectCallCount = 0;
    bool disconnectCalled = false;
    int disconnectCallCount = 0;
    bool isConnectedCalled = false;
    int isConnectedCallCount = 0;
    bool subscribeCalled = false;
    int subscribeCallCount = 0;
    bool unsubscribeCalled = false;
    int unsubscribeCallCount = 0;
    bool publishCalled = false;
    int publishCallCount = 0;
    bool loopCalled = false;
    int loopCallCount = 0;
    bool setMessageProcessorCalled = false;
    int setMessageProcessorCallCount = 0;
    
    // Store last call parameters
    std::string lastSubscribeTopic;
    std::string lastUnsubscribeTopic;
    std::string lastPublishTopic;
    std::string lastPublishPayload;
    IMessageProcessor* lastMessageProcessor = nullptr;
    
    // Mock return values
    bool connectReturnValue = true;
    bool isConnectedReturnValue = true;
    bool subscribeReturnValue = true;
    bool unsubscribeReturnValue = true;
    bool publishReturnValue = true;
    
    // Implement interface methods
    bool connect() override {
        connectCalled = true;
        connectCallCount++;
        return connectReturnValue;
    }
    
    void disconnect() override {
        disconnectCalled = true;
        disconnectCallCount++;
    }
    
    bool isConnected() override {
        isConnectedCalled = true;
        isConnectedCallCount++;
        return isConnectedReturnValue;
    }
    
    bool subscribe(const char* topic) override {
        subscribeCalled = true;
        subscribeCallCount++;
        if (topic) {
            lastSubscribeTopic = topic;
        } else {
            lastSubscribeTopic = "";
        }
        return subscribeReturnValue;
    }
    
    bool unsubscribe(const char* topic) override {
        unsubscribeCalled = true;
        unsubscribeCallCount++;
        if (topic) {
            lastUnsubscribeTopic = topic;
        } else {
            lastUnsubscribeTopic = "";
        }
        return unsubscribeReturnValue;
    }
    
    bool publish(const char* topic, const char* payload) override {
        publishCalled = true;
        publishCallCount++;
        if (topic) {
            lastPublishTopic = topic;
        } else {
            lastPublishTopic = "";
        }
        if (payload) {
            lastPublishPayload = payload;
        } else {
            lastPublishPayload = "";
        }
        return publishReturnValue;
    }
    
    void loop() override {
        loopCalled = true;
        loopCallCount++;
    }
    
    void setMessageProcessor(IMessageProcessor* processor) override {
        setMessageProcessorCalled = true;
        setMessageProcessorCallCount++;
        lastMessageProcessor = processor;
    }
    
    // Reset for test setup
    void reset() {
        connectCalled = false;
        connectCallCount = 0;
        disconnectCalled = false;
        disconnectCallCount = 0;
        isConnectedCalled = false;
        isConnectedCallCount = 0;
        subscribeCalled = false;
        subscribeCallCount = 0;
        unsubscribeCalled = false;
        unsubscribeCallCount = 0;
        publishCalled = false;
        publishCallCount = 0;
        loopCalled = false;
        loopCallCount = 0;
        setMessageProcessorCalled = false;
        setMessageProcessorCallCount = 0;
        
        lastSubscribeTopic = "";
        lastUnsubscribeTopic = "";
        lastPublishTopic = "";
        lastPublishPayload = "";
        lastMessageProcessor = nullptr;
        
        connectReturnValue = true;
        isConnectedReturnValue = true;
        subscribeReturnValue = true;
        unsubscribeReturnValue = true;
        publishReturnValue = true;
    }
    
    // Constructor
    MockMqttManager() {
        reset();
    }
};

#endif // MOCK_MQTT_MANAGER_H
