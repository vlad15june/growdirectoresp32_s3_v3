#ifndef LED_USE_CASES_H
#define LED_USE_CASES_H

#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/INetworkStatusProvider.h"
#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"
#include "Core/Domain/Interfaces/UseCases/Sensors/IScanEntryStatus.h"
#include "Core/Domain/Enums/LedBootStageEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include <stdarg.h>

// Use case class for LED behaviors and patterns
class LedUseCases : public ILedUseCases {
private:
    ILedAdapter* ledAdapter;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IDeviceModel* deviceModel;
    INetworkStatusProvider* networkStatusProvider;
    ITaskScheduler* taskScheduler = nullptr;
    IScanEntryStatus* scanEntryStatus = nullptr;

    // LED timing constants (in milliseconds)
    static const unsigned long BLINK_FAST_ON_TIME = 500;     // 0.5 seconds on
    static const unsigned long BLINK_FAST_OFF_TIME = 500;    // 0.5 seconds off
    static const unsigned long BLINK_SLOW_ON_TIME = 1000;    // 1 second on
    static const unsigned long BLINK_SLOW_OFF_TIME = 5000;   // 5 seconds off
    
    // The boot pattern: green, twice a second, for the whole of a boot. Fast enough to be read as
    // "working on it" at a glance, and not a rate any working pattern of this class uses - the
    // configuration modes blink at 1 Hz and the working heartbeat is one second in six.
    static const unsigned long BOOT_BLINK_PERIOD = 500;
    static const unsigned long BOOT_BLINK_ON_TIME = 250;

    // The boot indication has a task of its own because the waits it reports on block the task
    // that runs setup: WiFi association is a single call that does not return for up to a minute,
    // and a pattern rendered from that task would be a pattern rendered never.
    static const uint32_t BOOT_TASK_TICK_MS = 25;
    static const uint32_t BOOT_TASK_STOP_TIMEOUT_MS = 500;
    static const uint32_t BOOT_TASK_STACK_BYTES = 3072;
    static const uint8_t BOOT_TASK_PRIORITY = 1;

    // The scan-entry overlay: two short flashes, then a long gap, over whatever the working
    // indication is doing. Absolute time, so the pair keeps its shape whatever the base pattern
    // is in the middle of.
    static const unsigned long SCAN_BLINK_PERIOD = 5000;
    static const unsigned long SCAN_BLINK_PULSE_PERIOD = 160;
    static const unsigned long SCAN_BLINK_PULSE_ON_TIME = 80;
    static const unsigned long SCAN_BLINK_WINDOW = 320;      // two pulses

    // State variables for LED blinking
    unsigned long lastBlinkTime = 0;
    bool ledState = false;

    bool _blinking = false;
    bool _redReported = false;

    // Written by setup, read by the boot indication task.
    volatile LedBootStageEnum _bootStage = LED_BOOT_STAGE_RUNNING;
    volatile bool _bootTaskAlive = false;
    bool _bootTaskStarted = false;
    /** What the boot indication last wrote to the first LED; -1 before it wrote anything. */
    volatile int8_t _bootLedState = -1;
    uint32_t _bootTaskStopTimeoutCount = 0;
    uint32_t _taskStartFailureCount = 0;
    bool _scanBlinkWindowOpen = false;
    /** What the flash pair last wrote to the first green LED; -1 between two pairs. */
    int8_t _scanPulseState = -1;

    void log(const char* format, ...);
    void logOffline(const char* format, ...);

    void logRedIndicationTransition(uint8_t configStatus, bool networkOffline);
    void handleBlinkingMode(unsigned long currentTime);
    void blinkFirstLedFast(unsigned long currentTime, uint8_t ledsCount, bool useYellow, const char* label);
    void handleConfiguredDoneOffline(unsigned long currentTime, uint8_t ledsCount);
    void handleConfiguredDoneOnline(unsigned long currentTime, uint8_t ledsCount);
    void renderConfiguredDoneOnline(uint8_t ledsCount);
    void renderPerDeviceOffline(uint8_t ledsCount);
    void renderPerDeviceOnline(uint8_t ledsCount);
    bool isDeviceModule() const;
    bool isDeviceOn(const Device* device) const;

    void renderBootStage(unsigned long currentTime);
    void startBootIndicationTask();
    void stopBootIndicationTask();
    bool scanEntryRunning() const;
    bool renderScanEntryBlink(unsigned long currentTime, uint8_t ledsCount);

public:
    // Constructor
    LedUseCases(ILedAdapter* adapter, IModuleConfig* moduleConfig, IArduinoAdapter* arduinoAdapter,
                ILogger* loggerInstance = nullptr, IDebugSystem* debugSystemInstance = nullptr,
                IDeviceModel* deviceModel = nullptr,
                INetworkStatusProvider* networkStatusProvider = nullptr);
    
    // Destructor
    ~LedUseCases() override;
    
    // LED behaviors implemented from ILedUseCases
    void ledOnLoad() override;
    void ledOffLoad() override;
    void ledBlinkGreen2Times() override;
    void ledBlinkYellow2Times() override;
    
    // LED state management based on module configuration
    void loop() override;

    void setBlinking(bool blinking) override;
    bool isBlinking() const override;

    void setBootStage(LedBootStageEnum stage) override;
    LedBootStageEnum bootStage() const override;

    /**
     * @brief The task the boot stages are rendered on. Without one they are shown but not blinked.
     *
     * A setter rather than a constructor argument because a module in a configuration mode never
     * reaches a stage that needs it, and because the graph in globals.h wires this object before
     * it has anything else to say to the scheduler.
     */
    void setTaskScheduler(ITaskScheduler* scheduler) { taskScheduler = scheduler; }

    /** @brief Where the overlay asks whether a sensor-discovery pass with an end is under way. */
    void setScanEntryStatus(IScanEntryStatus* status) { scanEntryStatus = status; }

    /**
     * @brief Boots that ended without the render task confirming it had stopped.
     *
     * Zero on a healthy module. Anything else means the working indication and the boot
     * indication were both writing to the LED for a moment.
     */
    uint32_t bootTaskStopTimeoutCount() const { return _bootTaskStopTimeoutCount; }

    /**
     * @brief Render tasks the scheduler refused to create.
     *
     * Anything but zero means a boot put one level on the LED and held it there for the whole
     * boot - a steady light or a dark LED, neither of which says the module is coming up.
     */
    uint32_t taskStartFailureCount() const { return _taskStartFailureCount; }
};

#endif // LED_USE_CASES_H