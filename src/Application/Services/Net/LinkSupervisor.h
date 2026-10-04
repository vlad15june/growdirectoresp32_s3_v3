#pragma once

#include "Application/Interfaces/IBootAssociation.h"
#include "Application/Interfaces/IOutbox.h"
#include "Application/Services/BackgroundActs.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Core/Domain/UseCases/Network/LinkTroubleshooting.h"
#include "Core/Domain/Interfaces/INetworkStatusProvider.h"
#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include <cstdarg>
#include <cstdio>

// Replace defines with enum
enum DiagnosticsNetworkState {
    DIAGNOSTICS_NETWORK_STATE_OK = 0,
    DIAGNOSTICS_NETWORK_STATE_ERROR = 1
};

/**
 * @brief What the module does about its link, on a tick of the control loop: finding out which part of
 *        the path failed and restarting the radio or the module only where that helps, and the
 *        module's own offline decision.
 *
 * The decisions are LinkTroubleshooting's, a state machine in the domain; this class feeds it and
 * carries out what it asks. Each superviseOnce() turns the module's readings - the association, the
 * broker and the server, the faults the module's parts reported (ModuleStatusBoard) - into the
 * machine's events, and gives it the tick. What the machine asks
 * for goes where it runs: a probe of the link and a radio restart are jobs of the network task, and
 * come back as completions; a restart of the module is a mode request. The control loop calls it
 * every thirty seconds from millis 0, and asks it for the boot's association on its first iteration.
 *
 * The offline decision is the other half, and it is the module's state as the rest of the firmware
 * reads it (INetworkStatusProvider): the front panel, the rule and catalog syncs that answer a
 * connectivity edge, the firmware update recovery.
 *
 * The diagnostic report reads what was decided here - the network failures, the server's state, the
 * last outage - and decides nothing.
 */
class LinkSupervisor : public INetworkStatusProvider, public IBootAssociation
{
private:
    IRestAPIService *_restApiService;
    IModuleConfig *_moduleConfig;
    ILogger &_logger;
    IDebugSystem &_debugSystem;
    IWifiAdapter *_wifiAdapter;
    INTPTimeAdapter *_timeAdapter;
    IArduinoAdapter *_arduinoAdapter;
    IMqttManager *_mqttManager;
    IOutbox *_outbox = nullptr;
    BackgroundActBoard *_actBoard = nullptr;

    static const int _maxCountOffline = 5;
    static const unsigned long WIFI_OFFLINE_DEBOUNCE_MS = 30 * 1000;
    unsigned long _wifiDownSince = 0;
    bool _mqttConnected;
    int _networkFails = 0;  // Track network failures
    DiagnosticsNetworkState _lastNetworkState = DIAGNOSTICS_NETWORK_STATE_OK;  // Track network state using enum

    static const int MAX_FAILS_HISTORY = 60; // Store last 60 minutes
    unsigned long _failsHistory[MAX_FAILS_HISTORY];
    int _failsHistoryIndex;

    /** Where the module's parts record their faults and the server's answers; optional. */
    ModuleStatusBoard *_statusBoard = nullptr;

    /** The troubleshooting machine's state; changed only by feed(). */
    LinkTroubleshooting::State _trouble;

    /** The radio restart and the probe in flight on the network task, by ticket; kRejected when none. */
    uint32_t _restartTicket = 0;
    uint32_t _probeTicket = 0;

    /** The boot's association: asked for once; in flight until its job comes back or is given up on. */
    bool _bootAssociationAsked = false;
    bool _bootAssociationInFlight = false;
    uint32_t _bootAssociationAskedAtMs = 0;

    /** The last outage the machine recorded, for the diagnostic report. */
    bool _hasOutage = false;
    LinkTroubleshooting::Outage _lastOutage;

    /** The module faults seen on the last tick, as bits of ModuleFault; for the tape. */
    uint8_t _faults = 0;

    int countOffline() const
    {
        return _restApiService ? _restApiService->getCountOffline() : 0;
    }

    void log(const char *format, ...)
    {
        if (!_debugSystem.isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG))
        {
            return;
        }

        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        _logger.println(DebugFeatures::DIAGNOSTIC_DEBUG, buffer);
    }

    void logSetup(const char *format, ...)
    {
        if (!_debugSystem.isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            return;
        }

        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature *feature = _debugSystem.getFeature(DebugFeatures::SETUP_DEBUG);
        MqttLoggerMode channel = feature ? feature->getLogChannel() : MqttLoggerMode::MqttAndSerial;
        _logger.println(DebugFeatures::SETUP_DEBUG, buffer, channel);
    }

    void logOffline(const char *format, ...)
    {
        if (!_debugSystem.isFeatureEnabled(DebugFeatures::OFFLINE_INDICATION_DEBUG))
        {
            return;
        }

        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature *feature = _debugSystem.getFeature(DebugFeatures::OFFLINE_INDICATION_DEBUG);
        MqttLoggerMode channel = feature ? feature->getLogChannel() : MqttLoggerMode::MqttAndSerial;
        _logger.println(DebugFeatures::OFFLINE_INDICATION_DEBUG, buffer, channel);
    }

    // Write board state to a file when network state changes
    void writeBoardStateToFile()
    {
        log("DIAG: Network state changed to %s, writing board state to file",
            _lastNetworkState == DIAGNOSTICS_NETWORK_STATE_OK ? "OK" : "ERROR");

        // TODO: Implement file writing logic or use a file adapter
        // This would depend on the specific implementation for your system

        log("DIAG: Board state recorded");
    }

    /** The module faults the machine is told about, as bits; for the tape. */
    enum ModuleFault : uint8_t
    {
        FAULT_TLS_MEMORY = 1,
        FAULT_NETWORK_TASK = 2,
        FAULT_RADIO_START = 4,
        FAULT_SOCKET = 16
    };

    /** The faults of the module as its parts reported them. */
    uint8_t readModuleFaults(uint32_t nowMs) const
    {
        uint8_t faults = 0;
        if (_statusBoard != nullptr)
        {
            if (_statusBoard->tlsOutOfMemoryWithin(nowMs, TLS_FAULT_FRESH_MS))
            {
                faults |= FAULT_TLS_MEMORY;
            }
            if (_statusBoard->networkTaskRefused())
            {
                faults |= FAULT_NETWORK_TASK;
            }
            if (_statusBoard->radioNotStarted())
            {
                faults |= FAULT_RADIO_START;
            }
            if (_statusBoard->socketsRefusedWithin(nowMs, SOCKET_FAULT_FRESH_MS, SOCKET_REFUSALS_FOR_A_FAULT))
            {
                faults |= FAULT_SOCKET;
            }
        }
        return faults;
    }

    static void describeFaults(uint8_t faults, char *out, size_t size)
    {
        snprintf(out, size, "%s%s%s%s%s",
                 faults == 0 ? "none" : "",
                 (faults & FAULT_TLS_MEMORY) ? " tls-memory" : "",
                 (faults & FAULT_NETWORK_TASK) ? " network-task" : "",
                 (faults & FAULT_RADIO_START) ? " radio-start" : "",
                 (faults & FAULT_SOCKET) ? " socket" : "");
    }

    /**
     * @brief One event to the machine, and what it asks for carried out.
     *
     * The radio restart and the probe go last: when they cannot be handed to the network task they
     * come back at once, as another event, and the machine has to be in its new state by then.
     */
    void feed(const LinkTroubleshooting::Event &event)
    {
        const LinkTroubleshooting::Step step = LinkTroubleshooting::step(_trouble, event);
        if (!step.expected)
        {
            return;
        }

        const LinkTroubleshooting::Phase before = _trouble.phase;
        _trouble = step.next;
        if (_trouble.phase != before)
        {
            logSetup("LINK_CHECK: %s -> %s (%s) at %lu ms", LinkTroubleshooting::phaseName(before),
                     LinkTroubleshooting::phaseName(_trouble.phase), step.why, (unsigned long)event.nowMs);
        }

        if (step.actions & LinkTroubleshooting::RECORD_OUTAGE)
        {
            recordOutage(step.outage);
        }
        if (step.actions & LinkTroubleshooting::RESTART_MODULE)
        {
            restartTheModule(event.nowMs);
        }
        if (step.actions & LinkTroubleshooting::RESTART_RADIO)
        {
            askForARadioRestart(event.nowMs, step.why);
        }
        if (step.actions & LinkTroubleshooting::PROBE)
        {
            askForAProbe(event.nowMs);
        }
    }

    void feed(LinkTroubleshooting::EventKind kind, uint32_t nowMs)
    {
        LinkTroubleshooting::Event event;
        event.kind = kind;
        event.nowMs = nowMs;
        feed(event);
    }

    /**
     * @brief The readings of this tick as the machine's events, then the tick itself.
     *
     * A fault first, so that a link lost with a fault reported goes straight to the module's fault
     * rather than to a radio restart; the association before the services, because without it the
     * services cannot work and the loss of the association is the more specific of the two.
     */
    void troubleshootOnce(uint32_t nowMs)
    {
        const bool associated = (_wifiAdapter == nullptr) || _wifiAdapter->isConnected();
        const bool brokerUp = (_mqttManager == nullptr) || _mqttConnected;
        const bool servicesUp = associated && brokerUp && !isHttpConnectionDown();

        const uint8_t faults = readModuleFaults(nowMs);
        if (faults != _faults)
        {
            char described[64];
            describeFaults(faults, described, sizeof(described));
            logSetup("LINK_CHECK: module faults:%s at %lu ms", described, (unsigned long)nowMs);
            _faults = faults;
        }

        if ((faults != 0) != _trouble.faultReported)
        {
            feed(faults != 0 ? LinkTroubleshooting::EventKind::MODULE_FAULT
                             : LinkTroubleshooting::EventKind::MODULE_FAULT_GONE,
                 nowMs);
        }
        if (associated != _trouble.wifiUp)
        {
            feed(associated ? LinkTroubleshooting::EventKind::WIFI_BACK : LinkTroubleshooting::EventKind::WIFI_LOST,
                 nowMs);
        }
        if (servicesUp != _trouble.servicesUp)
        {
            feed(servicesUp ? LinkTroubleshooting::EventKind::SERVICES_RESTORED
                            : LinkTroubleshooting::EventKind::SERVICES_FAILING,
                 nowMs);
        }
        feed(LinkTroubleshooting::EventKind::TICK, nowMs);
    }

    /**
     * @brief Have the radio restarted.
     *
     * The restart holds its caller for up to a minute, so with an outbox it is handed to the network
     * task and its end comes back through onOutboxCompletion(); without one it runs here and ends at
     * once. One restart in flight at a time: asked for again while one runs, the machine waits for
     * the one running.
     */
    void askForARadioRestart(uint32_t nowMs, const char *why)
    {
        logSetup("LINK_CHECK: radio restart asked (%s) at %lu ms", why, (unsigned long)nowMs);

        if (_wifiAdapter == nullptr)
        {
            radioRestartIsOver(nowMs);
            return;
        }

        if (_outbox == nullptr)
        {
            const bool associated = _wifiAdapter->reconnect();
            logSetup("WIFI_RECONNECT: restart result=%d", associated ? 1 : 0);
            radioRestartIsOver(nowMs);
            return;
        }

        if (_restartTicket != IOutbox::kRejected)
        {
            logSetup("WIFI_RECONNECT: restart already in flight, ticket %lu", (unsigned long)_restartTicket);
            return;
        }

        // Asked for with no association, the restart is for a lost link and is not run if the link
        // is back by the time the network task reaches it. Asked for with the station associated -
        // for a gateway that stays silent - it is meant to take that association down.
        const uint32_t ticket = _wifiAdapter->isConnected() ? _outbox->submitWifiReconnect()
                                                            : _outbox->submitWifiReconnectForALostLink();
        if (ticket == IOutbox::kRejected)
        {
            logSetup("WIFI_RECONNECT: restart refused by the outbox");
            radioRestartIsOver(nowMs);
            return;
        }

        _restartTicket = ticket;
        logSetup("WIFI_RECONNECT: restart handed to the network task, ticket %lu", (unsigned long)ticket);
    }

    void radioRestartIsOver(uint32_t nowMs)
    {
        LinkTroubleshooting::Event event;
        event.kind = LinkTroubleshooting::EventKind::RESTART_DONE;
        event.nowMs = nowMs;
        feed(event);
    }

    /** @brief Have the link probed on the network task: the gateway, then a public address. */
    void askForAProbe(uint32_t nowMs)
    {
        if (_outbox == nullptr)
        {
            LinkProbeResult nothing;
            nothing.gateway = LinkReach::UNAVAILABLE;
            probeIsBack(nowMs, nothing);
            return;
        }

        if (_probeTicket != IOutbox::kRejected)
        {
            logSetup("LINK_CHECK: probe already in flight, ticket %lu", (unsigned long)_probeTicket);
            return;
        }

        const uint32_t ticket = _outbox->submitLinkProbe();
        if (ticket == IOutbox::kRejected)
        {
            logSetup("LINK_CHECK: probe refused by the outbox");
            LinkProbeResult nothing;
            nothing.gateway = LinkReach::UNAVAILABLE;
            probeIsBack(nowMs, nothing);
            return;
        }

        _probeTicket = ticket;
        logSetup("LINK_CHECK: probe handed to the network task, ticket %lu at %lu ms", (unsigned long)ticket,
                 (unsigned long)nowMs);
    }

    /** @brief A probe's result, on the tape with what the server said, and to the machine. */
    void probeIsBack(uint32_t nowMs, const LinkProbeResult &result)
    {
        const bool serverAnswered =
            _statusBoard != nullptr && _statusBoard->serverAnsweredSince(nowMs, _trouble.outageStartedAtMs);
        logSetup("LINK_CHECK: probe gateway=%s public=%s server_answered=%d at %lu ms",
                 LinkTroubleshooting::reachName(result.gateway),
                 LinkTroubleshooting::reachName(result.publicAddress), serverAnswered ? 1 : 0,
                 (unsigned long)nowMs);

        LinkTroubleshooting::Event event;
        event.kind = LinkTroubleshooting::EventKind::PROBE_DONE;
        event.nowMs = nowMs;
        event.probe = result;
        event.serverAnswered = serverAnswered;
        feed(event);
    }

    /**
     * @brief Restart the module, which is what a fault of the module that holds calls for.
     *
     * Restarted as a technical boot that skips the sensor discovery: the module is coming back for its
     * own sake, not for its sensors, and the stored ones are read as on any boot.
     */
    void restartTheModule(uint32_t nowMs)
    {
        char described[64];
        describeFaults(_faults, described, sizeof(described));
        logSetup("LINK_CHECK: the fault of the module held (%s) - restarting the module at %lu ms", described,
                 (unsigned long)nowMs);

        if (_moduleConfig == nullptr)
        {
            return;
        }
        ISettings *settings = _moduleConfig->getSettings();
        if (settings == nullptr)
        {
            log("DIAG: ERROR - Cannot trigger restart, settings not available");
            return;
        }

        // Set noScanSensors flag to skip sensor scan after reboot
        settings->setNoScanSensors(1);
        log("DIAG: Set noScanSensors flag for faster recovery");

        // Set boot type to technical
        settings->setBootType(BOOT_TYPE_TECHNICAL);
        log("DIAG: Set boot type to BOOT_TYPE_TECHNICAL");

        // Request module restart
        _moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
        log("DIAG: Automatic module restart triggered by a fault of the module");
    }

    void recordOutage(const LinkTroubleshooting::Outage &outage)
    {
        _lastOutage = outage;
        _hasOutage = true;
        logSetup("LINK_CHECK: outage over - cause=%s, started at %lu ms, lasted %lu ms",
                 LinkTroubleshooting::causeName(outage.cause), (unsigned long)outage.startedAtMs,
                 (unsigned long)outage.durationMs);
    }

    /**
     * @brief Is the boot's association still on its way?
     *
     * Not past the bound the machine gives any job of the link (a completion that never comes), and
     * not while the network task that would run it could not be created - then nothing is on its way,
     * and the module's fault is what there is to decide on.
     */
    bool theBootAssociationIsInFlight(uint32_t nowMs)
    {
        if (!_bootAssociationInFlight)
        {
            return false;
        }
        if (nowMs - _bootAssociationAskedAtMs >= LinkTroubleshooting::LINK_JOB_CEILING_MS)
        {
            logSetup("WIFI_RECONNECT: the boot association never came back - given up on at %lu ms",
                     (unsigned long)nowMs);
            _bootAssociationInFlight = false;
            _restartTicket = IOutbox::kRejected;
            return false;
        }
        return _statusBoard == nullptr || !_statusBoard->networkTaskRefused();
    }

    /** @brief Is the network task moving the station from one access point to another right now? */
    bool aMoveBetweenAccessPointsIsInFlight() const
    {
        if (_actBoard == nullptr)
        {
            return false;
        }

        BackgroundActBoard::Snapshot snapshot;
        _actBoard->capture(_arduinoAdapter != nullptr ? (uint32_t)_arduinoAdapter->millis() : 0u,
                           snapshot);
        const BackgroundActBoard::Reading reading =
            BackgroundActBoard::read(snapshot, BackgroundActBoard::NETWORK);
        return !reading.finished &&
               reading.act == BackgroundActs::ofOutboxKind(OutboxKind::WIFI_ROAM);
    }

public:
    /** @brief How often the control loop calls superviseOnce(): thirty seconds, from millis 0. */
    static const unsigned long TICK_INTERVAL_MS = 30 * 1000;

    /**
     * @brief How long a handshake that failed for memory stays a fault of the module: three ticks.
     *
     * A diagnostic is submitted every tick and each request is attempted up to four times, 0.5, 2 and
     * 8 s apart; while the server is reachable a module short of memory for its handshakes fails one in
     * every tick or two. A failure older than three ticks with nothing since is a module that stopped
     * trying - its link is down, and a link that is down says nothing about memory.
     */
    static const uint32_t TLS_FAULT_FRESH_MS = 3 * TICK_INTERVAL_MS;

    /**
     * @brief How many connects in a row the module's own network stack must refuse a socket - with
     *        no socket opened in between - before it is a fault of the module: three.
     *
     * One request is attempted up to four times, 0.5, 2 and 8 s apart. A single refusal is a moment -
     * another allocation at its peak, a socket a moment from being freed; three in a row is a stack
     * that stayed short across the two shorter backoffs, 2.5 s and two more attempts, with nothing
     * given back. Any socket that opens since starts the count again.
     */
    static const uint32_t SOCKET_REFUSALS_FOR_A_FAULT = 3;

    /**
     * @brief How long the last of those refusals stays a fault of the module: three ticks, for the
     *        reason TLS_FAULT_FRESH_MS gives - a module that stopped trying says nothing about its
     *        sockets.
     */
    static const uint32_t SOCKET_FAULT_FRESH_MS = 3 * TICK_INTERVAL_MS;

    LinkSupervisor(
        IRestAPIService *restApiService,
        IModuleConfig *moduleConfig,
        ILogger &logger,
        IDebugSystem &debugSystem,
        IWifiAdapter *wifiAdapter = nullptr,
        INTPTimeAdapter *timeAdapter = nullptr,
        IArduinoAdapter *arduinoAdapter = nullptr,
        IMqttManager *mqttManager = nullptr)
        : _restApiService(restApiService),
          _moduleConfig(moduleConfig),
          _logger(logger),
          _debugSystem(debugSystem),
          _wifiAdapter(wifiAdapter),
          _timeAdapter(timeAdapter),
          _arduinoAdapter(arduinoAdapter),
          _mqttManager(mqttManager),
          _mqttConnected(false),
          _failsHistoryIndex(0)
    {
        for (int i = 0; i < MAX_FAILS_HISTORY; i++)
        {
            _failsHistory[i] = 0;
        }

        // Initialize MQTT connection status if manager is available
        if (_mqttManager != nullptr)
        {
            _mqttConnected = _mqttManager->isConnected();
        }
    }

    /**
     * @brief The radio restart is a job of the outbox: handed over, and its outcome told back.
     */
    void setOutbox(IOutbox *outbox) { _outbox = outbox; }

    /**
     * @brief The board this supervisor reads before deciding that a link is down.
     *
     * Optional, and read-only: a link that is down because the network task is moving the station
     * between two access points of the same network is not a link that needs the radio restarted.
     */
    void setActBoard(BackgroundActBoard *actBoard) { _actBoard = actBoard; }

    /**
     * @brief Where the module's parts record their faults and the server's answers.
     *
     * Optional. Without it this supervision knows of no fault of the module, and every outage past the
     * router is recorded as the internet's unless a public address answered.
     */
    void setStatusBoard(ModuleStatusBoard *statusBoard) { _statusBoard = statusBoard; }

    /**
     * @brief Have the station associated for the first time this boot. Control loop, every iteration;
     *        it asks once.
     *
     * Setup waits for no network, so the boot's association is the first thing this supervision asks
     * for, on the loop's first iteration: the same job a radio restart is - a cold radio started and
     * associated with the networks setup handed it - for a link that has not been there yet. Until it
     * comes back nothing about the link is decided: the station has no association because it has not
     * yet had the time to make one, not because a link was lost.
     */
    void associateForTheBoot()
    {
        if (_bootAssociationAsked)
        {
            return;
        }
        _bootAssociationAsked = true;

        const uint32_t now = _arduinoAdapter ? (uint32_t)_arduinoAdapter->millis() : 0u;
        if (_wifiAdapter == nullptr)
        {
            return;
        }
        if (_outbox == nullptr)
        {
            const bool associated = _wifiAdapter->reconnect();
            logSetup("WIFI_RECONNECT: boot association result=%d", associated ? 1 : 0);
            return;
        }

        const uint32_t ticket = _outbox->submitWifiReconnectForALostLink();
        if (ticket == IOutbox::kRejected)
        {
            logSetup("WIFI_RECONNECT: boot association refused by the outbox");
            return;
        }

        _restartTicket = ticket;
        _bootAssociationInFlight = true;
        _bootAssociationAskedAtMs = now;
        logSetup("WIFI_RECONNECT: boot association handed to the network task, ticket %lu at %lu ms",
                 (unsigned long)ticket, (unsigned long)now);
    }

    bool isTheBootAssociationOver() const override
    {
        return _bootAssociationAsked && !_bootAssociationInFlight;
    }

    /** @brief The troubleshooting machine's phase, as of the last event. */
    LinkTroubleshooting::Phase troublePhase() const { return _trouble.phase; }

    /**
     * @brief The last outage the module came back from: what it was found to be, when it started and
     *        how long it lasted, on the module's uptime.
     * @return false while no outage has been recorded since the boot
     */
    bool lastOutage(LinkTroubleshooting::Outage &out) const
    {
        if (!_hasOutage)
        {
            return false;
        }
        out = _lastOutage;
        return true;
    }

    bool isNetworkOffline() override
    {
        return _lastNetworkState == DIAGNOSTICS_NETWORK_STATE_ERROR;
    }

    bool isHttpConnectionDown() override
    {
        return countOffline() > _maxCountOffline;
    }

    /** @brief Times the module's offline decision went from OK to ERROR. */
    int networkFails() const { return _networkFails; }

    void recordFailure()
    {
        if (_arduinoAdapter)
        {
            _failsHistory[_failsHistoryIndex] = _arduinoAdapter->millis();
            _failsHistoryIndex = (_failsHistoryIndex + 1) % MAX_FAILS_HISTORY;
        }
    }

    /** @brief Failures recordFailure() noted in the last hour. */
    unsigned long failsInHour() const
    {
        unsigned long totalFails = 0;
        unsigned long currentTime = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        const unsigned long ONE_HOUR_MS = 60 * 60 * 1000;

        for (int i = 0; i < MAX_FAILS_HISTORY; i++)
        {
            if (_failsHistory[i] > 0 && currentTime - _failsHistory[i] < ONE_HOUR_MS)
            {
                totalFails++;
            }
        }

        return totalFails;
    }

    /**
     * @brief One tick of the decisions about the link. Control loop only.
     *
     * The troubleshooting first - the readings of this tick to the machine, and what it asks for
     * carried out - unless the module's own access point is up or the network task is moving the
     * station between two access points: then the link is down on purpose and nothing is decided.
     * Last the module's offline decision: the link gone for longer than its debounce, or the broker
     * and the server both down; its edge to ERROR counts a network failure, its edge back to OK has
     * the clock synchronized again.
     */
    void superviseOnce()
    {
        // A network task the heap refused when the last request was submitted is created here. On a
        // module with no link the radio restart this supervision asked for is the only request, and
        // nothing else submits until the link is back.
        if (_outbox != nullptr)
        {
            _outbox->ensureNetworkTask();
        }

        if (_mqttManager != nullptr)
        {
            _mqttConnected = _mqttManager->isConnected();
        }

        unsigned long now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;

        if (_wifiAdapter != nullptr && _wifiAdapter->isAccessPointActive())
        {
            logSetup("WIFI_RECONNECT: skip (access point active)");
        }
        else if (aMoveBetweenAccessPointsIsInFlight())
        {
            // The network task is deliberately between two access points: the association is down
            // because this module took it down, and it comes back within seconds. Nothing is
            // remembered here, so the loss is seen as usual on the next tick if the link really is
            // gone.
            logSetup("WIFI_RECONNECT: skip (move between access points in flight)");
        }
        else if (theBootAssociationIsInFlight((uint32_t)now))
        {
            // The station has not been given its first association yet. Nothing is remembered here
            // either: once it is back, the next tick sees the link as it is.
            logSetup("WIFI_RECONNECT: skip (boot association in flight)");
        }
        else
        {
            troubleshootOnce((uint32_t)now);
        }

        bool wifiConnected = (_wifiAdapter != nullptr) ? _wifiAdapter->isConnected() : true;
        if (wifiConnected)
        {
            _wifiDownSince = 0;
        }
        else if (_wifiDownSince == 0)
        {
            _wifiDownSince = now;
        }
        bool routerDown = (_wifiDownSince != 0) && (now - _wifiDownSince >= WIFI_OFFLINE_DEBOUNCE_MS);

        bool httpDown = isHttpConnectionDown();
        bool offline = routerDown || (!_mqttConnected && httpDown);

        if (offline)
        {
            if (_lastNetworkState == DIAGNOSTICS_NETWORK_STATE_OK)
            {
                _lastNetworkState = DIAGNOSTICS_NETWORK_STATE_ERROR;
                writeBoardStateToFile(); // when switching ok to not ok or vice versa
                _networkFails++;

                log("DIAG: Network state changed to ERROR. Total network fails: %d", _networkFails);
            }
        }
        else
        {
            if (_lastNetworkState == DIAGNOSTICS_NETWORK_STATE_ERROR)
            {
                _lastNetworkState = DIAGNOSTICS_NETWORK_STATE_OK;
                writeBoardStateToFile(); // when switching ok to not ok or vice versa

                // When network is restored, force time synchronization
                if (_timeAdapter != nullptr)
                {
                    log("DIAG: Network restored, forcing time synchronization");
                    _timeAdapter->forceSyncTime();
                }

                log("DIAG: Network state changed to OK");
            }
        }

        logOffline("OFFLINE_INDICATION: decision offline=%d state=%s netFails=%d | routerDown=%d wifiConn=%d wifiDownSince=%lu | httpDown=%d countOffline=%d | mqttConn=%d",
                   offline ? 1 : 0,
                   _lastNetworkState == DIAGNOSTICS_NETWORK_STATE_ERROR ? "ERROR" : "OK",
                   _networkFails,
                   routerDown ? 1 : 0,
                   wifiConnected ? 1 : 0,
                   _wifiDownSince,
                   httpDown ? 1 : 0,
                   countOffline(),
                   _mqttConnected ? 1 : 0);
    }

    /**
     * @brief The end of the radio restart or of the probe this supervisor asked for, if this
     *        completion is one of them.
     *
     * For the restart `delivered` is its answer - associated, or not; for the probe the answer travels
     * in `linkProbe`. Either way the ticket is freed, so that the machine's next decision can ask again.
     */
    void onOutboxCompletion(const OutboxCompletion &completion)
    {
        const uint32_t now = _arduinoAdapter ? (uint32_t)_arduinoAdapter->millis() : 0u;

        if (completion.kind == OutboxKind::WIFI_RECONNECT && _restartTicket != IOutbox::kRejected &&
            completion.ticket == _restartTicket)
        {
            _restartTicket = IOutbox::kRejected;
            if (_bootAssociationInFlight)
            {
                _bootAssociationInFlight = false;
                logSetup("WIFI_RECONNECT: boot association result=%d at %lu ms", completion.delivered ? 1 : 0,
                         (unsigned long)now);
            }
            else if (completion.attempts == 0)
            {
                // The network task found the station associated and left the radio alone.
                logSetup("WIFI_RECONNECT: restart not run - the link it was asked for is back");
            }
            else
            {
                logSetup("WIFI_RECONNECT: restart result=%d", completion.delivered ? 1 : 0);
            }
            radioRestartIsOver(now);
            return;
        }

        if (completion.kind == OutboxKind::LINK_PROBE && _probeTicket != IOutbox::kRejected &&
            completion.ticket == _probeTicket)
        {
            _probeTicket = IOutbox::kRejected;
            probeIsBack(now, LinkProbeResult::decode(completion.linkProbe));
        }
    }
};
