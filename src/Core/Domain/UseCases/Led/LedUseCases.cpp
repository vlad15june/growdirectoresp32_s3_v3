#include "LedUseCases.h"

// Constructor
LedUseCases::LedUseCases(ILedAdapter *adapter, IModuleConfig *moduleConfig, IArduinoAdapter *arduinoAdapter,
                         ILogger *loggerInstance, IDebugSystem *debugSystemInstance, IDeviceModel *deviceModel)
    : ledAdapter(adapter),
      moduleConfig(moduleConfig),
      arduinoAdapter(arduinoAdapter),
      logger(loggerInstance),
      debugSystem(debugSystemInstance),
      deviceModel(deviceModel)
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

        logger->println(DebugFeatures::LED_DEBUG, buffer);
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
    if (moduleConfig == nullptr || arduinoAdapter == nullptr || ledAdapter == nullptr)
    {
        log("Loop skipped: Required dependencies are null");
        return;
    }

    ISettings *settings = moduleConfig->getSettings();
    if (settings == nullptr)
    {
        log("Loop skipped: Settings is null");
        return;
    }

    // Get the current module configuration status
    uint8_t configStatus = settings->getIfConfigured();
    unsigned long currentTime = arduinoAdapter->millis();
    uint8_t ledsCount = moduleConfig->getLedsCount();

    // Handle LED states based on module configuration status
    switch (configStatus)
    {
    case ModuleModeEnum::USER_CAN_CONFIGURE:
        // One Yellow LED blinks with a 0.5-second on/off cycle, all others are off
        if (currentTime - lastBlinkTime > (ledState ? BLINK_FAST_ON_TIME : BLINK_FAST_OFF_TIME))
        {
            lastBlinkTime = currentTime;
            ledState = !ledState;

            // Turn off all LEDs first
            ledAdapter->setAllRedLedOff();
            ledAdapter->setAllGreenLedOff();

            // If LED should be on, turn on yellow (red+green together) for the first LED only
            if (ledState && ledsCount > 0)
            {
                ledAdapter->setYellowLedOn(0); // Use the first LED for yellow blinking
            }

            log("USER_CAN_CONFIGURE: Yellow LED blink state: %s", ledState ? "ON" : "OFF");
        }
        break;

    case ModuleModeEnum::USER_CAN_RE_CONFIGURE:
        // Green LED blinks with a 0.5-second on/off cycle, all others are off
        if (currentTime - lastBlinkTime > (ledState ? BLINK_FAST_ON_TIME : BLINK_FAST_OFF_TIME))
        {
            lastBlinkTime = currentTime;
            ledState = !ledState;

            // Turn off all LEDs first
            ledAdapter->setAllRedLedOff();
            ledAdapter->setAllGreenLedOff();

            // If LED should be on, turn on green for the first LED only
            if (ledState && ledsCount > 0)
            {
                ledAdapter->setGreenLedOn(0); // Use the first LED for green blinking
            }

            log("USER_CAN_RE_CONFIGURE: Green LED blink state: %s", ledState ? "ON" : "OFF");
        }
        break;

    case ModuleModeEnum::CONFIGURED_DONE:
        if (currentTime - lastBlinkTime > (ledState ? BLINK_SLOW_ON_TIME : BLINK_SLOW_OFF_TIME))
        {
            lastBlinkTime = currentTime;
            ledState = !ledState;

            ledAdapter->setAllRedLedOff();

            if (moduleConfig->getModuleType() == MODULE_TYPE_DEVICE ||
                moduleConfig->getModuleType() == MODULE_TYPE_HYDRO ||
                moduleConfig->getModuleSubType() == MODULE_SUBTYPE_DIMMER)
            {
                if (deviceModel != nullptr)
                {
                    auto devices = deviceModel->getAllDevices();

                    for (uint8_t i = 0; i < ledsCount && i < devices.size(); i++)
                    {
                        bool deviceIsOn = (devices[i]->state == DEVICE_STATE_ON ||
                                           devices[i]->state == DEVICE_STATE_STEP_ON);

                        if (deviceIsOn)
                        {
                            ledAdapter->setGreenLedOn(i);
                        }
                        else
                        {
                            if (ledState)
                            {
                                ledAdapter->setGreenLedOn(i);
                            }
                            else
                            {
                                ledAdapter->setGreenLedOff(i);
                            }
                        }
                    }
                }
                else
                {
                    if (ledState)
                    {
                        ledAdapter->setAllGreenLedOn();
                    }
                    else
                    {
                        ledAdapter->setAllGreenLedOff();
                    }
                }
            }
            else
            {
                if (ledState)
                {
                    ledAdapter->setAllGreenLedOn();
                }
                else
                {
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