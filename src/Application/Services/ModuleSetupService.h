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

// Forward declaration of the test helper class
class ModuleSetupServiceTestHelper;

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

    // New methods for setupAP refactoring
    bool validateWifiAdapter();
    const char *determineWifiPrefix();
    char *generateSSID(const char *wifiPrefix, const char *macAddress);
    bool createAndConfigureAP(const char *ssid, const char *password);

    void setupWebServer();

public:
    // Declare the test helper as a friend class so it can access private methods
    friend class ModuleSetupServiceTestHelper;

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

        // isConfigured = ModuleModeEnum::CONFIGURED_NEED_RESTART; //NOTE need create in ModuleConfig currentModuleMode

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
    if (!validateWifiAdapter())
    {
        return;
    }

    if (wifiAdapter->isAccessPointActive() == false)
    {
        const char *wifiPrefix = determineWifiPrefix();
        const char *macAddress = wifiAdapter->getMACAddress();

        char *ssid = generateSSID(wifiPrefix, macAddress);
        log("SETUP: WiFi Name: %s", ssid);

        const char *password = "123456789";
        if (createAndConfigureAP(ssid, password))
        {
            log("SETUP: WiFi Password: %s", password);
        }

        delete[] ssid; // Free the allocated memory
    }

    log("SETUP: Access Point setup completed");
}

inline bool ModuleSetupService::validateWifiAdapter()
{
    if (!wifiAdapter)
    {
        log("SETUP: Error - WiFi adapter is missing");
        return false;
    }
    return true;
}

inline const char *ModuleSetupService::determineWifiPrefix()
{
    int moduleType = moduleConfig->getModuleType();
    int moduleSubType = moduleConfig->getModuleSubType();

    const char *wifiPrefix = "GrowDirector__";

    if (moduleType == MODULE_TYPE_SENSOR)
    {
        wifiPrefix = "SensorDirector__";
    }
    else if (moduleType == MODULE_TYPE_HYDRO)
    {
        wifiPrefix = "HydroDirector__";
    }
    else if (moduleType == MODULE_TYPE_DEVICE)
    {
        wifiPrefix = "DryContactDirector__";
        if (moduleSubType == MODULE_SUBTYPE_SOKET)
        {
            wifiPrefix = "SocketDirector__";
        }
    }
    else if (moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE)
    {
        wifiPrefix = "DimmerDirector__";
    }

    return wifiPrefix;
}

inline char *ModuleSetupService::generateSSID(const char *wifiPrefix, const char *macAddress)
{
    // Generate random part
    char randomPart[10];
    int randomNum = (int)(std::rand() % 1000);
    snprintf(randomPart, sizeof(randomPart), "_%d", randomNum);

    // Calculate the total SSID
    size_t prefixLen = strlen(wifiPrefix);
    size_t macLen = strlen(macAddress);
    size_t randomLen = strlen(randomPart);

    // Ensure the SSID fits in 32 characters
    size_t maxMacLen = 32 - prefixLen - randomLen;

    // Allocate memory for the SSID
    char *ssid = new char[33]; // Max 32 chars + null terminator

    // Copy prefix to SSID
    strcpy(ssid, wifiPrefix);

    // If macAddress is too long, truncate from the start
    if (macLen > maxMacLen)
    {
        strcat(ssid, macAddress + (macLen - maxMacLen));
    }
    else
    {
        strcat(ssid, macAddress);
    }

    // Add random part
    strcat(ssid, randomPart);

    return ssid;
}

inline bool ModuleSetupService::createAndConfigureAP(const char *ssid, const char *password)
{
    wifiAdapter->createAccessPoint(ssid, password, 1, false, 4);

    // WiFi event handlers would go here if needed
    // WiFi.onEvent(WiFiStationApStart, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_START);
    // WiFi.onEvent(WiFiStationApStop, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STOP);
    // WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    // WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
    // WiFi.onEvent(WiFiStationIpAssign, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);
    // WiFi.onEvent(WiFiStationProbeRequest, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED);

    const char *initialIP = wifiAdapter->getLocalIP();
    log("SETUP: Initial AP IP address: %s", initialIP);

    // Set static IP
    const char *AP_LOCAL_IP = "192.168.0.1";
    const char *AP_GATEWAY_IP = "192.168.0.1";
    const char *AP_NETWORK_MASK = "255.255.255.0";

    if (wifiAdapter->configureAccessPoint(AP_LOCAL_IP, AP_GATEWAY_IP, AP_NETWORK_MASK) == false)
    {
        log("SETUP: AP Config Failed");
        return false;
    }

    // Show the local IP
    const char *staticIP = wifiAdapter->getLocalIP();
    log("SETUP: Final AP IP address: %s", staticIP);

    return true;
}

inline void ModuleSetupService::setupWebServer()
{
    if (!webServerLauncher)
    {
        log("SETUP: Error - Web server launcher is missing");
        return;
    }
    
    log("SETUP: Starting web server configuration...");
    webServerLauncher->launchConfigurationServer();
    log("SETUP: Web server setup completed");
    
    // Get the server URL if available
    if (wifiAdapter)
    {
        const char* ip = wifiAdapter->getLocalIP();
        log("SETUP: Configuration interface available at http://%s/", ip);
    }
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
