#ifndef LED_USE_CASES_H
#define LED_USE_CASES_H

#include "Core/Domain/Interfaces/ILedAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include <memory>

// Use case class for LED behaviors and patterns
class LedUseCases {
private:
    std::shared_ptr<ILedAdapter> ledAdapter;
    std::shared_ptr<IModuleConfig> moduleConfig;

public:
    // Constructor
    LedUseCases(std::shared_ptr<ILedAdapter> adapter, std::shared_ptr<IModuleConfig> moduleConfig);
    
    // Destructor
    ~LedUseCases();
    
    // LED behaviors
    void ledOnLoad();
    void ledOffLoad();
    void ledBlinkGreen2Times();
    void ledBlinkYellow2Times();
};

#endif // LED_USE_CASES_H