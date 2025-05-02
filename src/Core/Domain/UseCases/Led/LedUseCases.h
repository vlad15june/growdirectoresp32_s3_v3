#ifndef LED_USE_CASES_H
#define LED_USE_CASES_H

#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <memory>
#include <stdarg.h>

// Use case class for LED behaviors and patterns
class LedUseCases : public ILedUseCases {
private:
    ILedAdapter* ledAdapter;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    ILogger* logger;
    IDebugSystem* debugSystem;
    const uint8_t buttonIndex;

    void log(const char* format, ...);

public:
    // Constructor
    LedUseCases(ILedAdapter* adapter, IModuleConfig* moduleConfig, IArduinoAdapter* arduinoAdapter,
                ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr, uint8_t index = 0);
    
    // Destructor
    ~LedUseCases() override;
    
    // LED behaviors implemented from ILedUseCases
    void ledOnLoad() override;
    void ledOffLoad() override;
    void ledBlinkGreen2Times() override;
    void ledBlinkYellow2Times() override;
};

#endif // LED_USE_CASES_H