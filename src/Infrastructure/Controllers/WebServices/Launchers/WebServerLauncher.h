#ifndef WEB_SERVER_LAUNCHER_H
#define WEB_SERVER_LAUNCHER_H

#include <WebServer.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include "Application/Interfaces/WebServices/IWebServerLauncher.h"
#include "Infrastructure/Controllers/WebServices/ModuleConfigurationWebServer.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"

class WebServerLauncher : public IWebServerLauncher {
private:
    ILogger* logger;
    IDebugSystem* debugSystem;
    ModuleConfigurationWebServer configServer;
    
    void log(const char* format, ...) {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(DebugFeatures::SETUP_DEBUG, buffer);
        }
    }

public:
    WebServerLauncher(
        ILogger* loggerInstance,
        IDebugSystem* debugSystemInstance,
        ModuleConfigurationWebServer moduleConfigServer
    ) : 
        logger(loggerInstance),
        debugSystem(debugSystemInstance),
        configServer(moduleConfigServer) {
    }
    
    bool launchConfigurationServer() override {
        log("Launching configuration web server...");
        
        try {
            configServer.setupWebServer();

            configServer.launchServer();
            
            log("Configuration web server launched successfully");
            return true;
        } catch (const std::exception& e) {
            log("Error launching configuration server: %s", e.what());
            return false;
        }
    }
    
    bool launchTestServer() override {
        return false; // Not implemented
    }
        
};

#endif // WEB_SERVER_LAUNCHER_H
