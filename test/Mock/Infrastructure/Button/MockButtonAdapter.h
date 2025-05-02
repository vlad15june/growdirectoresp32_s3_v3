#ifndef MOCK_BUTTON_ADAPTER_H
#define MOCK_BUTTON_ADAPTER_H

#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"
#include <vector>

class MockButtonAdapter : public IButtonAdapter {
public:
    // State tracking variables
    bool pressedState = false;
    bool releasedState = false;
    bool heldState = false;
    uint8_t lastButtonIndex = 0;
    unsigned long lastDuration = 0;
    unsigned long debounceTime = 50;
    
    // Call tracking variables
    bool isButtonPressedCalled = false;
    bool isButtonReleasedCalled = false;
    bool wasButtonPressedCalled = false;
    bool wasButtonReleasedCalled = false;
    bool wasButtonHeldCalled = false;
    bool setupButtonCalled = false;
    bool setupModuleButtonsCalled = false;
    bool setDebounceTimeCalled = false;
    bool getButtonPinCalled = false;
    bool updateButtonStatesCalled = false;
    bool initCalled = false;
    
    // Last parameters used
    uint8_t lastPin = 0;
    uint8_t lastMode = 0;
    bool lastPullUp = true;
    std::vector<uint8_t> buttonPins;
    uint8_t buttonCount = 0;
    
    MockButtonAdapter() = default;
    ~MockButtonAdapter() override = default;
    
    void reset() {
        pressedState = false;
        releasedState = false;
        heldState = false;
        lastButtonIndex = 0;
        lastDuration = 0;
        debounceTime = 50;
        
        isButtonPressedCalled = false;
        isButtonReleasedCalled = false;
        wasButtonPressedCalled = false;
        wasButtonReleasedCalled = false;
        wasButtonHeldCalled = false;
        setupButtonCalled = false;
        setupModuleButtonsCalled = false;
        setDebounceTimeCalled = false;
        getButtonPinCalled = false;
        updateButtonStatesCalled = false;
        initCalled = false;
        
        lastPin = 0;
        lastMode = 0;
        lastPullUp = true;
        buttonPins.clear();
        buttonCount = 0;
    }
    
    // IButtonAdapter interface implementations
    void setupButton(uint8_t pin, uint8_t mode, bool pullUp = true) override {
        setupButtonCalled = true;
        lastPin = pin;
        lastMode = mode;
        lastPullUp = pullUp;
    }
    
    bool isButtonPressed(uint8_t index) override {
        isButtonPressedCalled = true;
        lastButtonIndex = index;
        return pressedState;
    }
    
    bool isButtonReleased(uint8_t index) override {
        isButtonReleasedCalled = true;
        lastButtonIndex = index;
        return releasedState;
    }
    
    void setupModuleButtons(const uint8_t* pins, uint8_t count) override {
        setupModuleButtonsCalled = true;
        buttonPins.clear();
        for (uint8_t i = 0; i < count; i++) {
            buttonPins.push_back(pins[i]);
        }
        buttonCount = count;
    }
    
    bool wasButtonPressed(uint8_t index) override {
        wasButtonPressedCalled = true;
        lastButtonIndex = index;
        return pressedState;
    }
    
    bool wasButtonReleased(uint8_t index) override {
        wasButtonReleasedCalled = true;
        lastButtonIndex = index;
        return releasedState;
    }
    
    bool wasButtonHeld(uint8_t index, unsigned long duration) override {
        wasButtonHeldCalled = true;
        lastButtonIndex = index;
        lastDuration = duration;
        return heldState;
    }
    
    void setDebounceTime(unsigned long debounceMs) override {
        setDebounceTimeCalled = true;
        debounceTime = debounceMs;
    }
    
    uint8_t getButtonPin(uint8_t index) override {
        getButtonPinCalled = true;
        lastButtonIndex = index;
        if (index < buttonPins.size()) {
            return buttonPins[index];
        }
        return 0;
    }
    
    void updateButtonStates() override {
        updateButtonStatesCalled = true;
    }
};

#endif // MOCK_BUTTON_ADAPTER_H
