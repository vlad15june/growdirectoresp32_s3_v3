#ifndef I_LED_USE_CASES_H
#define I_LED_USE_CASES_H

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
};

#endif // I_LED_USE_CASES_H
