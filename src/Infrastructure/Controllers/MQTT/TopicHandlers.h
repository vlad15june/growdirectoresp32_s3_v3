#ifndef TOPIC_HANDLERS_H
#define TOPIC_HANDLERS_H

#include "ITopicHandler.h"
#include <string.h>

/**
 * Base Topic Handler implementation
 */
class BaseTopicHandler : public ITopicHandler {
protected:
    std::string _topicPattern;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    
    // Logging method
    void log(const char* format, ...) {
        if (_logger != nullptr && _debugSystem != nullptr && _debugSystem->isFeatureEnabled(DebugFeatures::MQTT_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            _logger->println(buffer);
        }
    }
    
    // Helper method to check if topic matches pattern
    bool topicMatches(const char* topic, const char* pattern) {
        // Simple wildcard matching implementation
        // This is a basic implementation - you might want to improve it for more complex cases
        
        // If the pattern is exactly "*", it matches everything
        if (strcmp(pattern, "*") == 0) {
            return true;
        }
        
        // Check if the pattern ends with a wildcard
        size_t patternLen = strlen(pattern);
        bool endsWithWildcard = patternLen > 0 && pattern[patternLen - 1] == '#';
        
        if (endsWithWildcard) {
            // Compare the prefix (excluding the wildcard)
            patternLen--; // Exclude the '#'
            return strncmp(topic, pattern, patternLen) == 0;
        } else {
            // Exact match required
            return strcmp(topic, pattern) == 0;
        }
    }
    
public:
    BaseTopicHandler(
        const char* topicPattern,
        ILogger* logger,
        IDebugSystem* debugSystem
    ) : _topicPattern(topicPattern),
        _logger(logger),
        _debugSystem(debugSystem) {
    }
    
    ~BaseTopicHandler() override = default;
    
    bool canHandle(const char* topic) override {
        return topicMatches(topic, _topicPattern.c_str());
    }
    
    const char* getTopicPattern() override {
        return _topicPattern.c_str();
    }
};

/**
 * Example implementation for a specific topic type
 * More handlers can be implemented as needed
 */
class CommandTopicHandler : public BaseTopicHandler {
public:
    CommandTopicHandler(
        ILogger* logger,
        IDebugSystem* debugSystem
    ) : BaseTopicHandler("command/#", logger, debugSystem) {
    }
    
    void handleMessage(const char* topic, const byte* payload, unsigned int length) override {
        log("Handling command message");
        
        // Convert payload to a null-terminated string
        char* payloadStr = new char[length + 1];
        memcpy(payloadStr, payload, length);
        payloadStr[length] = '\0';
        
        // Extract command type from topic (assuming format like "command/[commandType]")
        const char* commandType = strchr(topic, '/');
        if (commandType != nullptr) {
            commandType++; // Skip the '/'
            
            log("Command type: %s, Payload: %s", commandType, payloadStr);
            
            // Process different command types
            if (strcmp(commandType, "restart") == 0) {
                log("Processing restart command");
                // Implement restart logic
            } else if (strcmp(commandType, "set_config") == 0) {
                log("Processing set_config command");
                // Implement configuration update logic
            } else {
                log("Unknown command type: %s", commandType);
            }
        } else {
            log("Invalid command topic format");
        }
        
        delete[] payloadStr;
    }
};

#endif // TOPIC_HANDLERS_H
