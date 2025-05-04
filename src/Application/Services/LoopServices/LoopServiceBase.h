#ifndef LOOP_SERVICE_BASE_H
#define LOOP_SERVICE_BASE_H

#include "../../Interfaces/LoopServices/ILoopService.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeModuleModeUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
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
    
    LoopServiceBase(
        ILedUseCases* ledUseCases = nullptr,
        IButtonUseCases* buttonUseCases = nullptr,
        IOnChangeModuleModeUseCase* moduleModeUseCase = nullptr,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        IWebServerLooper* webServer = nullptr,
        IModuleConfig* moduleConfig = nullptr,
        IArduinoAdapter* arduinoAdapter = nullptr
    ) : ledUseCases(ledUseCases),
        buttonUseCases(buttonUseCases),
        onChangeModuleModeUseCase(moduleModeUseCase),
        logger(logger),
        debugSystem(debugSystem),
        webServer(webServer),
        moduleConfig(moduleConfig),
        arduinoAdapter(arduinoAdapter) {}
    
    void log(const char* format, ...) {
        if (logger != nullptr && debugSystem != nullptr && 
            debugSystem->isFeatureEnabled(DebugFeatures::LOOP_SERVICE_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(buffer);
        }
    }

public:
    virtual ~LoopServiceBase() = default;
    
    // Common loop implementation that will be called by derived classes
    void loop() override {
        // Check if button was pressed when on and then held for duration
        if (buttonUseCases && onChangeModuleModeUseCase && moduleConfig) {
            if (buttonUseCases->isButtonPressedWhenOnThenHeldForDuration()) {
                log("Button was pressed when on and held for duration - switching to TEST mode");
                // Request test mode by setting the module config
                moduleConfig->setRequestedModuleMode(ModuleModeEnum::AP_TEST_MODE);
            }
        }
                
        // Check module mode changes
        if (onChangeModuleModeUseCase) {
            onChangeModuleModeUseCase->loop();
        }
        
        // Call specific implementation for this mode
        executeLoopImplementation();
        
        // Handle LED updates
        if (ledUseCases) {
            ledUseCases->loop();
        }
    }
    
    // This is what derived classes will implement
    virtual void executeLoopImplementation() = 0;
    
    // Method to set required use cases after construction (if needed)
    virtual void setUseCases(
        ILedUseCases* ledUC,
        IButtonUseCases* buttonUC,
        IOnChangeModuleModeUseCase* modeUC) {
        this->ledUseCases = ledUC;
        this->buttonUseCases = buttonUC;
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
};

#endif // LOOP_SERVICE_BASE_H
