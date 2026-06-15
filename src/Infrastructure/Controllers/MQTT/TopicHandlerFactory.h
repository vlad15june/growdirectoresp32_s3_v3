#ifndef TOPIC_HANDLER_FACTORY_H
#define TOPIC_HANDLER_FACTORY_H

#include <vector>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Mqtt/ITopicHandler.h"
#include "Application/Interfaces/ICommandService.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "TopicHandlers.h"
#include <cstdarg>
#include <cstdio>

/**
 * Factory class to create topic handlers
 */
class TopicHandlerFactory
{
private:
    ILogger *_logger;
    IDebugSystem *_debugSystem;
    IModuleConfig *_moduleConfig;
    ICommandService* _commandService;
    IJsonConverter* _jsonConverter;

    // Logging method
    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr && _debugSystem->isFeatureEnabled(DebugFeatures::MQTT_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::MQTT_DEBUG, buffer);
        }
    }

public:
    TopicHandlerFactory(
        ILogger *logger,
        IDebugSystem *debugSystem,
        IModuleConfig *moduleConfig = nullptr,
        ICommandService* commandService = nullptr,
        IJsonConverter* jsonConverter = nullptr) : _logger(logger),
                                                   _debugSystem(debugSystem),
                                                   _moduleConfig(moduleConfig),
                                                   _commandService(commandService),
                                                   _jsonConverter(jsonConverter)
    {
    }

    // Create and return all required topic handlers
    std::vector<ITopicHandler *> createHandlers()
    {
        std::vector<ITopicHandler *> handlers;

        if (_moduleConfig != nullptr)
        {
            const char *moduleUid = _moduleConfig->getModuleUid();
            log("Creating handlers for module commands, moduleUID: %s", moduleUid);
            handlers.push_back(new CommandTopicHandler(
                _logger, 
                _debugSystem, 
                moduleUid,
                _commandService,
                _jsonConverter));
        }

        return handlers;
    }

    // Add setters for dependencies that might not be available at construction time
    void setModuleConfig(IModuleConfig *moduleConfig)
    {
        _moduleConfig = moduleConfig;
    }

    void setCommandService(ICommandService* commandService)
    {
        _commandService = commandService;
    }

    void setJsonConverter(IJsonConverter* jsonConverter)
    {
        _jsonConverter = jsonConverter;
    }

    // Getter for module config
    IModuleConfig *getModuleConfig() const
    {
        return _moduleConfig;
    }
};

#endif // TOPIC_HANDLER_FACTORY_H
