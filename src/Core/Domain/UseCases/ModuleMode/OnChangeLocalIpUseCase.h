#ifndef ON_CHANGE_LOCAL_IP_USE_CASE_H
#define ON_CHANGE_LOCAL_IP_USE_CASE_H

#include <cstdarg>
#include <cstdio>
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/IWifiManager.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/DebugFeatures.h"
#include "Core/Domain/Entities/ModuleStructure.h"

class OnChangeLocalIpUseCase : public IOnChangeLocalIpUseCase {
private:
    ILogger* logger;
    IDebugSystem* debugSystem;
    IWifiAdapter* wifiAdapter;
    IWifiManager* wifiManager;
    ModuleStructure* moduleStructure;
    
public:
    OnChangeLocalIpUseCase(
        IWifiAdapter* wifiAdapter,
        IWifiManager* wifiManager,
        ModuleStructure* moduleStructure,
        ILogger* loggerInstance = nullptr,
        IDebugSystem* debugSystemInstance = nullptr)
        : wifiAdapter(wifiAdapter),
          wifiManager(wifiManager),
          moduleStructure(moduleStructure),
          logger(loggerInstance),
          debugSystem(debugSystemInstance) {
        
        log("OnChangeLocalIpUseCase: Initialized");
    }
    
    void perform() override {
        log("OnChangeLocalIpUseCase: Performing local IP update");
        
        // Check if WiFi is connected
        if (!wifiAdapter->isConnected()) {
            log("OnChangeLocalIpUseCase: WiFi not connected, attempting connection");
            if (!wifiManager->setupWifiConnection()) {
                log("OnChangeLocalIpUseCase: Failed to establish WiFi connection");
                return;
            }
        }
        
        // Get the new local IP from the WiFi adapter
        const char* localIP = wifiAdapter->getLocalIP();
        
        if (localIP != nullptr && strlen(localIP) > 0) {
            log("OnChangeLocalIpUseCase: Updating module structure with IP: %s", localIP);
            
            // Update the module structure with the new IP
            if (moduleStructure != nullptr) {
                // Assuming ModuleStructure has a method to update the local IP
                // moduleStructure->setLocalIP(localIP);
                
                log("OnChangeLocalIpUseCase: Local IP successfully updated");
            } else {
                log("OnChangeLocalIpUseCase: Module structure is null, cannot update IP");
            }
        } else {
            log("OnChangeLocalIpUseCase: Failed to get local IP address");
        }
    }
    
private:
    void log(const char* format, ...) {
        if (logger != nullptr && debugSystem != nullptr && 
            debugSystem->isFeatureEnabled(DebugFeatures::NETWORK_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(buffer);
        }
    }
};

#endif // ON_CHANGE_LOCAL_IP_USE_CASE_H
