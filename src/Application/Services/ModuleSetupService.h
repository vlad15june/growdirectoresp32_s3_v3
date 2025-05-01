#ifndef MODULE_SETUP_SERVICE_H
#define MODULE_SETUP_SERVICE_H

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILedAdapter.h"

class IModuleSetupService {
public:
    virtual void setupModule() = 0;
    virtual ~IModuleSetupService() = default;
};

class ModuleSetupService : public IModuleSetupService {
private:
    ILogger* logger;
    IDebugSystem* debugSystem;
    IModuleConfig* moduleConfig;
    ILedAdapter* ledAdapter;

    void log(const char* format, ...);

public:
    ModuleSetupService(
        ILogger* logger,
        IDebugSystem* debugSystem,
        IModuleConfig* moduleConfig,
        ILedAdapter* ledAdapter = nullptr
    );

    void setupModule() override;
};

// Implementation

inline ModuleSetupService::ModuleSetupService(
    ILogger* logger,
    IDebugSystem* debugSystem,
    IModuleConfig* moduleConfig,
    ILedAdapter* ledAdapter
) : logger(logger),
    debugSystem(debugSystem),
    moduleConfig(moduleConfig),
    ledAdapter(ledAdapter) {}

inline void ModuleSetupService::log(const char* format, ...) {
    if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG)) {
        char buffer[128];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        logger->println(buffer);
    }
}

inline void ModuleSetupService::setupModule() {
    if (!moduleConfig) {
        log("SETUP: Error - Module configuration is missing");
        return;
    }        

    // Initialize the module configuration (includes settings)
    moduleConfig->init();
    
    log("SETUP: Module configured as Type: %d, SubType: %d, DeviceCount: %d",
        moduleConfig->getModuleType(),
        moduleConfig->getModuleSubType(),
        moduleConfig->getDeviceCount());
    
    // Setup LEDs if LED adapter is available
    if (ledAdapter) {
        // Use module config values for LEDs
        uint8_t* greenLeds = reinterpret_cast<uint8_t*>(moduleConfig->getGreenLeds());
        uint8_t* redLeds = reinterpret_cast<uint8_t*>(moduleConfig->getRedLeds());
        uint8_t ledsCount = static_cast<uint8_t>(moduleConfig->getLedsCount());
        
        ledAdapter->setupModuleLeds(greenLeds, redLeds, ledsCount);
        log("SETUP: LEDs initialized for %d devices", ledsCount);
    }
    
    log("SETUP: Module setup completed");
}

#endif // MODULE_SETUP_SERVICE_H
