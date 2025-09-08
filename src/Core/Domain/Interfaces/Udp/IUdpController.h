#ifndef I_UDP_CONTROLLER_H
#define I_UDP_CONTROLLER_H

#include <functional>

// Forward declaration for packet data
struct UdpPacketData {
    const char* data;
    size_t length;
    const char* remoteIP;
    uint16_t remotePort;
    const char* localIP;
    uint16_t localPort;
    bool isBroadcast;
    bool isMulticast;
};

// Callback function type for packet reception
using UdpPacketCallback = std::function<void(const UdpPacketData&)>;

class IUdpController {
public:
    virtual ~IUdpController() = default;
    
    // Setup UDP listening
    virtual bool startListening(uint16_t port) = 0;
    virtual bool startMulticastListening(const char* multicastIP, uint16_t port) = 0;
    virtual void stopListening() = 0;
    
    // Packet handling
    virtual void setPacketCallback(UdpPacketCallback callback) = 0;
    virtual void handleIncomingPackets() = 0;
    
    // Status
    virtual bool isListening() const = 0;
    virtual uint16_t getListeningPort() const = 0;
};

#endif // I_UDP_CONTROLLER_H