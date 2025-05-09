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
    ILogger& _logger;
    IDebugSystem& _debugSystem;
    ModuleModeEnum _currentMode;
    
public:
    OnChangeModuleModeUseCase(
        IArduinoAdapter& arduinoAdapter, 
        IModuleConfig& moduleConfig,
        ILogger& loggerInstance,
        IDebugSystem& debugSystemInstance)
        : _arduinoAdapter(arduinoAdapter), 
          _moduleConfig(moduleConfig),
          _logger(loggerInstance),
          _debugSystem(debugSystemInstance),
          _currentMode(CONFIGURED_DONE) {
        
        log("OnChangeModuleModeUseCase: Initialized with mode %d", static_cast<int>(_currentMode));
    }
    
    void loop() override {
        ModuleModeEnum requestedMode = _moduleConfig.getRequestedModuleMode();      
        _currentMode = static_cast<ModuleModeEnum>(_moduleConfig.getSettings()->getIfConfigured()); //TODO: move   _currentMode to _moduleConfig.getCurrentModuleMode();
        
        // Only process if the requested mode is different from current mode
        if (requestedMode != _currentMode) {
            log("OnChangeModuleModeUseCase: Mode transition from %d to %d", 
                static_cast<int>(_currentMode), static_cast<int>(requestedMode));
            
            // Handle mode transitions
            switch (requestedMode) {
                case CONFIGURED_NEED_RESTART:
                    // For restart mode, we don't save the mode - just restart
                    log("OnChangeModuleModeUseCase: NEED_RESTART detected - immediate restart without saving mode");
                    
                    // Set boot type before restart
                    _moduleConfig.getSettings()->setBootType(BOOT_TYPE_USER);
                    log("OnChangeModuleModeUseCase: Set boot type to USER");
                    
                    // Delay to ensure settings are saved
                    _arduinoAdapter.delay(100);
                    
                    // Restart immediately
                    restart();
                    break;
                    
                case USER_CAN_CONFIGURE:
                case USER_CAN_RE_CONFIGURE:
                case AP_TEST_MODE:
                case CONFIGURED_DONE:
                    // Save the requested mode to settings for other mode changes
                    _moduleConfig.getSettings()->setConfigured(requestedMode);
                    log("OnChangeModuleModeUseCase: Saved requested mode %d to settings", 
                        static_cast<int>(requestedMode));
                    
                    // Set boot type before restart
                    _moduleConfig.getSettings()->setBootType(BOOT_TYPE_USER);
                    log("OnChangeModuleModeUseCase: Set boot type to USER");
                    
                    // Delay to ensure settings are saved
                    _arduinoAdapter.delay(100);
                    
                    // Update current mode and restart for all mode transitions
                    _currentMode = requestedMode;
                    log("OnChangeModuleModeUseCase: Mode updated to %d, restarting...", 
                        static_cast<int>(_currentMode));
                    restart();
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
        if (_debugSystem.isFeatureEnabled(DebugFeatures::MODULE_MODE_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            _logger.println(buffer);
        }
    }
};

#endif // ON_CHANGE_MODULE_MODE_USE_CASE_H
