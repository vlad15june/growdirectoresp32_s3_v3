#ifndef MODULE_SETUP_SERVICE_H
#define MODULE_SETUP_SERVICE_H

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILedAdapter.h"
#include "Core/Domain/Interfaces/ILedUseCases.h"

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
    ILedUseCases* ledUseCases;

    void log(const char* format, ...);

public:
    ModuleSetupService(
        ILogger* logger,
        IDebugSystem* debugSystem,
        IModuleConfig* moduleConfig,
        ILedAdapter* ledAdapter = nullptr,
        ILedUseCases* ledUseCases = nullptr
    );

    void setupModule() override;
};

// Implementation

inline ModuleSetupService::ModuleSetupService(
    ILogger* logger,
    IDebugSystem* debugSystem,
    IModuleConfig* moduleConfig,
    ILedAdapter* ledAdapter,
    ILedUseCases* ledUseCases
) : logger(logger),
    debugSystem(debugSystem),
    moduleConfig(moduleConfig),
    ledAdapter(ledAdapter),
    ledUseCases(ledUseCases) {}

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
        uint8_t* greenLeds = moduleConfig->getGreenLeds();
        uint8_t* redLeds = moduleConfig->getRedLeds();
        uint8_t ledsCount = moduleConfig->getLedsCount();
        
        ledAdapter->setupModuleLeds(greenLeds, redLeds, ledsCount);

        log("SETUP: LEDs initialized for %d devices", ledsCount);
        log("SETUP: LED Arrays contents:");
        for (uint8_t i = 0; i < ledsCount; i++) {
            log("SETUP: LED[%d] - Green: %d, Red: %d", i, greenLeds[i], redLeds[i]);
        } 
    }
    
    // Use LED use cases for initial LED indication if available
    if (ledUseCases) {
        ledUseCases->ledOnLoad();
        log("SETUP: LED on-load sequence executed");
    }

    if (ledUseCases) {
        ledUseCases->ledOffLoad();
        log("SETUP: LED off-load sequence executed");
    }
    
    log("SETUP: Module setup completed");
}

#endif // MODULE_SETUP_SERVICE_H
