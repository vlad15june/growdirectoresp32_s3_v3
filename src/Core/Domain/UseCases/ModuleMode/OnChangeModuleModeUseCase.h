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

class OnChangeModuleModeUseCase : public IOnChangeModuleModeUseCase
{
private:
    IArduinoAdapter &_arduinoAdapter;
    IModuleConfig &_moduleConfig;
    ILogger &_logger;
    IDebugSystem &_debugSystem;

public:
    OnChangeModuleModeUseCase(
        IArduinoAdapter &arduinoAdapter,
        IModuleConfig &moduleConfig,
        ILogger &loggerInstance,
        IDebugSystem &debugSystemInstance)
        : _arduinoAdapter(arduinoAdapter),
          _moduleConfig(moduleConfig),
          _logger(loggerInstance),
          _debugSystem(debugSystemInstance)
    {
    }

    void loop() override
    {
        ModuleModeEnum requestedMode = _moduleConfig.getRequestedModuleMode();
        ModuleModeEnum currentMode = _moduleConfig.getCurrentModuleMode();

        if (requestedMode != currentMode)
        {
            log("OnChangeModuleModeUseCase: Mode transition from %d to %d",
                static_cast<int>(currentMode), static_cast<int>(requestedMode));

            switch (requestedMode)
            {
            case CONFIGURED_NEED_RESTART:
                log("OnChangeModuleModeUseCase: NEED_RESTART detected - immediate restart without saving mode");
                if (_moduleConfig.getSettings()->getBootType() != BOOT_TYPE_TECHNICAL)
                {
                    _moduleConfig.getSettings()->setBootType(BOOT_TYPE_USER);
                    log("OnChangeModuleModeUseCase: Set boot type to USER");
                }
                else
                {
                    log("OnChangeModuleModeUseCase: Keeping boot type TECHNICAL (set by diagnostic)");
                }
                _arduinoAdapter.delay(100);
                restart();
                break;

            case USER_CAN_CONFIGURE:
            case USER_CAN_RE_CONFIGURE:
            case AP_TEST_MODE:
            case CONFIGURED_DONE:
                _moduleConfig.getSettings()->setConfigured(requestedMode);
                _moduleConfig.setCurrentModuleMode(requestedMode);
                log("OnChangeModuleModeUseCase: Saved requested mode %d to settings",
                    static_cast<int>(requestedMode));
                _moduleConfig.getSettings()->setBootType(BOOT_TYPE_USER);
                log("OnChangeModuleModeUseCase: Set boot type to USER");
                _arduinoAdapter.delay(100);
                log("OnChangeModuleModeUseCase: Mode updated to %d, restarting...",
                    static_cast<int>(requestedMode));
                restart();
                break;
            }
        }
    }

private:
    void restart()
    {
        log("OnChangeModuleModeUseCase: Restarting device...");
        _arduinoAdapter.delay(1000);
        _arduinoAdapter.restart();
    }

    void log(const char *format, ...)
    {
        if (_debugSystem.isFeatureEnabled(DebugFeatures::MODULE_MODE_DEBUG))
        {
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
