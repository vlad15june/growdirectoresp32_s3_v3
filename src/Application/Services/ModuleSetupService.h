#ifndef MODULE_SETUP_SERVICE_H
#define MODULE_SETUP_SERVICE_H

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/IWifiManager.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Application/Interfaces/WebServices/IWebServerLauncher.h"
#include "Application/Interfaces/LoopServices/ILoopService.h"
#include "Application/Interfaces/LoopServices/ILoopServiceFactory.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/FileSystemStatusEnum.h"

class IModuleSetupService
{
public:
    virtual void setupModule() = 0;
    virtual std::unique_ptr<ILoopService> getLoopService() = 0;
    virtual ~IModuleSetupService() = default;
};

class ModuleSetupServiceTestHelper;
class FillModuleStructureTestHelper;

class ModuleSetupService : public IModuleSetupService
{
private:
    ILogger *logger;
    IDebugSystem *debugSystem;
    IModuleConfig *moduleConfig;
    ILedAdapter *ledAdapter;
    ILedUseCases *ledUseCases;
    IButtonUseCases *buttonUseCases;
    IWifiAdapter *wifiAdapter;
    IWifiManager *wifiManager;
    IWebServerLauncher *webServerLauncher;
    ILoopServiceFactory *loopServiceFactory;
    IMqttManager *mqttManager;
    ModuleStructure *moduleStructure;

    void log(const char *format, ...);

    bool initializeModuleConfiguration();
    bool setupLeds();
    void handleModuleMode();
    void UserCanConfigModeConfigure();
    void UserReConfigModeConfigure();
    void ConfigureDoneModeConfigure();
    void clearAllFileLog();
    void clearStoreRules();
    void setupAP();
    bool validateWifiAdapter();
    const char *determineWifiPrefix();
    char *generateSSID(const char *wifiPrefix, const char *macAddress);
    bool createAndConfigureAP(const char *ssid, const char *password);
    void setupWebServer();
    void setupMqttConnection();
    void setupWifiConnection();
    void fillModuleStructure();

public:
    friend class ModuleSetupServiceTestHelper;
    friend class FillModuleStructureTestHelper;

    ModuleSetupService(
        ILogger *logger,
        IDebugSystem *debugSystem,
        IModuleConfig *moduleConfig,
        ILedAdapter *ledAdapter = nullptr,
        ILedUseCases *ledUseCases = nullptr,
        IButtonUseCases *buttonUseCases = nullptr,
        IWifiAdapter *wifiAdapter = nullptr,
        IWifiManager *wifiManager = nullptr,
        IWebServerLauncher *webServerLauncher = nullptr,
        ILoopServiceFactory *loopServiceFactory = nullptr,
        IMqttManager *mqttManager = nullptr,
        ModuleStructure *moduleStructure = nullptr);

    void setupModule() override;
    std::unique_ptr<ILoopService> getLoopService() override;
};

// Implementation

inline ModuleSetupService::ModuleSetupService(
    ILogger *logger,
    IDebugSystem *debugSystem,
    IModuleConfig *moduleConfig,
    ILedAdapter *ledAdapter,
    ILedUseCases *ledUseCases,
    IButtonUseCases *buttonUseCases,
    IWifiAdapter *wifiAdapter,
    IWifiManager *wifiManager,
    IWebServerLauncher *webServerLauncher,
    ILoopServiceFactory *loopServiceFactory,
    IMqttManager *mqttManager,
    ModuleStructure *moduleStructure) : logger(logger),
                                        debugSystem(debugSystem),
                                        moduleConfig(moduleConfig),
                                        ledAdapter(ledAdapter),
                                        ledUseCases(ledUseCases),
                                        buttonUseCases(buttonUseCases),
                                        wifiAdapter(wifiAdapter),
                                        wifiManager(wifiManager),
                                        webServerLauncher(webServerLauncher),
                                        loopServiceFactory(loopServiceFactory),
                                        mqttManager(mqttManager),
                                        moduleStructure(moduleStructure)
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

        log("SETUP: Updated configuration status to USER_CAN_CONFIGURE, needs restart");
    }

    buttonUseCases->checkButtonPressedWhenOn();

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
        UserCanConfigModeConfigure();
        clearAllFileLog();
        clearStoreRules();
    }
    else if (isConfigured == ModuleModeEnum::USER_CAN_RE_CONFIGURE)
    {
        log("SETUP: Module is in USER_CAN_RE_CONFIGURE mode");
        UserReConfigModeConfigure();
    }
    else if (isConfigured == ModuleModeEnum::CONFIGURED_DONE)
    {
        log("SETUP: Module is in CONFIGURED_DONE mode");
        ConfigureDoneModeConfigure();
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

inline void ModuleSetupService::UserReConfigModeConfigure()
{
    setupAP();
    setupWebServer();

    log("SETUP: Module prepared for reconfiguration");
}

inline void ModuleSetupService::ConfigureDoneModeConfigure()
{
    setupWifiConnection();
    setupMqttConnection();

    fillModuleStructure();
    // udpSetup();

    log("SETUP: Module prepared for operation");
}

inline void ModuleSetupService::clearAllFileLog()
{
}

inline void ModuleSetupService::clearStoreRules()
{
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

        delete[] ssid;
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
    char randomPart[10];
    int randomNum = (int)(std::rand() % 1000);
    snprintf(randomPart, sizeof(randomPart), "_%d", randomNum);

    size_t prefixLen = strlen(wifiPrefix);
    size_t macLen = strlen(macAddress);
    size_t randomLen = strlen(randomPart);

    size_t maxMacLen = 32 - prefixLen - randomLen;

    char *ssid = new char[33];

    strcpy(ssid, wifiPrefix);

    if (macLen > maxMacLen)
    {
        strcat(ssid, macAddress + (macLen - maxMacLen));
    }
    else
    {
        strcat(ssid, macAddress);
    }

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

    const char *AP_LOCAL_IP = "192.168.0.1";
    const char *AP_GATEWAY_IP = "192.168.0.1";
    const char *AP_NETWORK_MASK = "255.255.255.0";

    if (wifiAdapter->configureAccessPoint(AP_LOCAL_IP, AP_GATEWAY_IP, AP_NETWORK_MASK) == false)
    {
        log("SETUP: AP Config Failed");
        return false;
    }

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

    if (wifiAdapter)
    {
        const char *ip = wifiAdapter->getLocalIP();
        log("SETUP: Configuration interface available at http://%s/", ip);
    }
}

inline void ModuleSetupService::setupWifiConnection()
{
    if (!wifiManager)
    {
        log("SETUP: Error - WiFi manager is missing");
        return;
    }

    if (wifiManager->setupWifiConnection())
    {
        log("SETUP: Connected to WiFi network");
    }
    else
    {
        log("SETUP: Failed to connect to WiFi network");
    }
}

inline void ModuleSetupService::setupMqttConnection()
{
    if (!mqttManager)
    {
        log("SETUP: Error - MQTT manager is missing");
        return;
    }

    if (mqttManager->connect())
    {
        log("SETUP: Connected to MQTT broker");
    }
    else
    {
        log("SETUP: Failed to connect to MQTT broker");
    }
}

inline void ModuleSetupService::fillModuleStructure()
{
    if (!moduleConfig)
    {
        log("SETUP: Error - Module configuration is missing for structure fill");
        return;
    }

    if (!moduleStructure)
    {
        log("SETUP: Error - Module structure is not available");
        return;
    }

    log("SETUP: Filling module structure with configuration data");

    int moduleType = moduleConfig->getModuleType();
    int moduleSubType = moduleConfig->getModuleSubType();
    int deviceCount = moduleConfig->getDeviceCount();

    log("SETUP: Module Type: %d, SubType: %d, DeviceCount: %d",
        moduleType, moduleSubType, deviceCount);

    ISettings *settings = moduleConfig->getSettings();
    if (!settings)
    {
        log("SETUP: Error - Settings not available for structure fill");
        return;
    }

    moduleStructure->nodes.clear();

    char buffer[BUFSIZE];

    std::string macAddress = wifiAdapter ? wifiAdapter->getMACAddress() : "unknown";
    std::string ipAddress = wifiAdapter ? wifiAdapter->getLocalIP() : "0.0.0.0";
    log("SETUP: MAC Address: %s, IP Address: %s", macAddress.c_str(), ipAddress.c_str());

    settings->getSsid(buffer, BUFSIZE);
    std::string ssid = buffer;

    moduleStructure->growdirectorUid = macAddress;
    moduleStructure->firmware = moduleConfig->getFirmwareVersion();
    log("SETUP: Firmware version: %s", moduleStructure->firmware.c_str());

    settings->getUser(buffer, BUFSIZE);
    moduleStructure->email = buffer;

    settings->getAccountUid(buffer, BUFSIZE);
    moduleStructure->accountUid = buffer;

    float latitude = settings->getLatitude();
    float longitude = settings->getLongitude();
    if (std::abs(latitude) > 0.001 || std::abs(longitude) > 0.001)
    {
        moduleStructure->latitude = latitude;
        moduleStructure->longitude = longitude;
    }

    Node currentNode;
    currentNode.type = "director_wifi";
    currentNode.uid = macAddress;
    currentNode.ip = ipAddress;
    currentNode.ssid = ssid;
    currentNode.firmware = moduleStructure->firmware;

    uint8_t bootTypeVal = settings->getBootType();
    BootTypeEnum bootType = static_cast<BootTypeEnum>(bootTypeVal);
    currentNode.bootType = getBootTypeName(bootType);
    log("SETUP: Boot type: %d, name: %s", bootTypeVal, currentNode.bootType.c_str());
    settings->setBootType(BOOT_TYPE_POWER_ON);

    FileSystemStatusEnum fsStatus = FILE_SYSTEM_STATUS_NONE;
    currentNode.fileSystem = getFileSystemStatusName(fsStatus);
    log("SETUP: File system status: %s", currentNode.fileSystem.c_str());

    switch (moduleType)
    {
    case MODULE_TYPE_SENSOR:
    {
        log("SETUP: Preparing sensor module structure");
        currentNode.subtype = "sensor";

        int sensorPinCount = moduleConfig->getSensorPinCount();
        for (int group = 0; group < sensorPinCount; group++)
        {
            for (int i = 0; i < 3; i++)
            {
                Sensor sensor;
                sensor.pin = sensorPinCount - group;
                sensor.type = "none";

                char sensorId[20];
                sprintf(sensorId, ".%02d.%02d", group, i + 1);
                sensor.uid = macAddress + sensorId;

                currentNode.sensors.push_back(sensor);

                log("SETUP: Added sensor group %d index %d, type: %s, pin: %d, uid: %s",
                    group, i, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
            }
        }
        break;
    }

    case MODULE_TYPE_HYDRO:
    {
        log("SETUP: Preparing hydro module structure");
        currentNode.subtype = "hydro";

        int sensorPinCount = moduleConfig->getSensorPinCount();
        for (int i = 0; i < sensorPinCount; i++)
        {
            Sensor sensor;
            sensor.pin = i + 1;
            sensor.type = "none";

            char sensorId[10];
            sprintf(sensorId, ".%02d", i);
            sensor.uid = macAddress + sensorId;

            currentNode.sensors.push_back(sensor);

            log("SETUP: Added sensor %d, type: %s, pin: %d, uid: %s",
                i, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
        }

        for (int i = 0; i < deviceCount; i++)
        {
            Pump pump;
            pump.pin = i + 1;

            char pumpId[10];
            sprintf(pumpId, ".pm.%02d", i);
            pump.uid = macAddress + pumpId;

            currentNode.pumps.push_back(pump);

            log("SETUP: Added pump %d, pin: %d, uid: %s",
                i, pump.pin, pump.uid.c_str());
        }
        break;
    }

    case MODULE_TYPE_DEVICE:
    {
        log("SETUP: Preparing device module structure");

        if (moduleSubType == MODULE_SUBTYPE_SOKET)
        {
            currentNode.subtype = "socket";
            log("SETUP: Socket subtype detected");

            for (int i = 0; i < deviceCount; i++)
            {
                Relay relay;
                relay.pin = i + 1;

                char relayId[10];
                sprintf(relayId, ".%02d", i);
                relay.uid = macAddress + relayId;

                currentNode.relays.push_back(relay);

                log("SETUP: Added socket relay %d, pin: %d, uid: %s",
                    i, relay.pin, relay.uid.c_str());
            }
        }
        else if (moduleSubType == MODULE_SUBTYPE_RELAY)
        {
            currentNode.subtype = "drycontact";
            log("SETUP: Dry contact subtype detected");

            for (int i = 0; i < deviceCount; i++)
            {
                Relay relay;
                relay.pin = i + 1;

                char relayId[10];
                sprintf(relayId, ".%02d", i);
                relay.uid = macAddress + relayId;

                currentNode.relays.push_back(relay);

                log("SETUP: Added dry contact relay %d, pin: %d, uid: %s",
                    i, relay.pin, relay.uid.c_str());
            }
        }
        else
        {
            currentNode.subtype = "unknown";
            log("SETUP: Unknown device subtype: %d", moduleSubType);
        }
        break;
    }

    case MODULE_TYPE_ANALOG_INPUT_DEVICE:
    {
        log("SETUP: Preparing analog input device structure");
        currentNode.subtype = "dimmer";

        for (int i = 0; i < deviceCount; i++)
        {
            Relay relay;
            relay.pin = i + 1;

            char relayId[10];
            sprintf(relayId, ".%02d", i);
            relay.uid = macAddress + relayId;

            currentNode.relays.push_back(relay);

            log("SETUP: Added dimmer %d, pin: %d, uid: %s",
                i, relay.pin, relay.uid.c_str());
        }
        break;
    }

    default:
        log("SETUP: Unknown module type for structure fill: %d", moduleType);
        currentNode.subtype = "unknown";
        break;
    }

    moduleStructure->nodes.push_back(currentNode);

    log("SETUP: Module structure filled successfully with %zu nodes, node type: %s, subtype: %s",
        moduleStructure->nodes.size(),
        currentNode.type.c_str(),
        currentNode.subtype.c_str());
}

inline void ModuleSetupService::setupModule()
{
    if (!initializeModuleConfiguration())
    {
        log("SETUP: Module setup failed - unable to initialize configuration");
        return;
    }

    if (!setupLeds())
    {
        log("SETUP: LED setup failed");
    }

    if (ledUseCases)
    {
        ledUseCases->ledOnLoad();
        log("SETUP: LED on-load sequence executed");
    }

    handleModuleMode();

    if (ledUseCases)
    {
        ledUseCases->ledOffLoad();
        log("SETUP: LED off-load sequence executed");
    }

    log("SETUP: Module setup completed");
}

inline std::unique_ptr<ILoopService> ModuleSetupService::getLoopService()
{
    if (!moduleConfig || !loopServiceFactory)
    {
        log("SETUP: Error - Cannot create loop service, required dependencies are missing");
        return nullptr;
    }

    ISettings *settings = moduleConfig->getSettings();
    if (!settings)
    {
        log("SETUP: Error - Settings not available for loop service creation");
        return nullptr;
    }

    ModuleModeEnum moduleMode = static_cast<ModuleModeEnum>(settings->getIfConfigured());
    log("SETUP: Creating loop service for module mode: %d", static_cast<int>(moduleMode));

    try
    {
        return loopServiceFactory->createLoopService(moduleMode);
    }
    catch (const std::exception &e)
    {
        log("SETUP: Error creating loop service: %s", e.what());
        return loopServiceFactory->createLoopService(ModuleModeEnum::USER_CAN_CONFIGURE);
    }
}

#endif // MODULE_SETUP_SERVICE_H
