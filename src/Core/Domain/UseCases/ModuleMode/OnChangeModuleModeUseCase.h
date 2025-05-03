#ifndef ON_CHANGE_MODULE_MODE_USE_CASE_H
#define ON_CHANGE_MODULE_MODE_USE_CASE_H

#include <cstdarg>
#include <cstdio>
#include "Core/Domain/Interfaces/UseCases/IOnChangeModuleModeUseCase.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/BootTypeEnum.h"

class OnChangeModuleModeUseCase : public IOnChangeModuleModeUseCase {
private:
    IArduinoAdapter& _arduinoAdapter;
    IModuleConfig& _moduleConfig;
    ILogger* logger;
    IDebugSystem* debugSystem;
    ModuleModeEnum _currentMode;
    
public:
    OnChangeModuleModeUseCase(IArduinoAdapter& arduinoAdapter, 
                      IModuleConfig& moduleConfig,
                      ILogger* loggerInstance = nullptr,
                      IDebugSystem* debugSystemInstance = nullptr)
        : _arduinoAdapter(arduinoAdapter), 
          _moduleConfig(moduleConfig),
          logger(loggerInstance),
          debugSystem(debugSystemInstance),
          _currentMode(CONFIGURED_DONE) {
        
        log("OnChangeModuleModeUseCase: Initialized with mode %d", static_cast<int>(_currentMode));
    }
    
    void loop() override {
        ModuleModeEnum requestedMode = _moduleConfig.getRequestedModuleMode();
        
        // Only process if the requested mode is different from current mode
        if (requestedMode != _currentMode) {
            log("OnChangeModuleModeUseCase: Mode transition from %d to %d", 
                static_cast<int>(_currentMode), static_cast<int>(requestedMode));
            
            // Handle mode transitions
            switch (requestedMode) {
                case CONFIGURED_NEED_RESTART:
                    log("OnChangeModuleModeUseCase: Preparing for restart");
                    // Save any necessary state before restarting
                    _moduleConfig.getSettings()->setBootType(BOOT_TYPE_USER);
                    
                    // Delay to ensure settings are saved
                    _arduinoAdapter.delay(100);
                    
                    // Restart the device
                    restart();
                    break;
                    
                // Add other mode transitions as needed
                case USER_CAN_CONFIGURE:
                case USER_CAN_RE_CONFIGURE:
                case AP_TEST_MODE:
                case CONFIGURED_DONE:
                    // Update current mode
                    _currentMode = requestedMode;
                    log("OnChangeModuleModeUseCase: Mode updated to %d", static_cast<int>(_currentMode));
                    break;
            }
        }
    }
    
private:
    void restart() {
        log("OnChangeModuleModeUseCase: Restarting device...");        
        _arduinoAdapter.delay(1000);
        _arduinoAdapter.restart();
    }
    
    void log(const char* format, ...) {
        if (logger != nullptr && debugSystem != nullptr && 
            debugSystem->isFeatureEnabled(DebugFeatures::MODULE_MODE_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(buffer);
        }
    }
};

#endif // ON_CHANGE_MODULE_MODE_USE_CASE_H
