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
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h" //TODO: move to Core/Domain/Interfaces/Adapters/Time/INTPTimeAdapter.h
#include "constants.h"

class OnChangeModuleModeUseCase : public IOnChangeModuleModeUseCase
{
private:
    IArduinoAdapter &_arduinoAdapter;
    IModuleConfig &_moduleConfig;
    ILogger &_logger;
    IDebugSystem &_debugSystem;
    INTPTimeAdapter *_timeAdapter;

public:
    OnChangeModuleModeUseCase(
        IArduinoAdapter &arduinoAdapter,
        IModuleConfig &moduleConfig,
        ILogger &loggerInstance,
        IDebugSystem &debugSystemInstance,
        INTPTimeAdapter *timeAdapter = nullptr)
        : _arduinoAdapter(arduinoAdapter),
          _moduleConfig(moduleConfig),
          _logger(loggerInstance),
          _debugSystem(debugSystemInstance),
          _timeAdapter(timeAdapter)
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
                {
                    // Boot types deliberately set by the caller must survive this restart so the
                    // next boot can report the real reboot reason (TECHNICAL - diagnostic,
                    // FIRMWARE_UPDATE - OTA, SENSOR_CHNG - sensor configuration change).
                    uint8_t currentBootType = _moduleConfig.getSettings()->getBootType();
                    if (currentBootType != BOOT_TYPE_TECHNICAL &&
                        currentBootType != BOOT_TYPE_FIRMWARE_UPDATE &&
                        currentBootType != BOOT_TYPE_SENSOR_CHNG)
                    {
                        _moduleConfig.getSettings()->setBootType(BOOT_TYPE_USER);
                        log("OnChangeModuleModeUseCase: Set boot type to USER");
                    }
                    else
                    {
                        log("OnChangeModuleModeUseCase: Keeping boot type %d (set by caller)", currentBootType);
                    }
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
        // Every restart the module decides on itself arrives here. The board has no
        // battery-backed RTC, so the next boot starts its clock from the time in EEPROM until a
        // server answers - and this is the last moment that value can be made current.
        if (_timeAdapter != nullptr)
        {
            _timeAdapter->saveTimeToStorage();
        }

        uint8_t bootType = _moduleConfig.getSettings()->getBootType();
        _arduinoAdapter.delay(1000);
        if (bootType == BOOT_TYPE_TECHNICAL)
        {
            log("OnChangeModuleModeUseCase: hard_reset bootType=%d sleepMs=%d", bootType, HARD_RESET_SLEEP_MS);
            _arduinoAdapter.hardReset();
        }
        else
        {
            log("OnChangeModuleModeUseCase: soft_reset bootType=%d", bootType);
            _arduinoAdapter.restart();
        }
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
