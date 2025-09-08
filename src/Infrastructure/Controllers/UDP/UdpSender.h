#ifndef UDP_SENDER_H
#define UDP_SENDER_H

#include <AsyncUDP.h>
#include <WiFi.h>
#include <cstdarg>
#include "Core/Domain/Interfaces/Udp/IUdpSender.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class UdpSender : public IUdpSender {
private:
    AsyncUDP udp;
    bool initialized;
    
    // Dependencies
    ILogger* logger;
    IDebugSystem* debugSystem;
    
    void log(const char* format, ...) {
        if (logger && debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_SEND_DEBUG)) {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(DebugFeatures::UDP_SEND_DEBUG, buffer);
        }
    }

public:
    UdpSender(ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr)
        : initialized(false), logger(loggerInstance), debugSystem(debugSystemInstance) {
    }

    ~UdpSender() override {
        if (initialized) {
            udp.close();
        }
    }
    
    // Initialize sender
    bool initialize() {
        if (initialized) {
            return true;
        }
        
        // AsyncUDP sender doesn't need explicit initialization like receiver
        initialized = true;
        log("UDP_SEND: Sender initialized");
        return true;
    }
    
    // IUdpSender interface
    bool sendBroadcast(const char* data, uint16_t port) override {
        return sendBroadcast(data, strlen(data), port);
    }
    
    bool sendBroadcast(const char* data, size_t length, uint16_t port) override {
        if (!initialized && !initialize()) {
            return false;
        }
        
        size_t result = udp.broadcastTo(data, port);
        bool success = (result > 0);
        
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::UDP_SEND_DEBUG)) {
            log("UDP_SEND: Broadcast to port %d, length: %d, result: %s", 
                port, length, success ? "success" : "failed");
            log("UDP_SEND: Data: %.*s", (int)length, data);
        }
        
        return success;
    }
    
    bool sendUnicast(const char* data, const char* targetIP, uint16_t port) override {
        return sendUnicast(data, strlen(data), targetIP, port);
    }
    
    bool sendUnicast(const char* data, size_t length, const char* targetIP, uint16_t port) override {
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
    
    bool sendMulticast(const char* data, const char* multicastIP, uint16_t port) override {
        return sendMulticast(data, strlen(data), multicastIP, port);
    }
    
    bool sendMulticast(const char* data, size_t length, const char* multicastIP, uint16_t port) override {
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
    
    bool isInitialized() const override {
        return initialized;
    }
};

#endif // UDP_SENDER_H