#ifndef MESSAGE_PROCESSOR_H
#define MESSAGE_PROCESSOR_H

#include <Arduino.h>
#include <vector>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "IMessageProcessor.h"
#include "ITopicHandler.h"

/**
 * MQTT Message Processor implementation
 */
class MessageProcessor : public IMessageProcessor {
private:
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    std::vector<ITopicHandler*> _topicHandlers;
    
public:
    MessageProcessor(
        ILogger* logger,
        IDebugSystem* debugSystem
    ) : _logger(logger),
        _debugSystem(debugSystem) {
    }
    
    ~MessageProcessor() override = default;
    
    void processMessage(const char* topic, const byte* payload, unsigned int length) override {
        _logger->log(LogLevel::Debug, "Processing message from topic: %s", topic);
        
        // Convert payload to a null-terminated string for logging
        char* payloadStr = new char[length + 1];
        memcpy(payloadStr, payload, length);
        payloadStr[length] = '\0';
        
        String logMsg = "Payload: ";
        logMsg += payloadStr;
        _logger->log(LogLevel::Debug, logMsg.c_str());
        
        // Find a handler for this topic
        ITopicHandler* handler = findHandlerForTopic(topic);
        
        if (handler != nullptr) {
            try {
                handler->handleMessage(topic, payload, length);
            } catch (const std::exception& e) {
                String errorMsg = "Error handling message: ";
                errorMsg += e.what();
                _logger->log(LogLevel::Error, errorMsg.c_str());
            }
        } else {
            String noHandlerMsg = "No handler found for topic: ";
            noHandlerMsg += topic;
            _logger->log(LogLevel::Warning, noHandlerMsg.c_str());
        }
        
        delete[] payloadStr;
    }
    
    void addTopicHandler(ITopicHandler* handler) override {
        if (handler != nullptr) {
            String msg = "Adding topic handler for: ";
            msg += handler->getTopicPattern();
            _logger->log(LogLevel::Info, msg.c_str());
            
            _topicHandlers.push_back(handler);
        }
    }
    
    void removeTopicHandler(ITopicHandler* handler) override {
        if (handler != nullptr) {
            auto it = std::find(_topicHandlers.begin(), _topicHandlers.end(), handler);
            if (it != _topicHandlers.end()) {
                String msg = "Removing topic handler for: ";
                msg += handler->getTopicPattern();
                _logger->log(LogLevel::Info, msg.c_str());
                
                _topicHandlers.erase(it);
            }
        }
    }
    
    // Helper method to find a handler for a specific topic
    ITopicHandler* findHandlerForTopic(const char* topic) {
        for (auto* handler : _topicHandlers) {
            if (handler->canHandle(topic)) {
                return handler;
            }
        }
        
        return nullptr;
    }
};

#endif // MESSAGE_PROCESSOR_H
