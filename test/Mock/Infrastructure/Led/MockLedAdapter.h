#ifndef MOCK_LED_ADAPTER_H
#define MOCK_LED_ADAPTER_H

#include "Core/Domain/Interfaces/ILedAdapter.h"

class MockLedAdapter : public ILedAdapter {
public:
    bool setupLedCalled = false;
    bool setLedOnCalled = false;
    bool setLedOffCalled = false;
    bool toggleLedCalled = false;
    bool setupModuleLedsCalled = false;
    
    uint8_t lastSetupPin = 0;
    uint8_t lastSetupMode = 0;
    uint8_t lastOnPin = 0;
    uint8_t lastOffPin = 0;
    uint8_t lastTogglePin = 0;
    
    uint8_t greenLeds[4] = {0};
    uint8_t redLeds[4] = {0};
    uint8_t ledsCount = 0;

    // Basic pin control
    void setupLed(uint8_t pin, uint8_t mode) override {
        setupLedCalled = true;
        lastSetupPin = pin;
        lastSetupMode = mode;
    }
    
    void setLedOn(uint8_t pin) override {
        setLedOnCalled = true;
        lastOnPin = pin;
    }
    
    void setLedOff(uint8_t pin) override {
        setLedOffCalled = true;
        lastOffPin = pin;
    }
    
    void toggleLed(uint8_t pin) override {
        toggleLedCalled = true;
        lastTogglePin = pin;
    }
    
    // Module specific LED setup
    void setupModuleLeds(const uint8_t* greenLedPins, const uint8_t* redLedPins, uint8_t count) override {
        setupModuleLedsCalled = true;
        ledsCount = count;
        
        // Store the LED pin configurations
        if (greenLedPins != nullptr) {
            for (int i = 0; i < count && i < 4; i++) {
                greenLeds[i] = greenLedPins[i];
            }
        }
        
        if (redLedPins != nullptr) {
            for (int i = 0; i < count && i < 4; i++) {
                redLeds[i] = redLedPins[i];
            }
        }
    }
    
    // Get the current LED pin based on module type/subtype
    uint8_t getGreenLedPin(uint8_t index) override {
        return (index < ledsCount) ? greenLeds[index] : 0;
    }
    
    uint8_t getRedLedPin(uint8_t index) override {
        return (index < ledsCount) ? redLeds[index] : 0;
    }

    void reset() {
        setupLedCalled = false;
        setLedOnCalled = false;
        setLedOffCalled = false;
        toggleLedCalled = false;
        setupModuleLedsCalled = false;
        
        lastSetupPin = 0;
        lastSetupMode = 0;
        lastOnPin = 0;
        lastOffPin = 0;
        lastTogglePin = 0;
        
        ledsCount = 0;
        memset(greenLeds, 0, sizeof(greenLeds));
        memset(redLeds, 0, sizeof(redLeds));
    }
};

#endif // MOCK_LED_ADAPTER_H
