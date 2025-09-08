#ifndef UDP_MANAGER_H
#define UDP_MANAGER_H

#include "Core/Domain/Interfaces/Controllers/IUdpManager.h"
#include "UdpController.h"
#include "UdpSender.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class UdpManager : public IUdpManager {
private:
    UdpController* controller;
    UdpSender* sender;
    uint16_t localPort;
    String multicastIP;
    bool initialized;
    
    // Dependencies
    ILogger* logger;
    IDebugSystem* debugSystem;
    
    void log(const char* format, ...);

public:
    UdpManager(ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr);
    ~UdpManager() override;
    
    // IUdpManager interface
    bool initialize(uint16_t localPort, const char* multicastIP = "239.1.2.3") override;
    void shutdown() override;
    
    IUdpController* getController() override;
    IUdpSender* getSender() override;
    
    void setLocalPort(uint16_t port) override;
    uint16_t getLocalPort() const override;
    void setMulticastIP(const char* multicastIP) override;
    const char* getMulticastIP() const override;
    
    bool isInitialized() const override;
};

#endif // UDP_MANAGER_H