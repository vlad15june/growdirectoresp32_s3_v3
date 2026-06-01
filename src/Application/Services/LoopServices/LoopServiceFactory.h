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

class LoopServiceFactory : public ILoopServiceFactory {
private:
    ILedUseCases* ledUseCases;
    IButtonUseCases* buttonUseCases;
    IOnChangeModuleModeUseCase* moduleModeUseCase;
    IOnChangeLocalIpUseCase* onChangeLocalIpUseCase;
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
                configDoneService->setMqttManager(mqttManager);
                configDoneService->setDiagnosticService(diagnosticService);
                configDoneService->setDeviceService(deviceService);
                configDoneService->setSensorService(sensorService);
                configDoneService->setRuleService(ruleService);
                
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
        service->setClearModuleDataService(clearModuleDataService);
        
        return service;
    }
    
    // Setter for OnChangeLocalIpUseCase
    void setOnChangeLocalIpUseCase(IOnChangeLocalIpUseCase* useCase) {
        this->onChangeLocalIpUseCase = useCase;
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
};

#endif // LOOP_SERVICE_FACTORY_H
