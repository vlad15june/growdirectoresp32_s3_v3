#include "LedUseCases.h"
#include "esp_timer.h" // For esp_timer functions

// Constructor
LedUseCases::LedUseCases(ILedAdapter* adapter, IModuleConfig* moduleConfig, IArduinoAdapter* arduinoAdapter)
    : ledAdapter(adapter),
      moduleConfig(moduleConfig),
      arduinoAdapter(arduinoAdapter) {    
}

// Destructor
LedUseCases::~LedUseCases() {
    // No dynamic memory to clean up
}

// Turn LEDs on during module load
void LedUseCases::ledOnLoad() {
    ledAdapter->setAllYellowLedOn();    
}

// Turn LEDs off during module unload
void LedUseCases::ledOffLoad() {
    ledAdapter->setAllYellowLedOff();
}

// Blink green LED twice
void LedUseCases::ledBlinkGreen2Times() {
    ledAdapter->setGreenLedOn(0);
    arduinoAdapter->delay(500);
    ledAdapter->setGreenLedOff(0);
    arduinoAdapter->delay(500);
    ledAdapter->setGreenLedOn(0);
    arduinoAdapter->delay(500);
    ledAdapter->setGreenLedOff(0);
}

// Blink yellow LED (red+green together) twice
void LedUseCases::ledBlinkYellow2Times() {
    ledAdapter->setAllYellowLedOn();
    arduinoAdapter->delay(500);
    ledAdapter->setAllYellowLedOff();
    arduinoAdapter->delay(500);
    ledAdapter->setAllYellowLedOn();
    arduinoAdapter->delay(500);
    ledAdapter->setAllYellowLedOff();
}