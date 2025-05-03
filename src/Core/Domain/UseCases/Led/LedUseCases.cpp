#include "LedUseCases.h"

// Constructor
LedUseCases::LedUseCases(ILedAdapter *adapter, IModuleConfig *moduleConfig, IArduinoAdapter *arduinoAdapter,
                         ILogger *loggerInstance, IDebugSystem *debugSystemInstance)
    : ledAdapter(adapter),
      moduleConfig(moduleConfig), //TODO: remove and set all need data from LedUseService
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
    ledAdapter->setYellowLedOn(0);
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

// Main loop for managing LED states based on module configuration
void LedUseCases::loop()
{
    if (moduleConfig == nullptr || arduinoAdapter == nullptr || ledAdapter == nullptr) {
        log("Loop skipped: Required dependencies are null");
        return;
    }

    ISettings* settings = moduleConfig->getSettings();
    if (settings == nullptr) {
        log("Loop skipped: Settings is null");
        return;
    }

    // Get the current module configuration status
    uint8_t configStatus = settings->getIfConfigured();
    unsigned long currentTime = arduinoAdapter->millis();
    uint8_t ledsCount = moduleConfig->getLedsCount();

    // Handle LED states based on module configuration status
    switch (configStatus) {
        case ModuleModeEnum::USER_CAN_CONFIGURE:
            // One Yellow LED blinks with a 0.5-second on/off cycle, all others are off
            if (currentTime - lastBlinkTime > (ledState ? BLINK_FAST_ON_TIME : BLINK_FAST_OFF_TIME)) {
                lastBlinkTime = currentTime;
                ledState = !ledState;
                
                // Turn off all LEDs first
                ledAdapter->setAllRedLedOff();
                ledAdapter->setAllGreenLedOff();
                
                // If LED should be on, turn on yellow (red+green together) for the first LED only
                if (ledState && ledsCount > 0) {
                    ledAdapter->setYellowLedOn(0); // Use the first LED for yellow blinking
                }
                
                log("USER_CAN_CONFIGURE: Yellow LED blink state: %s", ledState ? "ON" : "OFF");
            }
            break;
            
        case ModuleModeEnum::USER_CAN_RE_CONFIGURE:
            // Green LED blinks with a 0.5-second on/off cycle, all others are off
            if (currentTime - lastBlinkTime > (ledState ? BLINK_FAST_ON_TIME : BLINK_FAST_OFF_TIME)) {
                lastBlinkTime = currentTime;
                ledState = !ledState;
                
                // Turn off all LEDs first
                ledAdapter->setAllRedLedOff();
                ledAdapter->setAllGreenLedOff();
                
                // If LED should be on, turn on green for the first LED only
                if (ledState && ledsCount > 0) {
                    ledAdapter->setGreenLedOn(0); // Use the first LED for green blinking
                }
                
                log("USER_CAN_RE_CONFIGURE: Green LED blink state: %s", ledState ? "ON" : "OFF");
            }
            break;
            
        case ModuleModeEnum::CONFIGURED_DONE:
            // Rarely blink Green LED (1 second on / 5 seconds off)
            // If relay is on, corresponding green LED is solid on, others blink
            if (currentTime - lastBlinkTime > (ledState ? BLINK_SLOW_ON_TIME : BLINK_SLOW_OFF_TIME)) {
                lastBlinkTime = currentTime;
                ledState = !ledState;
                
                // Turn off all red LEDs
                ledAdapter->setAllRedLedOff();
                
                // For green LEDs: special handling based on relay state
                if (moduleConfig->getModuleType() == MODULE_TYPE_DEVICE && 
                    (moduleConfig->getModuleSubType() == MODULE_SUBTYPE_RELAY || 
                     moduleConfig->getModuleSubType() == MODULE_SUBTYPE_SOKET)) {
                    
                    // TODO: Check actual relay state here when relay interface is available
                    // For now, assume first LED is for relay and it's on
                    ledAdapter->setGreenLedOn(0); // Keep first LED on for relay
                    
                    // For the rest of the LEDs, apply blinking pattern
                    for (uint8_t i = 1; i < ledsCount; i++) {
                        if (ledState) {
                            ledAdapter->setGreenLedOn(i);
                        } else {
                            ledAdapter->setGreenLedOff(i);
                        }
                    }
                } else {
                    // For non-relay modules, all LEDs follow the blink pattern
                    if (ledState) {
                        ledAdapter->setAllGreenLedOn();
                    } else {
                        ledAdapter->setAllGreenLedOff();
                    }
                }
                
                log("CONFIGURED_DONE: Green LED blink state: %s", ledState ? "ON" : "OFF");
            }
            break;
            
        default:
            // For any other state, turn off all LEDs
            ledAdapter->setAllRedLedOff();
            ledAdapter->setAllGreenLedOff();
            log("Unknown config status: %d, all LEDs off", configStatus);
            break;
    }
}