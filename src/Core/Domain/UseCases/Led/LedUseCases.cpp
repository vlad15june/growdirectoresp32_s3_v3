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
    // The render task holds this object; it must be gone before the object is.
    _bootStage = LED_BOOT_STAGE_RUNNING;
    stopBootIndicationTask();
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
    log("Setting yellow LED on during module load");
    ledAdapter->setYellowLedOn(0);
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

void LedUseCases::renderConfiguredDoneOnline(uint8_t ledsCount)
{
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
}

void LedUseCases::handleConfiguredDoneOnline(unsigned long currentTime, uint8_t ledsCount)
{
    if (currentTime - lastBlinkTime <= (ledState ? BLINK_SLOW_ON_TIME : BLINK_SLOW_OFF_TIME))
    {
        return;
    }

    lastBlinkTime = currentTime;
    ledState = !ledState;

    renderConfiguredDoneOnline(ledsCount);

    log("CONFIGURED_DONE: Green LED blink state: %s", ledState ? "ON" : "OFF");
}

bool LedUseCases::scanEntryRunning() const
{
    return scanEntryStatus != nullptr && scanEntryStatus->isScanEntryRunning();
}

/**
 * @brief Two short flashes every five seconds, on top of the working green.
 *
 * The pair is placed by absolute time rather than by a timer of its own, so it keeps its shape
 * whatever the green heartbeat happens to be doing when it lands - and so a module that starts a
 * scan mid-heartbeat does not have to wait for the heartbeat to come round.
 *
 * Only the first LED, which is the one that carries module-level state everywhere else in this
 * class; on a relay module the other three keep saying what their relays are doing.
 *
 * @return true while the pair owns the LED - the caller must not draw the working pattern then
 */
bool LedUseCases::renderScanEntryBlink(unsigned long currentTime, uint8_t ledsCount)
{
    if (ledsCount == 0)
    {
        return false;
    }

    const unsigned long phase = currentTime % SCAN_BLINK_PERIOD;
    if (phase >= SCAN_BLINK_WINDOW)
    {
        return false;
    }

    // Written at the edges only: the control loop comes round every few milliseconds, and a pulse
    // redrawn on each of them is thirty GPIO writes where two are meant.
    const int8_t wanted =
        ((phase % SCAN_BLINK_PULSE_PERIOD) < SCAN_BLINK_PULSE_ON_TIME) ? 1 : 0;
    if (wanted != _scanPulseState)
    {
        _scanPulseState = wanted;
        if (wanted != 0)
        {
            ledAdapter->setGreenLedOn(0);
        }
        else
        {
            ledAdapter->setGreenLedOff(0);
        }
    }
    return true;
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
            // Red wins: a module that has lost the server has something worse to say than that
            // it is looking at its sensors.
            _scanBlinkWindowOpen = false;
            _scanPulseState = -1;
            handleConfiguredDoneOffline(currentTime, ledsCount);
        }
        else
        {
            const bool blinkHasTheLed =
                scanEntryRunning() && renderScanEntryBlink(currentTime, ledsCount);
            if (blinkHasTheLed)
            {
                _scanBlinkWindowOpen = true;
                break;
            }
            if (_scanBlinkWindowOpen)
            {
                // The pair is over. The working pattern only draws at its own edges, which can be
                // five seconds away, so what it had on screen is put back here.
                _scanBlinkWindowOpen = false;
                _scanPulseState = -1;
                renderConfiguredDoneOnline(ledsCount);
            }
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

// --- The boot indication ----------------------------------------------------------------------

/**
 * @brief One frame of the boot indication. Runs on the render task and on the setup task both.
 *
 * Writes only when the wanted state differs from the one already on the LED, so a 25 ms tick does
 * not turn into 40 GPIO writes a second - and, with LED debugging on, 40 log lines.
 *
 * The pattern is drawn from absolute time rather than from a timer of its own, so its edges do not
 * drift with whatever the setup task is blocking on.
 */
void LedUseCases::renderBootStage(unsigned long currentTime)
{
    if (_bootStage == LED_BOOT_STAGE_RUNNING || ledAdapter == nullptr)
    {
        return;
    }

    const int8_t wanted = ((currentTime % BOOT_BLINK_PERIOD) < BOOT_BLINK_ON_TIME) ? 1 : 0;
    if (wanted == _bootLedState)
    {
        return;
    }
    _bootLedState = wanted;

    if (wanted != 0)
    {
        ledAdapter->setGreenLedOn(0);
    }
    else
    {
        ledAdapter->setGreenLedOff(0);
    }
}

/**
 * @brief The render task, created when the boot indication starts.
 *
 * Setup blocks: WiFi association is one call that does not return for up to a minute, and the
 * first exchanges with the backend are more of the same. Nothing on that task can draw a pattern
 * while it waits, which is why this one exists - and why it ends the moment setup does.
 */
void LedUseCases::startBootIndicationTask()
{
    if (_bootTaskStarted || taskScheduler == nullptr || arduinoAdapter == nullptr)
    {
        return;
    }

    _bootTaskAlive = true;
    // The body returns once the boot is over, which ends the task. It never puts itself under
    // the watchdog, so it has no entry there to take off first.
    _bootTaskStarted = taskScheduler->createTask(
        "LedBootStage", BOOT_TASK_STACK_BYTES, BOOT_TASK_PRIORITY, [this]() {
            while (_bootStage != LED_BOOT_STAGE_RUNNING)
            {
                renderBootStage(arduinoAdapter->millis());
                taskScheduler->delayMs(BOOT_TASK_TICK_MS);
            }
            _bootTaskAlive = false;
        });

    if (!_bootTaskStarted)
    {
        _bootTaskAlive = false;
        _taskStartFailureCount++;
        log("Boot indication task could not be created - the boot shows a level, not a pattern");
    }
}

/**
 * @brief Wait for the render task to let go of the LED before the working indication takes it.
 *
 * The boot is already over when this is called, so the task will stop at its next tick; what is
 * waited for is that it has actually stopped. Skipping the wait would leave two writers on the
 * LED for a moment, and the loser is the working pattern, which redraws only at its own edges -
 * up to five seconds later.
 */
void LedUseCases::stopBootIndicationTask()
{
    if (!_bootTaskStarted)
    {
        return;
    }

    for (uint32_t waited = 0; _bootTaskAlive && waited < BOOT_TASK_STOP_TIMEOUT_MS;
         waited += BOOT_TASK_TICK_MS)
    {
        taskScheduler->delayMs(BOOT_TASK_TICK_MS);
    }

    _bootTaskStarted = false;

    if (_bootTaskAlive)
    {
        _bootTaskStopTimeoutCount++;
        log("Boot indication task did not confirm it had stopped within %u ms",
            (unsigned)BOOT_TASK_STOP_TIMEOUT_MS);
    }
}

void LedUseCases::setBootStage(LedBootStageEnum stage)
{
    if (stage == _bootStage)
    {
        return;
    }

    log("Boot stage: %d -> %d", (int)_bootStage, (int)stage);
    _bootStage = stage;

    if (stage == LED_BOOT_STAGE_RUNNING)
    {
        stopBootIndicationTask();
        return;
    }

    // The boot indication owns the first LED from here until the working indication takes it, so
    // it starts by taking it: the yellow-off writes both colours of that LED low, whatever the
    // pins were left holding.
    if (ledAdapter != nullptr)
    {
        ledAdapter->setYellowLedOff(0);
    }

    // Drawn here as well as on the task so that the pattern is on the LED the instant the boot
    // enters it, and so that a module without a scheduler still shows a light.
    _bootLedState = -1;
    renderBootStage(arduinoAdapter != nullptr ? arduinoAdapter->millis() : 0);

    startBootIndicationTask();
}

LedBootStageEnum LedUseCases::bootStage() const
{
    return _bootStage;
}
