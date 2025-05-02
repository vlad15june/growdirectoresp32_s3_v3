#ifndef BUTTON_ADAPTER_H
#define BUTTON_ADAPTER_H

#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include <vector>
#include <map>

#define MAX_BUTTONS 16

class ButtonAdapter : public IButtonAdapter {
private:
    IArduinoAdapter* arduinoAdapter;
    
    // Button state tracking
    struct ButtonState {
        bool currentState;
        bool lastState;
        bool lastDebounceState;
        unsigned long lastDebounceTime;
    };
    
    std::map<uint8_t, ButtonState> buttonStates;
    std::vector<uint8_t> buttonPins;
    unsigned long debounceTime = 50; // Default debounce time in ms
    
public:
    explicit ButtonAdapter(IArduinoAdapter* arduinoAdapter);
    ~ButtonAdapter() override = default;
    
    // Button setup
    void setupButton(uint8_t pin, uint8_t mode, bool pullUp = true) override;
    
    // Button state reading
    bool isButtonPressed(uint8_t index) override;
    bool isButtonReleased(uint8_t index) override;
    
    // Module specific button setup
    void setupModuleButtons(const uint8_t* buttonPins, uint8_t buttonCount) override;
    
    // Button event detection
    bool wasButtonPressed(uint8_t index) override;
    bool wasButtonReleased(uint8_t index) override;
    bool wasButtonHeld(uint8_t index, unsigned long duration) override;
    
    // Button debouncing
    void setDebounceTime(unsigned long debounceMs) override;
    
    // Get button pin
    uint8_t getButtonPin(uint8_t index) override;
    
    // Update button states (should be called in loop)
    void updateButtonStates() override;

private:
    // Internal helper methods
    bool readButtonRaw(uint8_t pin);
    bool checkButtonExists(uint8_t pin);
    void initializeButtonState(uint8_t pin);
    uint8_t getPin(uint8_t index); // Helper to convert index to pin
};

#endif // BUTTON_ADAPTER_H
