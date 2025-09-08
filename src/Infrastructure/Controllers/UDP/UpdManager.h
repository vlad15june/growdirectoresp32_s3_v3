#ifndef UDP_MANAGER_H
#define UDP_MANAGER_H

#include <cstdarg>
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
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
    
    void log(const char* format, ...) {
        if (logger && debugSystem && 
            (debugSystem->isFeatureEnabled(DebugFeatures::UDP_RECEIVE_DEBUG) || 
             debugSystem->isFeatureEnabled(DebugFeatures::UDP_SEND_DEBUG))) {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println("UDP_MANAGER", buffer);
        }
    }

public:
    UdpManager(ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr)
        : controller(nullptr), sender(nullptr), localPort(1234), 
          multicastIP("239.1.2.3"), initialized(false),
          logger(loggerInstance), debugSystem(debugSystemInstance) {
    }

    ~UdpManager() override {
        shutdown();
    }
    
    // IUdpManager interface
    bool initialize(uint16_t localPort, const char* multicastIP = "239.1.2.3") override {
        if (initialized) {
            shutdown();
        }
        
        this->localPort = localPort;
        this->multicastIP = multicastIP;
        
        // Create controller and sender instances
        controller = new UdpController(logger, debugSystem);
        sender = new UdpSender(logger, debugSystem);
        
        // Initialize sender
        if (!sender->initialize()) {
            log("UDP_MANAGER: Failed to initialize sender");
            shutdown();
            return false;
        }
        
        // Start listening on the specified port
        if (!controller->startListening(localPort)) {
            log("UDP_MANAGER: Failed to start controller listening on port %d", localPort);
            shutdown();
            return false;
        }
        
        // Start multicast listening
        if (!controller->startMulticastListening(multicastIP, localPort)) {
            log("UDP_MANAGER: Failed to start multicast listening on %s:%d", multicastIP, localPort);
            // Continue anyway, unicast might still work
        }
        
        initialized = true;
        log("UDP_MANAGER: Initialized successfully on port %d with multicast %s", localPort, multicastIP);
        
        return true;
    }
    
    void shutdown() override {
        if (controller) {
            controller->stopListening();
            delete controller;
            controller = nullptr;
        }
        
        if (sender) {
            delete sender;
            sender = nullptr;
        }
        
        initialized = false;
        log("UDP_MANAGER: Shutdown completed");
    }
    
    IUdpController* getController() override {
        return controller;
    }
    
    IUdpSender* getSender() override {
        return sender;
    }
    
    void setLocalPort(uint16_t port) override {
        if (localPort != port) {
            localPort = port;
            
            // Reinitialize if already initialized
            if (initialized) {
                initialize(localPort, multicastIP.c_str());
            }
        }
    }
    
    uint16_t getLocalPort() const override {
        return localPort;
    }
    
    void setMulticastIP(const char* multicastIP) override {
        if (this->multicastIP != multicastIP) {
            this->multicastIP = multicastIP;
            
            // Reinitialize if already initialized
            if (initialized) {
                initialize(localPort, this->multicastIP.c_str());
            }
        }
    }
    
    const char* getMulticastIP() const override {
        return multicastIP.c_str();
    }
    
    bool isInitialized() const override {
        return initialized;
    }
};

#endif // UDP_MANAGER_H