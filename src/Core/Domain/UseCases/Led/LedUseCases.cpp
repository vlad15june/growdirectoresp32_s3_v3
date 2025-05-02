#include "LedUseCases.h"

// Constructor
LedUseCases::LedUseCases(ILedAdapter *adapter, IModuleConfig *moduleConfig, IArduinoAdapter *arduinoAdapter,
                         ILogger *loggerInstance, IDebugSystem *debugSystemInstance)
    : ledAdapter(adapter),
      moduleConfig(moduleConfig),
      arduinoAdapter(arduinoAdapter),
      logger(loggerInstance),
      debugSystem(debugSystemInstance)
{
}

// Destructor
LedUseCases::~LedUseCases()
{
    // No dynamic memory to clean up
    log("LedUseCases destroyed");
}

inline void LedUseCases::log(const char *format, ...)
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
void LedUseCases::ledOnLoad()
{
    log("Setting all yellow LEDs on during module load");
    ledAdapter->setAllYellowLedOn();
}

// Turn LEDs off during module unload
void LedUseCases::ledOffLoad()
{
    log("Setting all yellow LEDs off during module unload");
    ledAdapter->setAllYellowLedOff();
}

// Blink green LED twice
void LedUseCases::ledBlinkGreen2Times()
{

}

// Blink yellow LED (red+green together) twice
void LedUseCases::ledBlinkYellow2Times()
{

}