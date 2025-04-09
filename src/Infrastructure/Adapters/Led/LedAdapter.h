#ifndef LED_ADAPTER_H
#define LED_ADAPTER_H

#include "Core/Domain/Interfaces/ILedAdapter.h"
#include <Arduino.h>
#include <vector>

// Module type constants
#define MODULE_TYPE_DEVICE 1
#define MODULE_TYPE_SENSOR 2
#define MODULE_TYPE_HYDRO 3
#define MODULE_TYPE_ANALOG_INPUT_DEVICE 4

// Module subtype constants
#define MODULE_SUBTYPE_RELAY 1
#define MODULE_SUBTYPE_SOKET 2
#define MODULE_SUBTYPE_DIMMER 3

// Implementation of LED adapter for ESP32 hardware
class LedAdapter : public ILedAdapter {
private:
    // Arrays to store LED pins for different module types
    uint8_t LED_G_RELAY[4];
    uint8_t LED_R_RELAY[4];
    uint8_t LED_G_SKT[4];
    uint8_t LED_R_SKT[4];
    uint8_t LED_G_DIMMER[4];
    uint8_t LED_R_DIMMER[4];
    uint8_t LED_G_HYDRO[4];
    uint8_t LED_R_HYDRO[4];
    
    // Current working LED pins
    uint8_t LED_G[4];
    uint8_t LED_R[4];
    
    // Sensor LED pins
    uint8_t LED_G_Sensor;
    uint8_t LED_R_Sensor;
    
    // Number of devices
    uint8_t deviceCount;

public:
    // Constructor
    LedAdapter(uint8_t deviceCount = 1) : deviceCount(deviceCount) {
        // Initialize arrays to defaults (all zeros)
        for (int i = 0; i < 4; i++) {
            LED_G_RELAY[i] = 0;
            LED_R_RELAY[i] = 0;
            LED_G_SKT[i] = 0;
            LED_R_SKT[i] = 0;
            LED_G_DIMMER[i] = 0;
            LED_R_DIMMER[i] = 0;
            LED_G_HYDRO[i] = 0;
            LED_R_HYDRO[i] = 0;
            LED_G[i] = 0;
            LED_R[i] = 0;
        }
        
        LED_G_Sensor = 0;
        LED_R_Sensor = 0;
    }
    
    // Initialize with pin mappings
    void initialize(const uint8_t* greenRelayPins, const uint8_t* redRelayPins,
                   const uint8_t* greenSocketPins, const uint8_t* redSocketPins,
                   const uint8_t* greenDimmerPins, const uint8_t* redDimmerPins,
                   const uint8_t* greenHydroPins, const uint8_t* redHydroPins,
                   uint8_t greenSensorPin, uint8_t redSensorPin) {
        // Copy pin values to internal arrays
        for (int i = 0; i < deviceCount; i++) {
            if (greenRelayPins) LED_G_RELAY[i] = greenRelayPins[i];
            if (redRelayPins) LED_R_RELAY[i] = redRelayPins[i];
            if (greenSocketPins) LED_G_SKT[i] = greenSocketPins[i];
            if (redSocketPins) LED_R_SKT[i] = redSocketPins[i];
            if (greenDimmerPins) LED_G_DIMMER[i] = greenDimmerPins[i];
            if (redDimmerPins) LED_R_DIMMER[i] = redDimmerPins[i];
            if (greenHydroPins) LED_G_HYDRO[i] = greenHydroPins[i];
            if (redHydroPins) LED_R_HYDRO[i] = redHydroPins[i];
        }
        
        LED_G_Sensor = greenSensorPin;
        LED_R_Sensor = redSensorPin;
    }
    
    // Implementation of ILedAdapter methods
    void setupLed(uint8_t pin, uint8_t mode) override {
        if (pin != 0) {
            pinMode(pin, mode);
        }
    }
    
    void setLedOn(uint8_t pin) override {
        if (pin != 0) {
            digitalWrite(pin, LOW);
        }
    }
    
    void setLedOff(uint8_t pin) override {
        if (pin != 0) {
            digitalWrite(pin, HIGH);
        }
    }
    
    void toggleLed(uint8_t pin) override {
        if (pin != 0) {
            digitalWrite(pin, !digitalRead(pin));
        }
    }
    
    void setupRgbLed(uint8_t redPin, uint8_t greenPin, uint8_t bluePin) override {
        setupLed(redPin, OUTPUT);
        setupLed(greenPin, OUTPUT);
        setupLed(bluePin, OUTPUT);
    }
    
    void setRgbColor(uint8_t redPin, uint8_t greenPin, uint8_t bluePin,
                     bool redState, bool greenState, bool blueState) override {
        digitalWrite(redPin, redState ? LOW : HIGH);
        digitalWrite(greenPin, greenState ? LOW : HIGH);
        digitalWrite(bluePin, blueState ? LOW : HIGH);
    }
    
    void setupModuleLeds(uint8_t moduleType, uint8_t moduleSubType) override {
        if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE) {
            if (moduleSubType == MODULE_SUBTYPE_RELAY) {
                LED_G[0] = LED_G_RELAY[0];
                LED_R[0] = LED_R_RELAY[0];
            } else if (moduleSubType == MODULE_SUBTYPE_SOKET) {
                LED_G[0] = LED_G_SKT[0];
                LED_R[0] = LED_R_SKT[0];
            } else if (moduleSubType == MODULE_SUBTYPE_DIMMER) {
                LED_G[0] = LED_G_DIMMER[0];
                LED_R[0] = LED_R_DIMMER[0];
            }
            
            setupLed(LED_G[0], OUTPUT);
            setupLed(LED_R[0], OUTPUT);
            setLedOff(LED_G[0]);  // Initialize to OFF state
            setLedOff(LED_R[0]);  // Initialize to OFF state
        } 
        else if (moduleType == MODULE_TYPE_SENSOR) {
            setupLed(LED_G_Sensor, OUTPUT);
            setupLed(LED_R_Sensor, OUTPUT);
            setLedOff(LED_G_Sensor);  // Initialize to OFF state
            setLedOff(LED_R_Sensor);  // Initialize to OFF state
        } 
        else if (moduleType == MODULE_TYPE_HYDRO) {
            LED_G[0] = LED_G_HYDRO[0];
            LED_R[0] = LED_R_HYDRO[0];
            setupLed(LED_G[0], OUTPUT);
            setupLed(LED_R[0], OUTPUT);
            setLedOff(LED_G[0]);  // Initialize to OFF state
            setLedOff(LED_R[0]);  // Initialize to OFF state
        }
    }
    
    uint8_t getGreenLedPin(uint8_t index) override {
        if (index < deviceCount) {
            return LED_G[index];
        }
        return 0;
    }
    
    uint8_t getRedLedPin(uint8_t index) override {
        if (index < deviceCount) {
            return LED_R[index];
        }
        return 0;
    }
};

#endif // LED_ADAPTER_H