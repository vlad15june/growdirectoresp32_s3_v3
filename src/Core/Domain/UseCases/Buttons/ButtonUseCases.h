#ifndef BUTTON_USE_CASES_H
#define BUTTON_USE_CASES_H

#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Application/Interfaces/IClearModuleDataService.h"

class ButtonUseCases : public IButtonUseCases {
private:
    IButtonAdapter* buttonAdapter;
    ILogger *logger;
    IDebugSystem *debugSystem;
    ISettings *settings;
    IModuleConfig* moduleConfig;
    IClearModuleDataService* clearModuleDataService;
    const uint8_t buttonIndex;
    bool isSystemOn;
    bool buttonPressedState;
    bool buttonWasPressedBeforeReboot;
    unsigned long lastPressTime;
    const unsigned long durationMs = 10000;  // 10 seconds for test mode (changed from 15)
    const unsigned long factoryResetDurationMs = 10000;  // 10 seconds minimum for factory reset
    const unsigned long addModeDurationMs = 10000;  // 10 seconds minimum for add mode

    void log(const char *format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::BUTTON_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(DebugFeatures::BUTTON_DEBUG, buffer);
        }
    }

public:
    ButtonUseCases(IButtonAdapter* adapter, ILogger* loggerInstance = nullptr, 
                  IDebugSystem* debugSystemInstance = nullptr, uint8_t index = 0,
                  ISettings* settingsInstance = nullptr, IModuleConfig* moduleConfigInstance = nullptr,
                  IClearModuleDataService* clearModuleDataServiceInstance = nullptr)
        : buttonAdapter(adapter), logger(loggerInstance), debugSystem(debugSystemInstance), 
          settings(settingsInstance), moduleConfig(moduleConfigInstance), 
          clearModuleDataService(clearModuleDataServiceInstance), buttonIndex(index), isSystemOn(false), 
          buttonPressedState(false), buttonWasPressedBeforeReboot(false), lastPressTime(0) {
        if (settings != nullptr) {
            buttonWasPressedBeforeReboot = settings->getButtonPressedBeforeReboot() != 0;
            log("Button %d state before reboot: %s", buttonIndex, buttonWasPressedBeforeReboot ? "pressed" : "not pressed");
        }
    }
    
    ~ButtonUseCases() override = default;
    
    bool checkButtonPressedWhenOn() override {
        bool pressed = buttonAdapter->isButtonPressed(buttonIndex);
        if (pressed) {
            buttonPressedState = true;
            if (settings != nullptr) {
                settings->setButtonPressedBeforeReboot(1);
                log("Button %d state saved to settings for potential reboot", buttonIndex);
            }
            log("Button %d is pressed when system is on", buttonIndex);
        } else {
            if (settings != nullptr && !buttonWasPressedBeforeReboot) {
                settings->setButtonPressedBeforeReboot(0);
                log("Button %d normal boot - clearing any stale saved state", buttonIndex);
            }
        }
        log("Button %d pressed state: %s", buttonIndex, buttonPressedState ? "true" : "false");
        return buttonPressedState;
    }
    
    bool isButtonPressedWhenOn() override {
        return buttonPressedState;
    }
    
    bool isButtonHeldForDuration() override {
        bool held = buttonAdapter->wasButtonHeld(buttonIndex, durationMs);
        log("Button %d held for %lu ms: %s", buttonIndex, durationMs, held ? "true" : "false");
        return held;
    }
    
    bool isTestModeRequested() override {
        if (!buttonWasPressedBeforeReboot) {
            log("Button %d wasn't pressed before reboot - test mode not possible", buttonIndex);
            return false;
        }
        
        if (!buttonPressedState) {
            log("Button %d not pressed at startup - test mode not possible", buttonIndex);
            if (settings != nullptr) {
                settings->setButtonPressedBeforeReboot(0);
            }
            return false;
        }
        
        bool held = buttonAdapter->wasButtonHeld(buttonIndex, durationMs);
        
        if (held) {
            log("Button %d TEST MODE activated (pressed before reboot, held at startup for %lu ms)", 
                buttonIndex, durationMs);
            if (settings != nullptr) {
                settings->setButtonPressedBeforeReboot(0);
            }
        } else {
            log("Button %d held time insufficient for test mode", buttonIndex);
        }
        
        return held;
    }
    
    bool isFactoryResetRequested() override {
        if (buttonWasPressedBeforeReboot) {
            log("Button %d was pressed before reboot - this is test mode, not factory reset", buttonIndex);
            return false;
        }
        
        if (!buttonPressedState) {
            log("Button %d wasn't pressed during power on - factory reset not possible", buttonIndex);
            return false;
        }
        
        bool held = buttonAdapter->wasButtonHeld(buttonIndex, factoryResetDurationMs);
        log("Button %d factory reset requested (pressed at power on, held for %lu ms): %s", 
            buttonIndex, factoryResetDurationMs, held ? "true" : "false");
        return held;
    }
    
    bool isAddModeRequested() override {
        bool held = buttonAdapter->wasButtonHeld(buttonIndex, addModeDurationMs);
        log("Button %d add mode requested (held for %lu ms): %s", 
            buttonIndex, addModeDurationMs, held ? "true" : "false");
        return held;
    }
    
    void clearSavedButtonState() override {
        if (settings != nullptr) {
            settings->setButtonPressedBeforeReboot(0);
            log("Button %d saved state cleared", buttonIndex);
        }
        buttonWasPressedBeforeReboot = false;
    }
    
    void loop() override {
        if (!moduleConfig) {
            return;
        }
        
        if (isTestModeRequested()) {
            log("Test mode requested - switching to AP_TEST_MODE");
            moduleConfig->setRequestedModuleMode(ModuleModeEnum::AP_TEST_MODE);
        }
        else if (isFactoryResetRequested()) {
            log("Factory reset requested - switching to USER_CAN_CONFIGURE mode");
            if (clearModuleDataService) {
                clearModuleDataService->clearAllData();
                log("Module data cleared");
            }
            moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_CONFIGURE);
        }
        else if (isAddModeRequested()) {
            log("Add mode requested - switching to USER_CAN_RE_CONFIGURE mode");
            moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
        }
    }
};

#endif // BUTTON_USE_CASES_H
