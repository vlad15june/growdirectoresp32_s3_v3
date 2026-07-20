#ifndef PREVENTIVE_RESTART_USE_CASE_H
#define PREVENTIVE_RESTART_USE_CASE_H

#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Interfaces/UseCases/Rules/IEvaluateRulesUseCase.h"
#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IBootEventLog.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include <cstdarg>
#include <cstdio>

inline bool isDeviceOffForPreventiveRestart(DeviceStateEnum state) {
    return state == DEVICE_STATE_OFF ||
           state == DEVICE_STATE_OFF_BY_POWER_ON ||
           state == DEVICE_STATE_DELAY_OFF ||
           state == DEVICE_STATE_STEP_WAITING;
}

class PreventiveRestartUseCase {
private:
    IModuleConfig* _moduleConfig;
    IDeviceModel* _deviceModel;
    IEvaluateRulesUseCase* _evaluateRulesUseCase;
    IFirmwareRemoteUpdate* _firmwareRemoteUpdate;
    INTPTimeAdapter* _timeAdapter;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IBootEventLog* _restartEventLog;

    static constexpr int PREVENTIVE_RESTART_HOUR = 2;
    static constexpr int PREVENTIVE_RESTART_LOOKAHEAD_MIN = 5;

    void log(const char* format, ...) {
        if (!_logger || !_debugSystem || !_debugSystem->isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG)) {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _logger->println(DebugFeatures::DIAGNOSTIC_DEBUG, buffer);
    }

    bool isModuleFullyConfigured() {
        return _moduleConfig && _moduleConfig->getRequestedModuleMode() == CONFIGURED_DONE;
    }

    bool isOtaInProgress() {
        return _firmwareRemoteUpdate && _firmwareRemoteUpdate->isUpdateInProgress();
    }

    bool isInsideRestartWindow(const struct tm& localTime) {
        // Default: legacy preventive restart window 02:00–02:59 local time.
        return localTime.tm_hour == PREVENTIVE_RESTART_HOUR;

        // Alternative: restart only at exact minute, e.g. 17:20.
        // Uncomment the line below and comment out the default return if needed.
        // return localTime.tm_hour == 17 && localTime.tm_min == 20;
    }

    bool wasAlreadyRestartedToday(const struct tm& localTime) {
        ISettings* settings = _moduleConfig ? _moduleConfig->getSettings() : nullptr;
        if (!settings) {
            return false;
        }
        uint8_t lastDay = settings->getLastPreventiveRestartDay();
        return lastDay == static_cast<uint8_t>(localTime.tm_mday);
    }

    bool areAllDevicesOff() {
        if (!_deviceModel) {
            return true; // no devices -> safe to restart
        }
        for (Device* device : _deviceModel->getAllDevices()) {
            if (!device) {
                continue;
            }
            if (!isDeviceOffForPreventiveRestart(device->state)) {
                return false;
            }
        }
        return true;
    }

    bool willAnyRuleStartSoon() {
        return _evaluateRulesUseCase &&
               _evaluateRulesUseCase->willAnyRuleTurnDeviceOnWithinMinutes(PREVENTIVE_RESTART_LOOKAHEAD_MIN);
    }

    void writeRestartLogEntry(const char* message) {
        if (_restartEventLog) {
            _restartEventLog->write(message);
        }
    }

    void markRestartedToday(const struct tm& localTime) {
        ISettings* settings = _moduleConfig ? _moduleConfig->getSettings() : nullptr;
        if (settings) {
            settings->setLastPreventiveRestartDay(static_cast<uint8_t>(localTime.tm_mday));
        }
    }

    void triggerRestart() {
        ISettings* settings = _moduleConfig ? _moduleConfig->getSettings() : nullptr;
        if (!settings || !_moduleConfig) {
            log("PREVENTIVE_RESTART: cannot trigger restart, settings or module config missing");
            return;
        }

        settings->setBootType(BOOT_TYPE_TECHNICAL);
        _moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
    }

public:
    PreventiveRestartUseCase(
        IModuleConfig* moduleConfig,
        IDeviceModel* deviceModel,
        IEvaluateRulesUseCase* evaluateRulesUseCase,
        IFirmwareRemoteUpdate* firmwareRemoteUpdate,
        INTPTimeAdapter* timeAdapter,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        IBootEventLog* restartEventLog = nullptr)
        : _moduleConfig(moduleConfig),
          _deviceModel(deviceModel),
          _evaluateRulesUseCase(evaluateRulesUseCase),
          _firmwareRemoteUpdate(firmwareRemoteUpdate),
          _timeAdapter(timeAdapter),
          _logger(logger),
          _debugSystem(debugSystem),
          _restartEventLog(restartEventLog) {}

    void perform() {
        if (!_moduleConfig || !_timeAdapter) {
            return;
        }

        if (isOtaInProgress()) {
            log("PREVENTIVE_RESTART: skipped, OTA in progress");
            return;
        }

        if (!isModuleFullyConfigured()) {
            log("PREVENTIVE_RESTART: skipped, module not in CONFIGURED_DONE");
            return;
        }

        struct tm localTime;
        if (!_timeAdapter->getLocalTime(&localTime)) {
            log("PREVENTIVE_RESTART: skipped, local time not available");
            return;
        }

        if (!isInsideRestartWindow(localTime)) {
            return;
        }

        if (wasAlreadyRestartedToday(localTime)) {
            log("PREVENTIVE_RESTART: skipped, already restarted today (day=%d)", localTime.tm_mday);
            return;
        }

        if (!areAllDevicesOff()) {
            log("PREVENTIVE_RESTART: skipped, at least one device is ON");
            return;
        }

        if (willAnyRuleStartSoon()) {
            log("PREVENTIVE_RESTART: skipped, a rule will turn a device ON within %d min",
                PREVENTIVE_RESTART_LOOKAHEAD_MIN);
            return;
        }

        log("PREVENTIVE_RESTART: conditions met, requesting technical restart at %02d:%02d",
            localTime.tm_hour, localTime.tm_min);

        char record[160];
        snprintf(record, sizeof(record),
                 "PREVENTIVE_RESTART: preventive restart at 02:%02d - all devices off and no rules will start",
                 localTime.tm_min);
        writeRestartLogEntry(record);

        markRestartedToday(localTime);
        triggerRestart();
    }
};

#endif // PREVENTIVE_RESTART_USE_CASE_H
