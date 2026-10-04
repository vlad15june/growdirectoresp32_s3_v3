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
#include "Core/Domain/Interfaces/Udp/IUdpInboundDispatcher.h"
#include "Core/Domain/Interfaces/Adapters/IRemoteModuleCommandReceiver.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"
#include "Core/Domain/Interfaces/UseCases/IOnConnectivityRestoredUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Sensors/IStartupSensorScan.h"
#include "Core/Domain/UseCases/Network/WifiLinkLogUseCase.h"
#include "Application/Interfaces/INetworkListeners.h"
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
class CatalogReaderTestHelper;
class StartupScanTestHelper;

class ModuleSetupService : public IModuleSetupService, public INetworkListeners
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
    IArduinoAdapter *arduinoAdapter;
    ISyncSensorTypesUseCase *syncSensorTypesUseCase;
    IRemoteSensorReceiver *remoteSensorReceiver;
    IOnConnectivityRestoredUseCase *onConnectivityRestoredUseCase;
    IOtaUpdateLog *otaUpdateLog;
    IUdpInboundDispatcher *udpInboundDispatcher;
    IRemoteModuleCommandReceiver *remoteModuleCommandReceiver;

    ISensorStructureUseCase *sensorStructureUseCase;
    IStartupSensorScan *startupSensorScan = nullptr;
    WifiLinkLogUseCase *wifiLinkLog = nullptr;

    void log(const char *format, ...);

    bool initializeModuleConfiguration();
    bool setupLeds();
    bool setupButtons();
    bool setupRelayOutputs();
    void setupSensorBus();
    void powerTheStoredSensorPorts();
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
    void registerWifiNetworks();
    void setupUdp();
    void fillModuleStructure();

    bool validateModuleStructurePrerequisites();
    void fillModuleCommonData();
    void createBaseNodeStructure(Node &node);
    void fillSensorModuleStructure(Node &node);
    void fillHydroModuleStructure(Node &node);
    void fillDeviceModuleStructure(Node &node);
    void fillAnalogInputDeviceStructure(Node &node);

    void addMultiChannelModbusSensor(Node &node, const SensorEntity &sensorEntity);
    void addMultiChannelI2CSensor(Node &node, const SensorEntity &sensorEntity);
    void addSingleChannelSensor(Node &node, const SensorEntity &sensorEntity);
    void addSensorToStructure(Node &node, const SensorEntity &sensorEntity);

    void createPlaceholderSensors(Node &node);
    void processSensorsFromRepository(Node &node);

    void addHydroSensors(Node &node);
    void addHydroPumps(Node &node);

public:
    friend class ModuleSetupServiceTestHelper;
    friend class FillModuleStructureTestHelper;
    friend class CatalogReaderTestHelper;
    friend class StartupScanTestHelper;
    friend class LiveFactTestHelper;
    friend class LivenessTestHelper;
    friend class RegistryUnreadTestHelper;

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
        IArduinoAdapter *arduinoAdapter = nullptr,
        ISyncSensorTypesUseCase *syncSensorTypesUseCase = nullptr,
        IRemoteSensorReceiver *remoteSensorReceiver = nullptr,
        IOnConnectivityRestoredUseCase *onConnectivityRestoredUseCase = nullptr,
        IOtaUpdateLog *otaUpdateLog = nullptr,
        IUdpInboundDispatcher *udpInboundDispatcher = nullptr,
        IRemoteModuleCommandReceiver *remoteModuleCommandReceiver = nullptr);

    /**
     * @brief Inject the builder of the sensor records of the structure (GDV1-3782)
     *
     * A setter and not a constructor argument: the constructor of this service is already 33
     * positional parameters long, and globals.h wires the rest of the late dependencies the same
     * way. The same instance is given to RepublishModuleStructureUseCase.
     */
    void setSensorStructureUseCase(ISensorStructureUseCase *useCase) { sensorStructureUseCase = useCase; }

    /**
     * @brief Inject the sensor discovery this service arms instead of performing
     *
     * A setter for the same reason as setSensorStructureUseCase above: the constructor of this
     * service is already 33 positional parameters long. Setup decides whether the discovery runs
     * at all - that is what `noScanSensors` says - and the discovery itself runs on the task that
     * owns the RS-485 line, after the control loop has started.
     *
     * @param scan Discovery armed at the end of the sensor structure fill
     */
    void setStartupSensorScan(IStartupSensorScan *scan) { startupSensorScan = scan; }

    /**
     * @brief Inject the log of what happens to the station link
     *
     * A setter for the same reason as the two above. Setup is where it is armed, because the radio
     * has to be reporting its events before the first association is asked for.
     *
     * @param linkLog Log the radio's station events are reported to
     */
    void setWifiLinkLog(WifiLinkLogUseCase *linkLog) { wifiLinkLog = linkLog; }

    void setupModule() override;
    std::unique_ptr<ILoopService> getLoopService() override;

    /** @brief The UDP listeners, once the boot's link has an address. */
    void startListening() override { setupUdp(); }
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
    IArduinoAdapter *arduinoAdapter,
    ISyncSensorTypesUseCase *syncSensorTypesUseCase,
    IRemoteSensorReceiver *remoteSensorReceiver,
    IOnConnectivityRestoredUseCase *onConnectivityRestoredUseCase,
    IOtaUpdateLog *otaUpdateLog,
    IUdpInboundDispatcher *udpInboundDispatcher,
    IRemoteModuleCommandReceiver *remoteModuleCommandReceiver) : logger(logger),
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
                                                       arduinoAdapter(arduinoAdapter),
                                                       syncSensorTypesUseCase(syncSensorTypesUseCase),
                                                       remoteSensorReceiver(remoteSensorReceiver),
                                                       onConnectivityRestoredUseCase(onConnectivityRestoredUseCase),
                                                       otaUpdateLog(otaUpdateLog),
                                                       udpInboundDispatcher(udpInboundDispatcher),
                                                       remoteModuleCommandReceiver(remoteModuleCommandReceiver),
                                                       sensorStructureUseCase(nullptr)
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

/**
 * @brief Begin the RS-485 UARTs once, before anything on this module can put a frame on them.
 *
 * Every mode goes through here - the ordinary boot, a boot with `noScanSensors`, the board test,
 * the configuration modes - and it runs before the mode is handled: before the board test sets its
 * devices up, before the structure is filled, and before the control loop starts the acquisition
 * task that carries the polls, the discovery and the raw commands from the backend. The first
 * transaction of a boot is as often one of those as it is a scan, and a UART nobody has begun
 * writes none of them.
 *
 * 9600 baud is the rate of every model of the catalog. A scan still begins the ports again at its
 * model's own rate on its way in (SensorReader::scanModbusSensor()); this call does not depend on a
 * scan ever happening.
 *
 * A sensor module only: nothing a module of another type does on its own begins this line. The
 * discovery and the preparation of stored ModBus sensors are armed only by the structure fill of a
 * sensor module (processSensorsFromRepository()), and a hydro module scans analog sensor types only
 * (addHydroSensors()). On two of the other types the UART pins are wired to something else
 * (ModuleConfig.h; the UARTs are RX/TX/DE 18/17/19 and 15/14/20, globals.h): a hydro module drives
 * its three pumps from 16, 17 and 18 (setupRelayOutputs()) and a green LED from 14, a relay module
 * its red LEDs from 15 and 14 - beginning a UART there would hand pump 2's output to a TX line.
 * The DE pins 19 and 20 are outputs held low on every module type, by the ModbusRS485 constructors.
 */
inline void ModuleSetupService::setupSensorBus()
{
    if (!sensorReader || !moduleConfig)
    {
        log("SETUP: RS-485 bus not initialized - no sensor reader or configuration");
        return;
    }

    const int moduleType = moduleConfig->getModuleType();
    if (moduleType != MODULE_TYPE_SENSOR)
    {
        log("SETUP: RS-485 bus not initialized - not a sensor module (type %d)", moduleType);
        return;
    }

    sensorReader->init(9600);
}

/**
 * @brief Give the ports of the stored ModBus sensors their power now, early in the boot.
 *
 * The ports start dark - the ModbusRS485 constructors drive every SENSOR_POWER pin low - and the
 * sweep does not read a port that came up less than PortPower::WAKE_UP_MS ago. Switched on here,
 * a stored sensor's port has had the rest of the setup to come up by the sweep's first visit. The
 * transitions go through the port itself (ISensorReader::setModbusPortPower()), so its power
 * record says when.
 *
 * Only where the sweep will read them: a sensor module (the line is begun only there, see
 * setupSensorBus()) booting CONFIGURED_DONE, the one mode whose loop reads sensors
 * (LoopServiceFactory). The free ports are left to the discovery, which powers and releases them
 * itself, and so is a module with no stored row.
 */
inline void ModuleSetupService::powerTheStoredSensorPorts()
{
    if (!sensorReader || !sensorRepository || !moduleConfig ||
        moduleConfig->getModuleType() != MODULE_TYPE_SENSOR)
    {
        return;
    }
    ISettings *settings = moduleConfig->getSettings();
    if (!settings || settings->getIfConfigured() != ModuleModeEnum::CONFIGURED_DONE)
    {
        return;
    }

    for (const auto &sensor : sensorRepository->getAll())
    {
        if (sensor.protocol != "ModBus" || sensor.pin < 0 ||
            sensor.pin >= moduleConfig->getSensorPinCount())
        {
            continue;
        }
        if (sensorReader->setModbusPortPower(sensor.pin, true))
        {
            log("SETUP: port %d of stored sensor %s powered", sensor.pin, sensor.uid.c_str());
        }
    }
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

    buttonUseCases->checkStartupFactoryReset();

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

    // The server's address and the module's token, read from the settings into the REST client
    // once: every request of this boot - the outbox's, the command task's - goes out with them, and
    // in this mode nothing changes them without a restart. Nothing is sent here.
    if (hybridApiService)
    {
        const bool restReady = hybridApiService->init();
        log("SETUP: REST client given the stored server address: %s", restReady ? "yes" : "no");
    }

    // The networks the station may associate with, and nothing more: setup waits for no network.
    // The association is the first thing the link's supervision asks for once the loop runs.
    registerWifiNetworks();

    unsigned long ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
    setupMqttConnection();
    log("[%lu ms] SETUP: setupMqttConnection done (took %lu ms)",
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

/**
 * @brief Arm the radio's link log and hand it the networks it may associate with.
 *
 * The local half of the association: the station is associated later, on the network task.
 */
inline void ModuleSetupService::registerWifiNetworks()
{
    if (!wifiManager)
    {
        log("SETUP: Error - WiFi manager is missing");
        return;
    }

    // Before the first association is asked for, so that the connect it produces is on the same
    // path every later one is: what the radio reports about the link, including the reason code a
    // drop carries and nothing else does.
    if (wifiAdapter != nullptr && wifiLinkLog != nullptr)
    {
        wifiAdapter->reportLinkEventsTo(wifiLinkLog);
    }

    const int networks = wifiManager->registerNetworks();
    log("SETUP: %d WiFi network(s) handed to the radio - it associates once the loop runs", networks);
}

inline void ModuleSetupService::setupMqttConnection()
{
    if (!mqttManager)
    {
        log("SETUP: Error - MQTT manager is missing");
        return;
    }

    // The broker files the session - the subscriptions, and whatever was published to this module
    // while it was away - under the client ID, so the ID has to be the same string after every
    // restart and a different one on every module. It is the deployment prefix followed by the
    // module UID, which is the station MAC read out of the chip: nothing in it comes from storage,
    // from the network or from a counter, so a restart, a factory reset and a re-registration all
    // present the broker the same client.
    //
    // The UID is appended only if it is not there already: a second pass would otherwise produce
    // prefix+uid+uid, and a module that changes its client ID hands the broker a client it has no
    // session for - losing everything queued under the old one, quietly on both sides.
    std::string moduleUid = moduleConfig->getModuleUid();
    std::string clientId = mqttManager->getClientId();
    if (clientId.size() < moduleUid.size() ||
        clientId.compare(clientId.size() - moduleUid.size(), moduleUid.size(), moduleUid) != 0)
    {
        clientId += moduleUid;
    }
    mqttManager->setClientId(clientId.c_str());
    log("SETUP: MQTT client ID set to: %s", clientId.c_str());

    // Without a link connect() only configures the client and says no; the CONNECT is then made by
    // the client's own state machine, on the network task, once the link is up.
    if (mqttManager->connect())
    {
        log("SETUP: Connected to MQTT broker");
    }
    else
    {
        log("SETUP: MQTT client configured - it connects once the link is up");
    }

    // Once, and whatever connect() said: a topic subscribed to while the client is not connected is
    // kept and subscribed to on every CONNACK, and without the handlers the commands of this boot
    // would arrive with nobody to take them.
    mqttManager->setupMqttTopicHandlers();
}

inline void ModuleSetupService::setupUdp()
{
    if (!udpManager)
    {
        log("UDP: Error - UDP manager is missing");
        return;
    }

    // Default UDP port and multicast IP
    uint16_t udpPort = 1234;
    const char *multicastIP = "239.1.2.3";

    if (udpManager->initialize(udpPort, multicastIP))
    {
        log("UDP: initialized on port %d with multicast %s", udpPort, multicastIP);
    }
    else
    {
        log("UDP: Failed to initialize on port %d", udpPort);
    }

    if (udpInboundDispatcher)
    {
        udpInboundDispatcher->init();
        log("UDP: inbound dispatcher initialized");
    }

    if (remoteModuleCommandReceiver)
    {
        remoteModuleCommandReceiver->init();
        log("UDP: remote module command receiver initialized");
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

// The records themselves are built by SensorStructureUseCase; these stay as the entry points
// the setup flow (and its tests) already use.
inline void ModuleSetupService::addMultiChannelModbusSensor(Node &node, const SensorEntity &sensorEntity)
{
    if (sensorStructureUseCase)
    {
        sensorStructureUseCase->addMultiChannelModbusSensor(node, sensorEntity);
    }
}

inline void ModuleSetupService::addMultiChannelI2CSensor(Node &node, const SensorEntity &sensorEntity)
{
    if (sensorStructureUseCase)
    {
        sensorStructureUseCase->addMultiChannelI2CSensor(node, sensorEntity);
    }
}

inline void ModuleSetupService::addSingleChannelSensor(Node &node, const SensorEntity &sensorEntity)
{
    if (sensorStructureUseCase)
    {
        sensorStructureUseCase->addSingleChannelSensor(node, sensorEntity);
    }
}

inline void ModuleSetupService::addSensorToStructure(Node &node, const SensorEntity &sensorEntity)
{
    if (!sensorStructureUseCase)
    {
        log("SETUP: sensor structure use case is not wired - sensor %s is not added to the structure",
            sensorEntity.uid.c_str());
        return;
    }

    sensorStructureUseCase->addSensor(node, sensorEntity);
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

    // No catalog on flash: nothing to describe the sensors by, and setup asks the server for
    // nothing - the catalog is asked for once the boot's structure has settled. The structure goes
    // out with placeholders, and what the discovery owes is armed for when the catalog arrives.
    if (sensorTypeRepository)
    {
        // count(), not getAll(): the question is how many types the module holds, and getAll()
        // answers it with a second copy of the whole catalog.
        if (sensorTypeRepository->count() == 0)
        {
            log("SETUP: No sensor types in the repository, creating placeholders");
            // Read before createPlaceholderSensors() clears it: the flag is consumed on this
            // boot, and what it asked for is armed with the catalog that has not arrived yet.
            ISettings *settings = moduleConfig ? moduleConfig->getSettings() : nullptr;
            const bool preparationOnly = settings && settings->getNoScanSensors();
            createPlaceholderSensors(node);
            if (startupSensorScan)
            {
                startupSensorScan->armWhenTheCatalogArrives(preparationOnly);
            }
            return;
        }

        log("SETUP: Found %d sensor types in repository", sensorTypeRepository->count());
    }

    processSensorsFromRepository(node);

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

        // Get all sensor types for hydro module. The catalog is read through the snapshot it is
        // held as, and the hydro types are picked out of it by address: neither the whole catalog
        // nor the types picked from it are copied to be walked over.
        SensorTypeCatalog catalog = sensorTypeRepository->catalogSnapshot();
        std::vector<const SensorType *> hydroSensorTypes;

        // Filter for hydro-specific analog protocols
        for (const auto &sensorType : *catalog)
        {
            if (sensorType.protocol == SensorProtocol::ANALOG_BNC_PROTOCOL ||
                sensorType.protocol == SensorProtocol::ANALOG_EC2CH_PROTOCOL ||
                sensorType.protocol == SensorProtocol::ANALOG_PH_PROTOCOL)
            {
                hydroSensorTypes.push_back(&sensorType);
                log("SETUP: Found hydro sensor type: %s, protocol: %s",
                    sensorType.name.c_str(), sensorType.getProtocolString().c_str());
            }
        }

        std::string moduleUid = moduleConfig->getModuleUid();
        std::vector<bool> pinUsed(sensorPinCount + 1, false);

        for (const auto *hydroSensorType : hydroSensorTypes)
        {
            const SensorType &sensorType = *hydroSensorType;
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

    unsigned long busStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
    setupSensorBus();
    log("[%lu ms] SETUP: setupSensorBus done (took %lu ms)",
        arduinoAdapter ? arduinoAdapter->millis() : 0,
        (arduinoAdapter ? arduinoAdapter->millis() : 0) - busStart);

    powerTheStoredSensorPorts();

    if (ledUseCases)
    {
        unsigned long ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
        // The pins exist from here on, so the front panel can start saying that the module is
        // coming up. It says it in one pattern until setup is over.
        ledUseCases->setBootStage(LED_BOOT_STAGE_BOOTING);
        log("[%lu ms] SETUP: LED on-load sequence executed (took %lu ms)",
            arduinoAdapter ? arduinoAdapter->millis() : 0,
            (arduinoAdapter ? arduinoAdapter->millis() : 0) - ts);
    }

    unsigned long modeStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
    log("[%lu ms] SETUP: handleModuleMode begin", modeStart);
    handleModuleMode();
    unsigned long modeEnd = arduinoAdapter ? arduinoAdapter->millis() : 0;
    log("[%lu ms] SETUP: handleModuleMode done (took %lu ms)", modeEnd, modeEnd - modeStart);

    // The end of the boot, in one line and in the module's own uptime: everything above this line
    // is setup and everything below it is the control loop, and the front panel changes hands on
    // the same millisecond.
    log("BOOT: complete in %lu ms", arduinoAdapter ? arduinoAdapter->millis() : 0);

    if (ledUseCases)
    {
        unsigned long ts = arduinoAdapter ? arduinoAdapter->millis() : 0;
        // The boot indication ends before the LED is cleared, so that what it was drawing has
        // stopped by the time the working indication takes the LED over.
        ledUseCases->setBootStage(LED_BOOT_STAGE_RUNNING);
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

/**
 * @brief The sensor records of the structure, built from the registry and from nothing else.
 *
 * No transaction on the line happens here. The module describes the sensors it stored
 * before the restart, starts polling them on the first sweep, and asks the line what else is out
 * there afterwards - IStartupSensorScan, on the task that owns the line. A boot is seconds,
 * and the discovery runs while the module is already working.
 *
 * `noScanSensors` decides whether the discovery is armed around the sensors the module holds, and
 * it is cleared here, on the boot that consumed it, exactly as before. What the flag has never
 * skipped is the preparation of the sensors already stored - the per-model register writes and the
 * port power arrangement - so a boot that skips the discovery arms that alone. A module that holds
 * no sensor has nothing to prepare: its discovery is armed whatever the flag says.
 */
inline void ModuleSetupService::processSensorsFromRepository(Node &node)
{
    log("SETUP: Found %d sensors in repository", sensorRepository->count());

    ISettings *settings = moduleConfig->getSettings();
    const bool skipDiscovery = settings && settings->getNoScanSensors();
    if (skipDiscovery)
    {
        settings->setNoScanSensors(false);
    }

    if (!startupSensorScan)
    {
        log("SETUP: startup sensor discovery is not wired - no sensor discovery will happen");
        for (const auto &sensorEntity : sensorRepository->getAll())
        {
            addSensorToStructure(node, sensorEntity);
        }
        return;
    }

    startupSensorScan->describeStoredSensors(node);

    if (skipDiscovery && sensorRepository->count() > 0)
    {
        // The flag skips the discovery, not the per-model register writes: those are owed to the
        // sensors the module already holds, on this boot as on any other.
        startupSensorScan->armPreparationOnly();
        log("SETUP: noScanSensors flag is set - only the sensor preparation is armed");
        return;
    }

    if (skipDiscovery)
    {
        // A module that holds no sensor has nothing to prepare, and the flag is used up on this
        // boot: skipped, the discovery would leave it with no sensor and the server with none. The
        // same rule a catalog that arrives late follows (IStartupSensorScan::onCatalogSettled()).
        log("SETUP: noScanSensors flag is set, but no sensor is stored - the discovery is armed");
    }

    startupSensorScan->arm();
    log("SETUP: startup sensor discovery armed - it runs on the acquisition task");
}

#endif // MODULE_SETUP_SERVICE_H
