#ifndef I_LED_ADAPTER_H
#define I_LED_ADAPTER_H

#include <stdint.h>

// Interface for LED hardware control
class ILedAdapter {
public:
    virtual ~ILedAdapter() = default;

    // Basic pin control
    virtual void setupLed(uint8_t pin, uint8_t mode) = 0;
    virtual void setLedOn(uint8_t pin) = 0;
    virtual void setLedOff(uint8_t pin) = 0;
    virtual void toggleLed(uint8_t pin) = 0;
    
    // Module specific LED setup
    virtual void setupModuleLeds(const uint8_t* greenLeds = nullptr, const uint8_t* redLeds = nullptr, 
                               uint8_t ledsCount = 1) = 0;
    
    // Get the current LED pin based on module type/subtype
    virtual uint8_t getGreenLedPin(uint8_t index) = 0;
    virtual uint8_t getRedLedPin(uint8_t index) = 0;
};

#endif // I_LED_ADAPTER_H