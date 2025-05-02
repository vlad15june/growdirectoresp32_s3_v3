#ifndef MODULE_SETUP_SERVICE_H
#define MODULE_SETUP_SERVICE_H

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h" // Added IWifiAdapter include
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h" // Added IButtonUseCases include
#include "Core/Domain/Enums/ModuleModeEnum.h"                // Added ModuleModeEnum include
#include "Application/Interfaces/WebServices/IWebServerLauncher.h"

class IModuleSetupService
{
public:
    virtual void setupModule() = 0;
    virtual ~IModuleSetupService() = default;
};

class ModuleSetupService : public IModuleSetupService
{
private:
    ILogger *logger;
    IDebugSystem *debugSystem;
    IModuleConfig *moduleConfig;
    ILedAdapter *ledAdapter;
    ILedUseCases *ledUseCases;
    IButtonUseCases *buttonUseCases;
    IWifiAdapter *wifiAdapter; // Added wifiAdapter member
    IWebServerLauncher *webServerLauncher;

    void log(const char *format, ...);

    bool initializeModuleConfiguration();
    bool setupLeds();
    void handleModuleMode();
    void UserCanConfigModeConfigure();
    void clearAllFileLog();
    void clearStoreRules();
    void setupAP();
    void setupWebServer();

public:
    ModuleSetupService(
        ILogger *logger,
        IDebugSystem *debugSystem,
        IModuleConfig *moduleConfig,
        ILedAdapter *ledAdapter = nullptr,
        ILedUseCases *ledUseCases = nullptr,
        IButtonUseCases *buttonUseCases = nullptr,
        IWifiAdapter *wifiAdapter = nullptr,
        IWebServerLauncher *webServerLauncher = nullptr);

    void setupModule() override;
};

// Implementation

inline ModuleSetupService::ModuleSetupService(
    ILogger *logger,
    IDebugSystem *debugSystem,
    IModuleConfig *moduleConfig,
    ILedAdapter *ledAdapter,
    ILedUseCases *ledUseCases,
    IButtonUseCases *buttonUseCases,
    IWifiAdapter *wifiAdapter, // Added wifiAdapter parameter
    IWebServerLauncher *webServerLauncher) : logger(logger),
                                             debugSystem(debugSystem),
                                             moduleConfig(moduleConfig),
                                             ledAdapter(ledAdapter),
                                             ledUseCases(ledUseCases),
                                             buttonUseCases(buttonUseCases),
                                             wifiAdapter(wifiAdapter), // Initialize wifiAdapter
                                             webServerLauncher(webServerLauncher)
{
}

inline void ModuleSetupService::log(const char *format, ...)
{
    if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
    {
        char buffer[128];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        logger->println(buffer);
    }
}

inline bool ModuleSetupService::initializeModuleConfiguration()
{
    if (!moduleConfig)
    {
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

inline bool ModuleSetupService::setupLeds()
{
    if (!ledAdapter)
        return false;

    // Use module config values for LEDs
    uint8_t *greenLeds = moduleConfig->getGreenLeds();
    uint8_t *redLeds = moduleConfig->getRedLeds();
    uint8_t ledsCount = moduleConfig->getLedsCount();

    ledAdapter->setupModuleLeds(greenLeds, redLeds, ledsCount);

    log("SETUP: LEDs initialized for %d devices", ledsCount);
    log("SETUP: LED Arrays contents:");
    for (uint8_t i = 0; i < ledsCount; i++)
    {
        log("SETUP: LED[%d] - Green: %d, Red: %d", i, greenLeds[i], redLeds[i]);
    }

    return true;
}

inline void ModuleSetupService::handleModuleMode()
{
    if (!moduleConfig)
        return;

    ISettings *settings = moduleConfig->getSettings();
    if (!settings)
    {
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

        isConfigured = ModuleModeEnum::CONFIGURED_NEED_RESTART; //NOTE need create in ModuleConfig currentModuleMode
        
        log("SETUP: Updated configuration status to USER_CAN_CONFIGURE, needs restart");
    }

    if (isConfigured == ModuleModeEnum::CONFIGURED_NEED_RESTART)
    {
        log("SETUP: Module is in CONFIGURED_NEED_RESTART mode");
    }
    else if (isConfigured == ModuleModeEnum::AP_TEST_MODE)
    {
        log("SETUP: Module is in AP_TEST_MODE");
    }
    else if (isConfigured == ModuleModeEnum::USER_CAN_CONFIGURE)
    {
        log("SETUP: Module is in USER_CAN_CONFIGURE mode");
        buttonUseCases->checkButtonPressedWhenOn();
        UserCanConfigModeConfigure();
        clearAllFileLog();
        clearStoreRules();
    }
    else if (isConfigured == ModuleModeEnum::USER_CAN_RE_CONFIGURE)
    {
        log("SETUP: Module is in USER_CAN_RE_CONFIGURE mode");
    }
    else if (isConfigured == ModuleModeEnum::CONFIGURED_DONE)
    {
        log("SETUP: Module is in CONFIGURED_DONE mode");
    }
    else
    {
        log("SETUP: Unknown module mode");
    }
}

inline void ModuleSetupService::UserCanConfigModeConfigure()
{
    setupAP();
    setupWebServer();
}

inline void ModuleSetupService::clearAllFileLog()
{
    // Empty implementation
}

inline void ModuleSetupService::clearStoreRules()
{
    // Empty implementation
}

inline void ModuleSetupService::setupAP()
{
    // Empty implementation
}

inline void ModuleSetupService::setupWebServer()
{
    webServerLauncher->launchConfigurationServer();
    log("SETUP: Web server setup completed");
}

inline void ModuleSetupService::setupModule()
{
    // Initialize configuration
    if (!initializeModuleConfiguration())
    {
        log("SETUP: Module setup failed - unable to initialize configuration");
        return;
    }

    // Setup LEDs
    if (!setupLeds())
    {
        log("SETUP: LED setup failed");
        // Continue with setup even if LEDs fail
    }

    // Initial LED indication
    if (ledUseCases)
    {
        ledUseCases->ledOnLoad();
        log("SETUP: LED on-load sequence executed");
    }

    handleModuleMode();

    // LED off sequence
    if (ledUseCases)
    {
        ledUseCases->ledOffLoad();
        log("SETUP: LED off-load sequence executed");
    }

    log("SETUP: Module setup completed");
}

#endif // MODULE_SETUP_SERVICE_H
