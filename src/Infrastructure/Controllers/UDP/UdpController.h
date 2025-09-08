#ifndef UDP_CONTROLLER_H
#define UDP_CONTROLLER_H

#include <AsyncUDP.h>
#include "Core/Domain/Interfaces/Controllers/IUdpController.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class UdpController : public IUdpController {
private:
    AsyncUDP udp;
    UdpPacketCallback packetCallback;
    uint16_t listeningPort;
    bool listening;
    char ipBuffer[16];
    String multicastIP;
    
    // Dependencies
    ILogger* logger;
    IDebugSystem* debugSystem;
    
    // Convert AsyncUDPPacket to UdpPacketData
    UdpPacketData convertPacket(AsyncUDPPacket& packet);
    
    // Internal packet handler
    void onPacketReceived(AsyncUDPPacket packet);
    
    void log(const char* format, ...);

public:
    UdpController(ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr);
    ~UdpController() override;
    
    // IUdpController interface
    bool startListening(uint16_t port) override;
    bool startMulticastListening(const char* multicastIP, uint16_t port) override;
    void stopListening() override;
    
    void setPacketCallback(UdpPacketCallback callback) override;
    void handleIncomingPackets() override;
    
    bool isListening() const override;
    uint16_t getListeningPort() const override;
};

#endif // UDP_CONTROLLER_H