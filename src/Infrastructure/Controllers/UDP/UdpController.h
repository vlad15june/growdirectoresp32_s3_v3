#ifndef UDP_CONTROLLER_H
#define UDP_CONTROLLER_H

#include <AsyncUDP.h>
#include <cstdarg>
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
    UdpPacketData convertPacket(AsyncUDPPacket& packet) {
        // Convert IP addresses to strings
        strncpy(ipBuffer, packet.remoteIP().toString().c_str(), sizeof(ipBuffer) - 1);
        ipBuffer[sizeof(ipBuffer) - 1] = '\0';
        
        UdpPacketData data;
        data.data = (const char*)packet.data();
        data.length = packet.length();
        data.remoteIP = ipBuffer;
        data.remotePort = packet.remotePort();
        data.localIP = packet.localIP().toString().c_str();
        data.localPort = packet.localPort();
        data.isBroadcast = packet.isBroadcast();
        data.isMulticast = packet.isMulticast();
        
        return data;
    }
    
    // Internal packet handler
    void onPacketReceived(AsyncUDPPacket packet) {
        if (!packetCallback) {
            return;
        }
        
        // Log packet details if debug is enabled
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_RECEIVE_DEBUG)) {
            String stringData = String((char*)packet.data(), packet.length());
            
            log("UDP_RECEIVE: ------- Packet Received -------");
            log("UDP_RECEIVE: Packet Type: %s", 
                packet.isBroadcast() ? "Broadcast" : (packet.isMulticast() ? "Multicast" : "Unicast"));
            log("UDP_RECEIVE: From: %s:%d", packet.remoteIP().toString().c_str(), packet.remotePort());
            log("UDP_RECEIVE: To: %s:%d", packet.localIP().toString().c_str(), packet.localPort());
            log("UDP_RECEIVE: Length: %d", packet.length());
            log("UDP_RECEIVE: Data: %s", stringData.c_str());
        }
        
        // Convert and call callback
        UdpPacketData data = convertPacket(packet);
        packetCallback(data);
    }
    
    void log(const char* format, ...) {
        if (logger && debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_RECEIVE_DEBUG)) {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(DebugFeatures::UDP_RECEIVE_DEBUG, buffer);
        }
    }

public:
    UdpController(ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr)
        : listeningPort(0), listening(false), logger(loggerInstance), debugSystem(debugSystemInstance) {
        memset(ipBuffer, 0, sizeof(ipBuffer));
    }

    ~UdpController() override {
        stopListening();
    }
    
    // IUdpController interface
    bool startListening(uint16_t port) override {
        if (listening) {
            stopListening();
        }
        
        byte tries = 0;
        while (!udp.listen(port) && tries < 30) {
            delay(100);
            tries++;
        }
        
        if (tries >= 30) {
            log("UDP_RECEIVE: Failed to start listening on port %d", port);
            return false;
        }
        
        listeningPort = port;
        listening = true;
        
        // Set up packet callback
        udp.onPacket([this](AsyncUDPPacket packet) {
            this->onPacketReceived(packet);
        });
        
        log("UDP_RECEIVE: Started listening on port %d", port);
        return true;
    }
    
    bool startMulticastListening(const char* multicastIP, uint16_t port) override {
        if (!startListening(port)) {
            return false;
        }
        
        IPAddress multicastAddr;
        if (!multicastAddr.fromString(multicastIP)) {
            log("UDP_RECEIVE: Invalid multicast IP: %s", multicastIP);
            return false;
        }
        
        if (udp.listenMulticast(multicastAddr, port)) {
            this->multicastIP = multicastIP;
            log("UDP_RECEIVE: Started multicast listening on %s:%d", multicastIP, port);
            log("UDP_RECEIVE: Listening on IP: %s", WiFi.localIP().toString().c_str());
            return true;
        }
        
        log("UDP_RECEIVE: Failed to start multicast listening on %s:%d", multicastIP, port);
        return false;
    }
    
    void stopListening() override {
        if (listening) {
            udp.close();
            listening = false;
            listeningPort = 0;
            log("UDP_RECEIVE: Stopped listening");
        }
    }
    
    void setPacketCallback(UdpPacketCallback callback) override {
        packetCallback = callback;
    }
    
    void handleIncomingPackets() override {
        // AsyncUDP handles packets automatically through callbacks
        // This method can be used for additional processing if needed
    }
    
    bool isListening() const override {
        return listening;
    }
    
    uint16_t getListeningPort() const override {
        return listeningPort;
    }
};

#endif // UDP_CONTROLLER_H