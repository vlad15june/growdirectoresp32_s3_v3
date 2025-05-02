#ifndef MODULE_SETUP_SERVICE_H
#define MODULE_SETUP_SERVICE_H

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"  // Added ModuleModeEnum include

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
    
    bool initializeModuleConfiguration();
    bool setupLeds(); // Modified return type from void to bool
    void handleModuleMode();

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

inline bool ModuleSetupService::initializeModuleConfiguration() {
    if (!moduleConfig) {
        log("SETUP: Error - Module configuration is missing");
        return false;
    }
    
    moduleConfig->init();
    
    log("SETUP: Module configured as Type: %d, SubType: %d, DeviceCount: %d",
        moduleConfig->getModuleType(),
        moduleConfig->getModuleSubType(),
        moduleConfig->getDeviceCount());
    
    return true;
}

inline bool ModuleSetupService::setupLeds() {
    if (!ledAdapter) return false;
    
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
    
    return true;
}

inline void ModuleSetupService::handleModuleMode() {
    if (!moduleConfig) return;
    
    ISettings* settings = moduleConfig->getSettings();
    if (!settings) {
        log("SETUP: Error - Settings not available");
        return;
    }
    
    uint8_t isConfigured = settings->getIfConfigured();
    log("SETUP: Current configuration status: %d", isConfigured);
    
    if (isConfigured != ModuleModeEnum::CONFIGURED_NEED_RESTART && 
        isConfigured != ModuleModeEnum::AP_TEST_MODE && 
        isConfigured != ModuleModeEnum::USER_CAN_CONFIGURE &&
        isConfigured != ModuleModeEnum::USER_CAN_RE_CONFIGURE && 
        isConfigured != ModuleModeEnum::CONFIGURED_DONE)
    {
        settings->setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
        isConfigured = ModuleModeEnum::CONFIGURED_NEED_RESTART;
        log("SETUP: Updated configuration status to USER_CAN_CONFIGURE, needs restart");
    }
}

inline void ModuleSetupService::setupModule() {
    // Initialize configuration
    if (!initializeModuleConfiguration()) {
        log("SETUP: Module setup failed - unable to initialize configuration");
        return;
    }
    
    // Setup LEDs
    if (!setupLeds()) {
        log("SETUP: LED setup failed");
        // Continue with setup even if LEDs fail
    }
    
    // Initial LED indication
    if (ledUseCases) {
        ledUseCases->ledOnLoad();
        log("SETUP: LED on-load sequence executed");
    }

    handleModuleMode();

    // LED off sequence
    if (ledUseCases) {
        ledUseCases->ledOffLoad();
        log("SETUP: LED off-load sequence executed");
    }
    
    log("SETUP: Module setup completed");
}

#endif // MODULE_SETUP_SERVICE_H
