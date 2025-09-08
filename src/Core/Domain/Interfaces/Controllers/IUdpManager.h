#ifndef I_UDP_MANAGER_H
#define I_UDP_MANAGER_H

#include "IUdpController.h"
#include "IUdpSender.h"

class IUdpManager {
public:
    virtual ~IUdpManager() = default;
    
    // Initialization
    virtual bool initialize(uint16_t localPort, const char* multicastIP = "239.1.2.3") = 0;
    virtual void shutdown() = 0;
    
    // Component access
    virtual IUdpController* getController() = 0;
    virtual IUdpSender* getSender() = 0;
    
    // Configuration
    virtual void setLocalPort(uint16_t port) = 0;
    virtual uint16_t getLocalPort() const = 0;
    virtual void setMulticastIP(const char* multicastIP) = 0;
    virtual const char* getMulticastIP() const = 0;
    
    // Status
    virtual bool isInitialized() const = 0;
};

#endif // I_UDP_MANAGER_H