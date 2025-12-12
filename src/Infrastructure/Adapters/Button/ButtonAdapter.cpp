#include "ButtonAdapter.h"
#include <Arduino.h>

ButtonAdapter::ButtonAdapter(IArduinoAdapter* arduinoAdapter) 
    : arduinoAdapter(arduinoAdapter) {
    Serial.println("[ButtonAdapter] Constructor called");
}

void ButtonAdapter::setupButton(uint8_t pin, uint8_t mode, bool pullUp) {
    Serial.printf("[ButtonAdapter::setupButton] pin=%d, mode=%d, pullUp=%d\n", pin, mode, pullUp);
    arduinoAdapter->pinMode(pin, mode);
    if (pullUp && mode == arduinoAdapter->getPinModeInput()) {
        arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalHigh()); // Enable internal pull-up resistor
    }
    
    initializeButtonState(pin);
    
    // Add to list if not already present
    bool pinExists = false;
    for (size_t i = 0; i < buttonPins.size(); i++) {
        if (buttonPins[i] == pin) {
            pinExists = true;
            break;
        }
    }
    
    if (!pinExists) {
        buttonPins.push_back(pin);
    }
}

uint8_t ButtonAdapter::getPin(uint8_t index) {
    Serial.printf("[ButtonAdapter::getPin] index=%d\n", index);
    if (index < buttonPins.size()) {
        uint8_t pin = buttonPins[index];
        Serial.printf("[ButtonAdapter::getPin] Returned pin=%d\n", pin);
        return pin;
    }
    Serial.println("[ButtonAdapter::getPin] Index out of range, returning 0");
    return 0; // Return 0 (invalid pin) if index is out of range
}

bool ButtonAdapter::isButtonPressed(uint8_t index) {
    uint8_t pin = getPin(index);
    if (pin == 0 || !checkButtonExists(pin)) {
        Serial.printf("[ButtonAdapter::isButtonPressed] index=%d, invalid pin or button not exists\n", index);
        return false;
    }
    
    bool pressed = !buttonStates[pin].currentState; // Active LOW (pressed when LOW)
    Serial.printf("[ButtonAdapter::isButtonPressed] index=%d, pin=%d, pressed=%d\n", index, pin, pressed);
    return pressed;
}

bool ButtonAdapter::isButtonReleased(uint8_t index) {
    uint8_t pin = getPin(index);
    if (pin == 0 || !checkButtonExists(pin)) {
        Serial.printf("[ButtonAdapter::isButtonReleased] index=%d, invalid pin or button not exists\n", index);
        return true;
    }
    
    bool released = buttonStates[pin].currentState; // Active LOW (released when HIGH)
    Serial.printf("[ButtonAdapter::isButtonReleased] index=%d, pin=%d, released=%d\n", index, pin, released);
    return released;
}

void ButtonAdapter::setupModuleButtons(const uint8_t* pins, uint8_t buttonCount) {
    Serial.printf("[ButtonAdapter::setupModuleButtons] buttonCount=%d\n", buttonCount);
    for (uint8_t i = 0; i < buttonCount && i < MAX_BUTTONS; i++) {
        if (pins[i] != 0) { // Skip pin 0 (often reserved)
            Serial.printf("[ButtonAdapter::setupModuleButtons] Setting up button %d on pin %d\n", i, pins[i]);
            setupButton(pins[i], arduinoAdapter->getPinModeInput(), true);
        }
    }
    Serial.println("[ButtonAdapter::setupModuleButtons] Completed");
}

bool ButtonAdapter::wasButtonPressed(uint8_t index) {
    uint8_t pin = getPin(index);
    if (pin == 0 || !checkButtonExists(pin)) {
        Serial.printf("[ButtonAdapter::wasButtonPressed] index=%d, invalid pin or button not exists\n", index);
        return false;
    }
    
    ButtonState& state = buttonStates[pin];
    bool wasPressed = state.lastState && !state.currentState; // Transition from released to pressed
    state.lastState = state.currentState;
    Serial.printf("[ButtonAdapter::wasButtonPressed] index=%d, pin=%d, wasPressed=%d\n", index, pin, wasPressed);
    return wasPressed;
}

bool ButtonAdapter::wasButtonReleased(uint8_t index) {
    uint8_t pin = getPin(index);
    if (pin == 0 || !checkButtonExists(pin)) {
        Serial.printf("[ButtonAdapter::wasButtonReleased] index=%d, invalid pin or button not exists\n", index);
        return false;
    }
    
    ButtonState& state = buttonStates[pin];
    bool wasReleased = !state.lastState && state.currentState; // Transition from pressed to released
    state.lastState = state.currentState;
    Serial.printf("[ButtonAdapter::wasButtonReleased] index=%d, pin=%d, wasReleased=%d\n", index, pin, wasReleased);
    return wasReleased;
}

bool ButtonAdapter::wasButtonHeld(uint8_t index, unsigned long duration) {
    Serial.printf("[ButtonAdapter::wasButtonHeld] index=%d, duration=%lu\n", index, duration);
    uint8_t pin = getPin(index);
    if (pin == 0 || !checkButtonExists(pin) || !isButtonPressed(index)) {
        Serial.printf("[ButtonAdapter::wasButtonHeld] index=%d, invalid condition\n", index);
        return false;
    }
    
    unsigned long currentTime = arduinoAdapter->millis();
    ButtonState& state = buttonStates[pin];
    
    // Check if button has been pressed for the specified duration
    if (!state.currentState && (currentTime - state.lastDebounceTime) >= duration) {
        Serial.printf("[ButtonAdapter::wasButtonHeld] index=%d, pin=%d, held=true\n", index, pin);
        return true;
    }
    
    return false;
}

void ButtonAdapter::setDebounceTime(unsigned long debounceMs) {
    Serial.printf("[ButtonAdapter::setDebounceTime] debounceMs=%lu\n", debounceMs);
    this->debounceTime = debounceMs;
}

uint8_t ButtonAdapter::getButtonPin(uint8_t index) {
    Serial.printf("[ButtonAdapter::getButtonPin] index=%d\n", index);
    uint8_t pin = getPin(index);
    Serial.printf("[ButtonAdapter::getButtonPin] Returned pin=%d\n", pin);
    return pin;
}

void ButtonAdapter::updateButtonStates() {
    Serial.println("[ButtonAdapter::updateButtonStates] Starting update");
    unsigned long currentTime = arduinoAdapter->millis();
    
    for (uint8_t pin : buttonPins) {
        bool reading = readButtonRaw(pin);
        ButtonState& state = buttonStates[pin];
        
        // If the button state changed, reset the debounce timer
        if (reading != state.lastDebounceState) {
            state.lastDebounceTime = currentTime;
            state.lastDebounceState = reading;
        }
        
        // If debounce time has passed, update the actual button state
        if ((currentTime - state.lastDebounceTime) > debounceTime) {
            if (state.currentState != reading) {
                Serial.printf("[ButtonAdapter::updateButtonStates] pin=%d state changed: %d -> %d\n", pin, state.currentState, reading);
            }
            state.currentState = reading;
        }
    }
    Serial.println("[ButtonAdapter::updateButtonStates] Completed");
}

bool ButtonAdapter::readButtonRaw(uint8_t pin) {
    bool state = arduinoAdapter->digitalRead(pin) == arduinoAdapter->getDigitalHigh();
    Serial.printf("[ButtonAdapter::readButtonRaw] pin=%d, state=%d\n", pin, state);
    return state;
}

bool ButtonAdapter::checkButtonExists(uint8_t pin) {
    bool exists = buttonStates.find(pin) != buttonStates.end();
    Serial.printf("[ButtonAdapter::checkButtonExists] pin=%d, exists=%d\n", pin, exists);
    return exists;
}

void ButtonAdapter::initializeButtonState(uint8_t pin) {
    Serial.printf("[ButtonAdapter::initializeButtonState] pin=%d\n", pin);
    bool initialState = readButtonRaw(pin);
    buttonStates[pin] = {
        initialState,   // currentState
        initialState,   // lastState
        initialState,   // lastDebounceState
        arduinoAdapter->millis() // lastDebounceTime
    };
    Serial.printf("[ButtonAdapter::initializeButtonState] pin=%d initialized with state=%d\n", pin, initialState);
}
