#ifndef I_UDP_INBOUND_DISPATCHER_H
#define I_UDP_INBOUND_DISPATCHER_H

#include "IUdpController.h"

class IUdpInboundDispatcher {
public:
    virtual ~IUdpInboundDispatcher() = default;

    virtual void init() = 0;
    virtual void addHandler(UdpPacketCallback handler) = 0;
};

#endif // I_UDP_INBOUND_DISPATCHER_H
