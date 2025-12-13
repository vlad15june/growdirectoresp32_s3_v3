#ifndef LOOP_SERVICE_BASE_H
#define LOOP_SERVICE_BASE_H

#include "../../Interfaces/LoopServices/ILoopService.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeModuleModeUseCase.h"
#include "Application/Interfaces/IClearModuleDataService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/BoardTesting/IBoardTest.h"
#include "Application/Interfaces/WebServices/IWebServerLooper.h"
#include <stdarg.h>

class LoopServiceBase : public ILoopService {
protected:
    ILedUseCases* ledUseCases;
    IButtonUseCases* buttonUseCases;
    IOnChangeModuleModeUseCase* onChangeModuleModeUseCase;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IWebServerLooper* webServer;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    IBoardTest* boardTest;
    IClearModuleDataService* clearModuleDataService;
    
    LoopServiceBase(
        ILedUseCases* ledUseCases = nullptr,
        IButtonUseCases* buttonUseCases = nullptr,
        IOnChangeModuleModeUseCase* moduleModeUseCase = nullptr,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        IWebServerLooper* webServer = nullptr,
        IModuleConfig* moduleConfig = nullptr,
        IArduinoAdapter* arduinoAdapter = nullptr,
        IBoardTest* boardTest = nullptr,
        IClearModuleDataService* clearModuleDataService = nullptr
    ) : ledUseCases(ledUseCases),
        buttonUseCases(buttonUseCases),
        onChangeModuleModeUseCase(moduleModeUseCase),
        logger(logger),
        debugSystem(debugSystem),
        webServer(webServer),
        moduleConfig(moduleConfig),
        arduinoAdapter(arduinoAdapter),
        boardTest(boardTest),
        clearModuleDataService(clearModuleDataService) {}
    
    void log(const char* format, ...) {
        if (logger != nullptr && debugSystem != nullptr && 
            debugSystem->isFeatureEnabled(DebugFeatures::LOOP_SERVICE_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(DebugFeatures::LOOP_SERVICE_DEBUG, buffer);
        }
    }

public:
    virtual ~LoopServiceBase() = default;
    
    void loop() override {
        if (buttonUseCases) {
            buttonUseCases->loop();
        }
                
        if (onChangeModuleModeUseCase) {
            onChangeModuleModeUseCase->loop();
        }
        
        executeLoopImplementation();
        
        if (ledUseCases) {
            ledUseCases->loop();
        }
    }
    
    virtual void executeLoopImplementation() = 0;
    
    virtual void setLedUseCases(ILedUseCases* ledUC) {
        this->ledUseCases = ledUC;
    }
    
    virtual void setButtonUseCases(IButtonUseCases* buttonUC) {
        this->buttonUseCases = buttonUC;
    }
    
    virtual void setOnChangeModuleModeUseCase(IOnChangeModuleModeUseCase* modeUC) {
        this->onChangeModuleModeUseCase = modeUC;
    }
    
    // Method to set logging components
    virtual void setLogging(ILogger* logger, IDebugSystem* debugSystem) {
        this->logger = logger;
        this->debugSystem = debugSystem;
    }
    
    // Method to set web server
    virtual void setWebServer(IWebServerLooper* server) {
        this->webServer = server;
    }
    
    // Method to set module config
    virtual void setModuleConfig(IModuleConfig* config) {
        this->moduleConfig = config;
    }
    
    // Method to set Arduino adapter
    virtual void setArduinoAdapter(IArduinoAdapter* adapter) {
        this->arduinoAdapter = adapter;
    }
    
    // Method to set board test
    virtual void setBoardTest(IBoardTest* test) {
        this->boardTest = test;
    }
    
    // Method to set clear module data service
    virtual void setClearModuleDataService(IClearModuleDataService* service) {
        this->clearModuleDataService = service;
    }
};

#endif // LOOP_SERVICE_BASE_H
