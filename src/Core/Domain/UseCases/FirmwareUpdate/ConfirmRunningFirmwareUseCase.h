#pragma once

#include "Core/Domain/Interfaces/UseCases/IConfirmRunningFirmwareUseCase.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IOtaPartitionControl.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "constants.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

/**
 * @brief Confirm a freshly booted image by working, not by starting.
 *
 * With verifyRollbackLater() overridden, a new image boots on probation: unless somebody calls
 * esp_ota_mark_app_valid_cancel_rollback(), the next reset - a crash, the watchdog, a power
 * cycle - returns the previous image by the bootloader alone. This class is that somebody, and
 * its definition of "the image works" is the module reaching its working state:
 *
 *  - the module has been up for at least the task watchdog threshold - the main gate: living
 *    longer than WDT_TIMEOUT_S means one full round of every liveness check the device runs
 *    has passed without tripping, since that threshold is by construction the longest any
 *    stall can go unnoticed;
 *  - the control loop is the CONFIGURED_DONE one and has completed a run of iterations, which
 *    means setup survived - the settings and the module structure were read (the structure
 *    carries the module uid only after the setup flow filled it), every service initialized,
 *    and the loop is turning without crashing or starving the task watchdog;
 *  - network availability is deliberately NOT in the criterion: a module on a greenhouse shelf
 *    with no internet is legitimate and must not be rolled back for it.
 *
 * The probation window is chosen generously rather than thriftily, because the cost of error
 * is asymmetric: a false rollback is cheap - the previous working image returns, the backend
 * sees the old version, the update is simply repeated - while a false confirmation is terminal:
 * a broken image pinned for good, recoverable only with physical access.
 *
 * Confirmation happens at most once per boot; a boot whose image is not on probation (every
 * normal boot) is recognized and left alone.
 */
class ConfirmRunningFirmwareUseCase : public IConfirmRunningFirmwareUseCase {
public:
    /**
     * How many completed CONFIGURED_DONE iterations count as "the module works". At the loop's
     * pace this is on the order of seconds of sustained operation - enough to know the loop is
     * really turning (a firmware that crashes in a service does so on the first iterations),
     * short enough that the probation window closes well before anyone reaches for the power
     * switch to "fix" a module that looks done booting.
     */
    static constexpr uint32_t REQUIRED_HEALTHY_ITERATIONS = 100;

    /**
     * The main gate: minimum uptime before a probation image may confirm itself. Derived from
     * the task watchdog threshold, not picked: WDT_TIMEOUT_S is the longest any subscribed
     * task can stall unnoticed, so an image alive past it has survived one full round of every
     * liveness check the device runs. Derived by expression so the two move together; the
     * assert catches the one mistake the derivation allows - the expression replaced by a
     * literal shorter than the watchdog round it stands for.
     */
    static constexpr uint32_t MIN_UPTIME_MS = (uint32_t)WDT_TIMEOUT_S * 1000UL;
    static_assert(MIN_UPTIME_MS >= (uint32_t)WDT_TIMEOUT_S * 1000UL,
                  "the probation window must cover at least one full watchdog round");

    /**
     * Confirmation write attempts before giving up. The write is one otadata flash operation;
     * a transient refusal must not be terminal - the next reset would roll back a working
     * image - so it is retried on the following iterations, bounded so a flash that refuses
     * for good produces a loud verdict instead of a forever-retrying loop.
     */
    static constexpr uint8_t MAX_CONFIRM_ATTEMPTS = 3;

    ConfirmRunningFirmwareUseCase(IOtaPartitionControl* otaControl,
                                  ModuleStructure* moduleStructure,
                                  IArduinoAdapter* arduinoAdapter,
                                  ILogger* logger = nullptr,
                                  IDebugSystem* debugSystem = nullptr,
                                  IOtaUpdateLog* otaLog = nullptr)
        : _otaControl(otaControl),
          _moduleStructure(moduleStructure),
          _arduino(arduinoAdapter),
          _logger(logger),
          _debugSystem(debugSystem),
          _otaLog(otaLog)
    {
    }

    void onControlLoopIteration() override
    {
        if (!_bootOutcomeLogged)
        {
            _bootOutcomeLogged = true;
            logBootOutcome();
        }

        if (_done)
        {
            return;
        }

        if (_otaControl == nullptr)
        {
            _done = true;
            return;
        }

        // Iterations start counting only once the structure carries the module uid: the setup
        // flow fills it after the settings and the stored structure were read successfully, so
        // an image that boots but cannot read its own configuration never confirms itself.
        if (_moduleStructure == nullptr || _moduleStructure->growdirectorUid.empty())
        {
            return;
        }

        _healthyIterations++;
        if (_healthyIterations < REQUIRED_HEALTHY_ITERATIONS)
        {
            return;
        }

        // The main gate. An adapter that cannot answer keeps the image unconfirmed: the cost
        // asymmetry decides the direction of every doubt here - an unearned rollback is cheap,
        // an unearned confirmation is terminal.
        if (_arduino == nullptr || _arduino->millis() < MIN_UPTIME_MS)
        {
            return;
        }

        if (_otaControl->runningImageState() != OtaImageState::PENDING_VERIFY)
        {
            // Nothing on probation - the normal boot of an already confirmed image.
            _done = true;
            return;
        }

        _confirmAttempts++;
        if (_otaControl->markRunningImageValid())
        {
            _done = true;
            otaWrite("CONFIRMED: image on %s marked valid after %u healthy loop iterations "
                     "and %lu ms of uptime",
                     _otaControl->runningPartitionLabel(), (unsigned)_healthyIterations,
                     (unsigned long)_arduino->millis());
            log("FIRMWARE_CONFIRM: running image confirmed");
            return;
        }

        if (_confirmAttempts >= MAX_CONFIRM_ATTEMPTS)
        {
            // Left unconfirmed on purpose: if the bootloader keeps refusing the confirmation,
            // pretending otherwise only hides it. The next reset rolls back, and the log says
            // why - loudly, once, after the bounded retries are spent.
            _done = true;
            otaWrite("ERROR: confirming the running image failed %u times - giving up; "
                     "next reset rolls back",
                     (unsigned)_confirmAttempts);
            log("FIRMWARE_CONFIRM: confirmation failed");
            return;
        }

        // A single refused write is not a verdict on the image: the write is one otadata flash
        // operation and gets its bounded retries on the following iterations.
        otaWrite("ERROR: confirming the running image failed (attempt %u of %u) - retrying",
                 (unsigned)_confirmAttempts, (unsigned)MAX_CONFIRM_ATTEMPTS);
    }

    // --- Observation ---------------------------------------------------------------------------

    bool finished() const { return _done; }
    uint32_t healthyIterations() const { return _healthyIterations; }

private:
    /**
     * @brief Say once, on the first loop iteration of the boot, what the bootloader decided.
     *
     * The device's answer to "did the update take?": after a rollback the rejected image sits
     * in the other app slot in the aborted state, so that boot names the rollback outright; a
     * probation boot announces itself so the confirmation that follows (or the rollback that
     * follows a missing one) can be read off the same log. Lines go to the OTA log, which the
     * loop already persists and flushes over MQTT - no protocol is extended. The version the
     * module reports to the server is the running image's, so after a rollback the backend sees
     * the old version again without any help from here.
     */
    void logBootOutcome()
    {
        if (_otaControl == nullptr)
        {
            return;
        }

        const OtaImageState running = _otaControl->runningImageState();
        const OtaImageState other = _otaControl->otherImageState();

        otaWrite("BOOT: running %s state=%s, other %s state=%s",
                 _otaControl->runningPartitionLabel(), getOtaImageStateName(running),
                 _otaControl->otherPartitionLabel(), getOtaImageStateName(other));

        if (other == OtaImageState::ABORTED)
        {
            otaWrite("ROLLBACK: the bootloader rejected the update in %s and booted this image",
                     _otaControl->otherPartitionLabel());
        }

        if (running == OtaImageState::PENDING_VERIFY)
        {
            otaWrite("TRIAL: this image is on probation - a reset before confirmation rolls back");
        }
    }

    void otaWrite(const char* format, ...)
    {
        if (_otaLog == nullptr)
        {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _otaLog->write(buffer);
    }

    void log(const char* format, ...)
    {
        if (_logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::UPDATE_FIRMWARE_DEBUG))
        {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _logger->println(DebugFeatures::UPDATE_FIRMWARE_DEBUG, buffer);
    }

    IOtaPartitionControl* _otaControl;
    ModuleStructure* _moduleStructure;
    IArduinoAdapter* _arduino;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IOtaUpdateLog* _otaLog;

    uint32_t _healthyIterations = 0;
    uint8_t _confirmAttempts = 0;
    bool _done = false;
    bool _bootOutcomeLogged = false;
};
