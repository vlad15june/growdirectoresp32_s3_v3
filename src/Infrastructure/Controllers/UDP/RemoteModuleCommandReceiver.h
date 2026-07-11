#ifndef REMOTE_MODULE_COMMAND_RECEIVER_H
#define REMOTE_MODULE_COMMAND_RECEIVER_H

#include <string>
#include <cstdarg>
#include <cstdio>
#include "Core/Domain/Interfaces/Adapters/IRemoteModuleCommandReceiver.h"
#include "Core/Domain/Interfaces/Udp/IUdpInboundDispatcher.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"

class RemoteModuleCommandReceiver : public IRemoteModuleCommandReceiver
{
private:
    IUdpInboundDispatcher* _dispatcher;
    IModuleConfig* _moduleConfig;
    ISettings* _settings;
    IJsonConverter* _jsonConverter;
    ILogger* _logger;
    IDebugSystem* _debugSystem;

    void log(const char* format, ...)
    {
        if (!_logger || !_debugSystem || !_debugSystem->isFeatureEnabled(DebugFeatures::MODULE_UDP_COMMAND_DEBUG))
        {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::MODULE_UDP_COMMAND_DEBUG);
        if (feature)
        {
            _logger->println(DebugFeatures::MODULE_UDP_COMMAND_DEBUG, buffer, feature->getLogChannel());
        }
        else
        {
            _logger->println(DebugFeatures::MODULE_UDP_COMMAND_DEBUG, buffer);
        }
    }

    void onUdpPacket(const UdpPacketData& packet)
    {
        std::string payload(packet.data, packet.length);

        std::string command;
        std::string targetUid;
        if (!_jsonConverter || !_jsonConverter->remoteModuleCommandFromJson(payload, command, targetUid))
        {
            log("MODULE_UDP_CMD: drop (not a module command)");
            return;
        }

        std::string selfUid = _moduleConfig ? std::string(_moduleConfig->getModuleUid()) : "";
        if (targetUid != selfUid)
        {
            log("MODULE_UDP_CMD: ignored targetUid=%s self=%s", targetUid.c_str(), selfUid.c_str());
            return;
        }

        if (command == "Reboot")
        {
            if (_settings)
            {
                _settings->setNoScanSensors(1);
                _settings->setBootType(BOOT_TYPE_TECHNICAL);
            }
            if (_moduleConfig)
            {
                _moduleConfig->setRequestedModuleMode(ModuleModeEnum::CONFIGURED_NEED_RESTART);
            }
            log("MODULE_UDP_CMD: reboot accepted targetUid=%s -> technical restart", targetUid.c_str());
        }
        else
        {
            log("MODULE_UDP_CMD: drop (unknown command=%s)", command.c_str());
        }
    }

public:
    RemoteModuleCommandReceiver(
        IUdpInboundDispatcher* dispatcher,
        IModuleConfig* moduleConfig,
        ISettings* settings,
        IJsonConverter* jsonConverter,
        ILogger* logger,
        IDebugSystem* debugSystem)
        : _dispatcher(dispatcher),
          _moduleConfig(moduleConfig),
          _settings(settings),
          _jsonConverter(jsonConverter),
          _logger(logger),
          _debugSystem(debugSystem) {}

    void init() override
    {
        if (_dispatcher)
        {
            _dispatcher->addHandler([this](const UdpPacketData& packet) {
                onUdpPacket(packet);
            });
        }
    }
};

#endif // REMOTE_MODULE_COMMAND_RECEIVER_H
