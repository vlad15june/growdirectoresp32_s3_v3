#ifndef TOPIC_HANDLERS_H
#define TOPIC_HANDLERS_H

#include "Core/Domain/Interfaces/Mqtt/ITopicHandler.h"
#include "Application/Interfaces/ICommandService.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Application/DTOs/CommandDto.h"
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

            IDebugFeature* debugFeature = _debugSystem->getFeature(DebugFeatures::MQTT_COMMAND_DEBUG);
            if (debugFeature)
            {
                _logger->println(DebugFeatures::MQTT_COMMAND_DEBUG, buffer, debugFeature->getLogChannel());
            }
            else
            {
                _logger->println(DebugFeatures::MQTT_COMMAND_DEBUG, buffer);
            }
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
private:
    ICommandService* commandService;
    IJsonConverter* jsonConverter;
    std::string _moduleUid;

public:
    CommandTopicHandler(
        ILogger *logger,
        IDebugSystem *debugSystem,
        const char *moduleUid,
        ICommandService* commandService,
        IJsonConverter* jsonConverter) : BaseTopicHandler((std::string("GrowDirectorMqttTopic/") + moduleUid).c_str(),
                                                          logger,
                                                          debugSystem),
                                        commandService(commandService),
                                        jsonConverter(jsonConverter),
                                        _moduleUid(moduleUid)
    {
    }

    void handleMessage(const char *topic, const uint8_t *payload, unsigned int length) override
    {
        log("CommandTopicHandler: Handling command message on topic: %s", topic);

        // Convert payload to a null-terminated string
        char *payloadStr = new char[length + 1];
        memcpy(payloadStr, payload, length);
        payloadStr[length] = '\0';
        
        log("CommandTopicHandler: Payload: %s", payloadStr);

        // Convert JSON payload to CommandDto
        CommandDto commandDto;
        std::string jsonString(payloadStr);
        
        if (jsonConverter && jsonConverter->commandFromJson(jsonString, commandDto)) {
            log("CommandTopicHandler: Successfully parsed command - type: %s", commandDto.type.c_str());

            if (!commandDto.growDirectorUid.empty() && commandDto.growDirectorUid != _moduleUid) {
                log("CommandTopicHandler: growDirectorUid mismatch, ignoring command");
                delete[] payloadStr;
                return;
            }

            // Process the command using the command service
            if (commandService) {
                bool success = commandService->process(commandDto);
                if (success) {
                    log("CommandTopicHandler: Command processed successfully");
                } else {
                    log("CommandTopicHandler: Failed to process command");
                }
            } else {
                log("CommandTopicHandler: Command service not available");
            }
        } else {
            log("CommandTopicHandler: Failed to parse JSON payload to CommandDto");
        }

        delete[] payloadStr;
    }
};

#endif // TOPIC_HANDLERS_H
