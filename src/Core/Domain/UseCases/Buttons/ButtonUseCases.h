#ifndef BUTTON_USE_CASES_H
#define BUTTON_USE_CASES_H

#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class ButtonUseCases : public IButtonUseCases {
private:
    IButtonAdapter* buttonAdapter;
    ILogger *logger;
    IDebugSystem *debugSystem;
    const uint8_t buttonIndex;
    bool isSystemOn;
    bool buttonPressedState;
    unsigned long lastPressTime;
    const unsigned long durationMs = 15000;

    void log(const char *format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::BUTTON_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(buffer);
        }
    }

public:
    ButtonUseCases(IButtonAdapter* adapter, ILogger* loggerInstance = nullptr, 
                  IDebugSystem* debugSystemInstance = nullptr, uint8_t index = 0)
        : buttonAdapter(adapter), logger(loggerInstance), debugSystem(debugSystemInstance), 
          buttonIndex(index), isSystemOn(false), buttonPressedState(false), lastPressTime(0) {}
    
    ~ButtonUseCases() override = default;
    
    // Check if button is pressed and store the state
    // This method will be called only on system start
    bool checkButtonPressedWhenOn() override {
        bool pressed = buttonAdapter->isButtonPressed(buttonIndex);
        if (pressed) {
            buttonPressedState = true;
            log("Button %d is pressed when system is on", buttonIndex);
        }
        log("Button %d pressed state: %s", buttonIndex, buttonPressedState ? "true" : "false");
        return buttonPressedState;
    }
    
    // Check if button is currently pressed (returns current state)
    bool isButtonPressedWhenOn() override {
        return buttonPressedState;
    }
    
    // Check if button is held for the specified duration
    bool isButtonHeldForDuration() override {
        bool held = buttonAdapter->wasButtonHeld(buttonIndex, durationMs);
        log("Button %d held for %lu ms: %s", buttonIndex, durationMs, held ? "true" : "false");
        return held;
    }
    
    // Check if button was pressed when system was on and then held for the specified duration
    bool isButtonPressedWhenOnThenHeldForDuration() override {
        // First check if the button was pressed when system was on
        if (!buttonPressedState) {
            log("Button %d wasn't pressed when system was on", buttonIndex);
            return false;
        }
        
        // Then check if it's being held for the duration
        bool held = buttonAdapter->wasButtonHeld(buttonIndex, durationMs);
        log("Button %d pressed when on and held for %lu ms: %s", buttonIndex, durationMs, held ? "true" : "false");
        return held;
    }
};

#endif // BUTTON_USE_CASES_H
