#include "LedUseCases.h"
#include <cstdarg>
#include <cstdio>

LedUseCases::LedUseCases(ILedAdapter *adapter, IModuleConfig *moduleConfig, IArduinoAdapter *arduinoAdapter,
                         ILogger *loggerInstance, IDebugSystem *debugSystemInstance, IDeviceModel *deviceModel,
                         INetworkStatusProvider *networkStatusProvider)
    : ledAdapter(adapter),
      moduleConfig(moduleConfig),
      arduinoAdapter(arduinoAdapter),
      logger(loggerInstance),
      debugSystem(debugSystemInstance),
      deviceModel(deviceModel),
      networkStatusProvider(networkStatusProvider)
{
}

LedUseCases::~LedUseCases()
{
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

void LedUseCases::logOffline(const char *format, ...)
{
    if (logger == nullptr || debugSystem == nullptr || !debugSystem->isFeatureEnabled(DebugFeatures::OFFLINE_INDICATION_DEBUG))
    {
        return;
    }

    char buffer[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    IDebugFeature *feature = debugSystem->getFeature(DebugFeatures::OFFLINE_INDICATION_DEBUG);
    MqttLoggerMode channel = feature ? feature->getLogChannel() : MqttLoggerMode::MqttAndSerial;
    logger->println(DebugFeatures::OFFLINE_INDICATION_DEBUG, buffer, channel);
}

void LedUseCases::ledOnLoad()
{
    log("Setting all yellow LEDs on during module load");
    ledAdapter->setAllYellowLedOn();
}

void LedUseCases::ledOffLoad()
{
    log("Setting all yellow LEDs off during module unload");
    ledAdapter->setAllYellowLedOff();
}

void LedUseCases::ledBlinkGreen2Times()
{
}

void LedUseCases::ledBlinkYellow2Times()
{
}

void LedUseCases::setBlinking(bool blinking)
{
    _blinking = blinking;
    log("setBlinking: %s", blinking ? "ON" : "OFF");
}

bool LedUseCases::isBlinking() const
{
    return _blinking;
}

bool LedUseCases::isDeviceModule() const
{
    return moduleConfig->getModuleType() == MODULE_TYPE_DEVICE ||
           moduleConfig->getModuleType() == MODULE_TYPE_HYDRO ||
           moduleConfig->getModuleSubType() == MODULE_SUBTYPE_DIMMER;
}

bool LedUseCases::isDeviceOn(const Device *device) const
{
    return device->state == DEVICE_STATE_ON || device->state == DEVICE_STATE_STEP_ON;
}

void LedUseCases::logRedIndicationTransition(uint8_t configStatus, bool networkOffline)
{
    bool redNow = _blinking || (configStatus == ModuleModeEnum::CONFIGURED_DONE && networkOffline);
    if (redNow && !_redReported)
    {
        _redReported = true;
        logOffline("OFFLINE_INDICATION: RED-on reason=%s isNetworkOffline=%d configStatus=%u",
                   _blinking ? "identify_blinking" : "network_offline", networkOffline ? 1 : 0, configStatus);
    }
    else if (!redNow && _redReported)
    {
        _redReported = false;
        logOffline("OFFLINE_INDICATION: RED-off configStatus=%u", configStatus);
    }
}

void LedUseCases::handleBlinkingMode(unsigned long currentTime)
{
    if (currentTime - lastBlinkTime <= (ledState ? BLINK_FAST_ON_TIME : BLINK_FAST_OFF_TIME))
    {
        return;
    }

    lastBlinkTime = currentTime;
    ledState = !ledState;

    ledAdapter->setAllGreenLedOff();

    if (ledState)
    {
        ledAdapter->setAllRedLedOn();
    }
    else
    {
        ledAdapter->setAllRedLedOff();
    }

    log("Blinking mode: Red LED state: %s", ledState ? "ON" : "OFF");
}

void LedUseCases::blinkFirstLedFast(unsigned long currentTime, uint8_t ledsCount, bool useYellow, const char *label)
{
    if (currentTime - lastBlinkTime <= (ledState ? BLINK_FAST_ON_TIME : BLINK_FAST_OFF_TIME))
    {
        return;
    }

    lastBlinkTime = currentTime;
    ledState = !ledState;

    ledAdapter->setAllRedLedOff();
    ledAdapter->setAllGreenLedOff();

    if (ledState && ledsCount > 0)
    {
        if (useYellow)
        {
            ledAdapter->setYellowLedOn(0);
        }
        else
        {
            ledAdapter->setGreenLedOn(0);
        }
    }

    log("%s blink state: %s", label, ledState ? "ON" : "OFF");
}

void LedUseCases::renderPerDeviceOffline(uint8_t ledsCount)
{
    auto devices = deviceModel->getAllDevices();

    for (uint8_t i = 0; i < ledsCount && i < devices.size(); i++)
    {
        if (isDeviceOn(devices[i]))
        {
            if (ledState)
            {
                ledAdapter->setGreenLedOff(i);
                ledAdapter->setRedLedOn(i);
            }
            else
            {
                ledAdapter->setGreenLedOn(i);
                ledAdapter->setRedLedOff(i);
            }
        }
        else
        {
            ledAdapter->setGreenLedOff(i);
            ledAdapter->setRedLedOn(i);
        }
    }
}

void LedUseCases::renderPerDeviceOnline(uint8_t ledsCount)
{
    auto devices = deviceModel->getAllDevices();

    for (uint8_t i = 0; i < ledsCount && i < devices.size(); i++)
    {
        if (isDeviceOn(devices[i]))
        {
            ledAdapter->setGreenLedOn(i);
        }
        else if (ledState)
        {
            ledAdapter->setGreenLedOn(i);
        }
        else
        {
            ledAdapter->setGreenLedOff(i);
        }
    }
}

void LedUseCases::handleConfiguredDoneOffline(unsigned long currentTime, uint8_t ledsCount)
{
    if (isDeviceModule() && deviceModel != nullptr)
    {
        if (currentTime - lastBlinkTime > BLINK_SLOW_ON_TIME)
        {
            lastBlinkTime = currentTime;
            ledState = !ledState;
        }

        renderPerDeviceOffline(ledsCount);
        log("CONFIGURED_DONE offline: per-device green/red state: %s", ledState ? "ON" : "OFF");
        return;
    }

    ledAdapter->setAllGreenLedOff();
    ledAdapter->setAllRedLedOn();
    log("CONFIGURED_DONE offline: solid red (no server connection)");
}

void LedUseCases::handleConfiguredDoneOnline(unsigned long currentTime, uint8_t ledsCount)
{
    if (currentTime - lastBlinkTime <= (ledState ? BLINK_SLOW_ON_TIME : BLINK_SLOW_OFF_TIME))
    {
        return;
    }

    lastBlinkTime = currentTime;
    ledState = !ledState;

    ledAdapter->setAllRedLedOff();

    if (isDeviceModule() && deviceModel != nullptr)
    {
        renderPerDeviceOnline(ledsCount);
    }
    else if (ledState)
    {
        ledAdapter->setAllGreenLedOn();
    }
    else
    {
        ledAdapter->setAllGreenLedOff();
    }

    log("CONFIGURED_DONE: Green LED blink state: %s", ledState ? "ON" : "OFF");
}

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

    uint8_t configStatus = settings->getIfConfigured();
    unsigned long currentTime = arduinoAdapter->millis();
    uint8_t ledsCount = moduleConfig->getLedsCount();

    bool networkOffline = (networkStatusProvider != nullptr && networkStatusProvider->isNetworkOffline());
    logRedIndicationTransition(configStatus, networkOffline);

    if (_blinking)
    {
        handleBlinkingMode(currentTime);
        return;
    }

    switch (configStatus)
    {
    case ModuleModeEnum::USER_CAN_CONFIGURE:
        blinkFirstLedFast(currentTime, ledsCount, true, "USER_CAN_CONFIGURE: Yellow LED");
        break;

    case ModuleModeEnum::USER_CAN_RE_CONFIGURE:
        blinkFirstLedFast(currentTime, ledsCount, false, "USER_CAN_RE_CONFIGURE: Green LED");
        break;

    case ModuleModeEnum::CONFIGURED_DONE:
        if (networkOffline)
        {
            handleConfiguredDoneOffline(currentTime, ledsCount);
        }
        else
        {
            handleConfiguredDoneOnline(currentTime, ledsCount);
        }
        break;

    case ModuleModeEnum::AP_TEST_MODE:
        break;

    default:
        ledAdapter->setAllRedLedOff();
        ledAdapter->setAllGreenLedOff();
        log("Unknown config status: %d, all LEDs off", configStatus);
        break;
    }
}
