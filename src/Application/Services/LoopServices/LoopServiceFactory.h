#ifndef LOOP_SERVICE_FACTORY_H
#define LOOP_SERVICE_FACTORY_H

#include "../../Interfaces/LoopServices/ILoopServiceFactory.h"
#include "LoopServiceCanConfigure.h"
#include "LoopServiceReConfigure.h"
#include "LoopServiceTestMode.h"
#include "LoopServiceConfigureDone.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeModuleModeUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include <stdexcept>

class LoopServiceFactory : public ILoopServiceFactory {
private:
    ILedUseCases* ledUseCases;
    IButtonUseCases* buttonUseCases;
    IOnChangeModuleModeUseCase* moduleModeUseCase;
    IOnChangeLocalIpUseCase* onChangeLocalIpUseCase;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IWebServerLooper* webServer;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    IMqttManager* mqttManager;

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
        IMqttManager* mqttManager = nullptr
    ) : ledUseCases(ledUseCases),
        buttonUseCases(buttonUseCases),
        moduleModeUseCase(moduleModeUseCase),
        logger(logger),
        debugSystem(debugSystem),
        webServer(webServer),
        moduleConfig(moduleConfig),
        arduinoAdapter(arduinoAdapter),
        onChangeLocalIpUseCase(onChangeLocalIpUseCase),
        mqttManager(mqttManager) {}
    
    ~LoopServiceFactory() override = default;

    std::unique_ptr<ILoopService> createLoopService(ModuleModeEnum moduleMode) override {
        std::unique_ptr<LoopServiceBase> service;
        
        switch (moduleMode) {
            case ModuleModeEnum::USER_CAN_CONFIGURE:
                service = std::unique_ptr<LoopServiceBase>(new LoopServiceCanConfigure());
                break;
            case ModuleModeEnum::USER_CAN_RE_CONFIGURE:
                service = std::unique_ptr<LoopServiceBase>(new LoopServiceReConfigure());
                break;
            case ModuleModeEnum::AP_TEST_MODE:
                service = std::unique_ptr<LoopServiceBase>(new LoopServiceTestMode());
                break;
            case ModuleModeEnum::CONFIGURED_DONE: {
                // Create a LoopServiceConfigureDone explicitly, then inject specialized dependencies
                LoopServiceConfigureDone* configDoneService = new LoopServiceConfigureDone();
                configDoneService->setOnChangeLocalIpUseCase(onChangeLocalIpUseCase);
                configDoneService->setMqttManager(mqttManager);
                
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
        service->setWebServer(webServer);
        service->setModuleConfig(moduleConfig);
        service->setArduinoAdapter(arduinoAdapter);
        
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
};

#endif // LOOP_SERVICE_FACTORY_H
