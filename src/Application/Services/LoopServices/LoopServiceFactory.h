#ifndef LOOP_SERVICE_FACTORY_H
#define LOOP_SERVICE_FACTORY_H

#include "../../Interfaces/LoopServices/ILoopServiceFactory.h"
#include "LoopServiceCanConfigure.h"
#include "LoopServiceReConfigure.h"
#include "LoopServiceTestMode.h"
#include "LoopServiceConfigureDone.h"
#include "Core/Domain/Interfaces/IClearModuleDataService.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeModuleModeUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IOnConnectivityRestoredUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Application/Interfaces/IDiagnosticService.h"
#include "Application/Services/Devices/IDeviceService.h"
#include "Application/Services/Sensors/ISensorService.h"
#include "Application/Services/Rules/IRuleService.h"
#include "Core/Domain/Interfaces/BoardTesting/IBoardTest.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Interfaces/IBootEventLog.h"

class LoopServiceFactory : public ILoopServiceFactory {
private:
    ILedUseCases* ledUseCases;
    IButtonUseCases* buttonUseCases;
    IOnChangeModuleModeUseCase* moduleModeUseCase;
    IOnChangeLocalIpUseCase* onChangeLocalIpUseCase;
    IOnConnectivityRestoredUseCase* onConnectivityRestoredUseCase;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IWebServerLooper* webServer;
    IWebServerLooper* webTestModeServer;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    IMqttManager* mqttManager;
    IDiagnosticService* diagnosticService;
    IDeviceService* deviceService;
    ISensorService* sensorService;
    IRuleService* ruleService;
    IClearModuleDataService* clearModuleDataService;
    IBoardTest* boardTest;
    IOtaUpdateLog* otaUpdateLog = nullptr;
    IBootEventLog* bootEventLog = nullptr;
    IBootEventLog* restartEventLog = nullptr;
    BackgroundActBoard* backgroundActBoard = nullptr;
#ifdef GD_HEAP_FORENSICS
    RxFrameClassCounter* rxFrameClasses = nullptr;
#endif

public:
    LoopServiceFactory(
        ILedUseCases* ledUseCases = nullptr,
        IButtonUseCases* buttonUseCases = nullptr,
        IOnChangeModuleModeUseCase* moduleModeUseCase = nullptr,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        IWebServerLooper* webServer = nullptr,
        IModuleConfig* moduleConfig = nullptr,
        IArduinoAdapter* arduinoAdapter = nullptr,
        IOnChangeLocalIpUseCase* onChangeLocalIpUseCase = nullptr,
        IOnConnectivityRestoredUseCase* onConnectivityRestoredUseCase = nullptr,
        IMqttManager* mqttManager = nullptr,
        IDiagnosticService* diagnosticService = nullptr,
        IDeviceService* deviceService = nullptr,
        ISensorService* sensorService = nullptr,
        IRuleService* ruleService = nullptr,
        IWebServerLooper* webTestModeServer = nullptr,
        IClearModuleDataService* clearModuleDataService = nullptr,
        IBoardTest* boardTest = nullptr
    ) : ledUseCases(ledUseCases),
        buttonUseCases(buttonUseCases),
        moduleModeUseCase(moduleModeUseCase),
        logger(logger),
        debugSystem(debugSystem),
        webServer(webServer),
        moduleConfig(moduleConfig),
        arduinoAdapter(arduinoAdapter),
        onChangeLocalIpUseCase(onChangeLocalIpUseCase),
        onConnectivityRestoredUseCase(onConnectivityRestoredUseCase),
        mqttManager(mqttManager),
        diagnosticService(diagnosticService),
        deviceService(deviceService),
        sensorService(sensorService),
        ruleService(ruleService),
        webTestModeServer(webTestModeServer),
        clearModuleDataService(clearModuleDataService),
        boardTest(boardTest) {}
    
    ~LoopServiceFactory() override = default;

    std::unique_ptr<ILoopService> createLoopService(ModuleModeEnum moduleMode) override {
        std::unique_ptr<LoopServiceBase> service;
        
        switch (moduleMode) {
            case ModuleModeEnum::USER_CAN_CONFIGURE:
                service = std::unique_ptr<LoopServiceBase>(new LoopServiceCanConfigure());
                service->setWebServer(webServer);
                break;
            case ModuleModeEnum::USER_CAN_RE_CONFIGURE:
                service = std::unique_ptr<LoopServiceBase>(new LoopServiceReConfigure());
                service->setWebServer(webServer);
                break;
            case ModuleModeEnum::AP_TEST_MODE:
                service = std::unique_ptr<LoopServiceBase>(new LoopServiceTestMode());
                service->setWebServer(webTestModeServer);
                service->setBoardTest(boardTest);
                break;
            case ModuleModeEnum::CONFIGURED_DONE: {
                // Create a LoopServiceConfigureDone explicitly, then inject specialized dependencies
                LoopServiceConfigureDone* configDoneService = new LoopServiceConfigureDone();
                configDoneService->setOnChangeLocalIpUseCase(onChangeLocalIpUseCase);
                configDoneService->setOnConnectivityRestoredUseCase(onConnectivityRestoredUseCase);
                configDoneService->setMqttManager(mqttManager);
                configDoneService->setDiagnosticService(diagnosticService);
                configDoneService->setDeviceService(deviceService);
                configDoneService->setSensorService(sensorService);
                configDoneService->setRuleService(ruleService);
                configDoneService->setOutbox(outbox);
                configDoneService->setCommandService(commandService);
                configDoneService->setSyncSensorTypesUseCase(syncSensorTypesUseCase);
                configDoneService->setSensorTypeCatalogWriter(sensorTypeCatalogWriter);
                configDoneService->setRepublishModuleStructureUseCase(republishModuleStructureUseCase);
                configDoneService->setOtaUpdateLog(otaUpdateLog);
                configDoneService->setBootEventLog(bootEventLog);
                configDoneService->setRestartEventLog(restartEventLog);
                configDoneService->setConfirmRunningFirmwareUseCase(confirmRunningFirmwareUseCase);
                configDoneService->setWifiRoamUseCase(wifiRoamUseCase);
                configDoneService->setBootSequencer(bootSequencer);
                configDoneService->setLinkSupervisor(linkSupervisor);
                configDoneService->setPreventiveRestartUseCase(preventiveRestartUseCase);

                // Then assign to the service unique_ptr
                service = std::unique_ptr<LoopServiceBase>(configDoneService);
                break;
            }
            default:
                throw std::invalid_argument("Unknown module mode");
        }
        
        // Inject dependencies using individual setters
        service->setLedUseCases(ledUseCases);
        service->setButtonUseCases(buttonUseCases);
        service->setOnChangeModuleModeUseCase(moduleModeUseCase);
        service->setLogging(logger, debugSystem);
        service->setModuleConfig(moduleConfig);
        service->setArduinoAdapter(arduinoAdapter);
        service->setBackgroundActBoard(backgroundActBoard);
#ifdef GD_HEAP_FORENSICS
        service->setRxFrameClasses(rxFrameClasses);
#endif
        service->setClearModuleDataService(clearModuleDataService);
        
        return service;
    }
    
    // Setter for OnChangeLocalIpUseCase
    void setOnChangeLocalIpUseCase(IOnChangeLocalIpUseCase* useCase) {
        this->onChangeLocalIpUseCase = useCase;
    }

    void setOnConnectivityRestoredUseCase(IOnConnectivityRestoredUseCase* useCase) {
        this->onConnectivityRestoredUseCase = useCase;
    }

    // Setter for MqttManager
    void setMqttManager(IMqttManager* manager) {
        this->mqttManager = manager;
    }

    // Setter for DiagnosticService
    void setDiagnosticService(IDiagnosticService* service) {
        this->diagnosticService = service;
    }

    // Setter for DeviceService
    void setDeviceService(IDeviceService* service) {
        this->deviceService = service;
    }

    // Setter for SensorService
    void setSensorService(ISensorService* service) {
        this->sensorService = service;
    }

    // Setter for RuleService
    void setRuleService(IRuleService* service) {
        this->ruleService = service;
    }

    // Setter for WebTestModeServer
    void setWebTestModeServer(IWebServerLooper* server) {
        this->webTestModeServer = server;
    }
    
    // Setter for ClearModuleDataService
    void setClearModuleDataService(IClearModuleDataService* service) {
        this->clearModuleDataService = service;
    }

    void setOtaUpdateLog(IOtaUpdateLog* otaLog) {
        this->otaUpdateLog = otaLog;
    }

    /** The board every loop service reads when its heap watermark trace finds a new low. */
    void setBackgroundActBoard(BackgroundActBoard* board) {
        this->backgroundActBoard = board;
    }

#ifdef GD_HEAP_FORENSICS
    /** The frame counter every loop service copies into its account of a heap low. */
    void setRxFrameClasses(RxFrameClassCounter* counter) {
        this->rxFrameClasses = counter;
    }
#endif

    /** The outbox whose completions the CONFIGURED_DONE loop drains at the top of its iteration. */
    void setOutbox(IOutbox* outboxToUse) {
        this->outbox = outboxToUse;
    }

    /** The command service the CONFIGURED_DONE loop drives once per iteration. */
    void setCommandService(ICommandService* service) {
        this->commandService = service;
    }

    void setBootEventLog(IBootEventLog* bootLog) {
        this->bootEventLog = bootLog;
    }

    void setRestartEventLog(IBootEventLog* restartLog) {
        this->restartEventLog = restartLog;
    }

    /** The catalog sync the CONFIGURED_DONE loop hands SENSOR_TYPES_FETCH completions to. */
    void setSyncSensorTypesUseCase(ISyncSensorTypesUseCase* useCase) {
        this->syncSensorTypesUseCase = useCase;
    }

    /** The storage task the CONFIGURED_DONE loop collects finished catalog writes from. */
    void setSensorTypeCatalogWriter(ISensorTypeCatalogWriter* writer) {
        this->sensorTypeCatalogWriter = writer;
    }

    /** The structure republish the CONFIGURED_DONE loop hands MODULE_STRUCTURE completions to. */
    void setRepublishModuleStructureUseCase(IRepublishModuleStructureUseCase* useCase) {
        this->republishModuleStructureUseCase = useCase;
    }

    /** The confirmation the CONFIGURED_DONE loop reports its completed iterations to. */
    void setConfirmRunningFirmwareUseCase(IConfirmRunningFirmwareUseCase* useCase) {
        this->confirmRunningFirmwareUseCase = useCase;
    }

    /** The roaming decision the CONFIGURED_DONE loop drives once per iteration. */
    void setWifiRoamUseCase(IWifiRoamUseCase* useCase) {
        this->wifiRoamUseCase = useCase;
    }

    /** The boot sequence the CONFIGURED_DONE loop ticks before its producers. */
    void setBootSequencer(BootSequencer* sequencer) {
        this->bootSequencer = sequencer;
    }

    /** The link's supervision the CONFIGURED_DONE loop ticks every thirty seconds. */
    void setLinkSupervisor(LinkSupervisor* supervisor) {
        this->linkSupervisor = supervisor;
    }

    /** The nightly restart, checked on the same tick. */
    void setPreventiveRestartUseCase(PreventiveRestartUseCase* useCase) {
        this->preventiveRestartUseCase = useCase;
    }

private:
    IOutbox* outbox = nullptr;
    ICommandService* commandService = nullptr;
    ISyncSensorTypesUseCase* syncSensorTypesUseCase = nullptr;
    ISensorTypeCatalogWriter* sensorTypeCatalogWriter = nullptr;
    IRepublishModuleStructureUseCase* republishModuleStructureUseCase = nullptr;
    IConfirmRunningFirmwareUseCase* confirmRunningFirmwareUseCase = nullptr;
    IWifiRoamUseCase* wifiRoamUseCase = nullptr;
    BootSequencer* bootSequencer = nullptr;
    LinkSupervisor* linkSupervisor = nullptr;
    PreventiveRestartUseCase* preventiveRestartUseCase = nullptr;
};

#endif // LOOP_SERVICE_FACTORY_H
