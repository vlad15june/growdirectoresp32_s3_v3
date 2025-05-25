#ifndef LED_USE_CASES_H
#define LED_USE_CASES_H

#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include <stdarg.h>

// Use case class for LED behaviors and patterns
class LedUseCases : public ILedUseCases {
private:
    ILedAdapter* ledAdapter;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IDeviceModel* deviceModel;

    // LED timing constants (in milliseconds)
    static const unsigned long BLINK_FAST_ON_TIME = 500;     // 0.5 seconds on
    static const unsigned long BLINK_FAST_OFF_TIME = 500;    // 0.5 seconds off
    static const unsigned long BLINK_SLOW_ON_TIME = 1000;    // 1 second on
    static const unsigned long BLINK_SLOW_OFF_TIME = 5000;   // 5 seconds off
    
    // State variables for LED blinking
    unsigned long lastBlinkTime = 0;
    bool ledState = false;

    void log(const char* format, ...);

public:
    // Constructor
    LedUseCases(ILedAdapter* adapter, IModuleConfig* moduleConfig, IArduinoAdapter* arduinoAdapter,
                ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr,
                IDeviceModel* deviceModel = nullptr);
    
    // Destructor
    ~LedUseCases() override;
    
    // LED behaviors implemented from ILedUseCases
    void ledOnLoad() override;
    void ledOffLoad() override;
    void ledBlinkGreen2Times() override;
    void ledBlinkYellow2Times() override;
    
    // LED state management based on module configuration
    void loop() override;
};

#endif // LED_USE_CASES_H