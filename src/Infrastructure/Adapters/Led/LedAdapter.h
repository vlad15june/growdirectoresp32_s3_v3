#ifndef LED_ADAPTER_H
#define LED_ADAPTER_H

#include "Core/Domain/Interfaces/ILedAdapter.h"
#include <Arduino.h>
#include <vector>

// Implementation of LED adapter for ESP32 hardware
class LedAdapter : public ILedAdapter {
private:    
    // Default LED pin values as class constants
    static uint8_t DEFAULT_GREEN_LEDS[4];
    static uint8_t DEFAULT_RED_LEDS[4];
    
    // Current working LED pins
    uint8_t greenLedPins[4];
    uint8_t redLedPins[4];
    
    // Number of devices
    uint8_t ledsCount;

public:
    // Empty constructor
    LedAdapter() : ledsCount(0) {}
    
    // Implementation of ILedAdapter methods
    void setupLed(uint8_t pin, uint8_t mode) override {
        if (pin != 0) {
            pinMode(pin, mode);
        }
    }
    
    void setLedOn(uint8_t pin) override {
        if (pin != 0) {
            digitalWrite(pin, HIGH);  // Changed to HIGH for active high
        }
    }
    
    void setLedOff(uint8_t pin) override {
        if (pin != 0) {
            digitalWrite(pin, LOW);  // Changed to LOW for active high
        }
    }
    
    void toggleLed(uint8_t pin) override {
        if (pin != 0) {
            digitalWrite(pin, !digitalRead(pin));
        }
    }
    
    void setupModuleLeds(const uint8_t* greenLeds = nullptr, const uint8_t* redLeds = nullptr, 
                        uint8_t ledsCount = 1) override {
        // Set the LED count
        this->ledsCount = ledsCount;
        
        // Copy provided LED data or use defaults
        for (uint8_t i = 0; i < 4 && i < ledsCount; i++) {
            greenLedPins[i] = greenLeds ? greenLeds[i] : DEFAULT_GREEN_LEDS[i];
            redLedPins[i] = redLeds ? redLeds[i] : DEFAULT_RED_LEDS[i];
            
            // Initialize pins as OUTPUT mode
            if (greenLedPins[i] != 0) {
                pinMode(greenLedPins[i], OUTPUT);
                digitalWrite(greenLedPins[i], LOW);  // Turn off initially (HIGH active)
            }
            
            if (redLedPins[i] != 0) {
                pinMode(redLedPins[i], OUTPUT);
                digitalWrite(redLedPins[i], LOW);  // Turn off initially (HIGH active)
            }
        }
    }
    
    uint8_t getGreenLedPin(uint8_t index) override {
        if (index < ledsCount) {
            return greenLedPins[index];
        }
        return 0;
    }
    
    uint8_t getRedLedPin(uint8_t index) override {
        if (index < ledsCount) {
            return redLedPins[index];
        }
        return 0;
    }
};

uint8_t LedAdapter::DEFAULT_GREEN_LEDS[4] = {38, 37, 38, 37};
uint8_t LedAdapter::DEFAULT_RED_LEDS[4] = {12, 13, 14, 15};

#endif // LED_ADAPTER_H