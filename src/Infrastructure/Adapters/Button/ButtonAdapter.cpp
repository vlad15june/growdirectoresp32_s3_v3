#include "ButtonAdapter.h"
#include <Arduino.h>

ButtonAdapter::ButtonAdapter(IArduinoAdapter *arduinoAdapter)
    : arduinoAdapter(arduinoAdapter)
{
}

void ButtonAdapter::setupButton(uint8_t pin, uint8_t mode, bool pullUp)
{
    arduinoAdapter->pinMode(pin, mode);
    if (pullUp && mode == arduinoAdapter->getPinModeInput())
    {
        arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalHigh()); // Enable internal pull-up resistor
    }

    initializeButtonState(pin);

    // Add to list if not already present
    bool pinExists = false;
    for (size_t i = 0; i < buttonPins.size(); i++)
    {
        if (buttonPins[i] == pin)
        {
            pinExists = true;
            break;
        }
    }

    if (!pinExists)
    {
        buttonPins.push_back(pin);
    }
}

uint8_t ButtonAdapter::getPin(uint8_t index)
{
    if (index < buttonPins.size())
    {
        return buttonPins[index];
    }
    return 0; // Return 0 (invalid pin) if index is out of range
}

bool ButtonAdapter::isButtonPressed(uint8_t index)
{
    uint8_t pin = getPin(index);
    if (!checkButtonExists(pin))
    {
        return false;
    }

    bool pressed = !buttonStates[pin].currentState; // Active LOW (pressed when LOW)
    return pressed;
}

bool ButtonAdapter::isButtonReleased(uint8_t index)
{
    uint8_t pin = getPin(index);
    if (!checkButtonExists(pin))
    {
        return true;
    }

    bool released = buttonStates[pin].currentState; // Active LOW (released when HIGH)
    return released;
}

void ButtonAdapter::setupModuleButtons(const uint8_t *pins, uint8_t buttonCount)
{
    for (uint8_t i = 0; i < buttonCount && i < MAX_BUTTONS; i++)
    {
        setupButton(pins[i], arduinoAdapter->getPinModeInput(), true);
    }
}

bool ButtonAdapter::wasButtonPressed(uint8_t index)
{
    uint8_t pin = getPin(index);
    if (!checkButtonExists(pin))
    {
        return false;
    }

    ButtonState &state = buttonStates[pin];
    bool wasPressed = state.lastState && !state.currentState; // Transition from released to pressed
    state.lastState = state.currentState;
    return wasPressed;
}

bool ButtonAdapter::wasButtonReleased(uint8_t index)
{
    uint8_t pin = getPin(index);
    if (!checkButtonExists(pin))
    {
        return false;
    }

    ButtonState &state = buttonStates[pin];
    bool wasReleased = !state.lastState && state.currentState; // Transition from pressed to released
    state.lastState = state.currentState;
    return wasReleased;
}

bool ButtonAdapter::wasButtonHeld(uint8_t index, unsigned long duration)
{
    uint8_t pin = getPin(index);
    if (!checkButtonExists(pin) || !isButtonPressed(index))
    {
        return false;
    }

    unsigned long currentTime = arduinoAdapter->millis();
    ButtonState &state = buttonStates[pin];

    // Check if button has been pressed for the specified duration
    if (!state.currentState && (currentTime - state.lastDebounceTime) >= duration)
    {
        Serial.printf("[ButtonAdapter] Button %d held for %lu ms\n", index, duration);
        return true;
    }

    return false;
}

void ButtonAdapter::setDebounceTime(unsigned long debounceMs)
{
    this->debounceTime = debounceMs;
}

uint8_t ButtonAdapter::getButtonPin(uint8_t index)
{
    return getPin(index);
}

void ButtonAdapter::updateButtonStates()
{
    if (buttonPins.empty())
    {
        return;
    }

    unsigned long currentTime = arduinoAdapter->millis();

    for (uint8_t pin : buttonPins)
    {
        bool reading = readButtonRaw(pin);
        ButtonState &state = buttonStates[pin];

        // If the button state changed, reset the debounce timer
        if (reading != state.lastDebounceState)
        {
            state.lastDebounceTime = currentTime;
            state.lastDebounceState = reading;
        }

        // If debounce time has passed, update the actual button state
        if ((currentTime - state.lastDebounceTime) > debounceTime)
        {
            if (state.currentState != reading)
            {
                Serial.printf("[ButtonAdapter] Pin %d state: %d -> %d\n", pin, state.currentState, reading);
            }
            state.currentState = reading;
        }
    }
}

bool ButtonAdapter::readButtonRaw(uint8_t pin)
{
    return arduinoAdapter->digitalRead(pin) == arduinoAdapter->getDigitalHigh();
}

bool ButtonAdapter::checkButtonExists(uint8_t pin)
{
    return buttonStates.find(pin) != buttonStates.end();
}

void ButtonAdapter::initializeButtonState(uint8_t pin)
{
    bool initialState = readButtonRaw(pin);
    buttonStates[pin] = {
        initialState,            // currentState
        initialState,            // lastState
        initialState,            // lastDebounceState
        arduinoAdapter->millis() // lastDebounceTime
    };
}
