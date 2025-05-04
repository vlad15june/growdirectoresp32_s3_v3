#ifndef TOPIC_HANDLER_FACTORY_H
#define TOPIC_HANDLER_FACTORY_H

#include <vector>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Mqtt/ITopicHandler.h"
#include "TopicHandlers.h"

/**
 * Factory class to create topic handlers
 */
class TopicHandlerFactory {
private:
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
    
public:
    TopicHandlerFactory(
        ILogger* logger,
        IDebugSystem* debugSystem
    ) : _logger(logger),
       _debugSystem(debugSystem) {
    }
    
    // Create and return all required topic handlers
    std::vector<ITopicHandler*> createHandlers() {
        std::vector<ITopicHandler*> handlers;
        
        // Add command topic handler
        handlers.push_back(new CommandTopicHandler(_logger, _debugSystem));
        log("Created CommandTopicHandler");
        
        // Add more handlers as needed
        
        return handlers;
    }
};

#endif // TOPIC_HANDLER_FACTORY_H