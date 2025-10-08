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
#include "Application/Services/Devices/IDeviceService.h"
#include "Application/Services/Rules/IRuleService.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/BoardTesting/IBoardTest.h"
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"

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
    IDeviceService *deviceService;
    IRuleService *ruleService;
    ISensorRepository *sensorRepository;
    ISensorTypeRepository *sensorTypeRepository;
    ISensorReader *sensorReader;
    ISpiffsAdapter *spiffsAdapter;
    IJsonConverter *jsonConverter;
    IBoardTest *boardTest;
    IUdpManager *udpManager;

    void log(const char *format, ...);

    bool initializeModuleConfiguration();
    bool setupLeds();
    void handleModuleMode();
    void ApTestModeModeConfigure();
    void UserCanConfigModeConfigure();
    void UserReConfigModeConfigure();
    void ConfigureDoneModeConfigure();
    void clearAllFileLog();
    void clearStoreRules();
    void setupAP(bool isTestMode = false);
    bool validateWifiAdapter();
    const char *determineWifiPrefix();
    char *generateSSID(const char *wifiPrefix, const char *macAddress);
    bool createAndConfigureAP(const char *ssid, const char *password, const char *localIP = "192.168.4.1", const char *gatewayIP = "192.168.4.1", const char *netmask = "255.255.255.0");
    void setupWebServer();
    void setupTestModeWebServer();
    void setupMqttConnection();
    void setupWifiConnection();
    void setupUdp();
    void fillModuleStructure();

    bool validateModuleStructurePrerequisites();
    void fillModuleCommonData();
    void createBaseNodeStructure(Node &node);
    void fillSensorModuleStructure(Node &node);
    void fillHydroModuleStructure(Node &node);
    void fillDeviceModuleStructure(Node &node);
    void fillAnalogInputDeviceStructure(Node &node);

    void scanEmptyPinsForSensors(Node &node, const std::vector<SensorEntity> &existingSensors);
    bool isPinOccupied(int pin, const std::vector<SensorEntity> &sensors, const std::string &protocol);
    void scanPinForSensorTypes(int pin, Node &node);

    void addMultiChannelModbusSensor(Node &node, const SensorEntity &sensorEntity);
    void addMultiChannelI2CSensor(Node &node, const SensorEntity &sensorEntity);
    void addSingleChannelSensor(Node &node, const SensorEntity &sensorEntity);
    void addSensorToStructure(Node &node, const SensorEntity &sensorEntity);
    void removeDisconnectedNewSensorPlaceholders(Node &node, const SensorEntity &sensorEntity);

    int findNextAvailableAddress(const std::map<int, std::vector<SensorEntity*>>& addressMap);

    void createPlaceholderSensors(Node &node);
    void processSensorsFromRepository(Node &node);
    void handleModbusAddressManagement(Node &node);
    void collectModbusSensorsAndAddresses(const std::vector<SensorEntity>& allSensorEntities, 
                                         std::map<int, std::vector<SensorEntity*>>& addressMap,
                                         std::vector<SensorEntity*>& sensorsNeedingNewAddress);
    void findDuplicateAddresses(const std::map<int, std::vector<SensorEntity*>>& addressMap,
                               std::vector<SensorEntity*>& sensorsNeedingNewAddress);
    void assignNewAddresses(std::vector<SensorEntity*>& sensorsNeedingNewAddress,
                           std::map<int, std::vector<SensorEntity*>>& addressMap);

    void addHydroSensors(Node &node);
    void addHydroPumps(Node &node);

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
        ModuleStructure *moduleStructure = nullptr,
        IDeviceService *deviceService = nullptr,
        IRuleService *ruleService = nullptr,
        ISensorRepository *sensorRepository = nullptr,
        ISensorTypeRepository *sensorTypeRepository = nullptr,
        ISensorReader *sensorReader = nullptr,
        ISpiffsAdapter *spiffsAdapter = nullptr,
        IJsonConverter *jsonConverter = nullptr,
        IBoardTest *boardTest = nullptr,
        IUdpManager *udpManager = nullptr);

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
    ModuleStructure *moduleStructure,
    IDeviceService *deviceService,
    IRuleService *ruleService,
    ISensorRepository *sensorRepository,
    ISensorTypeRepository *sensorTypeRepository,
    ISensorReader *sensorReader,
    ISpiffsAdapter *spiffsAdapter,
    IJsonConverter *jsonConverter,
    IBoardTest *boardTest,
    IUdpManager *udpManager) : logger(logger),
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
                                     moduleStructure(moduleStructure),
                                     deviceService(deviceService),
                                     ruleService(ruleService),
                                     sensorRepository(sensorRepository),
                                     sensorTypeRepository(sensorTypeRepository),
                                     sensorReader(sensorReader),
                                     spiffsAdapter(spiffsAdapter),
                                     jsonConverter(jsonConverter),
                                     boardTest(boardTest),
                                     udpManager(udpManager)
{
}

inline void ModuleSetupService::log(const char *format, ...)
{
    if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        logger->println(DebugFeatures::SETUP_DEBUG, buffer);
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
        settings->setConfigured(ModuleModeEnum::AP_TEST_MODE);

        log("SETUP: Updated configuration status to USER_CAN_CONFIGURE, needs restart");
    }

    buttonUseCases->checkButtonPressedWhenOn();

    moduleConfig->setRequestedModuleMode(static_cast<ModuleModeEnum>(isConfigured));
    moduleConfig->setCurrentModuleMode(static_cast<ModuleModeEnum>(isConfigured));
    log("SETUP: Requested module mode set to: %d", isConfigured);

    if (isConfigured == ModuleModeEnum::CONFIGURED_NEED_RESTART)
    {
        log("SETUP: Module is in CONFIGURED_NEED_RESTART mode");
    }
    else if (isConfigured == ModuleModeEnum::AP_TEST_MODE)
    {
        log("SETUP: Module is in AP_TEST_MODE");
        ApTestModeModeConfigure();
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

inline void ModuleSetupService::ApTestModeModeConfigure()
{
    log("SETUP: Module is in AP_TEST_MODE - configuring for board testing");
    
    // Setup AP with test mode IP configuration
    setupAP(true);
    setupTestModeWebServer();
    
    // Setup test devices for board testing
    if (boardTest) {
        log("SETUP: Setting up devices for test mode");
        boardTest->setupDevicesForTest(moduleConfig);
        log("SETUP: Test devices setup completed");
    } else {
        log("SETUP: BoardTest not available for test mode setup");
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
    setupUdp();

    fillModuleStructure();

    if (deviceService)
    {
        deviceService->init();
        log("SETUP: Device service initialized");
    }
    else
    {
        log("SETUP: Device service not available");
    }

    if (ruleService)
    {
        ruleService->init();
        log("SETUP: Rule service initialized");
    }
    else
    {
        log("SETUP: Rule service not available");
    }

    log("SETUP: Module prepared for operation");
}

inline void ModuleSetupService::clearAllFileLog()
{
}

inline void ModuleSetupService::clearStoreRules()
{
}

inline void ModuleSetupService::setupAP(bool isTestMode)
{
    if (!validateWifiAdapter())
    {
        return;
    }

    if (wifiAdapter->isAccessPointActive() == false)
    {
        const char *wifiPrefix = determineWifiPrefix();
        const char *macAddress = wifiAdapter->getMACAddress();
        log("SETUP: Using WiFi prefix: %s", wifiPrefix);
        log("SETUP: Device MAC address: %s", macAddress);

        char *ssid = generateSSID(wifiPrefix, macAddress);
        log("SETUP: WiFi Name: %s", ssid);

        const char *password = "123456789";
        
        const char *apLocalIp;
        const char *apGatewayIp;
        const char *apNetworkMask = "255.255.255.0";

        if (isTestMode)
        {
            apLocalIp = "192.168.0.1";
            apGatewayIp = "192.168.0.1";
            log("SETUP: Using test mode IP configuration");
        }
        else
        {
            apLocalIp = "192.168.4.1";
            apGatewayIp = "192.168.4.1";
            log("SETUP: Using default IP configuration");
        }

        if (createAndConfigureAP(ssid, password, apLocalIp, apGatewayIp, apNetworkMask))
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
    int randomNum = (int)(random(1000) % 1000);
    log("SETUP: Generating random part for SSID: %d", randomNum);
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

inline bool ModuleSetupService::createAndConfigureAP(const char *ssid, const char *password, const char *localIP, const char *gatewayIP, const char *netmask)
{
    wifiAdapter->createAccessPoint(ssid, password, 1, false, 4);

    const char *initialIP = wifiAdapter->getLocalIP();
    log("SETUP: Initial AP IP address: %s", initialIP);

    if (wifiAdapter->configureAccessPoint(localIP, gatewayIP, netmask) == false)
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

inline void ModuleSetupService::setupTestModeWebServer()
{
    if (!webServerLauncher)
    {
        log("SETUP: Error - Web server launcher is missing");
        return;
    }

    log("SETUP: Starting test mode web server configuration...");
    webServerLauncher->launchTestServer();
    log("SETUP: Test mode web server setup completed");

    if (wifiAdapter)
    {
        const char *ip = wifiAdapter->getLocalIP();
        log("SETUP: Test interface available at http://%s/", ip);
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

        mqttManager->setupMqttTopicHandlers();
    }
    else
    {
        log("SETUP: Failed to connect to MQTT broker");
    }
}

inline void ModuleSetupService::setupUdp()
{
    if (!udpManager)
    {
        log("SETUP: Error - UDP manager is missing");
        return;
    }

    // Default UDP port and multicast IP
    uint16_t udpPort = 1234;
    const char* multicastIP = "239.1.2.3";
    
    if (udpManager->initialize(udpPort, multicastIP))
    {
        log("SETUP: UDP initialized successfully on port %d with multicast %s", udpPort, multicastIP);
    }
    else
    {
        log("SETUP: Failed to initialize UDP on port %d", udpPort);
    }
}

inline bool ModuleSetupService::validateModuleStructurePrerequisites()
{
    if (!moduleConfig)
    {
        log("SETUP: Error - Module configuration is missing for structure fill");
        return false;
    }

    if (!moduleStructure)
    {
        log("SETUP: Error - Module structure is not available");
        return false;
    }

    ISettings *settings = moduleConfig->getSettings();
    if (!settings)
    {
        log("SETUP: Error - Settings not available for structure fill");
        return false;
    }

    return true;
}

inline void ModuleSetupService::fillModuleCommonData()
{
    char buffer[BUFSIZE];
    ISettings *settings = moduleConfig->getSettings();

    moduleStructure->growdirectorUid = moduleConfig->getModuleUid();
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
}

inline void ModuleSetupService::createBaseNodeStructure(Node &node)
{
    char buffer[BUFSIZE];
    ISettings *settings = moduleConfig->getSettings();

    std::string ipAddress = wifiAdapter ? wifiAdapter->getLocalIP() : "0.0.0.0";
    log("SETUP: IP Address: %s", ipAddress.c_str());

    settings->getSsid(buffer, BUFSIZE);
    std::string ssid = buffer;

    node.type = "director_wifi";
    node.uid = moduleConfig->getModuleUid();
    node.ip = ipAddress;
    node.ssid = ssid;
    node.firmware = moduleStructure->firmware;

    uint8_t bootTypeVal = settings->getBootType();
    BootTypeEnum bootType = static_cast<BootTypeEnum>(bootTypeVal);
    node.bootType = getBootTypeName(bootType);
    log("SETUP: Boot type: %d, name: %s", bootTypeVal, node.bootType.c_str());
    settings->setBootType(BOOT_TYPE_POWER_ON);

    node.fileSystem = spiffsAdapter ? getFileSystemStatusName(spiffsAdapter->getFileSystemStatus()) : "none";
    log("SETUP: File system status: %s", node.fileSystem.c_str());
}

inline void ModuleSetupService::addMultiChannelModbusSensor(Node &node, const SensorEntity &sensorEntity)
{
    for (const auto &config : sensorEntity.modbusRegisterConfigs)
    {
        Sensor sensor;
        sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
        sensor.type = config.sensorType.empty() ? sensorEntity.sensorType : config.channelName;
        sensor.pin = sensorEntity.pin;

        // Search for existing sensor with same UID
        bool found = false;
        for (auto &existingSensor : node.sensors)
        {
            if (existingSensor.uid == sensor.uid)
            {
                existingSensor = sensor; // Replace existing sensor
                found = true;
                log("SETUP: Updated existing multi-channel ModBus sensor %s, type: %s, channel: %d, pin: %d",
                    sensor.uid.c_str(), sensor.type.c_str(), config.channel, sensor.pin);
                break;
            }
        }

        if (!found)
        {
            node.sensors.push_back(sensor);
            log("SETUP: Added new multi-channel ModBus sensor %s, type: %s, channel: %d, pin: %d",
                sensor.uid.c_str(), sensor.type.c_str(), config.channel, sensor.pin);
        }
    }
}

inline void ModuleSetupService::addMultiChannelI2CSensor(Node &node, const SensorEntity &sensorEntity)
{
    for (const auto &config : sensorEntity.i2cRegisterConfigs)
    {
        Sensor sensor;
        sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
        sensor.type = config.sensorType.empty() ? sensorEntity.sensorType : config.channelName;
        sensor.pin = sensorEntity.pin;

        // Search for existing sensor with same UID
        bool found = false;
        for (auto &existingSensor : node.sensors)
        {
            if (existingSensor.uid == sensor.uid)
            {
                existingSensor = sensor; // Replace existing sensor
                found = true;
                log("SETUP: Updated existing multi-channel I2C sensor %s, type: %s, channel: %d, pin: %d",
                    sensor.uid.c_str(), sensor.type.c_str(), config.channel, sensor.pin);
                break;
            }
        }

        if (!found)
        {
            node.sensors.push_back(sensor);
            log("SETUP: Added new multi-channel I2C sensor %s, type: %s, channel: %d, pin: %d",
                sensor.uid.c_str(), sensor.type.c_str(), config.channel, sensor.pin);
        }
    }
}

inline void ModuleSetupService::addSingleChannelSensor(Node &node, const SensorEntity &sensorEntity)
{
    Sensor sensor;
    sensor.uid = sensorEntity.uid + ".01";
    sensor.type = sensorEntity.sensorType;
    sensor.pin = sensorEntity.pin;

    // Search for existing sensor with same UID
    bool found = false;
    for (auto &existingSensor : node.sensors)
    {
        if (existingSensor.uid == sensor.uid)
        {
            existingSensor = sensor; // Replace existing sensor
            found = true;
            log("SETUP: Updated existing single-channel sensor %s, type: %s, protocol: %s, pin: %d",
                sensor.uid.c_str(), sensor.type.c_str(), sensorEntity.protocol.c_str(), sensor.pin);
            break;
        }
    }

    if (!found)
    {
        node.sensors.push_back(sensor);
        log("SETUP: Added new single-channel sensor %s, type: %s, protocol: %s, pin: %d",
            sensor.uid.c_str(), sensor.type.c_str(), sensorEntity.protocol.c_str(), sensor.pin);
    }
}

inline void ModuleSetupService::addSensorToStructure(Node &node, const SensorEntity &sensorEntity)
{
    if (sensorEntity.isMultiChannel())
    {
        if (sensorEntity.protocol == "ModBus")
        {
            addMultiChannelModbusSensor(node, sensorEntity);
        }
        else if (sensorEntity.protocol == "I2C")
        {
            addMultiChannelI2CSensor(node, sensorEntity);
        }
        else
        {
            log("SETUP: Unsupported protocol for multi-channel sensor: %s", sensorEntity.protocol.c_str());
        }
    }
    else
    {
        addSingleChannelSensor(node, sensorEntity);
    }
}

inline void ModuleSetupService::fillSensorModuleStructure(Node &node)
{
    log("SETUP: Preparing sensor module structure");
    node.subtype = "sensor";

    if (!sensorRepository)
    {
        createPlaceholderSensors(node);
        return;
    }

    processSensorsFromRepository(node);
    handleModbusAddressManagement(node);

    log("SETUP: Sensor module structure filled with %zu sensors", node.sensors.size());
}

inline void ModuleSetupService::fillHydroModuleStructure(Node &node)
{
    log("SETUP: Preparing hydro module structure");
    node.subtype = "hydro";

    addHydroSensors(node);
    addHydroPumps(node);
}

inline void ModuleSetupService::addHydroSensors(Node &node)
{
    std::string macAddress = node.uid;
    int sensorPinCount = moduleConfig->getSensorPinCount();

    if (sensorTypeRepository && sensorReader)
    {
        log("SETUP: Scanning for hydro-specific analog sensors");

        // Get all sensor types for hydro module
        auto allSensorTypes = sensorTypeRepository->getAll();
        std::vector<SensorType> hydroSensorTypes;

        // Filter for hydro-specific analog protocols
        for (const auto &sensorType : allSensorTypes)
        {
            if (sensorType.protocol == SensorProtocol::ANALOG_BNC_PROTOCOL ||
                sensorType.protocol == SensorProtocol::ANALOG_EC2CH_PROTOCOL ||
                sensorType.protocol == SensorProtocol::ANALOG_PH_PROTOCOL)
            {
                hydroSensorTypes.push_back(sensorType);
                log("SETUP: Found hydro sensor type: %s, protocol: %s",
                    sensorType.name.c_str(), sensorType.getProtocolString().c_str());
            }
        }

        std::string moduleUid = moduleConfig->getModuleUid();
        std::vector<bool> pinUsed(sensorPinCount + 1, false);

        for (const auto &sensorType : hydroSensorTypes)
        {
            if (!sensorType.analogPinConfig.empty())
            {
                for (const auto &analogConfig : sensorType.analogPinConfig)
                {
                    int pin = analogConfig.pin;

                    // Validate pin is within range and not already used
                    if (pin >= 1 && pin <= sensorPinCount && !pinUsed[pin])
                    {
                        SensorEntity tempSensor;
                        tempSensor.sensorType = analogConfig.sensorType.empty() ? sensorType.name : analogConfig.sensorType;
                        tempSensor.model = sensorType.name;
                        tempSensor.protocol = sensorType.getProtocolString();
                        tempSensor.isActive = true;
                        tempSensor.pin = pin;
                        char pinStr[3];
                        sprintf(pinStr, "%02d", pin);
                        tempSensor.uid = moduleUid + "." + pinStr;
                        tempSensor.status = SensorStatusEnum::SENSOR_STATUS_NEW;
                        tempSensor.multiplier = analogConfig.multiplier;
                        tempSensor.offset = analogConfig.offset;

                        if (sensorReader->scanSensor(tempSensor))
                        {
                            log("SETUP: Detected %s sensor on pin %d (from AnalogPinConfig) with multiplier: %.3f, offset: %.3f",
                                tempSensor.sensorType.c_str(), pin, tempSensor.multiplier, tempSensor.offset);

                            addSensorToStructure(node, tempSensor);
                            pinUsed[pin] = true;

                            if (sensorRepository)
                            {
                                if (sensorRepository->createOrUpdateSensor(tempSensor))
                                {
                                    log("SETUP: Successfully saved detected hydro sensor %s to repository", tempSensor.uid.c_str());
                                }
                                else
                                {
                                    log("SETUP: Failed to save detected hydro sensor %s to repository", tempSensor.uid.c_str());
                                }
                            }
                        }
                        else
                        {
                            log("SETUP: No response from %s sensor on pin %d (from AnalogPinConfig)",
                                tempSensor.sensorType.c_str(), pin);
                        }
                    }
                    else
                    {
                        log("SETUP: Pin %d from AnalogPinConfig is out of range or already used", pin);
                    }
                }
            }
            else
            {
                log("SETUP: Sensor type %s has no AnalogPinConfig, will use default scanning", sensorType.name.c_str());
            }
        }

        // Then scan remaining pins for any undetected sensors or set to none
        for (int pin = 0; pin < sensorPinCount; pin++)
        {
            if (!pinUsed[pin])
            {
                Sensor sensor;
                sensor.pin = pin;
                sensor.type = "none";

                char sensorId[10];
                sprintf(sensorId, ".%02d", pin);
                sensor.uid = macAddress + sensorId;

                node.sensors.push_back(sensor);

                log("SETUP: Added placeholder sensor %d, type: %s, pin: %d, uid: %s",
                    pin, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
            }
        }
    }
    else
    {
        log("SETUP: SensorTypeRepository or SensorReader not available, creating placeholder sensors");

        // Fallback to creating placeholder sensors
        for (int i = 1; i <= sensorPinCount; i++)
        {
            Sensor sensor;
            sensor.pin = i;
            sensor.type = "none";

            char sensorId[10];
            sprintf(sensorId, ".%02d", i);
            sensor.uid = macAddress + sensorId;

            node.sensors.push_back(sensor);

            log("SETUP: Added placeholder sensor %d, type: %s, pin: %d, uid: %s",
                i, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
        }
    }
}

inline void ModuleSetupService::addHydroPumps(Node &node)
{
    std::string macAddress = node.uid;
    int deviceCount = moduleConfig->getDeviceCount();

    for (int i = 0; i < deviceCount; i++)
    {
        Pump pump;
        pump.pin = i + 1;

        char pumpId[10];
        sprintf(pumpId, ".pm.%02d", i);
        pump.uid = macAddress + pumpId;

        node.pumps.push_back(pump);

        log("SETUP: Added pump %d, pin: %d, uid: %s",
            i, pump.pin, pump.uid.c_str());
    }
}

inline void ModuleSetupService::fillDeviceModuleStructure(Node &node)
{
    log("SETUP: Preparing device module structure");

    int moduleSubType = moduleConfig->getModuleSubType();
    int deviceCount = moduleConfig->getDeviceCount();
    std::string macAddress = node.uid;

    if (moduleSubType == MODULE_SUBTYPE_SOKET)
    {
        node.subtype = "socket";
        log("SETUP: Socket subtype detected");

        for (int i = 0; i < deviceCount; i++)
        {
            Relay relay;
            relay.pin = i + 1;

            char relayId[10];
            sprintf(relayId, ".%02d", i);
            relay.uid = macAddress + relayId;

            node.relays.push_back(relay);

            log("SETUP: Added socket relay %d, pin: %d, uid: %s",
                i, relay.pin, relay.uid.c_str());
        }
    }
    else if (moduleSubType == MODULE_SUBTYPE_RELAY)
    {
        node.subtype = "drycontact";
        log("SETUP: Dry contact subtype detected");

        for (int i = 0; i < deviceCount; i++)
        {
            Relay relay;
            relay.pin = i + 1;

            char relayId[10];
            sprintf(relayId, ".%02d", i);
            relay.uid = macAddress + relayId;

            node.relays.push_back(relay);

            log("SETUP: Added dry contact relay %d, pin: %d, uid: %s",
                i, relay.pin, relay.uid.c_str());
        }
    }
    else
    {
        node.subtype = "unknown";
        log("SETUP: Unknown device subtype: %d", moduleSubType);
    }
}

inline void ModuleSetupService::fillAnalogInputDeviceStructure(Node &node)
{
    log("SETUP: Preparing analog input device structure");
    node.subtype = "dimmer";

    std::string macAddress = node.uid;
    int deviceCount = moduleConfig->getDeviceCount();

    for (int i = 0; i < deviceCount; i++)
    {
        Relay relay;
        relay.pin = i + 1;

        char relayId[10];
        sprintf(relayId, ".%02d", i);
        relay.uid = macAddress + relayId;

        node.relays.push_back(relay);

        log("SETUP: Added dimmer %d, pin: %d, uid: %s",
            i, relay.pin, relay.uid.c_str());
    }
}

inline void ModuleSetupService::fillModuleStructure()
{
    if (!validateModuleStructurePrerequisites())
    {
        log("SETUP: Module structure prerequisites validation failed, aborting fillModuleStructure");
        return;
    }

    log("SETUP: Filling module structure with configuration data");

    int moduleType = moduleConfig->getModuleType();
    int moduleSubType = moduleConfig->getModuleSubType();
    int deviceCount = moduleConfig->getDeviceCount();

    log("SETUP: Module Type: %d, SubType: %d, DeviceCount: %d",
        moduleType, moduleSubType, deviceCount);

    moduleStructure->nodes.clear();
    fillModuleCommonData();

    Node currentNode;
    createBaseNodeStructure(currentNode);

    switch (moduleType)
    {
    case MODULE_TYPE_SENSOR:
        fillSensorModuleStructure(currentNode);
        break;

    case MODULE_TYPE_HYDRO:
        fillHydroModuleStructure(currentNode);
        break;

    case MODULE_TYPE_DEVICE:
        fillDeviceModuleStructure(currentNode);
        break;

    case MODULE_TYPE_ANALOG_INPUT_DEVICE:
        fillAnalogInputDeviceStructure(currentNode);
        break;

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
    if (spiffsAdapter)
    {
        spiffsAdapter->initSPIFFS();
        log("SETUP: SPIFFS initialized");
    }
    else
    {
        log("SETUP: SPIFFS adapter not available");
    }

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

inline bool ModuleSetupService::isPinOccupied(int pin, const std::vector<SensorEntity> &sensors, const std::string &protocol)
{
    for (const auto &sensor : sensors)
    {
        if (sensor.protocol != protocol || sensor.status != SENSOR_STATUS_ADDED)
            continue;

        if (sensor.pin == pin)
        {
            return true;
        }
    }
    return false;
}

inline void ModuleSetupService::scanPinForSensorTypes(int pin, Node &node)
{
    if (!sensorTypeRepository || !sensorReader)
    {
        log("SETUP: Cannot scan pin %d - missing dependencies", pin);
        return;
    }

    log("SETUP: Scanning pin %d for connected sensors", pin);

    // Get all sensor types for scanning
    auto allSensorTypes = sensorTypeRepository->getAll();
    if (allSensorTypes.empty())
    {
        log("SETUP: No sensor types available for scanning");
        return;
    }

    bool isAdded = false;
    std::string moduleUid = moduleConfig->getModuleUid();
    for (const auto &sensorType : allSensorTypes)
    {
        SensorEntity tempSensor;
        tempSensor.sensorType = sensorType.name;
        tempSensor.model = sensorType.name;
        tempSensor.protocol = sensorType.getProtocolString();
        tempSensor.isActive = true;
        tempSensor.pin = pin; //NOTE: pin + 1 ?????? and after add to structure pin should be -1 ???? // OR add new property visualPin
        char pinStr[3];
        sprintf(pinStr, "%02d", pin);
        tempSensor.uid = moduleUid + "." + pinStr;
        tempSensor.status = SensorStatusEnum::SENSOR_STATUS_NEW;

        if (sensorType.isI2CSensor())
        {
            tempSensor.i2cAddress = sensorType.i2cAddress;
            tempSensor.chipType = sensorType.chipType;
            tempSensor.i2cRegisterConfigs.clear();
            for (const auto &regConfig : sensorType.i2cRegisterConfig)
            {
                tempSensor.i2cRegisterConfigs.push_back(regConfig);
            }
        }
        else if (sensorType.isModbusSensor())
        {
            tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
            tempSensor.baudRate = sensorType.baudRate;
            tempSensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
        }

        if (sensorReader->scanSensor(tempSensor))
        {
            //TODO: set new address ModBus sensors
            log("SETUP: Detected %s sensor on pin %d", sensorType.name.c_str(), pin);

            isAdded = true;

            addSensorToStructure(node, tempSensor);

            if (sensorRepository)
            {
                if (sensorRepository->createOrUpdateSensor(tempSensor))
                {
                    log("SETUP: Successfully saved detected sensor %s to repository with NEW status", tempSensor.uid.c_str());
                }
                else
                {
                    log("SETUP: Failed to save detected sensor %s to repository", tempSensor.uid.c_str());
                }
            }
            else
            {
                log("SETUP: Sensor repository not available, cannot save detected sensor");
            }

            break;
        }
    }

    if (isAdded == false)
    {
        for (int i = 0; i < 3; i++)
        {
            Sensor sensor;
            sensor.pin = pin;
            sensor.type = "none";

            char sensorId[20];
            sprintf(sensorId, ".%02d.%02d", pin, i + 1);
            sensor.uid = node.uid + sensorId;

            node.sensors.push_back(sensor);

            log("SETUP: Added placeholder sensor group %d index %d, type: %s, pin: %d, uid: %s",
                pin, i, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
        }
    }
}

inline void ModuleSetupService::scanEmptyPinsForSensors(Node &node, const std::vector<SensorEntity> &existingSensors)
{
    if (!sensorTypeRepository || !sensorReader)
    {
        log("SETUP: Cannot scan for sensors - missing sensor type repository or reader");
        return;
    }

    int sensorPinCount = moduleConfig->getSensorPinCount();
    log("SETUP: Scanning %d pins for undetected sensors", sensorPinCount);

    // Check each available pin
    for (int pin = 0; pin < sensorPinCount; pin++)
    {
        bool pinOccupied = false;

        // Check if pin is occupied by analog sensors
        if (isPinOccupied(pin, existingSensors, "Analog"))
        {
            pinOccupied = true;
        }

        // Check if pin is occupied by I2C sensors (less common to use pin numbers)
        if (!pinOccupied && isPinOccupied(pin, existingSensors, "I2C"))
        {
            pinOccupied = true;
        }

        // Check if pin is occupied by ModBus sensors
        if (!pinOccupied && isPinOccupied(pin, existingSensors, "ModBus"))
        {
            pinOccupied = true;
        }

        if (!pinOccupied)
        {
            log("SETUP: Pin %d is empty, scanning for sensors", pin);
            scanPinForSensorTypes(pin, node);
        }
        else
        {
            log("SETUP: Pin %d is occupied by existing sensor", pin);
        }
    }
}

inline void ModuleSetupService::removeDisconnectedNewSensorPlaceholders(Node &node, const SensorEntity &sensorEntity)
{
    if (sensorEntity.status == SENSOR_STATUS_NEW && sensorRepository)
    {
        log("SETUP: Sensor %s was auto-detected but is now disconnected, removing from repository", sensorEntity.uid.c_str());
        if (sensorRepository->remove(sensorEntity.uid.c_str()))
        {
            log("SETUP: Successfully removed disconnected sensor %s from repository", sensorEntity.uid.c_str());
        }
        else
        {
            log("SETUP: Failed to remove disconnected sensor %s from repository", sensorEntity.uid.c_str());
        }
    }

    if (sensorEntity.isMultiChannel())
    {
        if (sensorEntity.protocol == "ModBus")
        {
            for (const auto &config : sensorEntity.modbusRegisterConfigs)
            {
                Sensor sensor;
                sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
                sensor.type = "none";
                sensor.pin = sensorEntity.pin;

                node.sensors.push_back(sensor);

                log("SETUP: Added offline ModBus sensor placeholder %s, type: none, channel: %d, pin: %d",
                    sensor.uid.c_str(), config.channel, sensor.pin);
            }
        }
        else if (sensorEntity.protocol == "I2C")
        {
            for (const auto &config : sensorEntity.i2cRegisterConfigs)
            {
                Sensor sensor;
                sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
                sensor.type = "none";
                sensor.pin = sensorEntity.pin;

                node.sensors.push_back(sensor);

                log("SETUP: Added offline I2C sensor placeholder %s, type: none, channel: %d, pin: %d",
                    sensor.uid.c_str(), config.channel, sensor.pin);
            }
        }
        else
        {
            log("SETUP: Unsupported protocol for offline multi-channel sensor: %s", sensorEntity.protocol.c_str());
        }
    }
    else
    {
        Sensor sensor;
        sensor.uid = sensorEntity.uid + ".01";
        sensor.type = "none";
        sensor.pin = sensorEntity.pin;

        node.sensors.push_back(sensor);

        log("SETUP: Added offline single-channel sensor placeholder %s, type: none, protocol: %s, pin: %d",
            sensor.uid.c_str(), sensorEntity.protocol.c_str(), sensor.pin);
    }
}

inline int ModuleSetupService::findNextAvailableAddress(const std::map<int, std::vector<SensorEntity*>>& addressMap)
{
    // Search for next available address in range 0x70-0x78
    for (int address = 0x70; address <= 0x78; address++)
    {
        if (addressMap.find(address) == addressMap.end() || addressMap.at(address).empty())
        {
            log("SETUP: Found available address: 0x%02X", address);
            return address;
        }
    }
    
    log("SETUP: No available addresses found in range 0x70-0x78");
    return -1; // No available address found
}

inline void ModuleSetupService::createPlaceholderSensors(Node &node)
{
    log("SETUP: Sensor repository not available, creating placeholder sensors");
    
    // TODO: only for sensor for hydro without additional sensors
    // TODO: read connecter sensors
    // TODO: use setting to understand if sensor is connected, if yes use repository to get sensors with all properties
    // TODO: try connect to sensor and figureout does sensor online or not

    std::string nodeUid = node.uid;
    int sensorPinCount = moduleConfig->getSensorPinCount();

    for (int sensorPin = 0; sensorPin < sensorPinCount; sensorPin++)
    {
        for (int i = 0; i < 3; i++) // NOTE: to support old sensors with 3 channels
        {
            Sensor sensor;
            sensor.pin = sensorPin;
            sensor.type = "none";

            char sensorId[20];
            sprintf(sensorId, ".%02d.%02d", sensorPin, i + 1);
            sensor.uid = nodeUid + sensorId;

            node.sensors.push_back(sensor);

            log("SETUP: Added placeholder sensor group %d index %d, type: %s, pin: %d, uid: %s",
                sensorPin, i, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
        }
    }
}

inline void ModuleSetupService::processSensorsFromRepository(Node &node)
{
    // Read actual sensors from repository
    auto sensorEntities = sensorRepository->getAll();
    log("SETUP: Found %zu sensors in repository", sensorEntities.size());

    for (const auto &sensorEntity : sensorEntities)
    {
        if (jsonConverter)
        {
            std::string sensorJson = jsonConverter->sensorToJson(sensorEntity);
            log("SETUP: Processing sensor entity: %s", sensorJson.c_str());
        }
        else
        {
            log("SETUP: Processing sensor entity - UID: %s, Type: %s, Protocol: %s, Status: %s",
                sensorEntity.uid.c_str(),
                sensorEntity.sensorType.c_str(),
                sensorEntity.protocol.c_str(),
                getSensorStatusName(sensorEntity.status));
        }

        // Check if sensor is online, if yes then add to structure, if not then mark it as type none
        if (sensorReader && sensorReader->scanSensor(sensorEntity))
        {
            log("SETUP: Sensor %s is online, adding to structure", sensorEntity.uid.c_str());
            addSensorToStructure(node, sensorEntity);
        }
        else
        {
            log("SETUP: Sensor %s is offline", sensorEntity.uid.c_str());

            if (sensorEntity.status == SENSOR_STATUS_ADDED)
            {
                log("SETUP: Sensor %s was previously added but is now offline, updating status to ERROR", sensorEntity.uid.c_str());
                // Add the sensor to structure with its original type (not as placeholder)
                addSensorToStructure(node, sensorEntity);
            }
            else
            {
                log("SETUP: Sensor %s status is %s, adding placeholder with type 'none'",
                    sensorEntity.uid.c_str(), getSensorStatusName(sensorEntity.status));
                removeDisconnectedNewSensorPlaceholders(node, sensorEntity);
            }
        }
    }

    // Scan empty pins for additional sensors
    scanEmptyPinsForSensors(node, sensorEntities);
}

inline void ModuleSetupService::handleModbusAddressManagement(Node &node)
{
    // Address validation and duplicate handling for Modbus sensors
    if (!sensorRepository || !sensorReader)
    {
        log("SETUP: Skipping Modbus address management - missing dependencies");
        return;
    }

    log("SETUP: Starting address validation and duplicate handling for Modbus sensors");
    
    // Get all sensors again to include newly detected ones
    auto allSensorEntities = sensorRepository->getAll();
    
    // Collect all Modbus sensors and their addresses
    std::map<int, std::vector<SensorEntity*>> addressMap;
    std::vector<SensorEntity*> sensorsNeedingNewAddress;
    
    collectModbusSensorsAndAddresses(allSensorEntities, addressMap, sensorsNeedingNewAddress);
    findDuplicateAddresses(addressMap, sensorsNeedingNewAddress);
    assignNewAddresses(sensorsNeedingNewAddress, addressMap);
}

inline void ModuleSetupService::collectModbusSensorsAndAddresses(const std::vector<SensorEntity>& allSensorEntities, 
                                                                std::map<int, std::vector<SensorEntity*>>& addressMap,
                                                                std::vector<SensorEntity*>& sensorsNeedingNewAddress)
{
    for (auto& sensorEntity : const_cast<std::vector<SensorEntity>&>(allSensorEntities))
    {
        if (sensorEntity.protocol == "ModBus" && sensorEntity.status == SENSOR_STATUS_ADDED)
        {
            int address = sensorEntity.modbusDeviceAddress;
            
            // Check if address is outside valid range (0x70-0x78)
            if (address < 0x70 || address > 0x78)
            {
                log("SETUP: Sensor %s has address 0x%02X outside valid range 0x70-0x78, needs new address", 
                    sensorEntity.uid.c_str(), address);
                sensorsNeedingNewAddress.push_back(&sensorEntity);
            }
            else
            {
                addressMap[address].push_back(&sensorEntity);
            }
        }
    }
}

inline void ModuleSetupService::findDuplicateAddresses(const std::map<int, std::vector<SensorEntity*>>& addressMap,
                                                      std::vector<SensorEntity*>& sensorsNeedingNewAddress)
{
    // Find duplicates and mark for address change (keep first, change others)
    for (const auto& pair : addressMap)
    {
        int address = pair.first;
        const auto& sensors = pair.second;
        
        if (sensors.size() > 1)
        {
            log("SETUP: Found %zu sensors with duplicate address 0x%02X", sensors.size(), address);
            
            // Keep the first sensor, mark others for address change
            for (size_t i = 1; i < sensors.size(); i++)
            {
                sensorsNeedingNewAddress.push_back(sensors[i]);
                log("SETUP: Sensor %s marked for address change due to duplicate address 0x%02X", 
                    sensors[i]->uid.c_str(), address);
            }
        }
    }
}

inline void ModuleSetupService::assignNewAddresses(std::vector<SensorEntity*>& sensorsNeedingNewAddress,
                                                   std::map<int, std::vector<SensorEntity*>>& addressMap)
{
    // Assign new addresses to sensors that need them
    if (sensorsNeedingNewAddress.empty())
    {
        log("SETUP: No sensors need address changes");
        return;
    }

    log("SETUP: Assigning new addresses to %zu sensors", sensorsNeedingNewAddress.size());
    
    for (SensorEntity* sensorEntity : sensorsNeedingNewAddress)
    {
        // Find next available address in range 0x70-0x78
        int newAddress = findNextAvailableAddress(addressMap);
        
        if (newAddress != -1)
        {
            log("SETUP: Attempting to set new address 0x%02X for sensor %s (current: 0x%02X)", 
                newAddress, sensorEntity->uid.c_str(), sensorEntity->modbusDeviceAddress);
            
            // Write new address to sensor
            if (sensorReader->writeSensorAddress(*sensorEntity, newAddress))
            {
                int oldAddress = sensorEntity->modbusDeviceAddress;
                sensorEntity->modbusDeviceAddress = newAddress;
                
                // Update sensor in repository
                if (sensorRepository->createOrUpdateSensor(*sensorEntity))
                {
                    log("SETUP: Successfully changed sensor %s address from 0x%02X to 0x%02X", 
                        sensorEntity->uid.c_str(), oldAddress, newAddress);
                    
                    // Update address map for next iterations
                    addressMap[newAddress].push_back(sensorEntity);
                }
                else
                {
                    log("SETUP: Failed to update sensor %s in repository after address change", 
                        sensorEntity->uid.c_str());
                }
            }
            else
            {
                log("SETUP: Failed to write new address 0x%02X to sensor %s", 
                    newAddress, sensorEntity->uid.c_str());
            }
        }
        else
        {
            log("SETUP: No available addresses in range 0x70-0x78 for sensor %s", 
                sensorEntity->uid.c_str());
        }
    }
}

#endif // MODULE_SETUP_SERVICE_H
