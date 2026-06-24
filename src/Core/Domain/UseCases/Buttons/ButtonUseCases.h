#ifndef BUTTON_USE_CASES_H
#define BUTTON_USE_CASES_H

#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Interfaces/IClearModuleDataService.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/IBootEventLog.h"
#include <cstdarg>
#include <cstdio>

class ButtonUseCases : public IButtonUseCases {
private:
    IButtonAdapter* buttonAdapter;
    ILogger *logger;
    IDebugSystem *debugSystem;
    ISettings *settings;
    IModuleConfig* moduleConfig;
    IClearModuleDataService* clearModuleDataService;
    IArduinoAdapter* arduinoAdapter;
    IBootEventLog* bootEventLog;
    const uint8_t buttonIndex;
    bool isSystemOn;
    bool buttonPressedState;
    bool buttonWasPressedBeforeReboot;
    bool lastLoggedPressed;
    unsigned long lastPressTime;
    const unsigned long durationMs = 10000;  // 10 seconds for test mode (changed from 15)
    const unsigned long factoryResetDurationMs = 10000;  // 10 seconds minimum for factory reset
    const unsigned long addModeDurationMs = 10000;  // 10 seconds minimum for add mode

    void publishChannel(const char *buffer)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::BUTTON_DEBUG))
        {
            IDebugFeature *feature = debugSystem->getFeature(DebugFeatures::BUTTON_DEBUG);
            MqttLoggerMode channel = feature ? feature->getLogChannel() : MqttLoggerMode::MqttAndSerial;
            logger->println(DebugFeatures::BUTTON_DEBUG, buffer, channel);
        }
    }

    void log(const char *format, ...)
    {
        char buffer[160];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        publishChannel(buffer);
    }

    void logBoot(const char *format, ...)
    {
        char buffer[160];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        if (bootEventLog != nullptr)
        {
            bootEventLog->write(buffer);
        }

        publishChannel(buffer);
    }

public:
    ButtonUseCases(IButtonAdapter* adapter, ILogger* loggerInstance = nullptr, 
                  IDebugSystem* debugSystemInstance = nullptr, uint8_t index = 0,
                  ISettings* settingsInstance = nullptr, IModuleConfig* moduleConfigInstance = nullptr,
                  IClearModuleDataService* clearModuleDataServiceInstance = nullptr,
                  IArduinoAdapter* arduinoAdapterInstance = nullptr,
                  IBootEventLog* bootEventLogInstance = nullptr)
        : buttonAdapter(adapter), logger(loggerInstance), debugSystem(debugSystemInstance),
          settings(settingsInstance), moduleConfig(moduleConfigInstance),
          clearModuleDataService(clearModuleDataServiceInstance), arduinoAdapter(arduinoAdapterInstance),
          bootEventLog(bootEventLogInstance),
          buttonIndex(index), isSystemOn(false),
          buttonPressedState(false), buttonWasPressedBeforeReboot(false),
          lastLoggedPressed(false), lastPressTime(0) {
        if (settings != nullptr) {
            buttonWasPressedBeforeReboot = settings->getButtonPressedBeforeReboot() != 0;
        }
    }
    
    ~ButtonUseCases() override = default;
    
    bool checkButtonPressedWhenOn() override {
        bool pressed = buttonAdapter->isButtonPressed(buttonIndex);
        if (pressed) {
            buttonPressedState = true;
            if (settings != nullptr) {
                settings->setButtonPressedBeforeReboot(1);
            }
            logBoot("BUTTON: pressed at power-on (idx %d, beforeReboot=%d) - saved flag",
                buttonIndex, buttonWasPressedBeforeReboot ? 1 : 0);
        } else {
            if (settings != nullptr && !buttonWasPressedBeforeReboot) {
                settings->setButtonPressedBeforeReboot(0);
            }
            logBoot("BUTTON: not pressed at power-on (idx %d, beforeReboot=%d)",
                buttonIndex, buttonWasPressedBeforeReboot ? 1 : 0);
        }
        lastLoggedPressed = pressed;
        return buttonPressedState;
    }
    
    void checkStartupFactoryReset() override {
        if (!buttonPressedState) {
            return;
        }
        if (buttonWasPressedBeforeReboot) {
            return;
        }
        if (buttonAdapter == nullptr || arduinoAdapter == nullptr) {
            return;
        }

        unsigned long start = arduinoAdapter->millis();
        while (arduinoAdapter->millis() - start < factoryResetDurationMs) {
            buttonAdapter->updateButtonStates();
            if (!buttonAdapter->isButtonPressed(buttonIndex)) {
                logBoot("BUTTON: startup factory reset aborted - released before %lu ms", factoryResetDurationMs);
                return;
            }
            arduinoAdapter->feedTaskWatchdog();
            arduinoAdapter->delay(50);
        }

        logBoot("BUTTON: factory reset at startup (held %lu ms) - USER_CAN_CONFIGURE", factoryResetDurationMs);
        if (clearModuleDataService) {
            clearModuleDataService->clearAllData();
            logBoot("BUTTON: module data cleared");
        }
        if (moduleConfig) {
            moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_CONFIGURE);
        }
        arduinoAdapter->restart();
    }

    bool isButtonPressedWhenOn() override {
        return buttonPressedState;
    }
    
    bool isButtonHeldForDuration() override {
        return buttonAdapter->wasButtonHeld(buttonIndex, durationMs);
    }

    bool isTestModeRequested() override {
        if (!buttonWasPressedBeforeReboot) {
            return false;
        }

        if (!buttonPressedState) {
            if (settings != nullptr) {
                settings->setButtonPressedBeforeReboot(0);
            }
            return false;
        }

        bool held = buttonAdapter->wasButtonHeld(buttonIndex, durationMs);

        if (held && settings != nullptr) {
            settings->setButtonPressedBeforeReboot(0);
        }

        return held;
    }

    bool isFactoryResetRequested() override {
        if (buttonWasPressedBeforeReboot) {
            return false;
        }

        if (!buttonPressedState) {
            return false;
        }

        return buttonAdapter->wasButtonHeld(buttonIndex, factoryResetDurationMs);
    }

    bool isAddModeRequested() override {
        return buttonAdapter->wasButtonHeld(buttonIndex, addModeDurationMs);
    }
    
    void clearSavedButtonState() override {
        if (settings != nullptr) {
            settings->setButtonPressedBeforeReboot(0);
            log("BUTTON: saved state cleared (idx %d)", buttonIndex);
        }
        buttonWasPressedBeforeReboot = false;
    }
    
    void loop() override {
        if (!moduleConfig) {
            return;
        }

        bool nowPressed = buttonAdapter->isButtonPressed(buttonIndex);
        if (nowPressed != lastLoggedPressed) {
            log("BUTTON: %s (idx %d)", nowPressed ? "pressed" : "released", buttonIndex);
            lastLoggedPressed = nowPressed;
        }

        if (isTestModeRequested()) {
            logBoot("BUTTON: test mode requested (held %lu ms) - switching to AP_TEST_MODE", durationMs);
            moduleConfig->setRequestedModuleMode(ModuleModeEnum::AP_TEST_MODE);
        }
        else if (isFactoryResetRequested()) {
            logBoot("BUTTON: factory reset requested (held %lu ms) - clearing data, USER_CAN_CONFIGURE", factoryResetDurationMs);
            if (clearModuleDataService) {
                clearModuleDataService->clearAllData();
                logBoot("BUTTON: module data cleared");
            }
            moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_CONFIGURE);
            if (arduinoAdapter) {
                arduinoAdapter->restart();
            }
        }
        else if (isAddModeRequested()) {
            log("BUTTON: add mode requested (held %lu ms) - switching to USER_CAN_RE_CONFIGURE", addModeDurationMs);
            moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
        }
    }
};

#endif // BUTTON_USE_CASES_H
