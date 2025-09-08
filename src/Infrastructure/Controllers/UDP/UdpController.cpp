#include "UdpController.h"
#include <cstdarg>

UdpController::UdpController(ILogger* loggerInstance, IDebugSystem* debugSystemInstance)
    : listeningPort(0), listening(false), logger(loggerInstance), debugSystem(debugSystemInstance) {
    memset(ipBuffer, 0, sizeof(ipBuffer));
}

UdpController::~UdpController() {
    stopListening();
}

bool UdpController::startListening(uint16_t port) {
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

bool UdpController::startMulticastListening(const char* multicastIP, uint16_t port) {
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

void UdpController::stopListening() {
    if (listening) {
        udp.close();
        listening = false;
        listeningPort = 0;
        log("UDP_RECEIVE: Stopped listening");
    }
}

void UdpController::setPacketCallback(UdpPacketCallback callback) {
    packetCallback = callback;
}

void UdpController::handleIncomingPackets() {
    // AsyncUDP handles packets automatically through callbacks
    // This method can be used for additional processing if needed
}

bool UdpController::isListening() const {
    return listening;
}

uint16_t UdpController::getListeningPort() const {
    return listeningPort;
}

UdpPacketData UdpController::convertPacket(AsyncUDPPacket& packet) {
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

void UdpController::onPacketReceived(AsyncUDPPacket packet) {
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

void UdpController::log(const char* format, ...) {
    if (logger && debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_RECEIVE_DEBUG)) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        logger->println(DebugFeatures::UDP_RECEIVE_DEBUG, buffer);
    }
}