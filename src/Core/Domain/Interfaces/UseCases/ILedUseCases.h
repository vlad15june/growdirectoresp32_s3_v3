#ifndef I_LED_USE_CASES_H
#define I_LED_USE_CASES_H

#include "Core/Domain/Enums/LedBootStageEnum.h"

// Interface for LED use cases
class ILedUseCases {
public:
    // Virtual destructor for proper cleanup in derived classes
    virtual ~ILedUseCases() = default;
    
    // LED behaviors
    virtual void ledOnLoad() = 0;
    virtual void ledOffLoad() = 0;
    virtual void ledBlinkGreen2Times() = 0;
    virtual void ledBlinkYellow2Times() = 0;

    // LED state management based on module configuration
    virtual void loop() = 0;

    virtual void setBlinking(bool blinking) = 0;
    virtual bool isBlinking() const = 0;

    /**
     * @brief Say how far the boot has got.
     *
     * Setup calls this at the points where one wait ends and the next begins, and it must be the
     * fact that moves the stage on - an IP the module actually holds, not a connect function that
     * was called. Until the stage is RUNNING nothing else drives the LED; RUNNING hands it back
     * to loop().
     */
    virtual void setBootStage(LedBootStageEnum stage) = 0;

    /** @brief The stage the LED is showing. */
    virtual LedBootStageEnum bootStage() const = 0;
};

#endif // I_LED_USE_CASES_H
