#ifndef MOCK_LED_ADAPTER_H
#define MOCK_LED_ADAPTER_H

#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"

class MockLedAdapter : public ILedAdapter {
public:
    bool setupLedCalled = false;
    bool setLedOnCalled = false;
    bool setLedOffCalled = false;
    bool toggleLedCalled = false;
    bool setupModuleLedsCalled = false;
    bool setAllGreenLedOnCalled = false;
    bool setAllGreenLedOffCalled = false;
    bool setGreenLedOnCalled = false;
    bool setGreenLedOffCalled = false;
    bool setRedLedOnCalled = false;
    bool setRedLedOffCalled = false;
    bool setAllRedLedOnCalled = false;
    bool setAllRedLedOffCalled = false;
    bool setAllYellowLedOnCalled = false;
    bool setAllYellowLedOffCalled = false;
    bool setYellowLedOnCalled = false;
    bool setYellowLedOffCalled = false;
    
    uint8_t lastSetupPin = 0;
    uint8_t lastSetupMode = 0;
    uint8_t lastOnPin = 0;
    uint8_t lastOffPin = 0;
    uint8_t lastTogglePin = 0;
    uint8_t lastGreenLedIndex = 0;
    uint8_t lastRedLedIndex = 0;
    uint8_t lastYellowLedIndex = 0;
    
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
    
    // Green LED control
    void setAllGreenLedOn() override {
        setAllGreenLedOnCalled = true;
    }
    
    void setAllGreenLedOff() override {
        setAllGreenLedOffCalled = true;
    }
    
    void setGreenLedOn(uint8_t index) override {
        setGreenLedOnCalled = true;
        lastGreenLedIndex = index;
    }
    
    void setGreenLedOff(uint8_t index) override {
        setGreenLedOffCalled = true;
        lastGreenLedIndex = index;
    }
    
    // Red LED control
    void setAllRedLedOn() override {
        setAllRedLedOnCalled = true;
    }
    
    void setAllRedLedOff() override {
        setAllRedLedOffCalled = true;
    }
    
    void setRedLedOn(uint8_t index) override {
        setRedLedOnCalled = true;
        lastRedLedIndex = index;
    }
    
    void setRedLedOff(uint8_t index) override {
        setRedLedOffCalled = true;
        lastRedLedIndex = index;
    }
    
    // Yellow LED control (Red + Green together)
    void setAllYellowLedOn() override {
        setAllYellowLedOnCalled = true;
    }
    
    void setAllYellowLedOff() override {
        setAllYellowLedOffCalled = true;
    }
    
    void setYellowLedOn(uint8_t index) override {
        setYellowLedOnCalled = true;
        lastYellowLedIndex = index;
    }
    
    void setYellowLedOff(uint8_t index) override {
        setYellowLedOffCalled = true;
        lastYellowLedIndex = index;
    }

    void reset() {
        setupLedCalled = false;
        setLedOnCalled = false;
        setLedOffCalled = false;
        toggleLedCalled = false;
        setupModuleLedsCalled = false;
        setAllGreenLedOnCalled = false;
        setAllGreenLedOffCalled = false;
        setGreenLedOnCalled = false;
        setGreenLedOffCalled = false;
        setRedLedOnCalled = false;
        setRedLedOffCalled = false;
        setAllRedLedOnCalled = false;
        setAllRedLedOffCalled = false;
        setAllYellowLedOnCalled = false;
        setAllYellowLedOffCalled = false;
        setYellowLedOnCalled = false;
        setYellowLedOffCalled = false;
        
        lastSetupPin = 0;
        lastSetupMode = 0;
        lastOnPin = 0;
        lastOffPin = 0;
        lastTogglePin = 0;
        lastGreenLedIndex = 0;
        lastRedLedIndex = 0;
        lastYellowLedIndex = 0;
        
        ledsCount = 0;
        memset(greenLeds, 0, sizeof(greenLeds));
        memset(redLeds, 0, sizeof(redLeds));
    }
};

#endif // MOCK_LED_ADAPTER_H
