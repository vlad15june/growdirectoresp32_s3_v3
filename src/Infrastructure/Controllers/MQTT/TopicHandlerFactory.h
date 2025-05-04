#ifndef TOPIC_HANDLER_FACTORY_H
#define TOPIC_HANDLER_FACTORY_H

#include "ITopicHandler.h"
#include "TopicHandlers.h"
#include <vector>

/**
 * Factory class to create topic handlers
 */
class TopicHandlerFactory {
private:
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    
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
        
        // Add more handlers as needed
        
        return handlers;
    }
};

#endif // TOPIC_HANDLER_FACTORY_H