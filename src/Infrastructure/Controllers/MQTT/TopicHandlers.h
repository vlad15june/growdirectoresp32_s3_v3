#ifndef TOPIC_HANDLERS_H
#define TOPIC_HANDLERS_H

#include "Core/Domain/Interfaces/Mqtt/ITopicHandler.h"
#include <string.h>
#include <string>

/**
 * Base Topic Handler implementation
 */
class BaseTopicHandler : public ITopicHandler
{
protected:
    std::string _topicPattern;
    ILogger *_logger;
    IDebugSystem *_debugSystem;

    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr && _debugSystem->isFeatureEnabled(DebugFeatures::MQTT_COMMAND_DEBUG))
        {
            char buffer[1024];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::MQTT_DEBUG, buffer);
        }
    }

    // Helper method to check if topic matches pattern
    bool topicMatches(const char *topic, const char *pattern)
    {
        log("Checking if topic '%s' matches pattern '%s'", topic, pattern);

        // Simple wildcard matching implementation
        // This is a basic implementation - you might want to improve it for more complex cases

        // If the pattern is exactly "*", it matches everything
        if (strcmp(pattern, "*") == 0)
        {
            return true;
        }

        // Check if the pattern ends with a wildcard
        size_t patternLen = strlen(pattern);
        bool endsWithWildcard = patternLen > 0 && pattern[patternLen - 1] == '#';

        if (endsWithWildcard)
        {
            // Compare the prefix (excluding the wildcard)
            patternLen--; // Exclude the '#'
            return strncmp(topic, pattern, patternLen) == 0;
        }
        else
        {
            // Exact match required
            return strcmp(topic, pattern) == 0;
        }
    }

public:
    BaseTopicHandler(
        const char *topicPattern,
        ILogger *logger,
        IDebugSystem *debugSystem) : _topicPattern(topicPattern),
                                     _logger(logger),
                                     _debugSystem(debugSystem)
    {
    }

    ~BaseTopicHandler() override = default;

    bool canHandle(const char *topic) override
    {
        return topicMatches(topic, _topicPattern.c_str());
    }

    const char *getTopicPattern() override
    {
        return _topicPattern.c_str();
    }
};

/**
 * Example implementation for a specific topic type
 * More handlers can be implemented as needed
 */
class CommandTopicHandler : public BaseTopicHandler
{
public:
    CommandTopicHandler(
        ILogger *logger,
        IDebugSystem *debugSystem,
        const char *moduleUid) : BaseTopicHandler((std::string("GrowDirectorMqttTopic/") + moduleUid).c_str(),
                                                  logger,
                                                  debugSystem)
    {
    }

    void handleMessage(const char *topic, const uint8_t *payload, unsigned int length) override
    {
        log("Handling command message");

        // Convert payload to a null-terminated string
        char *payloadStr = new char[length + 1];
        memcpy(payloadStr, payload, length);
        payloadStr[length] = '\0';
        log("Payload: %s", payloadStr);


        delete[] payloadStr;
    }
};

#endif // TOPIC_HANDLERS_H
