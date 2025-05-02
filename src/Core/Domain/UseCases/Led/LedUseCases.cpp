#include "LedUseCases.h"
#include "esp_timer.h" // For esp_timer functions

// Constructor
LedUseCases::LedUseCases(ILedAdapter* adapter, IModuleConfig* moduleConfig, IArduinoAdapter* arduinoAdapter,
                        ILogger* loggerInstance, IDebugSystem* debugSystemInstance, uint8_t index)
    : ledAdapter(adapter),
      moduleConfig(moduleConfig),
      arduinoAdapter(arduinoAdapter),
      logger(loggerInstance),
      debugSystem(debugSystemInstance),
      buttonIndex(index) {
    log("LedUseCases initialized for button index %d", buttonIndex);
}

// Destructor
LedUseCases::~LedUseCases() {
    // No dynamic memory to clean up
    log("LedUseCases destroyed");
}

void LedUseCases::log(const char *format, ...)
{
    if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::LED_DEBUG))
    {
        char buffer[128];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        logger->println(buffer);
    }
}

// Turn LEDs on during module load
void LedUseCases::ledOnLoad() {
    log("Setting all yellow LEDs on during module load");
    ledAdapter->setAllYellowLedOn();    
}

// Turn LEDs off during module unload
void LedUseCases::ledOffLoad() {
    log("Setting all yellow LEDs off during module unload");
    ledAdapter->setAllYellowLedOff();
}

// Blink green LED twice
void LedUseCases::ledBlinkGreen2Times() {
    log("Blinking green LED 2 times");
    ledAdapter->setGreenLedOn(0);
    arduinoAdapter->delay(500);
    ledAdapter->setGreenLedOff(0);
    arduinoAdapter->delay(500);
    ledAdapter->setGreenLedOn(0);
    arduinoAdapter->delay(500);
    ledAdapter->setGreenLedOff(0);
    log("Completed blinking green LED 2 times");
}

// Blink yellow LED (red+green together) twice
void LedUseCases::ledBlinkYellow2Times() {
    log("Blinking yellow LED 2 times");
    ledAdapter->setAllYellowLedOn();
    arduinoAdapter->delay(500);
    ledAdapter->setAllYellowLedOff();
    arduinoAdapter->delay(500);
    ledAdapter->setAllYellowLedOn();
    arduinoAdapter->delay(500);
    ledAdapter->setAllYellowLedOff();
    log("Completed blinking yellow LED 2 times");
}