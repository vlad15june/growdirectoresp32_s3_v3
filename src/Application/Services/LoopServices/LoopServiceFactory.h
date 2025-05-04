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
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include <stdexcept>

class LoopServiceFactory : public ILoopServiceFactory {
private:
    ILedUseCases* ledUseCases;
    IButtonUseCases* buttonUseCases;
    IOnChangeModuleModeUseCase* moduleModeUseCase;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IWebServerLooper* webServer;
    IModuleConfig* moduleConfig;

public:
    LoopServiceFactory(
        ILedUseCases* ledUseCases = nullptr,
        IButtonUseCases* buttonUseCases = nullptr,
        IOnChangeModuleModeUseCase* moduleModeUseCase = nullptr,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        IWebServerLooper* webServer = nullptr,
        IModuleConfig* moduleConfig = nullptr
    ) : ledUseCases(ledUseCases),
        buttonUseCases(buttonUseCases),
        moduleModeUseCase(moduleModeUseCase),
        logger(logger),
        debugSystem(debugSystem),
        webServer(webServer),
        moduleConfig(moduleConfig) {}
    
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
            case ModuleModeEnum::CONFIGURED_DONE:
                service = std::unique_ptr<LoopServiceBase>(new LoopServiceConfigureDone());
                break;
            default:
                throw std::invalid_argument("Unknown module mode");
        }
        
        // Inject use cases
        service->setUseCases(ledUseCases, buttonUseCases, moduleModeUseCase);
        service->setLogging(logger, debugSystem);
        service->setWebServer(webServer);
        service->setModuleConfig(moduleConfig);
        
        return service;
    }
};

#endif // LOOP_SERVICE_FACTORY_H
