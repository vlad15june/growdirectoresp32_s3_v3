#ifndef I_BUTTON_ADAPTER_H
#define I_BUTTON_ADAPTER_H

#include <stdint.h>

// Interface for Button hardware control
class IButtonAdapter {
public:
    virtual ~IButtonAdapter() = default;

    // Button setup
    virtual void setupButton(uint8_t pin, uint8_t mode, bool pullUp = true) = 0;
    
    // Button state reading
    virtual bool isButtonPressed(uint8_t index) = 0;
    virtual bool isButtonReleased(uint8_t index) = 0;
    
    // Module specific button setup
    virtual void setupModuleButtons(const uint8_t* buttonPins, uint8_t buttonCount) = 0;
    
    // Button event detection
    virtual bool wasButtonPressed(uint8_t index) = 0;
    virtual bool wasButtonReleased(uint8_t index) = 0;
    virtual bool wasButtonHeld(uint8_t index, unsigned long duration) = 0;
    
    // Button debouncing
    virtual void setDebounceTime(unsigned long debounceMs) = 0;
    
    // Get button pin
    virtual uint8_t getButtonPin(uint8_t index) = 0;
    
    // Update button states (should be called in loop)
    virtual void updateButtonStates() = 0;
};

#endif // I_BUTTON_ADAPTER_H
