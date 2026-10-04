#ifndef ON_CHANGE_LOCAL_IP_USE_CASE_H
#define ON_CHANGE_LOCAL_IP_USE_CASE_H

#include <cstdarg>
#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/IWifiManager.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/UseCases/Network/LinkTroubleshooting.h"
#include <string.h>

/**
 * @brief Notice that the local IP changed and tell the server, without stopping the control loop.
 *
 * perform() is called from every control-loop iteration, so what it costs when the address *has*
 * changed is what the loop costs. It therefore never talks to the network itself: it submits the
 * structure to the outbox and returns, and onOutboxCompletion() - also on the control loop -
 * decides what the outcome means - including the rules and the catalog, which are asked for there,
 * once the structure has settled, and not beside it. Three properties are the point of the design
 * rather than side effects of it:
 *
 *  1. **The address counts as reported only once the server has confirmed it.** `_oldIpStr` moves
 *     forward on a delivered completion and at no other moment. Advancing it before the send
 *     would make `ipChanged` false while the server still holds the old address: the structure
 *     would never be offered again, and only a restart could recover.
 *  2. **A retry is an ordinary outbox retry.** While a structure is in flight this use case holds
 *     its ticket and submits nothing more; when the completion says undelivered, the address is
 *     still unreported, so the next iteration offers it again. The rate is therefore bounded by
 *     how long the outbox spends on one request, not by the loop period.
 *  3. **An undelivered structure never restarts the module.** A module that has been running for
 *     months does not reboot because a DHCP lease renewal could not be reported: for the backend the
 *     new address is a field to update, not a reason to re-establish the relationship. A module the
 *     server has never seen (`wasInitialSyncCompleted == 0`) cannot work until its structure is
 *     delivered, since that structure is what registers it - so it offers the structure again, after
 *     a pause that grows 1, 2, 5, then 10 minutes, while everything else it runs keeps running.
 */
class OnChangeLocalIpUseCase : public IOnChangeLocalIpUseCase
{
private:
    ILogger &_logger;
    IDebugSystem &_debugSystem;
    IWifiAdapter &_wifiAdapter;
    IWifiManager &_wifiManager;
    ModuleStructure &_moduleStructure;
    IModuleConfig &_moduleConfig;
    ISyncRulesUseCase &_syncRulesUseCase;
    ISyncSensorTypesUseCase &_syncSensorTypesUseCase;
    IOutbox *_outbox = nullptr;
    char _oldIpStr[16];     // The address the server has confirmed. Empty until it confirms one.
    char _pendingIpStr[16]; // The address of the structure currently in flight, if any.
    uint32_t _pendingTicket = IOutbox::kRejected;
    uint32_t _submitCount = 0;
    uint32_t _undeliveredCount = 0;
    uint32_t _settledCount = 0;
    // perform() runs on every iteration of the control loop, so a state that lasts is reported on
    // the iteration it begins and not on every iteration it lasts: a module without an address or
    // without a link would otherwise write the same line at the loop's own rate.
    bool _noValidIpReported = false;
    bool _linkDownReported = false;
    // The registration's retry: an unregistered module's structure that went undelivered is offered
    // again only once the pause has run, counted from that completion. The step picks the pause.
    IArduinoAdapter *_clock = nullptr;
    uint8_t _registrationRetryStep = 0;
    bool _registrationRetryPaused = false;
    uint32_t _registrationRetryFromMs = 0;
    uint32_t _registrationRetryPauseMs = 0;

public:
    OnChangeLocalIpUseCase(
        IWifiAdapter &wifiAdapter,
        IWifiManager &wifiManager,
        ModuleStructure &moduleStructure,
        IModuleConfig &moduleConfig,
        ISyncRulesUseCase &syncRulesUseCase,
        ISyncSensorTypesUseCase &syncSensorTypesUseCase,
        ILogger &loggerInstance,
        IDebugSystem &debugSystemInstance)
        : _wifiAdapter(wifiAdapter),
          _wifiManager(wifiManager),
          _moduleStructure(moduleStructure),
          _moduleConfig(moduleConfig),
          _syncRulesUseCase(syncRulesUseCase),
          _syncSensorTypesUseCase(syncSensorTypesUseCase),
          _logger(loggerInstance),
          _debugSystem(debugSystemInstance)
    {

        // Initialize oldIpStr to empty string
        _oldIpStr[0] = '\0';
        _pendingIpStr[0] = '\0';
        log("OnChangeLocalIpUseCase: Initialized");
    }

    /**
     * @brief The queue the structure upload travels through.
     *
     * Without one this use case reports nothing: sending from here would put a 30 s POST on
     * the control loop, which the outbox route exists to avoid.
     */
    void setOutbox(IOutbox *outbox)
    {
        _outbox = outbox;
    }

    /**
     * @brief The clock the registration's retry pauses are measured on.
     *
     * Without one an unregistered module offers its structure again on the next iteration after an
     * undelivered one, as a registered module does.
     */
    void setClock(IArduinoAdapter *clock)
    {
        _clock = clock;
    }

    /** @brief Structures this use case has handed to the outbox. */
    uint32_t submitCount() const { return _submitCount; }

    /** @brief Structures the outbox gave up on. */
    uint32_t undeliveredCount() const { return _undeliveredCount; }

    /** @brief The address the server has confirmed; empty until it has confirmed one. */
    const char *reportedIp() const override { return _oldIpStr; }

    /** @brief Structures of this use case that settled, delivered or not. */
    uint32_t structuresSettled() const override { return _settledCount; }

    void perform() override
    {
        // log("OnChangeLocalIpUseCase: Performing local IP update");

        // Get the new local IP from the WiFi adapter
        const char *newIpStr = _wifiAdapter.getLocalIP();

        // Skip processing if IP is missing or is 0.0.0.0
        if (newIpStr == nullptr || strcmp(newIpStr, "0.0.0.0") == 0)
        {
            if (!_noValidIpReported)
            {
                _noValidIpReported = true;
                log("OnChangeLocalIpUseCase: No valid IP available (null or 0.0.0.0)");
            }
            _linkDownReported = false;
            return;
        }
        _noValidIpReported = false;

        // Check if IP has changed
        bool ipChanged = (strlen(_oldIpStr) == 0 || strcmp(_oldIpStr, newIpStr) != 0);

        if (ipChanged)
        {
            // Only process if WiFi is connected
            if (_wifiAdapter.isConnected())
            {
                _linkDownReported = false;

                // A structure carrying this very address is already travelling. Submitting a
                // second one would put two copies of the same message on the wire and leave this
                // use case waiting on a ticket it had already replaced. Note what this does *not*
                // suppress: an address different from the one in flight is a different message and
                // is submitted at once, superseding the ticket being waited for.
                if (_pendingTicket != IOutbox::kRejected && strcmp(_pendingIpStr, newIpStr) == 0)
                {
                    return;
                }

                // An unregistered module whose last structure went undelivered offers the next one
                // only once its pause has run.
                if (registrationRetryIsPaused())
                {
                    return;
                }

                // Below the guard: the change is said once per structure that reports it, not on
                // every iteration that finds the address still unreported while it travels.
                log("OnChangeLocalIpUseCase: IP changed - old IP: %s, new IP: %s",
                    (strlen(_oldIpStr) > 0 ? _oldIpStr : "not set"), newIpStr);
                log("OnChangeLocalIpUseCase: WiFi connected, updating module structure");

                // Update IP in all nodes in the module structure
                std::string newIpString(newIpStr);
                for (auto &node : _moduleStructure.nodes)
                {
                    node.ip = newIpString;
                    log("OnChangeLocalIpUseCase: Updated IP for node to %s", newIpStr);
                }

                // Update SSID in module structure
                int networkIndex = 0; // Assuming the connected network is at index 0
                const char *ssid = _wifiAdapter.getSSID(networkIndex);
                if (ssid != nullptr)
                {
                    std::string ssidString(ssid);
                    for (auto &node : _moduleStructure.nodes)
                    {
                        node.ssid = ssidString;
                        log("OnChangeLocalIpUseCase: Updated SSID for node to %s", ssid);
                    }
                }

                log("OnChangeLocalIpUseCase: Sending updated module structure to server");
                if (_outbox == nullptr) {
                    // Nothing to report through, and no fallback: a synchronous send from here is
                    // exactly the 30 s the control loop must not spend. _oldIpStr stays where it
                    // is, so the address is still unreported and will be offered again.
                    log("OnChangeLocalIpUseCase: No outbox - the structure cannot be reported");
                    return;
                }

                const uint32_t ticket = _outbox->submitModuleStructure(_moduleStructure, ApiPriority::BOTH);
                if (ticket == IOutbox::kRejected) {
                    // The event ring was full. Nothing was consumed - _oldIpStr is untouched - so
                    // the next iteration offers the same structure again.
                    log("OnChangeLocalIpUseCase: The outbox refused the structure; will offer it again");
                    return;
                }

                _pendingTicket = ticket;
                strncpy(_pendingIpStr, newIpStr, sizeof(_pendingIpStr) - 1);
                _pendingIpStr[sizeof(_pendingIpStr) - 1] = '\0';
                _submitCount++;
                log("OnChangeLocalIpUseCase: Structure submitted to the outbox, ticket %u",
                    (unsigned)ticket);

                // The rules and the catalog are asked for once this structure has settled - see
                // onOutboxCompletion(): the structure is what the server knows the module by, and
                // asking before it has gone out puts both requests in the queue beside it, where
                // the outbox's rotation can take either one first.

                // Process additional server communications
                log("OnChangeLocalIpUseCase: Processing delete command and restoring account info");
                // These would be implemented in your HybridAPIService
                // _apiService.processDeleteGdCommand();
                // _apiService.restoreAccountInfoFromServer();
            }
            else if (!_linkDownReported)
            {
                _linkDownReported = true;
                log("OnChangeLocalIpUseCase: WiFi not connected, skipping update");
            }
        }
        else
        {
            // log("OnChangeLocalIpUseCase: IP has not changed, no update needed");
        }
    }

    void onOutboxCompletion(const OutboxCompletion &completion) override
    {
        if (completion.kind != OutboxKind::MODULE_STRUCTURE)
        {
            return;
        }

        // Structures are also submitted by CommandService, after a command changed the module.
        // The ticket is what separates ours from those; without the check this use case would
        // treat someone else's delivery as confirmation that our address arrived.
        if (_pendingTicket == IOutbox::kRejected || completion.ticket != _pendingTicket)
        {
            return;
        }

        _pendingTicket = IOutbox::kRejected;
        _settledCount++;

        if (completion.delivered)
        {
            // Only here. The server now knows this address, so the next perform() has nothing to
            // report until it changes again.
            strncpy(_oldIpStr, _pendingIpStr, sizeof(_oldIpStr) - 1);
            _oldIpStr[sizeof(_oldIpStr) - 1] = '\0';
            log("OnChangeLocalIpUseCase: Structure delivered after %u attempts - %s is now the "
                "address the server knows",
                (unsigned)completion.attempts, _oldIpStr);

            // Mark initial sync as completed after first successful structure send
            ISettings *settings = _moduleConfig.getSettings();
            if (settings != nullptr && settings->getWasInitialSyncCompleted() == 0)
            {
                settings->setWasInitialSyncCompleted(1);
                log("OnChangeLocalIpUseCase: Initial sync completed - module registered on server");
            }
            askForTheRulesAndTheCatalog();
            return;
        }

        _undeliveredCount++;

        // Not delivered. _oldIpStr was not advanced, so perform() will offer the same structure
        // again on the next iteration - that is the retry, and it is the outbox's, not a second
        // pass through this branch.
        ISettings *settings = _moduleConfig.getSettings();
        const bool everRegistered = (settings != nullptr) && (settings->getWasInitialSyncCompleted() != 0);

        if (everRegistered)
        {
            // A module the server already knows keeps running on the configuration it has: its
            // rules, its sensors and its devices are all still valid, and none of them depend on
            // the server having the current address. Restarting would cost the grower an outage
            // to fix a stale field.
            log("NET: Structure not delivered after %u attempts; the module is registered, so it "
                "keeps running and the structure will be offered again",
                (unsigned)completion.attempts);
            askForTheRulesAndTheCatalog();
            return;
        }

        // A module the server has never seen cannot work until it is registered: it has no rules, no
        // sensor types and no identity on the backend, and its structure is what registers it. The
        // structure is offered again after a pause - 1, 2 and 5 minutes, then every 10, the pauses
        // between the radio's restarts - and the module keeps running meanwhile. The rules and the
        // catalog are asked for once the structure is delivered, as on every delivery.
        _registrationRetryPauseMs = LinkTroubleshooting::restartPauseMs(_registrationRetryStep);
        if (_registrationRetryStep + 1 < LinkTroubleshooting::RESTART_PAUSE_COUNT)
        {
            _registrationRetryStep++;
        }
        _registrationRetryPaused = true;
        _registrationRetryFromMs = nowMs();
        log("NET: Structure not delivered after %u attempts; the module is not registered yet, so the "
            "structure will be offered again in %u s",
            (unsigned)completion.attempts, (unsigned)(_registrationRetryPauseMs / 1000));
    }

private:
    /**
     * @brief The rules and the catalog, asked for once the structure of an address has settled.
     *
     * Delivered or not: a registered module whose structure the server did not take still works on
     * the rules and the catalog it holds, and asks for both all the same. Not for a module that is
     * not registered yet - that module asks once its structure is delivered.
     */
    void askForTheRulesAndTheCatalog()
    {
        log("OnChangeLocalIpUseCase: Triggering rule synchronization after IP change");
        _syncRulesUseCase.forceSync();

        log("OnChangeLocalIpUseCase: Triggering sensor types synchronization after IP change");
        _syncSensorTypesUseCase.forceSync();
    }

    uint32_t nowMs() const
    {
        return _clock != nullptr ? static_cast<uint32_t>(_clock->millis()) : 0;
    }

    /** @brief Whether the registration's pause is still running; says so once when it ends. */
    bool registrationRetryIsPaused()
    {
        if (!_registrationRetryPaused)
        {
            return false;
        }
        if (_clock != nullptr && nowMs() - _registrationRetryFromMs < _registrationRetryPauseMs)
        {
            return true;
        }
        _registrationRetryPaused = false;
        log("NET: The registration's pause is over; offering the structure again");
        return false;
    }

    void log(const char *format, ...)
    {
        if (_debugSystem.isFeatureEnabled(DebugFeatures::IP_CHANGE_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger.println(DebugFeatures::IP_CHANGE_DEBUG, buffer);
        }
    }
};

#endif // ON_CHANGE_LOCAL_IP_USE_CASE_H
