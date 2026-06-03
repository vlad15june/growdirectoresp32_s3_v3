#ifndef MOCK_UDP_CONTROLLER_H
#define MOCK_UDP_CONTROLLER_H

#include "Core/Domain/Interfaces/Udp/IUdpController.h"
#include <string>

class MockUdpController : public IUdpController {
public:
    UdpPacketCallback callback;
    bool setPacketCallbackCalled = false;

    bool startListening(uint16_t) override { return true; }
    bool startMulticastListening(const char*, uint16_t) override { return true; }
    void stopListening() override {}

    void setPacketCallback(UdpPacketCallback cb) override {
        callback = cb;
        setPacketCallbackCalled = true;
    }

    void handleIncomingPackets() override {}
    bool isListening() const override { return true; }
    uint16_t getListeningPort() const override { return 1234; }

    void emitPacket(const std::string& payload, const char* fromIp = "192.168.1.50") {
        if (!callback) {
            return;
        }
        UdpPacketData packet;
        packet.data = payload.c_str();
        packet.length = payload.size();
        packet.remoteIP = fromIp;
        packet.remotePort = 1234;
        packet.localIP = "192.168.1.10";
        packet.localPort = 1234;
        packet.isBroadcast = false;
        packet.isMulticast = true;
        callback(packet);
    }
};

#endif // MOCK_UDP_CONTROLLER_H
