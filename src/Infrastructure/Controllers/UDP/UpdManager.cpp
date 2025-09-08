#include "UpdManager.h"
#include <cstdarg>

UdpManager::UdpManager(ILogger* loggerInstance, IDebugSystem* debugSystemInstance)
    : controller(nullptr), sender(nullptr), localPort(1234), 
      multicastIP("239.1.2.3"), initialized(false),
      logger(loggerInstance), debugSystem(debugSystemInstance) {
}

UdpManager::~UdpManager() {
    shutdown();
}

bool UdpManager::initialize(uint16_t localPort, const char* multicastIP) {
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

void UdpManager::shutdown() {
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

IUdpController* UdpManager::getController() {
    return controller;
}

IUdpSender* UdpManager::getSender() {
    return sender;
}

void UdpManager::setLocalPort(uint16_t port) {
    if (localPort != port) {
        localPort = port;
        
        // Reinitialize if already initialized
        if (initialized) {
            initialize(localPort, multicastIP.c_str());
        }
    }
}

uint16_t UdpManager::getLocalPort() const {
    return localPort;
}

void UdpManager::setMulticastIP(const char* multicastIP) {
    if (this->multicastIP != multicastIP) {
        this->multicastIP = multicastIP;
        
        // Reinitialize if already initialized
        if (initialized) {
            initialize(localPort, this->multicastIP.c_str());
        }
    }
}

const char* UdpManager::getMulticastIP() const {
    return multicastIP.c_str();
}

bool UdpManager::isInitialized() const {
    return initialized;
}

void UdpManager::log(const char* format, ...) {
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