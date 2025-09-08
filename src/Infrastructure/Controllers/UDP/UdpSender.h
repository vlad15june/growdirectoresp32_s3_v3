#ifndef UDP_SENDER_H
#define UDP_SENDER_H

#include <AsyncUDP.h>
#include <WiFi.h>
#include "Core/Domain/Interfaces/Controllers/IUdpSender.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class UdpSender : public IUdpSender {
private:
    AsyncUDP udp;
    bool initialized;
    
    // Dependencies
    ILogger* logger;
    IDebugSystem* debugSystem;
    
    void log(const char* format, ...);

public:
    UdpSender(ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr);
    ~UdpSender() override;
    
    // Initialize sender
    bool initialize();
    
    // IUdpSender interface
    bool sendBroadcast(const char* data, uint16_t port) override;
    bool sendBroadcast(const char* data, size_t length, uint16_t port) override;
    bool sendUnicast(const char* data, const char* targetIP, uint16_t port) override;
    bool sendUnicast(const char* data, size_t length, const char* targetIP, uint16_t port) override;
    bool sendMulticast(const char* data, const char* multicastIP, uint16_t port) override;
    bool sendMulticast(const char* data, size_t length, const char* multicastIP, uint16_t port) override;
    
    bool isInitialized() const override;
};

#endif // UDP_SENDER_H