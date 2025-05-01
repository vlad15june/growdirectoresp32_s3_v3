#include "LedUseCases.h"
#include "esp_timer.h" // For esp_timer functions

// Constructor
LedUseCases::LedUseCases(ILedAdapter* adapter, IModuleConfig* moduleConfig)
    : ledAdapter(adapter),
      moduleConfig(moduleConfig) {    
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
    
}

// Blink yellow LED (red+green together) twice
void LedUseCases::ledBlinkYellow2Times() {
    
}

// Helper method for delays
void delay(unsigned int ms) {
    
}