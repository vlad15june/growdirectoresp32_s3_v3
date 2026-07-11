#ifndef UDP_INBOUND_DISPATCHER_H
#define UDP_INBOUND_DISPATCHER_H

#include <vector>
#include <cstdarg>
#include <cstdio>
#include "Core/Domain/Interfaces/Udp/IUdpInboundDispatcher.h"
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class UdpInboundDispatcher : public IUdpInboundDispatcher {
private:
    IUdpManager* _udpManager;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    std::vector<UdpPacketCallback> _handlers;

    void log(const char* format, ...) {
        if (!_logger || !_debugSystem || !_debugSystem->isFeatureEnabled(DebugFeatures::MODULE_UDP_COMMAND_DEBUG)) {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::MODULE_UDP_COMMAND_DEBUG);
        if (feature) {
            _logger->println(DebugFeatures::MODULE_UDP_COMMAND_DEBUG, buffer, feature->getLogChannel());
        } else {
            _logger->println(DebugFeatures::MODULE_UDP_COMMAND_DEBUG, buffer);
        }
    }

    void dispatch(const UdpPacketData& packet) {
        log("MODULE_UDP_CMD: packet len=%zu handlers=%zu", packet.length, _handlers.size());
        for (const auto& handler : _handlers) {
            if (handler) {
                handler(packet);
            }
        }
    }

public:
    UdpInboundDispatcher(IUdpManager* udpManager, ILogger* logger, IDebugSystem* debugSystem)
        : _udpManager(udpManager), _logger(logger), _debugSystem(debugSystem) {}

    void init() override {
        if (!_udpManager) {
            log("MODULE_UDP_CMD: dispatcher udp manager missing");
            return;
        }
        IUdpController* controller = _udpManager->getController();
        if (!controller) {
            log("MODULE_UDP_CMD: dispatcher controller missing");
            return;
        }
        controller->setPacketCallback([this](const UdpPacketData& packet) {
            dispatch(packet);
        });
    }

    void addHandler(UdpPacketCallback handler) override {
        _handlers.push_back(handler);
    }
};

#endif // UDP_INBOUND_DISPATCHER_H
