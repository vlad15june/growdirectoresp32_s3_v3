#ifndef LED_USE_CASES_H
#define LED_USE_CASES_H

#include "Core/Domain/Interfaces/ILedAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILedUseCases.h"
#include <memory>

// Use case class for LED behaviors and patterns
class LedUseCases : public ILedUseCases {
private:
    std::shared_ptr<ILedAdapter> ledAdapter;
    std::shared_ptr<IModuleConfig> moduleConfig;

public:
    // Constructor
    LedUseCases(std::shared_ptr<ILedAdapter> adapter, std::shared_ptr<IModuleConfig> moduleConfig);
    
    // Destructor
    ~LedUseCases() override;
    
    // LED behaviors implemented from ILedUseCases
    void ledOnLoad() override;
    void ledOffLoad() override;
    void ledBlinkGreen2Times() override;
    void ledBlinkYellow2Times() override;
};

#endif // LED_USE_CASES_H