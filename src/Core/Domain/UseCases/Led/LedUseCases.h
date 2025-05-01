#ifndef LED_USE_CASES_H
#define LED_USE_CASES_H

#include "Core/Domain/Interfaces/ILedAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILedUseCases.h"
#include "Core/Domain/Interfaces/IArduinoAdapter.h"
#include <memory>

// Use case class for LED behaviors and patterns
class LedUseCases : public ILedUseCases {
private:
    ILedAdapter* ledAdapter;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;

public:
    // Constructor
    LedUseCases(ILedAdapter* adapter, IModuleConfig* moduleConfig, IArduinoAdapter* arduinoAdapter);
    
    // Destructor
    ~LedUseCases() override;
    
    // LED behaviors implemented from ILedUseCases
    void ledOnLoad() override;
    void ledOffLoad() override;
    void ledBlinkGreen2Times() override;
    void ledBlinkYellow2Times() override;
};

#endif // LED_USE_CASES_H