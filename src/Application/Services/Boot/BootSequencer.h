#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Application/Interfaces/IBootAssociation.h"
#include "Application/Interfaces/ICommandService.h"
#include "Application/Interfaces/INetworkListeners.h"
#include "Application/Services/Sensors/ISensorService.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Core/Domain/Interfaces/UseCases/Boot/IBootGates.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Sensors/IStartupSensorScan.h"
#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"

/**
 * @brief The order in which the module comes up once setup is over - an order of events, not of
 *        timers.
 *
 * The network track: LOCAL (the loop has started; the stored sensors are read from here on) ->
 * LINK (waiting for a link with an address) -> NET_STRUCTURE (the structure that reports the
 * module is on its way) -> NET_CATALOG (the sensor type catalog, asked for once the structure has
 * settled, is on its way) -> STEADY. The structure is the module's report of itself to the server,
 * so it is the first thing the module sends and everything the boot sends after it waits for it
 * to settle - delivered or given up on; nothing is held for its delivery. From NET_CATALOG on the
 * telemetry, the rule list and the device update may go - the first telemetry once SNTP has set
 * the clock on this boot, or SNTP_WAIT_CEILING_MS after the start without it.
 *
 * The same track serves every return of the link: from any network phase a link that is lost
 * takes the sequence back to LINK, which shuts the gates of the telemetry, the rule list and the
 * device update until the link is back and NET has run again. A return sends a structure only when
 * the address changed (the one the server has confirmed is not the one the link has) and a catalog
 * only when a synchronization that failed has been asked for again, both in the boot's order; what
 * the shut gates held goes when they open. The diagnostic's send and the discovery are the boot's
 * alone: once open, a return does not shut them.
 *
 * The discovery track: the startup discovery waits for the first outcome of every stored sensor -
 * a pass that isolates a wire must not take a stored sensor's power before the sensor has been read
 * once - and for the network phases to be over, or, on a boot with no link, for nothing more than
 * that if it was armed with the catalog on flash. Its own window (BOOT_SETTLE_MS) remains the bound:
 * the gate can only let it go sooner. A catalog that arrives on a boot that had none arms it late.
 * And whatever the discovery finds is reported after the boot's own structure, never before it.
 *
 * Two more events are the boot's. The diagnostic is sent from the first STEADY on - its tick runs
 * every 30 s from millis 0 regardless; only its send waits. And the subscriptions to the sensors
 * other modules publish are reconciled on the broker's first CONNACK.
 *
 * Two more things start on the network's events, once a boot: the module's UDP listeners on the
 * first link with an address - setup does nothing on the network - and, on the first settle of the
 * structure, the firmware update the previous boot kept and the ask for the module commands that
 * waited for the module while it was off; and on the first STEADY, the ask whether the server has
 * deleted the module.
 *
 * Nothing here sends, queues or reads anything: what an event starts, its owner queues. The
 * events are asked of their owners on every iteration - the link of the radio, the structure of
 * OnChangeLocalIpUseCase, the first readings of SensorService - and the producers ask their gates
 * (IBootGates) at the moment they would act.
 * Ticked once per control-loop iteration, after the completions of the last one have been handed
 * out and before any producer of this one has run, so every gate is decided on what the iteration
 * knows.
 *
 * A wait that does not end is given up on, each after a ceiling derived from what the thing waited
 * for can take - see Ceilings: the first readings, the structure, the catalog. Giving up does what
 * the event would have done and says so on the tape; the waits are long, and they exist for a
 * completion that never comes, not to hurry a slow one.
 *
 * Every line it writes starts with "BOOT_SEQ:", on the serial port only: they are what a serial
 * capture of a boot is read by.
 */
class BootSequencer : public IBootGates
{
public:
    enum class Phase : uint8_t
    {
        NOT_STARTED,
        LOCAL,
        LINK,
        NET_STRUCTURE,
        NET_CATALOG,
        STEADY
    };

    /**
     * @brief How long each wait may last before it is given up on, ms; 0 waits for ever.
     *
     * The first readings: counted from the loop's first iteration - firstReadingsBaseMs, and
     * firstReadingsPerChannelMs for every register channel of the sensors awaited. The structure and
     * the catalog: from the entry into their phase.
     */
    struct Ceilings
    {
        uint32_t firstReadingsBaseMs = 0;
        uint32_t firstReadingsPerChannelMs = 0;
        uint32_t netStructureMs = 0;
        uint32_t netCatalogMs = 0;
    };

    /**
     * @brief What the waits are made of: one place for the arithmetic, which the composition root and
     *        its stands fill with the same constants of the same owners.
     */
    struct CeilingInputs
    {
        /** The longest one REST attempt can take. */
        uint32_t restAttemptMs = 0;
        /** Attempts the outbox makes at one job, the pause before its second, and the factor of each next. */
        uint32_t attemptsPerJob = 0;
        uint32_t firstBackoffMs = 0;
        uint32_t backoffFactor = 0;
        /** One connect act of the broker, which requests wait behind. */
        uint32_t mqttActMs = 0;
        /** The jobs the outbox's rotation can serve ahead of the catalog fetch once it is asked for. */
        uint32_t jobsAheadOfTheCatalog = 0;
        /** What a stored sensor's port and the sweep can take before its first read, and a channel's read. */
        uint32_t portWakeUpMs = 0;
        uint32_t sweepIntervalMs = 0;
        uint32_t channelReadMs = 0;
    };

    /**
     * @brief The ceilings: a job of the outbox is its attempts and the pauses between them, and one
     *        connect act of the broker may stand in front of it.
     *
     * - structure: one act and one job - the boot's structure is the first job the outbox serves;
     * - catalog: the catalog's own and every job the rotation can take before it, each with its act;
     * - first readings: a port's warm-up, a sweep's interval, and every channel's read of a sensor that
     *   does not answer.
     */
    static Ceilings ceilingsFrom(const CeilingInputs &in)
    {
        uint32_t backoffs = 0;
        uint32_t pause = in.firstBackoffMs;
        for (uint32_t attempt = 1; attempt < in.attemptsPerJob; attempt++)
        {
            backoffs += pause;
            pause *= in.backoffFactor;
        }
        const uint32_t oneJobAndItsAct = in.mqttActMs + in.attemptsPerJob * in.restAttemptMs + backoffs;

        Ceilings ceilings;
        ceilings.netStructureMs = oneJobAndItsAct;
        ceilings.netCatalogMs = (in.jobsAheadOfTheCatalog + 1) * oneJobAndItsAct;
        ceilings.firstReadingsBaseMs = in.portWakeUpMs + in.sweepIntervalMs;
        ceilings.firstReadingsPerChannelMs = in.channelReadMs;
        return ceilings;
    }

    /**
     * @brief How long the first telemetry waits for SNTP to set the clock on this boot, ms from the
     *        module's start. The server drops an update whose time is minutes off its own; past this
     *        the telemetry goes with the clock the module has - seeded from storage, or none.
     */
    static const uint32_t SNTP_WAIT_CEILING_MS = 10UL * 60UL * 1000UL;

    BootSequencer(IArduinoAdapter *arduino = nullptr, ILogger *logger = nullptr,
                  IDebugSystem *debugSystem = nullptr)
        : _arduino(arduino), _logger(logger), _debugSystem(debugSystem)
    {
    }

    void setCeilings(const Ceilings &ceilings) { _ceilings = ceilings; }

    /** @brief The rule sync, asked for when the structure's wait is given up on - see onIteration(). */
    void setSyncRulesUseCase(ISyncRulesUseCase *rules) { _rules = rules; }

    /** @brief The radio: whether there is a link, and the address it holds. */
    void setWifiAdapter(IWifiAdapter *wifi) { _wifi = wifi; }

    /** @brief Who reports the module's address with the structure, and says when one settled. */
    void setOnChangeLocalIpUseCase(IOnChangeLocalIpUseCase *localIp) { _localIp = localIp; }

    /** @brief Who reads the stored sensors, and says when each has answered or failed once. */
    void setSensorService(ISensorService *sensors) { _sensors = sensors; }

    /** @brief Who synchronizes the catalog, and says when a synchronization has finished. */
    void setSyncSensorTypesUseCase(ISyncSensorTypesUseCase *catalog) { _catalog = catalog; }

    /** @brief The startup discovery: whether the boot armed it, and what a late catalog arms. */
    void setStartupSensorScan(IStartupSensorScan *scan) { _scan = scan; }

    /**
     * @brief The broker's connection, asked until its first CONNACK and never after: the
     *        subscriptions to remote sensors are reconciled then, once.
     *
     * MqttManager::isConnected() takes the client's lock only while it believes it is connected,
     * so the asking costs nothing until the answer is yes, and that answer is the last question.
     */
    void setMqttManager(IMqttManager *mqtt) { _mqtt = mqtt; }

    /** @brief The UDP listeners, started on the boot's first link with an address. */
    void setNetworkListeners(INetworkListeners *listeners) { _listeners = listeners; }

    /**
     * @brief Whether the boot's association is over: until it is, no link is not yet a boot without
     *        one, and the discovery does not go on that account.
     */
    void setBootAssociation(const IBootAssociation *association) { _association = association; }

    /** @brief The clock: whether SNTP has set it on this boot - the first telemetry waits for it. */
    void setClock(INTPTimeAdapter *clock) { _clock = clock; }

    /**
     * @brief Who resumes the kept update and fetches the module commands on the first settle, and
     *        checks the module's deletion on the first STEADY.
     */
    void setCommandService(ICommandService *commands) { _commands = commands; }
    void setRemoteSensorReceiver(IRemoteSensorReceiver *remoteSensors) { _remoteSensors = remoteSensors; }

    /**
     * @brief The control loop sent the diagnostic its tick had owed since the boot began. One line on
     *        the tape, nothing else.
     */
    void noteTheOwedDiagnosticWasSent(uint32_t owedSinceMs)
    {
        log("BOOT_SEQ: diagnostic owed since %lu ms sent on STEADY at %lu ms", (unsigned long)owedSinceMs,
            (unsigned long)nowMs());
    }

    /** @brief One step of the sequence. Control loop only, once per iteration, before the producers. */
    void onIteration()
    {
        const uint32_t now = nowMs();

        if (_phase == Phase::NOT_STARTED)
        {
            _phase = Phase::LOCAL;
            _localAtMs = now;
            // Armed in setup only when the catalog on flash had something to scan with.
            _scanArmedAtBoot = (_scan != nullptr) && _scan->isRunning();
            moveTo(Phase::LINK, "loop started", now);
        }

        noteTheFirstReadings(now);
        noteTheClock(now);
        advanceTheNetworkTrack(now);
        noteAFinishedCatalogSync();
        decideTheDiscovery();
        reconcileTheSubscriptionsOnTheFirstConnack();
        noteTheGatesThatOpened();
    }

    // --- IBootGates ---------------------------------------------------------------------------

    bool isSequencing() const override { return _phase != Phase::NOT_STARTED; }

    /**
     * Before the first tick every gate that holds a producer back is open and the one that lets the
     * discovery go early is shut: a sequence that has not started changes nothing.
     */
    bool allows(BootGate gate) const override
    {
        switch (gate)
        {
        case BootGate::TELEMETRY:
            return !isSequencing() || (theBootStructureHasSettled() && _readingsSettled && _clockSettled);
        case BootGate::RULE_LIST:
        case BootGate::DEVICE_UPDATE:
            return !isSequencing() || theBootStructureHasSettled();
        case BootGate::DISCOVERY:
            return _discoveryOpen;
        case BootGate::STRUCTURE_REPUBLISH:
            return !isSequencing() || _firstStructureSettled;
        case BootGate::DIAGNOSTIC_SEND:
            return !isSequencing() || _steadyReached;
        }
        return false;
    }

    // --- Observation --------------------------------------------------------------------------

    Phase phase() const { return _phase; }

    static const char *phaseName(Phase phase)
    {
        switch (phase)
        {
        case Phase::NOT_STARTED:
            return "NOT_STARTED";
        case Phase::LOCAL:
            return "LOCAL";
        case Phase::LINK:
            return "LINK";
        case Phase::NET_STRUCTURE:
            return "NET_STRUCTURE";
        case Phase::NET_CATALOG:
            return "NET_CATALOG";
        case Phase::STEADY:
            return "STEADY";
        }
        return "?";
    }

private:
    uint32_t nowMs() const { return _arduino != nullptr ? static_cast<uint32_t>(_arduino->millis()) : 0; }

    bool theBootStructureHasSettled() const
    {
        return _phase == Phase::NET_CATALOG || _phase == Phase::STEADY;
    }

    /**
     * @brief Is there a link, with an address on it?
     *
     * The same question OnChangeLocalIpUseCase asks before it hands the structure over, in the
     * same words: an association without an address is not a link anything can be sent on. A
     * module wired without a radio is taken to have one, as the outbox takes it.
     */
    bool theLinkHasAnAddress(const char *&ip) const
    {
        ip = nullptr;
        if (_wifi == nullptr)
        {
            return true;
        }
        if (!_wifi->isConnected())
        {
            return false;
        }
        ip = _wifi->getLocalIP();
        return ip != nullptr && strcmp(ip, "0.0.0.0") != 0;
    }

    /** @brief Does the server still have to be told this address? */
    bool theServerDoesNotHave(const char *ip) const
    {
        const char *reported = _localIp->reportedIp();
        if (reported == nullptr || reported[0] == '\0')
        {
            return true;
        }
        return ip != nullptr && strcmp(reported, ip) != 0;
    }

    /** @brief A network phase whose link has gone: back to LINK. Only the association is asked here. */
    bool theLinkIsLost() const
    {
        return _wifi != nullptr && !_wifi->isConnected();
    }

    void advanceTheNetworkTrack(uint32_t now)
    {
        if ((_phase == Phase::NET_STRUCTURE || _phase == Phase::NET_CATALOG || _phase == Phase::STEADY) &&
            theLinkIsLost())
        {
            moveTo(Phase::LINK, "link lost", now);
            return;
        }

        if (_phase == Phase::LINK)
        {
            const char *ip = nullptr;
            if (!theLinkHasAnAddress(ip))
            {
                return;
            }
            enterTheStructurePhase(ip, now);
        }

        if (_phase == Phase::NET_STRUCTURE)
        {
            const char *event = _structureOwed ? "structure settled" : "structure not owed";
            if (_structureOwed && _localIp->structuresSettled() == _structuresSettledAtEntry)
            {
                if (!aCeilingIsOver(_ceilings.netStructureMs, now))
                {
                    return;
                }
                // What the structure's completion would have done: the rules and the catalog are
                // asked for, and the gates behind the structure open. The structure itself stays in
                // the outbox's hands; if its completion does come, it asks for both once more.
                log("BOOT_SEQ: NET_STRUCTURE gave up after %lu ms (structure not settled)",
                    (unsigned long)(now - _phaseEnteredAtMs));
                if (_rules != nullptr)
                {
                    _rules->forceSync();
                }
                if (_catalog != nullptr)
                {
                    _catalog->forceSync();
                }
                event = "gave up";
            }
            _catalogSeenInFlight = false;
            moveTo(Phase::NET_CATALOG, event, now);
            if (!_firstStructureSettled)
            {
                _firstStructureSettled = true;
                log("BOOT_SEQ: gate STRUCTURE_REPUBLISH open (%s)", event);
                askForTheBootCommands(now);
            }
        }

        if (_phase == Phase::NET_CATALOG)
        {
            // Asked for on the completion of the structure, which this iteration's drain has just
            // handed out - so a catalog that is going to be fetched is already in flight here.
            const char *event = _catalogSeenInFlight ? "catalog settled" : "no catalog sync in flight";
            if (_catalog != nullptr && _catalog->isSyncInFlight())
            {
                _catalogSeenInFlight = true;
                if (!aCeilingIsOver(_ceilings.netCatalogMs, now))
                {
                    return;
                }
                log("BOOT_SEQ: NET_CATALOG gave up after %lu ms (catalog %s still in flight)",
                    (unsigned long)(now - _phaseEnteredAtMs), _catalog->isWritePending() ? "write" : "fetch");
                event = "gave up";
            }
            moveTo(Phase::STEADY, event, now);
            if (!_steadyReached)
            {
                _steadyReached = true;
                if (_commands != nullptr)
                {
                    // Not before: the reset a DELETED verdict brings writes the catalog through its
                    // DAO on the loop, and until STEADY a write of the catalog may be in flight.
                    log("BOOT_SEQ: deletion check asked for on the first STEADY at %lu ms", (unsigned long)now);
                    _commands->checkModuleDeletion();
                }
            }
        }
    }

    /**
     * @brief The first settle of the structure: the update the previous boot kept for this one,
     *        then the module commands left for the module while it was off.
     *
     * The command service remembers the updates it has started on this boot, so the same update
     * offered again by the server's list - which holds a command as new until a status for it
     * arrives - or by a notification the broker keeps is not carried out a second time, whichever
     * of the two reaches it first.
     */
    void askForTheBootCommands(uint32_t now)
    {
        if (_commands == nullptr)
        {
            return;
        }
        log("BOOT_SEQ: kept update and module commands asked for at %lu ms", (unsigned long)now);
        _commands->resumeKeptUpdate();
        _commands->requestModuleCommandFetch();
    }

    void reconcileTheSubscriptionsOnTheFirstConnack()
    {
        if (_firstConnackSeen || _mqtt == nullptr || !_mqtt->isConnected())
        {
            return;
        }

        _firstConnackSeen = true;
        log("BOOT_SEQ: first CONNACK at %lu ms - remote sensor subscriptions reconciled", (unsigned long)nowMs());
        if (_remoteSensors != nullptr)
        {
            _remoteSensors->onRuleSetChanged();
        }
    }

    /**
     * @brief The link has an address: the structure that reports it is what goes out first.
     *
     * Owed when the server has not confirmed this address - on every boot, since nothing is
     * confirmed before the first structure. What is waited for is the next structure of
     * OnChangeLocalIpUseCase to settle; it hands it over in this same iteration, after this.
     */
    void enterTheStructurePhase(const char *ip, uint32_t now)
    {
        _structureOwed = (_localIp != nullptr) && theServerDoesNotHave(ip);
        _structuresSettledAtEntry = (_localIp != nullptr) ? _localIp->structuresSettled() : 0;

        char reason[48];
        snprintf(reason, sizeof(reason), "link up, %s", ip != nullptr ? ip : "address unknown");
        moveTo(Phase::NET_STRUCTURE, reason, now);

        if (!_listening)
        {
            _listening = true;
            if (_listeners != nullptr)
            {
                log("BOOT_SEQ: UDP listeners started on the first link at %lu ms", (unsigned long)now);
                _listeners->startListening();
            }
        }
    }

    /**
     * @brief The catalog sync went from in flight to finished: what a catalog arms is armed now.
     *
     * Watched on every iteration and not only in NET_CATALOG: a catalog that failed at boot can be
     * fetched again much later, and that is the one the late arm is for.
     */
    void noteAFinishedCatalogSync()
    {
        const bool inFlight = (_catalog != nullptr) && _catalog->isSyncInFlight();
        if (_catalogWasInFlight && !inFlight && _scan != nullptr)
        {
            _scan->onCatalogSettled();
        }
        _catalogWasInFlight = inFlight;
    }

    /**
     * @brief Let the discovery go: once the first outcomes are in, and the network phases are over -
     *        or the boot's association came back without a link and the discovery has the catalog it
     *        was armed with.
     */
    void decideTheDiscovery()
    {
        if (_discoveryOpen || !_readingsSettled)
        {
            return;
        }

        if (_phase == Phase::STEADY)
        {
            _discoveryOpen = true;
            log("BOOT_SEQ: gate DISCOVERY open (first readings settled, STEADY)");
            return;
        }

        if (_phase == Phase::LINK && _scanArmedAtBoot &&
            (_association == nullptr || _association->isTheBootAssociationOver()))
        {
            _discoveryOpen = true;
            log("BOOT_SEQ: gate DISCOVERY open (first readings settled, no link, catalog on flash)");
        }
    }

    /** @brief Has the wait of the phase entered at _phaseEnteredAtMs outlasted this ceiling? */
    bool aCeilingIsOver(uint32_t ceilingMs, uint32_t now) const
    {
        return ceilingMs > 0 && (now - _phaseEnteredAtMs) >= ceilingMs;
    }

    void noteTheFirstReadings(uint32_t now)
    {
        if (_readingsSettled)
        {
            return;
        }

        if (_sensors == nullptr)
        {
            _readingsSettled = true;
            return;
        }

        const ISensorService::FirstReadings readings = _sensors->firstReadings();
        if (!readings.settled)
        {
            const uint32_t ceilingMs =
                _ceilings.firstReadingsBaseMs + readings.awaitedChannels * _ceilings.firstReadingsPerChannelMs;
            if (_ceilings.firstReadingsBaseMs == 0 || (now - _localAtMs) < ceilingMs)
            {
                return;
            }
            // Read without them: the telemetry and the discovery go on what there is.
            log("BOOT_SEQ: first readings gave up after %lu ms (%u of %u sensor(s) without an outcome)",
                (unsigned long)(now - _localAtMs), (unsigned)readings.outstanding, (unsigned)readings.awaited);
            _sensors->giveUpOnTheFirstReadings();
            _readingsSettled = true;
            return;
        }

        _readingsSettled = true;
        log("BOOT_SEQ: first readings settled (%u sensor(s), last at %lu ms)",
            (unsigned)readings.awaited, (unsigned long)readings.lastAtMs);
    }

    /**
     * @brief The clock the first telemetry is stamped with: set by SNTP on this boot, or - past
     *        SNTP_WAIT_CEILING_MS from the start - whatever the module has.
     */
    void noteTheClock(uint32_t now)
    {
        if (_clockSettled)
        {
            return;
        }
        if (_clock == nullptr)
        {
            _clockSettled = true;
            return;
        }
        if (_clock->wasSetBySntpThisBoot())
        {
            _clockSettled = true;
            log("BOOT_SEQ: clock set by SNTP at %lu ms (seeded from EEPROM: %s)", (unsigned long)_clock->sntpSetAtMs(),
                _clock->wasSeededFromStorage() ? "yes" : "no");
            return;
        }
        if (now < SNTP_WAIT_CEILING_MS)
        {
            return;
        }
        _clockSettled = true;
        log("BOOT_SEQ: no SNTP in %lu ms - the telemetry goes with the module's clock (seeded from EEPROM: %s)",
            (unsigned long)now, _clock->wasSeededFromStorage() ? "yes" : "no");
    }

    /** @brief One line per gate, on the iteration it opens: what the tape of a boot is read by. */
    void noteTheGatesThatOpened()
    {
        if (!_telemetryOpened && allows(BootGate::TELEMETRY))
        {
            _telemetryOpened = true;
            log("BOOT_SEQ: gate TELEMETRY open (structure settled, first readings settled, clock settled)");
        }
        if (!_ruleListOpened && allows(BootGate::RULE_LIST))
        {
            _ruleListOpened = true;
            log("BOOT_SEQ: gate RULE_LIST open (structure settled)");
        }
        if (!_deviceUpdateOpened && allows(BootGate::DEVICE_UPDATE))
        {
            _deviceUpdateOpened = true;
            log("BOOT_SEQ: gate DEVICE_UPDATE open (structure settled)");
        }
    }

    void moveTo(Phase next, const char *event, uint32_t now)
    {
        log("BOOT_SEQ: %s -> %s (%s) at %lu ms", phaseName(_phase), phaseName(next), event,
            (unsigned long)now);
        _phase = next;
        _phaseEnteredAtMs = now;
    }

    void log(const char *format, ...)
    {
        if (_logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            return;
        }

        char buffer[160];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        _logger->println(DebugFeatures::SETUP_DEBUG, buffer, MqttLoggerMode::SerialOnly);
    }

    IArduinoAdapter *_arduino;
    ILogger *_logger;
    IDebugSystem *_debugSystem;
    IWifiAdapter *_wifi = nullptr;
    IOnChangeLocalIpUseCase *_localIp = nullptr;
    ISensorService *_sensors = nullptr;
    ISyncSensorTypesUseCase *_catalog = nullptr;
    IStartupSensorScan *_scan = nullptr;
    IMqttManager *_mqtt = nullptr;
    ISyncRulesUseCase *_rules = nullptr;
    Ceilings _ceilings;
    IRemoteSensorReceiver *_remoteSensors = nullptr;
    INetworkListeners *_listeners = nullptr;
    const IBootAssociation *_association = nullptr;
    ICommandService *_commands = nullptr;
    INTPTimeAdapter *_clock = nullptr;

    Phase _phase = Phase::NOT_STARTED;
    uint32_t _localAtMs = 0;
    uint32_t _phaseEnteredAtMs = 0;
    bool _structureOwed = false;
    uint32_t _structuresSettledAtEntry = 0;
    bool _readingsSettled = false;
    bool _clockSettled = false;
    bool _catalogSeenInFlight = false;
    bool _catalogWasInFlight = false;
    bool _scanArmedAtBoot = false;
    bool _discoveryOpen = false;
    bool _firstStructureSettled = false;
    bool _steadyReached = false;
    bool _firstConnackSeen = false;
    bool _listening = false;
    bool _telemetryOpened = false;
    bool _ruleListOpened = false;
    bool _deviceUpdateOpened = false;
};
