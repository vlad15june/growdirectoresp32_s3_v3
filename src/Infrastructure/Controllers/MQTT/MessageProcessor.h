#ifndef MESSAGE_PROCESSOR_H
#define MESSAGE_PROCESSOR_H

#include <Arduino.h>
#include <vector>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Mqtt/IMessageProcessor.h"
#include "Core/Domain/Interfaces/Mqtt/ITopicHandler.h"

/**
 * MQTT Message Processor implementation
 */
class MessageProcessor : public IMessageProcessor
{
private:
    ILogger *_logger;
    IDebugSystem *_debugSystem;
    std::vector<ITopicHandler *> _topicHandlers;

    // Logging method
    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr && _debugSystem->isFeatureEnabled(DebugFeatures::MQTT_DEBUG))
        {
            char buffer[1024];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::MQTT_DEBUG, buffer);
        }
    }

public:
    MessageProcessor(
        ILogger *logger,
        IDebugSystem *debugSystem) : _logger(logger),
                                     _debugSystem(debugSystem)
    {
    }

    ~MessageProcessor() override = default;

    void processMessage(const char *topic, const uint8_t *payload, unsigned int length) override
    {
        // Convert payload to a null-terminated string for logging
        uint8_t *payloadStr = (uint8_t *)malloc(length + 1);
        memcpy(payloadStr, payload, length);
        payloadStr[length] = '\0';

        // Copy topic to new memory for safe handling
        char *topicStr = (char *)malloc(strlen(topic) + 1);
        strcpy(topicStr, topic);

        log("Processing message from topic: %s, length=%d", topicStr, length);
        log("Payload: %s", payloadStr);

        // Find a handler for this topic
        ITopicHandler *handler = findHandlerForTopic(topicStr);

        if (handler != nullptr)
        {
            try
            {
                handler->handleMessage(topicStr, payloadStr, length);
            }
            catch (const std::exception &e)
            {
                log("Error handling message: %s", e.what());
            }
        }
        else
        {
            log("No handler found for topic: %s", topicStr);
        }

        free(payloadStr);
    }

    void addTopicHandler(ITopicHandler *handler) override
    {
        if (handler != nullptr)
        {
            log("Adding topic handler for: %s", handler->getTopicPattern());
            _topicHandlers.push_back(handler); // TODO: check for duplicates
        }
    }

    void removeTopicHandler(ITopicHandler *handler) override
    {
        if (handler != nullptr)
        {
            auto it = std::find(_topicHandlers.begin(), _topicHandlers.end(), handler);
            if (it != _topicHandlers.end())
            {
                log("Removing topic handler for: %s", handler->getTopicPattern());
                _topicHandlers.erase(it);
            }
        }
    }

    // Helper method to find a handler for a specific topic
    ITopicHandler *findHandlerForTopic(const char *topic)
    {
        for (auto *handler : _topicHandlers)
        {
            if (handler->canHandle(topic))
            {
                return handler;
            }
        }

        return nullptr;
    }
};

#endif // MESSAGE_PROCESSOR_H
