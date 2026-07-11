#ifndef REMOTE_MODULE_COMMAND_SENDER_H
#define REMOTE_MODULE_COMMAND_SENDER_H

#include <string>
#include <cstdarg>
#include <cstdio>
#include "Core/Domain/Interfaces/Udp/IRemoteModuleCommandSender.h"
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class RemoteModuleCommandSender : public IRemoteModuleCommandSender
{
private:
    IUdpManager* _udpManager;
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

public:
    RemoteModuleCommandSender(IUdpManager* udpManager, IJsonConverter* jsonConverter, ILogger* logger, IDebugSystem* debugSystem)
        : _udpManager(udpManager), _jsonConverter(jsonConverter), _logger(logger), _debugSystem(debugSystem) {}

    bool sendReboot(const std::string& targetUid) override
    {
        if (!_udpManager || !_jsonConverter)
        {
            log("MODULE_UDP_CMD: sender not available");
            return false;
        }

        IUdpSender* sender = _udpManager->getSender();
        if (!sender)
        {
            log("MODULE_UDP_CMD: udp sender missing");
            return false;
        }

        std::string payload = _jsonConverter->remoteModuleCommandToJson("Reboot", targetUid);
        const char* multicastIP = _udpManager->getMulticastIP();
        uint16_t port = _udpManager->getLocalPort();

        bool ok = sender->sendMulticast(payload.c_str(), multicastIP, port);
        if (!ok)
        {
            ok = sender->sendBroadcast(payload.c_str(), port);
        }

        log("MODULE_UDP_CMD: multicast Reboot targetUid=%s sent=%d", targetUid.c_str(), ok ? 1 : 0);
        return ok;
    }
};

#endif // REMOTE_MODULE_COMMAND_SENDER_H
