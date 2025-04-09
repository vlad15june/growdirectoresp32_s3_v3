#include "LedUseCases.h"
#include "esp_timer.h" // Add this include for esp_timer functions

// Constructor
LedUseCases::LedUseCases(std::shared_ptr<ILedAdapter> adapter, std::shared_ptr<IModuleConfig> moduleConfig, uint8_t deviceCount)
    : ledAdapter(adapter),
      moduleType(moduleConfig->getModuleType()),
      moduleSubType(moduleConfig->getModuleSubType()),
      deviceCount(deviceCount),
      flag_led(1),
      timing(0),
      isBlinking(false),
      count_online(0),
      count_offline(0),
      max_count_online(10),
      max_count_offline(10) {
    
    // Initialize key array
    for (int i = 0; i < 4; i++) {
        key[i] = 0;
    }
    
    // Initialize pin values array
    pinValue = new bool[deviceCount];
    for (int i = 0; i < deviceCount; i++) {
        pinValue[i] = OFF;
    }
    
    // Setup LEDs for this module type
    ledAdapter->setupModuleLeds(moduleType, moduleSubType);
}

// Destructor to clean up dynamically allocated memory
LedUseCases::~LedUseCases() {
    delete[] pinValue;
}

// Set connection threshold parameters
void LedUseCases::setConnectionParameters(int maxOnlineCount, int maxOfflineCount) {
    max_count_online = maxOnlineCount;
    max_count_offline = maxOfflineCount;
}

// Set pin state values
void LedUseCases::setPinValues(bool* values, uint8_t count) {
    if (count > deviceCount) {
        count = deviceCount;
    }
    
    for (int i = 0; i < count; i++) {
        pinValue[i] = values[i];
    }
}

// Set blinking state
void LedUseCases::setBlinkingState(bool blinking) {
    isBlinking = blinking;
}

// Set online counter
void LedUseCases::setOnlineCounter(int count) {
    count_online = count;
}

// Set offline counter
void LedUseCases::setOfflineCounter(int count) {
    count_offline = count;
}

// Turn LEDs on during module load
void LedUseCases::ledOnLoad() {
    if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE) {
        for (uint8_t i = 0; i < deviceCount; i++) {
            ledAdapter->setLedOn(ledAdapter->getGreenLedPin(i));
            ledAdapter->setLedOn(ledAdapter->getRedLedPin(i));
        }
    }
    else if (moduleType == MODULE_TYPE_SENSOR) {
        uint8_t greenPin = ledAdapter->getGreenLedPin(0);
        uint8_t redPin = ledAdapter->getRedLedPin(0);
        ledAdapter->setLedOn(greenPin);
        ledAdapter->setLedOn(redPin);
    }
    else if (moduleType == MODULE_TYPE_HYDRO) {
        for (uint8_t i = 0; i < deviceCount; i++) {
            ledAdapter->setLedOn(ledAdapter->getGreenLedPin(i));
            ledAdapter->setLedOn(ledAdapter->getRedLedPin(i));
        }
    }
}

// Turn LEDs off during module unload
void LedUseCases::ledOffLoad() {
    if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE) {
        for (uint8_t i = 0; i < deviceCount; i++) {
            ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
            ledAdapter->setLedOff(ledAdapter->getRedLedPin(i));
        }
    }
    else if (moduleType == MODULE_TYPE_SENSOR) {
        uint8_t greenPin = ledAdapter->getGreenLedPin(0);
        uint8_t redPin = ledAdapter->getRedLedPin(0);
        ledAdapter->setLedOff(greenPin);
        ledAdapter->setLedOff(redPin);
    }
    else if (moduleType == MODULE_TYPE_HYDRO) {
        for (uint8_t i = 0; i < deviceCount; i++) {
            ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
            ledAdapter->setLedOff(ledAdapter->getRedLedPin(i));
        }
    }
}

// Blink green LED twice
void LedUseCases::ledBlinkGreen2Times() {
    if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE) {
        uint8_t greenPin = ledAdapter->getGreenLedPin(0);
        ledAdapter->setLedOn(greenPin);
        delay(300);
        ledAdapter->setLedOff(greenPin);
        delay(300);
        ledAdapter->setLedOn(greenPin);
        delay(300);
        ledAdapter->setLedOff(greenPin);
    }
    else if (moduleType == MODULE_TYPE_SENSOR) {
        uint8_t greenPin = ledAdapter->getGreenLedPin(0);
        ledAdapter->setLedOn(greenPin);
        delay(300);
        ledAdapter->setLedOff(greenPin);
        delay(300);
        ledAdapter->setLedOn(greenPin);
        delay(300);
        ledAdapter->setLedOff(greenPin);
    }
    else if (moduleType == MODULE_TYPE_HYDRO) {
        uint8_t greenPin = ledAdapter->getGreenLedPin(0);
        ledAdapter->setLedOff(greenPin);
        delay(300);
        ledAdapter->setLedOn(greenPin);
        delay(300);
        ledAdapter->setLedOff(greenPin);
        delay(300);
        ledAdapter->setLedOn(greenPin);
    }
}

// Blink yellow LED (red+green together) twice
void LedUseCases::ledBlinkYellow2Times() {
    if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE) {
        uint8_t greenPin = ledAdapter->getGreenLedPin(0);
        uint8_t redPin = ledAdapter->getRedLedPin(0);
        
        // Yellow on (both LEDs on)
        ledAdapter->setLedOn(greenPin);
        ledAdapter->setLedOn(redPin);
        delay(300);
        
        // Yellow off (both LEDs off)
        ledAdapter->setLedOff(greenPin);
        ledAdapter->setLedOff(redPin);
        delay(300);
        
        // Yellow on (both LEDs on)
        ledAdapter->setLedOn(greenPin);
        ledAdapter->setLedOn(redPin);
        delay(300);
        
        // Yellow off (both LEDs off)
        ledAdapter->setLedOff(greenPin);
        ledAdapter->setLedOff(redPin);
    }
    else if (moduleType == MODULE_TYPE_SENSOR) {
        uint8_t greenPin = ledAdapter->getGreenLedPin(0);
        uint8_t redPin = ledAdapter->getRedLedPin(0);
        
        // Yellow on (both LEDs on)
        ledAdapter->setLedOn(greenPin);
        ledAdapter->setLedOn(redPin);
        delay(300);
        
        // Yellow off (both LEDs off)
        ledAdapter->setLedOff(greenPin);
        ledAdapter->setLedOff(redPin);
        delay(300);
        
        // Yellow on (both LEDs on)
        ledAdapter->setLedOn(greenPin);
        ledAdapter->setLedOn(redPin);
        delay(300);
        
        // Yellow off (both LEDs off)
        ledAdapter->setLedOff(greenPin);
        ledAdapter->setLedOff(redPin);
    }
    else if (moduleType == MODULE_TYPE_HYDRO) {
        uint8_t greenPin = ledAdapter->getGreenLedPin(0);
        uint8_t redPin = ledAdapter->getRedLedPin(0);
        
        // Yellow on (both LEDs on)
        ledAdapter->setLedOff(greenPin);
        ledAdapter->setLedOff(redPin);
        delay(300);
        
        // Yellow off (both LEDs off)
        ledAdapter->setLedOn(greenPin);
        ledAdapter->setLedOn(redPin);
        delay(300);
        
        // Yellow on (both LEDs on)
        ledAdapter->setLedOff(greenPin);
        ledAdapter->setLedOff(redPin);
        delay(300);
        
        // Yellow off (both LEDs off)
        ledAdapter->setLedOn(greenPin);
        ledAdapter->setLedOn(redPin);
    }
}

// Main blinking logic
void LedUseCases::ledBlinks(bool mqttConnected) {
    // Device is online
    if (count_online > max_count_online || mqttConnected) {
        if (!isBlinking) {
            if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE || moduleType == MODULE_TYPE_HYDRO) {
                for (uint8_t i = 0; i < deviceCount; i++) {
                    ledAdapter->setLedOff(ledAdapter->getRedLedPin(i));
                }
            }
            if (moduleType == MODULE_TYPE_SENSOR) {
                ledAdapter->setLedOff(ledAdapter->getRedLedPin(0));
            }
        }
    }

    // Device is offline
    if (count_offline > max_count_offline && !mqttConnected) {
        if (moduleType == MODULE_TYPE_SENSOR) {
            ledAdapter->setLedOff(ledAdapter->getGreenLedPin(0));
            ledAdapter->setLedOn(ledAdapter->getRedLedPin(0));
        }
        
        if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE || moduleType == MODULE_TYPE_HYDRO) {
            for (uint8_t i = 0; i < deviceCount; i++) {
                if (pinValue[i] == ON) {
                    // Blink alternating red/green for active pins
                    if (flag_led == 1) {
                        if (millis() - timing > 200) {
                            timing = millis();
                            flag_led = 0;
                            ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
                            ledAdapter->setLedOn(ledAdapter->getRedLedPin(i));
                        }
                    } else {
                        if (millis() - timing > 200) {
                            timing = millis();
                            flag_led = 1;
                            ledAdapter->setLedOn(ledAdapter->getGreenLedPin(i));
                            ledAdapter->setLedOff(ledAdapter->getRedLedPin(i));
                        }
                    }
                } else {
                    // Solid red for inactive pins
                    ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
                    ledAdapter->setLedOn(ledAdapter->getRedLedPin(i));
                }
            }
        }
    }
    else { // Device is online
        if (isBlinking) {
            // Fast blinking mode
            if (moduleType == MODULE_TYPE_SENSOR) {
                if (flag_led == 1) {
                    if (millis() - timing > 500) {
                        timing = millis();
                        flag_led = 0;
                        ledAdapter->setLedOff(ledAdapter->getGreenLedPin(0));
                        ledAdapter->setLedOff(ledAdapter->getRedLedPin(0));
                    }
                } else {
                    if (millis() - timing > 500) {
                        timing = millis();
                        flag_led = 1;
                        ledAdapter->setLedOff(ledAdapter->getGreenLedPin(0));
                        ledAdapter->setLedOn(ledAdapter->getRedLedPin(0));
                    }
                }
            }
            
            if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE || moduleType == MODULE_TYPE_HYDRO) {
                if (flag_led == 1) {
                    if (millis() - timing > 500) {
                        timing = millis();
                        for (uint8_t i = 0; i < deviceCount; i++) {
                            ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
                            ledAdapter->setLedOff(ledAdapter->getRedLedPin(i));
                        }
                        flag_led = 0;
                    }
                } else {
                    if (millis() - timing > 500) {
                        timing = millis();
                        for (uint8_t i = 0; i < deviceCount; i++) {
                            ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
                            ledAdapter->setLedOn(ledAdapter->getRedLedPin(i));
                        }
                        flag_led = 1;
                    }
                }
            }
        }
        else {
            // Slow blinking heartbeat for normal operation
            if (moduleType == MODULE_TYPE_SENSOR) {
                if (flag_led == 1) {
                    if (millis() - timing > 1000) {
                        timing = millis();
                        flag_led = 0;
                        ledAdapter->setLedOn(ledAdapter->getGreenLedPin(0));
                    }
                } else {
                    if (millis() - timing > 5000) {
                        timing = millis();
                        flag_led = 1;
                        ledAdapter->setLedOff(ledAdapter->getGreenLedPin(0));
                    }
                }
            }
            
            if (moduleType == MODULE_TYPE_DEVICE || moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE || moduleType == MODULE_TYPE_HYDRO) {
                if (flag_led == 1) {
                    if (millis() - timing > 1000) {
                        timing = millis();
                        for (uint8_t i = 0; i < deviceCount; i++) {
                            if (pinValue[i] == OFF) {
                                ledAdapter->setLedOn(ledAdapter->getGreenLedPin(i));
                            } else {
                                ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
                            }
                        }
                        flag_led = 0;
                    }
                } else {
                    if (millis() - timing > 5000) {
                        timing = millis();
                        for (uint8_t i = 0; i < deviceCount; i++) {
                            ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
                        }
                        flag_led = 1;
                    }
                }
            }
        }
    }
}

// Turn specific LED on
void LedUseCases::ledON(int i) {
    if (i < deviceCount) {
        ledAdapter->setLedOff(ledAdapter->getGreenLedPin(i));
        key[i] = 1;
    }
}

// Turn specific LED off
void LedUseCases::ledOFF(int i) {
    if (i < deviceCount) {
        if (flag_led == 0 && key[i] == 1) {
            ledAdapter->setLedOn(ledAdapter->getGreenLedPin(i));
        }
        key[i] = 0;
    }
}

// Update method to be called in main loop
void LedUseCases::update(bool mqttConnected) {
    ledBlinks(mqttConnected);
}

void LedUseCases::delay(unsigned int ms) {

}
unsigned long LedUseCases::millis() {
}