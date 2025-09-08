#include "UdpSender.h"
#include <cstdarg>

UdpSender::UdpSender(ILogger* loggerInstance, IDebugSystem* debugSystemInstance)
    : initialized(false), logger(loggerInstance), debugSystem(debugSystemInstance) {
}

UdpSender::~UdpSender() {
    if (initialized) {
        udp.close();
    }
}

bool UdpSender::initialize() {
    if (initialized) {
        return true;
    }
    
    // AsyncUDP sender doesn't need explicit initialization like receiver
    initialized = true;
    log("UDP_SEND: Sender initialized");
    return true;
}

bool UdpSender::sendBroadcast(const char* data, uint16_t port) {
    return sendBroadcast(data, strlen(data), port);
}

bool UdpSender::sendBroadcast(const char* data, size_t length, uint16_t port) {
    if (!initialized && !initialize()) {
        return false;
    }
    
    bool result = udp.broadcastTo(data, length, port);
    
    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_SEND_DEBUG)) {
        log("UDP_SEND: Broadcast to port %d, length: %d, result: %s", 
            port, length, result ? "success" : "failed");
        log("UDP_SEND: Data: %.*s", (int)length, data);
    }
    
    return result;
}

bool UdpSender::sendUnicast(const char* data, const char* targetIP, uint16_t port) {
    return sendUnicast(data, strlen(data), targetIP, port);
}

bool UdpSender::sendUnicast(const char* data, size_t length, const char* targetIP, uint16_t port) {
    if (!initialized && !initialize()) {
        return false;
    }
    
    IPAddress targetAddr;
    if (!targetAddr.fromString(targetIP)) {
        log("UDP_SEND: Invalid target IP: %s", targetIP);
        return false;
    }
    
    bool result = udp.writeTo((const uint8_t*)data, length, targetAddr, port);
    
    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_SEND_DEBUG)) {
        log("UDP_SEND: Unicast to %s:%d, length: %d, result: %s", 
            targetIP, port, length, result ? "success" : "failed");
        log("UDP_SEND: Data: %.*s", (int)length, data);
    }
    
    return result;
}

bool UdpSender::sendMulticast(const char* data, const char* multicastIP, uint16_t port) {
    return sendMulticast(data, strlen(data), multicastIP, port);
}

bool UdpSender::sendMulticast(const char* data, size_t length, const char* multicastIP, uint16_t port) {
    if (!initialized && !initialize()) {
        return false;
    }
    
    IPAddress multicastAddr;
    if (!multicastAddr.fromString(multicastIP)) {
        log("UDP_SEND: Invalid multicast IP: %s", multicastIP);
        return false;
    }
    
    bool result = udp.writeTo((const uint8_t*)data, length, multicastAddr, port);
    
    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_SEND_DEBUG)) {
        log("UDP_SEND: Multicast to %s:%d, length: %d, result: %s", 
            multicastIP, port, length, result ? "success" : "failed");
        log("UDP_SEND: Data: %.*s", (int)length, data);
    }
    
    return result;
}

bool UdpSender::isInitialized() const {
    return initialized;
}

void UdpSender::log(const char* format, ...) {
    if (logger && debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_SEND_DEBUG)) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        logger->println(DebugFeatures::UDP_SEND_DEBUG, buffer);
    }
}