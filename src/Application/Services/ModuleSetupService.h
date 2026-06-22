#ifndef MODULE_SETUP_SERVICE_H
#define MODULE_SETUP_SERVICE_H

#include "constants.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"
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
#include "Core/Domain/Enums/PsramStatusEnum.h"
#include "Application/Services/Devices/IDeviceService.h"
#include "Application/Services/Rules/IRuleService.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/BoardTesting/IBoardTest.h"
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/IClearModuleDataService.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"
#include "Core/Domain/Interfaces/UseCases/IOnConnectivityRestoredUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ICheckModuleDeletionUseCase.h"
#include <memory>
#include <cstdarg>
#include <cstdio>

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
    IButtonAdapter *buttonAdapter;
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
    IHybridAPIService *hybridApiService;
    IClearModuleDataService *clearModuleDataService;
    IArduinoAdapter *arduinoAdapter;
    ICheckModuleDeletionUseCase *checkModuleDeletionUseCase;
    ISyncSensorTypesUseCase *syncSensorTypesUseCase;
    IRemoteSensorReceiver *remoteSensorReceiver;
    IOnConnectivityRestoredUseCase *onConnectivityRestoredUseCase;
    IOtaUpdateLog *otaUpdateLog;

    void log(const char *format, ...);

    bool initializeModuleConfiguration();
    bool setupLeds();
    bool setupButtons();
    bool setupRelayOutputs();
    void handleModuleMode();
    void ApTestModeModeConfigure();
    void UserCanConfigModeConfigure();
    void UserReConfigModeConfigure();
    void ConfigureDoneModeConfigure();
    void checkModuleDeletionOnStartup();
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
    bool detectSensor(Node &node, const SensorEntity &tempSensor, const SensorType &sensorType, int pin);

    void addMultiChannelModbusSensor(Node &node, const SensorEntity &sensorEntity);
    void addMultiChannelI2CSensor(Node &node, const SensorEntity &sensorEntity);
    void addSingleChannelSensor(Node &node, const SensorEntity &sensorEntity);
    void addSensorToStructure(Node &node, const SensorEntity &sensorEntity);
    void removeDisconnectedNewSensorPlaceholders(Node &node, const SensorEntity &sensorEntity);

    int findNextAvailableAddress(const std::map<int, std::vector<SensorEntity *>> &addressMap);

    void createPlaceholderSensors(Node &node);
    void processSensorsFromRepository(Node &node);
    void handleModbusAddressManagement(Node &node);
    void collectModbusSensorsAndAddresses(const std::vector<SensorEntity> &allSensorEntities,
                                          std::map<int, std::vector<SensorEntity *>> &addressMap,
                                          std::vector<SensorEntity *> &sensorsNeedingNewAddress);
    void findDuplicateAddresses(const std::map<int, std::vector<SensorEntity *>> &addressMap,
                                std::vector<SensorEntity *> &sensorsNeedingNewAddress);
    void assignNewAddresses(std::vector<SensorEntity *> &sensorsNeedingNewAddress,
                            std::map<int, std::vector<SensorEntity *>> &addressMap);

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
        IButtonAdapter *buttonAdapter = nullptr,
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
        IUdpManager *udpManager = nullptr,
        IHybridAPIService *hybridApiService = nullptr,
        IClearModuleDataService *clearModuleDataService = nullptr,
        IArduinoAdapter *arduinoAdapter = nullptr,
        ISyncSensorTypesUseCase *syncSensorTypesUseCase = nullptr,
        IRemoteSensorReceiver *remoteSensorReceiver = nullptr,
        IOnConnectivityRestoredUseCase *onConnectivityRestoredUseCase = nullptr,
        IOtaUpdateLog *otaUpdateLog = nullptr,
        ICheckModuleDeletionUseCase *checkModuleDeletionUseCase = nullptr);

    void setupModule() override;
    std::unique_ptr<ILoopService> getLoopService() override;
};

// Implementation

inline ModuleSetupService::ModuleSetupService(
    ILogger *logger,
    IDebugSystem *debugSystem,
    IModuleConfig *moduleConfig,
    ILedAdapter *ledAdapter,
    IButtonAdapter *buttonAdapter,
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
    IUdpManager *udpManager,
    IHybridAPIService *hybridApiService,
    IClearModuleDataService *clearModuleDataService,
    IArduinoAdapter *arduinoAdapter,
    ISyncSensorTypesUseCase *syncSensorTypesUseCase,
    IRemoteSensorReceiver *remoteSensorReceiver,
    IOnConnectivityRestoredUseCase *onConnectivityRestoredUseCase,
    IOtaUpdateLog *otaUpdateLog,
    ICheckModuleDeletionUseCase *checkModuleDeletionUseCase) : logger(logger),
                                                       debugSystem(debugSystem),
                                                       moduleConfig(moduleConfig),
                                                       ledAdapter(ledAdapter),
                                                       buttonAdapter(buttonAdapter),
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
                                                       udpManager(udpManager),
                                                       hybridApiService(hybridApiService),
                                                       clearModuleDataService(clearModuleDataService),
                                                       arduinoAdapter(arduinoAdapter),
                                                       syncSensorTypesUseCase(syncSensorTypesUseCase),
                                                       remoteSensorReceiver(remoteSensorReceiver),
                                                       onConnectivityRestoredUseCase(onConnectivityRestoredUseCase),
                                                       otaUpdateLog(otaUpdateLog),
                                                       checkModuleDeletionUseCase(checkModuleDeletionUseCase)
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

        IDebugFeature *feature = debugSystem->getFeature(DebugFeatures::SETUP_DEBUG);
        if (feature)
        {
            logger->println(DebugFeatures::SETUP_DEBUG, buffer, feature->getLogChannel());
        }
        else
        {
            logger->println(DebugFeatures::SETUP_DEBUG, buffer);
        }
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

inline bool ModuleSetupService::setupButtons()
{
    if (!buttonAdapter)
        return false;

    uint8_t buttonPin = moduleConfig->getButtonPin();
    uint8_t buttonPins[1] = {buttonPin};

    buttonAdapter->setupModuleButtons(buttonPins, 1);

    log("SETUP: Button initialized on pin: %d", buttonPin);

    return true;
}

inline bool ModuleSetupService::setupRelayOutputs()
{
    if (!moduleConfig || !arduinoAdapter)
        return false;

    int moduleType = moduleConfig->getModuleType();
    uint8_t *pins = nullptr;
    if (moduleType == MODULE_TYPE_DEVICE)
        pins = moduleConfig->getRelayPins();
    else if (moduleType == MODULE_TYPE_HYDRO)
        pins = moduleConfig->getPumpPins();
    else
        return true;

    int deviceCount = moduleConfig->getDeviceCount();

    if (!pins)
        return false;

    uint8_t inactive = arduinoAdapter->getDigitalLow();

    MqttLoggerMode channel = MqttLoggerMode::MqttAndSerial;
    IDebugFeature *feature = debugSystem ? debugSystem->getFeature(DebugFeatures::RELAY_DRIVE_DEBUG) : nullptr;
    if (feature)
        channel = feature->getLogChannel();

    for (int i = 0; i < deviceCount; i++)
    {
        arduinoAdapter->pinMode(pins[i], arduinoAdapter->getPinModeOutput());
        arduinoAdapter->digitalWrite(pins[i], inactive);

        if (logger && debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::RELAY_DRIVE_DEBUG))
        {
            char buffer[96];
            snprintf(buffer, sizeof(buffer), "RELAY_DRIVE: init ch=%d pin=%d level=%d", i, pins[i], inactive);
            logger->println(DebugFeatures::RELAY_DRIVE_DEBUG, buffer, channel);
        }
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

    if (isConfigured != ModuleModeEnum::CONFIGURED_NEED_RESTART &&
        isConfigured != ModuleModeEnum::AP_TEST_MODE &&
        isConfigured != ModuleModeEnum::USER_CAN_CONFIGURE &&
        isConfigured != ModuleModeEnum::USER_CAN_RE_CONFIGURE &&
        isConfigured != ModuleModeEnum::CONFIGURED_DONE)
    {
        settings->setConfigured(ModuleModeEnum::AP_TEST_MODE);
        isConfigured = ModuleModeEnum::AP_TEST_MODE;
    }

    log("SETUP: isConfigured = %d", isConfigured);

    buttonUseCases->checkButtonPressedWhenOn();

    if (buttonUseCases->isButtonPressedWhenOn() && settings)
    {
        settings->setNoScanSensors(1);
        log("SETUP: Button pressed at startup, noScanSensors flag set");
    }

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
    unsigned long t0 = arduinoAdapter ? arduinoAdapter->millis() : 0;
    log("[%lu ms] SETUP: ApTestModeModeConfigure begin - configuring for board testing", t0);

    unsigned long apStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
    setupAP(true);
    log("[%lu ms] SETUP: setupAP(test) done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - apStart);

    unsigned long wsStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
    setupTestModeWebServer();
    log("[%lu ms] SETUP: setupTestModeWebServer done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - wsStart);

    if (boardTest)
    {
        unsigned long ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
        log("[%lu ms] SETUP: setupDevicesForTest begin", ts);
        boardTest->setupDevicesForTest(moduleConfig);
        log("[%lu ms] SETUP: setupDevicesForTest done (took %lu ms)",
            arduinoAdapter ? arduinoAdapter->millis() : 0,
            (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);
    }
    else
    {
        log("SETUP: BoardTest not available for test mode setup");
    }

    log("[%lu ms] SETUP: ApTestModeModeConfigure done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - t0);
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
    unsigned long t0 = arduinoAdapter ? arduinoAdapter->millis() : 0;
    log("[%lu ms] SETUP: ConfigureDoneModeConfigure begin", t0);

    unsigned long ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
    setupWifiConnection();
    log("[%lu ms] SETUP: setupWifiConnection done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);

    ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
    checkModuleDeletionOnStartup();
    log("[%lu ms] SETUP: checkModuleDeletionOnStartup done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);

    ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
    setupMqttConnection();
    log("[%lu ms] SETUP: setupMqttConnection done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);

    ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
    setupUdp();
    log("[%lu ms] SETUP: setupUdp done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);

    ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
    fillModuleStructure();
    log("[%lu ms] SETUP: fillModuleStructure done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);

    if (deviceService)
    {
        ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
        deviceService->init();
        log("[%lu ms] SETUP: Device service initialized (took %lu ms)",
            arduinoAdapter ? arduinoAdapter->millis() : 0,
            (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);
    }
    else
    {
        log("SETUP: Device service not available");
    }

    if (ruleService)
    {
        ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
        ruleService->init();
        log("[%lu ms] SETUP: Rule service initialized (took %lu ms)",
            arduinoAdapter ? arduinoAdapter->millis() : 0,
            (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);
    }
    else
    {
        log("SETUP: Rule service not available");
    }

    if (remoteSensorReceiver)
    {
        remoteSensorReceiver->init();
        log("SETUP: Remote sensor receiver initialized");
    }

    if (onConnectivityRestoredUseCase)
    {
        onConnectivityRestoredUseCase->init();
        log("SETUP: Connectivity-restore use case initialized");
    }

    log("[%lu ms] SETUP: ConfigureDoneModeConfigure done - module prepared for operation (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - t0);
}

inline void ModuleSetupService::checkModuleDeletionOnStartup()
{
    // Only check if in configured done mode
    if (!moduleConfig || !moduleConfig->getSettings())
    {
        return;
    }

    ModuleModeEnum currentMode = static_cast<ModuleModeEnum>(
        moduleConfig->getSettings()->getIfConfigured());

    if (currentMode != ModuleModeEnum::CONFIGURED_DONE)
    {
        log("SETUP: Skipping deletion check - module not in CONFIGURED_DONE mode");
        return;
    }

    // Skip deletion check on first boot after configuration
    // Module needs to complete initial sync (send structure to server) first
    uint8_t wasInitialSyncCompleted = moduleConfig->getSettings()->getWasInitialSyncCompleted();
    if (wasInitialSyncCompleted == 0)
    {
        log("SETUP: Skipping deletion check - first boot after configuration (initial sync not yet completed)");
        return;
    }

    if (!checkModuleDeletionUseCase || !clearModuleDataService || !arduinoAdapter)
    {
        log("SETUP: Cannot check module deletion - missing required services");
        return;
    }

    log("SETUP: Checking if module was deleted from server...");

    ModuleDeletionStatusEnum status = ModuleDeletionStatusEnum::UNKNOWN;
    try
    {
        status = checkModuleDeletionUseCase->evaluate();
    }
    catch (...)
    {
        log("SETUP: Failed to check deletion status - continuing normal operation");
        return;
    }

    if (status == ModuleDeletionStatusEnum::DELETED)
    {
        log("SETUP: deletion verdict DELETED - factory reset");

        moduleConfig->getSettings()->setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
        log("SETUP: Module mode set to USER_CAN_CONFIGURE");

        clearModuleDataService->clearAllData();
        log("SETUP: All module data cleared");

        log("SETUP: Restarting module to complete reset...");
        arduinoAdapter->delay(1000);
        arduinoAdapter->restart();
    }
    else
    {
        log("SETUP: deletion verdict %s - continuing normal operation", getModuleDeletionStatusName(status));
    }
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

    if (isTestMode)
    {
        log("AP_TEST: setupAP entry, registering WiFi event logger");
        wifiAdapter->enableApEventLogging(logger);
    }

    if (wifiAdapter->isAccessPointActive() == false)
    {
        if (isTestMode) log("AP_TEST: AP not active, proceeding with creation");
        const char *macAddress = wifiAdapter->getMACAddress();
        log("SETUP: Device MAC address: %s", macAddress);

        char *ssid;
        if (isTestMode)
        {
            const char *jigPrefix = "grow_director_";
            size_t macLen = strlen(macAddress);
            size_t prefixLen = strlen(jigPrefix);
            ssid = new char[prefixLen + macLen + 1];
            strcpy(ssid, jigPrefix);
            strcat(ssid, macAddress);
            log("SETUP: Using JIG WiFi name: %s", ssid);
        }
        else
        {
            const char *wifiPrefix = determineWifiPrefix();
            log("SETUP: Using WiFi prefix: %s", wifiPrefix);
            ssid = generateSSID(wifiPrefix, macAddress);
            log("SETUP: WiFi Name: %s", ssid);
        }

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

        if (isTestMode)
        {
            log("AP_TEST: createAndConfigureAP ssid=%s pwd=%s ip=%s gw=%s mask=%s",
                ssid, password, apLocalIp, apGatewayIp, apNetworkMask);
        }

        if (createAndConfigureAP(ssid, password, apLocalIp, apGatewayIp, apNetworkMask))
        {
            log("SETUP: WiFi Password: %s", password);
            if (isTestMode)
            {
                log("AP_TEST: AP up, clients=%d, localIP=%s",
                    wifiAdapter->getConnectedClientCount(), wifiAdapter->getLocalIP());
            }
        }
        else if (isTestMode)
        {
            log("AP_TEST: createAndConfigureAP FAILED");
        }

        delete[] ssid;
    }
    else if (isTestMode)
    {
        log("AP_TEST: AP already active, skipping creation, clients=%d",
            wifiAdapter->getConnectedClientCount());
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
    snprintf(randomPart, sizeof(randomPart), "_%03d", randomNum);

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

    if (arduinoAdapter)
    {
        log("SETUP: Waiting 2s for AP to fully initialize...");
        arduinoAdapter->delay(2000);
    }

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

    // Set dynamic client ID: prefix + module UID
    std::string moduleUid = moduleConfig->getModuleUid();
    std::string clientId = mqttManager->getClientId() + moduleUid;
    mqttManager->setClientId(clientId.c_str());
    log("SETUP: MQTT client ID set to: %s", clientId.c_str());

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
    const char *multicastIP = "239.1.2.3";

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

    uint32_t psramSize = arduinoAdapter ? arduinoAdapter->getPsramSize() : 0;
    uint32_t psramFree = arduinoAdapter ? arduinoAdapter->getFreePsram() : 0;
    uint32_t internalHeapFree = arduinoAdapter ? arduinoAdapter->getFreeHeap() : 0;
    node.psram = getPsramStatusName(psramSize > 0 ? PSRAM_STATUS_DONE : PSRAM_STATUS_NONE);
    log("SETUP: PSRAM status: %s", node.psram.c_str());
    log("SETUP: PSRAM size: %u, free: %u, internal heap free: %u", psramSize, psramFree, internalHeapFree);
}

inline void ModuleSetupService::addMultiChannelModbusSensor(Node &node, const SensorEntity &sensorEntity)
{
    for (const auto &config : sensorEntity.modbusRegisterConfigs)
    {
        Sensor sensor;
        sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
        sensor.type = config.sensorType.empty() ? sensorEntity.sensorType : config.sensorType;
        sensor.pin = sensorEntity.pin;
        sensor.group = sensorEntity.modbusDeviceAddress;
        sensor.modelUid = sensorEntity.sensorTypeUid;
        log("SETUP: Adding multi-channel ModBus sensor - sensorTypeUid: %s", sensorEntity.sensorTypeUid.c_str());

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
        sensor.type = config.sensorType.empty() ? sensorEntity.sensorType : config.sensorType;
        sensor.pin = sensorEntity.pin;
        sensor.group = 1; // I2C typically uses a single address/group
        sensor.modelUid = sensorEntity.sensorTypeUid;

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
    sensor.group = 1;
    sensor.modelUid = sensorEntity.sensorTypeUid;

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

    // Check if sensor types are synchronized, if not - sync them first
    if (sensorTypeRepository)
    {
        auto sensorTypes = sensorTypeRepository->getAll();
        if (sensorTypes.empty())
        {
            log("SETUP: No sensor types found in repository, attempting synchronization");
            if (syncSensorTypesUseCase)
            {
                if (syncSensorTypesUseCase->init())
                {
                    log("SETUP: Forcing sensor types synchronization");
                    bool syncResult = syncSensorTypesUseCase->forceSync();
                    if (syncResult)
                    {
                        log("SETUP: Sensor types synchronized successfully");
                        // Verify that types were actually loaded
                        sensorTypes = sensorTypeRepository->getAll();
                        log("SETUP: Now have %zu sensor types available", sensorTypes.size());
                    }
                    else
                    {
                        log("SETUP: Failed to synchronize sensor types");
                    }
                }
                else
                {
                    log("SETUP: Failed to initialize sensor types sync use case");
                }
            }
            else
            {
                log("SETUP: Sensor types sync use case not available");
            }

            // If still no sensor types after sync attempt, create placeholders
            sensorTypes = sensorTypeRepository->getAll();
            if (sensorTypes.empty())
            {
                log("SETUP: Still no sensor types available after sync attempt, creating placeholders");
                createPlaceholderSensors(node);
                return;
            }
        }
        else
        {
            log("SETUP: Found %zu sensor types in repository", sensorTypes.size());
        }
    }

    ISettings *settingsForFastPath = moduleConfig ? moduleConfig->getSettings() : nullptr;
    bool noScanWasSet = settingsForFastPath && settingsForFastPath->getNoScanSensors();
    processSensorsFromRepository(node);
    if (!noScanWasSet)
    {
        handleModbusAddressManagement(node);
    }
    else
    {
        log("SETUP: noScanSensors flag was set - skipping handleModbusAddressManagement");
    }

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
                        tempSensor.sensorTypeUid = sensorType.uid;
                        tempSensor.isActive = true;
                        tempSensor.pin = pin;
                        char pinStr[3];
                        snprintf(pinStr, sizeof(pinStr), "%02d", pin);
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
                snprintf(sensorId, sizeof(sensorId), ".%02d", pin);
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
            snprintf(sensorId, sizeof(sensorId), ".%02d", i);
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
        snprintf(pumpId, sizeof(pumpId), ".pm.%02d", i);
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
            snprintf(relayId, sizeof(relayId), ".%02d", i);
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
            snprintf(relayId, sizeof(relayId), ".%02d", i);
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
        snprintf(relayId, sizeof(relayId), ".%02d", i);
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
    unsigned long t0 = arduinoAdapter ? arduinoAdapter->millis() : 0;
    log("[%lu ms] SETUP: setupModule begin", t0);

    if (spiffsAdapter)
    {
        unsigned long fsStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
        log("[%lu ms] SETUP: SPIFFS init begin", fsStart);
        spiffsAdapter->initSPIFFS();
        unsigned long fsEnd = arduinoAdapter ? arduinoAdapter->millis() : 0;
        log("[%lu ms] SETUP: SPIFFS init done (took %lu ms)", fsEnd, fsEnd - fsStart);
        if (moduleConfig)
        {
            moduleConfig->setFileSystemStatus(spiffsAdapter->getFileSystemStatus());
        }
    }
    else
    {
        log("SETUP: SPIFFS adapter not available");
    }

    if (otaUpdateLog && moduleConfig)
    {
        ISettings *bootSettings = moduleConfig->getSettings();
        if (bootSettings && bootSettings->getBootType() == BOOT_TYPE_FIRMWARE_UPDATE)
        {
            char bootRecord[160];
            snprintf(bootRecord, sizeof(bootRecord),
                     "BOOT in OTA: reset_reason=%d updateStatus=%d bootType=%d fw=%s",
                     arduinoAdapter ? arduinoAdapter->getResetReason() : -1,
                     (int)bootSettings->getUpdateStatus(),
                     (int)bootSettings->getBootType(),
                     moduleConfig->getFirmwareVersion());
            otaUpdateLog->write(bootRecord);
        }
    }

    if (!initializeModuleConfiguration())
    {
        log("SETUP: Module setup failed - unable to initialize configuration");
        return;
    }
    log("[%lu ms] SETUP: configuration initialized", arduinoAdapter ? arduinoAdapter->millis() : 0);

    unsigned long ledsStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
    if (!setupLeds())
    {
        log("SETUP: LED setup failed");
    }
    log("[%lu ms] SETUP: setupLeds done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - ledsStart);

    unsigned long btnStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
    if (!setupButtons())
    {
        log("SETUP: Button setup failed");
    }
    log("[%lu ms] SETUP: setupButtons done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - btnStart);

    unsigned long relayStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
    if (!setupRelayOutputs())
    {
        log("SETUP: Relay output setup failed");
    }
    log("[%lu ms] SETUP: setupRelayOutputs done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - relayStart);

    if (ledUseCases)
    {
        unsigned long ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
        ledUseCases->ledOnLoad();
        log("[%lu ms] SETUP: LED on-load sequence executed (took %lu ms)",
            arduinoAdapter ? arduinoAdapter->millis() : 0,
            (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);
    }

    unsigned long modeStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
    log("[%lu ms] SETUP: handleModuleMode begin", modeStart);
    handleModuleMode();
    unsigned long modeEnd = arduinoAdapter ? arduinoAdapter->millis() : 0;
    log("[%lu ms] SETUP: handleModuleMode done (took %lu ms)", modeEnd, modeEnd - modeStart);

    if (ledUseCases)
    {
        unsigned long ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
        ledUseCases->ledOffLoad();
        log("[%lu ms] SETUP: LED off-load sequence executed (took %lu ms)",
            arduinoAdapter ? arduinoAdapter->millis() : 0,
            (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);
    }

    unsigned long tEnd = arduinoAdapter ? arduinoAdapter->millis() : 0;
    log("[%lu ms] SETUP: Module setup completed (total %lu ms)", tEnd, tEnd - t0);
    log("SETUP: done");
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
        if (sensor.protocol != protocol || (sensor.status != SENSOR_STATUS_ADDED && sensor.status != SENSOR_STATUS_NEW))
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

    log("SETUP: Received %zu sensor types for scanning on pin %d:", allSensorTypes.size(), pin);
    for (size_t i = 0; i < allSensorTypes.size(); i++)
    {
        log("SETUP:   [%zu] name: %s, uid: %s, protocol: %s",
            i, allSensorTypes[i].name.c_str(), allSensorTypes[i].uid.c_str(),
            allSensorTypes[i].getProtocolString().c_str());
    }

    bool isAdded = false;
    std::string moduleUid = moduleConfig->getModuleUid();
    for (const auto &sensorType : allSensorTypes)
    {
        mqttManager->loop(); // NOTE: not good solution - Keep MQTT responsive during potentially long operations

        if (!sensorType.canScan || !sensorType.isSupported)
        {
            log("SETUP: Skipping sensor type %s (canScan=%s, isSupported=%s)",
                sensorType.name.c_str(),
                sensorType.canScan ? "true" : "false",
                sensorType.isSupported ? "true" : "false");
            continue;
        }

        SensorEntity tempSensor;
        tempSensor.sensorType = sensorType.name;
        tempSensor.model = sensorType.name;
        tempSensor.protocol = sensorType.getProtocolString();
        tempSensor.sensorTypeUid = sensorType.uid;
        tempSensor.isActive = true;
        tempSensor.pin = pin;
        char pinStr[3];
        snprintf(pinStr, sizeof(pinStr), "%02d", pin);
        tempSensor.uid = moduleUid + "." + pinStr;
        tempSensor.status = SensorStatusEnum::SENSOR_STATUS_NEW;

        log("SETUP: Creating temp sensor for scanning - UID: %s, Type: %s, Model: %s, Protocol: %s, Pin: %d, SensorTypeUID: %s",
            tempSensor.uid.c_str(), tempSensor.sensorType.c_str(), tempSensor.model.c_str(),
            tempSensor.protocol.c_str(), tempSensor.pin, tempSensor.sensorTypeUid.c_str());

        if (sensorType.isI2CSensor())
        {
            tempSensor.i2cAddress = sensorType.i2cAddress;
            tempSensor.chipType = sensorType.chipType;
            tempSensor.i2cRegisterConfigs.clear();
            for (const auto &regConfig : sensorType.i2cRegisterConfig)
            {
                tempSensor.i2cRegisterConfigs.push_back(regConfig);
            }
            if (detectSensor(node, tempSensor, sensorType, pin))
            {
                isAdded = true;
                break;
            }
        }
        else if (sensorType.isModbusSensor())
        {
            tempSensor.baudRate = sensorType.baudRate;
            tempSensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
            tempSensor.modbusPingAddress = sensorType.modbusPingAddress;
            tempSensor.registerToStoreModbusDeviceAddress = sensorType.registerToStoreModbusDeviceAddress;
            tempSensor.writeRegistersCommand = sensorType.writeRegistersCommand;

            int scanTargetAddress = MODBUS_ADDRESS_MIN + pin;

            tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
            log("SETUP: Attempting to set per-port address 0x%02X via broadcast on port %d for sensor type %s",
                scanTargetAddress, pin, sensorType.name.c_str());

            sensorReader->writeSensorAddress(tempSensor, scanTargetAddress);

            log("SETUP: Assuming per-port address 0x%02X was set, trying to detect sensor", scanTargetAddress);
            tempSensor.modbusDeviceAddress = scanTargetAddress;
            if (detectSensor(node, tempSensor, sensorType, pin))
            {
                isAdded = true;
                break;
            }
            log("SETUP: Sensor not detected at per-port address 0x%02X", scanTargetAddress);

            tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
            log("SETUP: Trying default ModBus address 0x%02X for sensor type %s", sensorType.modbusDeviceAddress, sensorType.name.c_str());

            if (detectSensor(node, tempSensor, sensorType, pin))
            {
                isAdded = true;
                break;
            }

            log("SETUP: Sensor not found at default address, scanning range 0x%02X-0x%02X", MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);
            for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
            {
                mqttManager->loop(); // NOTE: not good solution - Keep MQTT responsive during potentially long operations

                tempSensor.modbusDeviceAddress = address;
                log("SETUP: Scanning ModBus address 0x%02X for sensor type %s", address, sensorType.name.c_str());

                if (detectSensor(node, tempSensor, sensorType, pin))
                {
                    isAdded = true;
                    break;
                }
            }

            if (isAdded)
            {
                break;
            }
        }
    }

    if (isAdded == false)
    {
        Sensor sensor;
        sensor.pin = pin;
        sensor.type = "none";

        char sensorId[20];
        snprintf(sensorId, sizeof(sensorId), ".%02d", pin);
        sensor.uid = node.uid + sensorId;

        node.sensors.push_back(sensor);

        log("SETUP: Added placeholder sensor group %d, type: %s, pin: %d, uid: %s",
            pin, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());

        // for (int i = 0; i < MAX_SENSOR_CHANNELS; i++)
        // {
        //     Sensor sensor;
        //     sensor.pin = pin;
        //     sensor.type = "none";

        //     char sensorId[20];
        //     snprintf(sensorId, sizeof(sensorId), ".%02d.%02d", pin, i + 1);
        //     sensor.uid = node.uid + sensorId;

        //     node.sensors.push_back(sensor);

        //     log("SETUP: Added placeholder sensor group %d index %d, type: %s, pin: %d, uid: %s",
        //         pin, i, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
        // }
    }
}

inline bool ModuleSetupService::detectSensor(Node &node, const SensorEntity &tempSensor, const SensorType &sensorType, int pin)
{
    if (!sensorReader || !sensorReader->scanSensor(tempSensor))
    {
        return false;
    }

    // TODO: set new address ModBus sensors
    log("SETUP: Detected %s sensor on pin %d", sensorType.name.c_str(), pin);

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

    return true;
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
                sensor.group = 0;

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
                sensor.group = 0;

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

inline int ModuleSetupService::findNextAvailableAddress(const std::map<int, std::vector<SensorEntity *>> &addressMap)
{
    // Search for next available address in range MODBUS_ADDRESS_MIN-MODBUS_ADDRESS_MAX
    for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
    {
        if (addressMap.find(address) == addressMap.end() || addressMap.at(address).empty())
        {
            log("SETUP: Found available address: 0x%02X", address);
            return address;
        }
    }

    log("SETUP: No available addresses found in range 0x%02X-0x%02X", MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);
    return -1; // No available address found
}

inline void ModuleSetupService::createPlaceholderSensors(Node &node)
{
    log("SETUP: Sensor repository not available, creating placeholder sensors");

    ISettings *settings = moduleConfig ? moduleConfig->getSettings() : nullptr;
    if (settings && settings->getNoScanSensors())
    {
        settings->setNoScanSensors(false);
        log("SETUP: noScanSensors flag reset in placeholder path so next boot performs scan");
    }

    // TODO: only for sensor for hydro without additional sensors
    // TODO: read connecter sensors
    // TODO: use setting to understand if sensor is connected, if yes use repository to get sensors with all properties
    // TODO: try connect to sensor and figureout does sensor online or not

    std::string nodeUid = node.uid;
    int sensorPinCount = moduleConfig->getSensorPinCount();

    for (int sensorPin = 0; sensorPin < sensorPinCount; sensorPin++)
    {
        Sensor sensor;
        sensor.pin = sensorPin;
        sensor.type = "none";

        char sensorId[20];
        snprintf(sensorId, sizeof(sensorId), ".%02d", sensorPin);
        sensor.uid = node.uid + sensorId;

        node.sensors.push_back(sensor);

        log("SETUP: Added placeholder sensor group %d, type: %s, pin: %d, uid: %s",
            sensorPin, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());

        // for (int i = 0; i < MAX_SENSOR_CHANNELS; i++) // NOTE: to support old sensors with MAX_SENSOR_CHANNELS channels
        // {
        //     Sensor sensor;
        //     sensor.pin = sensorPin;
        //     sensor.type = "none";

        //     char sensorId[20];
        //     snprintf(sensorId, sizeof(sensorId), ".%02d.%02d", sensorPin, i + 1);
        //     sensor.uid = nodeUid + sensorId;

        //     node.sensors.push_back(sensor);

        //     log("SETUP: Added placeholder sensor group %d index %d, type: %s, pin: %d, uid: %s",
        //         sensorPin, i, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
        // }
    }
}

inline void ModuleSetupService::processSensorsFromRepository(Node &node)
{
    auto sensorEntities = sensorRepository->getAll();
    log("SETUP: Found %zu sensors in repository", sensorEntities.size());

    ISettings *settings = moduleConfig->getSettings();
    bool skipSensorScan = settings && settings->getNoScanSensors();

    if (skipSensorScan)
    {
        log("SETUP: noScanSensors flag is set - skipping existing sensor verification and empty pin scan");

        for (auto &sensorEntity : sensorEntities)
        {
            log("SETUP: Adding sensor %s to structure without verification (noScanSensors)",
                sensorEntity.uid.c_str());
            addSensorToStructure(node, sensorEntity);
            mqttManager->loop();
        }

        if (sensorReader)
        {
            sensorReader->prepareSensorsForReading(sensorEntities);
        }

        settings->setNoScanSensors(false);
        return;
    }

    for (auto &sensorEntity : sensorEntities)
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

        if (sensorReader && sensorReader->scanSensor(sensorEntity))
        {
            log("SETUP: Sensor %s is online, adding to structure", sensorEntity.uid.c_str());
            log("SETUP: Sensor modbusDeviceAddress %d", sensorEntity.modbusDeviceAddress);
            addSensorToStructure(node, sensorEntity);
        }
        else
        {
            log("SETUP: Sensor %s is offline", sensorEntity.uid.c_str());

            if (sensorEntity.status == SENSOR_STATUS_ADDED)
            {
                log("SETUP: Sensor %s was previously added but is now offline, updating status to ERROR", sensorEntity.uid.c_str());
                addSensorToStructure(node, sensorEntity);
            }
            else
            {
                log("SETUP: Sensor %s status is %s, adding placeholder with type 'none'",
                    sensorEntity.uid.c_str(), getSensorStatusName(sensorEntity.status));
                removeDisconnectedNewSensorPlaceholders(node, sensorEntity);
            }
        }
        mqttManager->loop();
    }

    scanEmptyPinsForSensors(node, sensorEntities);

    if (settings)
    {
        log("SETUP: Sensor scan completed");
    }
}

inline void ModuleSetupService::handleModbusAddressManagement(Node &node)
{
    log("SETUP: handleModbusAddressManagement called");

    // Address validation and duplicate handling for Modbus sensors
    if (!sensorRepository)
    {
        log("SETUP: Skipping Modbus address management - sensorRepository is NULL");
        return;
    }

    if (!sensorReader)
    {
        log("SETUP: Skipping Modbus address management - sensorReader is NULL");
        return;
    }

    log("SETUP: Starting address validation and duplicate handling for Modbus sensors");

    // Get all sensors again to include newly detected ones
    auto allSensorEntities = sensorRepository->getAll();
    log("SETUP: Retrieved %zu sensor entities from repository", allSensorEntities.size());

    if (allSensorEntities.empty())
    {
        log("SETUP: No sensors in repository, skipping address management");
        return;
    }

    // Collect all Modbus sensors and their addresses
    std::map<int, std::vector<SensorEntity *>> addressMap;
    std::vector<SensorEntity *> sensorsNeedingNewAddress;

    log("SETUP: Calling collectModbusSensorsAndAddresses...");
    collectModbusSensorsAndAddresses(allSensorEntities, addressMap, sensorsNeedingNewAddress);
    log("SETUP: collectModbusSensorsAndAddresses completed");

    log("SETUP: Calling findDuplicateAddresses...");
    findDuplicateAddresses(addressMap, sensorsNeedingNewAddress);
    log("SETUP: findDuplicateAddresses completed");

    log("SETUP: Calling assignNewAddresses...");
    assignNewAddresses(sensorsNeedingNewAddress, addressMap);
    log("SETUP: assignNewAddresses completed");
}

inline void ModuleSetupService::collectModbusSensorsAndAddresses(const std::vector<SensorEntity> &allSensorEntities,
                                                                 std::map<int, std::vector<SensorEntity *>> &addressMap,
                                                                 std::vector<SensorEntity *> &sensorsNeedingNewAddress)
{
    log("SETUP: collectModbusSensorsAndAddresses called with %zu sensor entities", allSensorEntities.size());
#ifdef ESP_PLATFORM
    log("SETUP: Free heap at start: %d", ESP.getFreeHeap());
#endif

    int modbusCount = 0;
    int addedCount = 0;
    int validAddressCount = 0;
    int invalidAddressCount = 0;

    for (auto &sensorEntity : const_cast<std::vector<SensorEntity> &>(allSensorEntities))
    {
        log("SETUP: Processing sensor - UID: %s, Protocol: %s, Status: %d",
            sensorEntity.uid.c_str(), sensorEntity.protocol.c_str(), sensorEntity.status);

        if (sensorEntity.protocol == "ModBus")
        {
            modbusCount++;
            log("SETUP: Found ModBus sensor %s with address 0x%02X",
                sensorEntity.uid.c_str(), sensorEntity.modbusDeviceAddress);

            if (sensorEntity.status == SENSOR_STATUS_NEW || sensorEntity.status == SENSOR_STATUS_ADDED)
            {
                addedCount++;
                int address = sensorEntity.modbusDeviceAddress;

                log("SETUP: ModBus sensor %s has status %d, checking address 0x%02X",
                    sensorEntity.uid.c_str(), sensorEntity.status, address);

                // Check if address is outside valid range (MODBUS_ADDRESS_MIN-MODBUS_ADDRESS_MAX)
                if (address < MODBUS_ADDRESS_MIN || address > MODBUS_ADDRESS_MAX)
                {
                    invalidAddressCount++;
                    log("SETUP: Sensor %s has address 0x%02X outside valid range 0x%02X-0x%02X, needs new address",
                        sensorEntity.uid.c_str(), address, MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);
                    sensorsNeedingNewAddress.push_back(&sensorEntity);
                    log("SETUP: Added sensor %s to sensorsNeedingNewAddress (total: %zu)",
                        sensorEntity.uid.c_str(), sensorsNeedingNewAddress.size());
                }
                else
                {
                    validAddressCount++;
                    addressMap[address].push_back(&sensorEntity);
                    log("SETUP: Added sensor %s to addressMap at address 0x%02X (sensors at this address: %zu)",
                        sensorEntity.uid.c_str(), address, addressMap[address].size());
                }
            }
            else
            {
                log("SETUP: ModBus sensor %s has status %d (not NEW/ADDED), skipping address validation",
                    sensorEntity.uid.c_str(), sensorEntity.status);
            }
        }
        else
        {
            log("SETUP: Sensor %s has protocol %s (not ModBus), skipping",
                sensorEntity.uid.c_str(), sensorEntity.protocol.c_str());
        }
    }

    log("SETUP: Collection summary - Total sensors: %zu, ModBus: %d, Added: %d, Valid addresses: %d, Invalid addresses: %d",
        allSensorEntities.size(), modbusCount, addedCount, validAddressCount, invalidAddressCount);
    log("SETUP: Address map size: %zu, Sensors needing new address: %zu",
        addressMap.size(), sensorsNeedingNewAddress.size());
#ifdef ESP_PLATFORM
    log("SETUP: Free heap at end: %d", ESP.getFreeHeap());
#endif
}

inline void ModuleSetupService::findDuplicateAddresses(const std::map<int, std::vector<SensorEntity *>> &addressMap,
                                                       std::vector<SensorEntity *> &sensorsNeedingNewAddress)
{
    log("SETUP: findDuplicateAddresses called with addressMap size: %zu", addressMap.size());
    log("SETUP: sensorsNeedingNewAddress initial size: %zu", sensorsNeedingNewAddress.size());

    int duplicatesFound = 0;
    int sensorsMarkedForChange = 0;

    // Find duplicates and mark for address change (keep first, change others)
    for (const auto &pair : addressMap)
    {
        int address = pair.first;
        const auto &sensors = pair.second;

        log("SETUP: Checking address 0x%02X with %zu sensors", address, sensors.size());

        if (sensors.size() > 1)
        {
            duplicatesFound++;
            log("SETUP: Found %zu sensors with duplicate address 0x%02X", sensors.size(), address);

            // Log all sensors at this address
            for (size_t i = 0; i < sensors.size(); i++)
            {
                log("SETUP: Sensor %zu at address 0x%02X: UID=%s, Status=%d",
                    i, address, sensors[i]->uid.c_str(), sensors[i]->status);
            }

            // Keep the first sensor, mark others for address change
            log("SETUP: Keeping first sensor %s, marking others for address change", sensors[0]->uid.c_str());
            for (size_t i = 1; i < sensors.size(); i++)
            {
                sensorsNeedingNewAddress.push_back(sensors[i]);
                sensorsMarkedForChange++;
                log("SETUP: Sensor %s marked for address change due to duplicate address 0x%02X",
                    sensors[i]->uid.c_str(), address);
            }
        }
        else
        {
            log("SETUP: Address 0x%02X has only 1 sensor, no duplicates", address);
        }
    }

    log("SETUP: findDuplicateAddresses completed - Duplicate addresses found: %d, Sensors marked for change: %d",
        duplicatesFound, sensorsMarkedForChange);
    log("SETUP: sensorsNeedingNewAddress final size: %zu", sensorsNeedingNewAddress.size());
}

inline void ModuleSetupService::assignNewAddresses(std::vector<SensorEntity *> &sensorsNeedingNewAddress,
                                                   std::map<int, std::vector<SensorEntity *>> &addressMap)
{
    // Assign new addresses to sensors that need them
    if (sensorsNeedingNewAddress.empty())
    {
        log("SETUP: No sensors need address changes");
        return;
    }

    log("SETUP: Assigning new addresses to %zu sensors", sensorsNeedingNewAddress.size());

    for (SensorEntity *sensorEntity : sensorsNeedingNewAddress)
    {
        // Find next available address in range MODBUS_ADDRESS_MIN-MODBUS_ADDRESS_MAX
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
                log("SETUP: Address successfully changed - Old: 0x%02X, New: 0x%02X", oldAddress, newAddress);

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
            log("SETUP: No available addresses in range 0x%02X-0x%02X for sensor %s",
                MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX, sensorEntity->uid.c_str());
        }
    }
}

#endif // MODULE_SETUP_SERVICE_H
