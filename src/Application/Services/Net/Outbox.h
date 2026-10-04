#pragma once

#include "Application/Interfaces/IOutbox.h"
#include "Application/Services/BackgroundActs.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Adapters/ILinkProbe.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiRoamRadio.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttLinkMachine.h"
#include "Core/Domain/Interfaces/Scheduling/IMessageQueue.h"
#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"
#include "Core/Domain/UseCases/Network/LinkProbeRun.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief One queue of outgoing requests and one task that makes them.
 *
 * The shape this replaces: every producer in the firmware called the REST client itself, from
 * the control loop, and waited. A REST call in this firmware is allowed ~90 s (three 30 s
 * attempts), and four such producers ran in one loop iteration, so the module could spend
 * minutes not reading a sensor, not evaluating a rule and not answering a button - while
 * reporting itself healthy, because the watchdog was fed from a task of its own.
 *
 * The shape here: producers submit and return within microseconds; a single task drains this
 * object and makes the calls. Three consequences are worth stating because they are the whole
 * point:
 *
 *  1. **The worst control-loop iteration does not depend on the server.** Submitting is a
 *     pointer swap under a lock held for exactly that, never across a network call.
 *  2. **The network task's work is bounded**, because the retry budget is set here rather than
 *     by how long the server chooses to stay silent - so this task can be put under the task
 *     watchdog, which no network path in this firmware could be before.
 *  3. **Producers still learn the outcome.** Every submit returns a ticket and every finished
 *     request comes back through the completion queue, drained by the control loop on its own
 *     task. Nothing calls back into a producer from this task, so no producer's state becomes
 *     shared between cores.
 *
 * Overflow is by kind, not by age. State - sensor readings, device states, diagnostics, the rule
 * list, the two command fetches - lives in one slot per kind and a new request *replaces* the one
 * waiting: three queued sensor snapshots are one stale picture and two staler ones, and the
 * backend wants the fresh one; two queued "go and look for commands" are one look. Events - rule
 * acknowledgements, rule state changes, command reports - go into a small ring and are never
 * dropped in silence; when the ring is full the submission is refused and the producer, which
 * has a ticket mechanism precisely for this, keeps its state and tries again.
 *
 * The link is this task's too. The radio restart is a job in the queue, served ahead of every
 * request; the MQTT reconnect is a state machine this task beats between requests and holds its
 * requests for while an act is in progress. One task for all of it is the point, not a saving:
 * the heap has to hold whatever is in flight at once, and with the handshake, the socket and the
 * bodies on one task that is the largest of them rather than their sum. And a request is not
 * attempted while the link is down - it waits, at no cost in attempts, for the link the radio
 * restart is about to bring back.
 */
class Outbox : public IOutbox {
public:
    /** @brief Stack for the network task, in bytes. Same figure the diagnostic sender used. */
    static const uint32_t DEFAULT_STACK_BYTES = 8192;

    /** @brief How long the network task sleeps when there is nothing to send. */
    static const uint32_t IDLE_POLL_MS = 50;

    /**
     * @brief How often the MQTT connect state machine is beaten while an act is in progress.
     *
     * The cadence at which the machine is driven. It bounds how late the machine notices a
     * CONNACK, a refusal or its own timeout, and it is the whole cost of the act to this task
     * between those events.
     */
    static const uint32_t LINK_BEAT_MS = 200;

    /**
     * @brief Attempts per request.
     *
     * Four. The budget is paid by this task alone, not by the control loop, so the bound on it
     * is not the patience of everything else in the module - persistence on a bad link is worth
     * more than a small retry count.
     */
    static const uint8_t DEFAULT_MAX_ATTEMPTS = 4;

    /**
     * @brief First backoff, quadrupled per attempt: 500 ms, 2 s, 8 s.
     *
     * The twelve retry loops in RestApiService paused a flat 200 ms (a few of them 200*try or
     * 1000*try). A flat pause is the wrong shape against a server that is down rather than
     * momentarily busy: it retries hardest exactly when retrying cannot help.
     *
     * Worst case for one whole request: DEFAULT_MAX_ATTEMPTS attempts plus 10.5 s of backoff.
     * That figure bounds how long a message can take to give up, but it is *not* the figure the
     * watchdog cares about - see runNetworkTask() for that one.
     */
    static const uint32_t DEFAULT_BACKOFF_BASE_MS = 500;
    static const uint32_t BACKOFF_FACTOR = 4;

    /**
     * @brief Events that may wait at once.
     *
     * Small on purpose. Collapsing state by kind is what lets the whole outbox be this size: the
     * four periodic producers need one slot each regardless of how long the network is down, so
     * the ring only has to absorb bursts of rule acknowledgements.
     */
    static const size_t EVENT_RING_CAPACITY = 8;

    /**
     * @brief Kinds that wait in a slot of their own: the seven state requests, the radio restart,
     *        the move to another access point and the probe of the link. Counted here because the
     *        completion queue is sized from it.
     */
    static const size_t STATE_KIND_COUNT = 10;

    /**
     * @brief The most requests that can wait at once: one per state slot plus a full event ring.
     *
     * This is how deep the completion queue has to be. Every request that waits produces exactly
     * one completion, and the control loop drains them once per iteration; a queue shallower than
     * this loses an outcome on the one day everything is owed at once - and a lost RULE_LIST
     * outcome is a module that never asks for rules again.
     */
    static const size_t MAX_IN_FLIGHT = STATE_KIND_COUNT + EVENT_RING_CAPACITY;

    /**
     * @brief Jobs that can be out of their slots and unfinished at the same time: a request
     *        held for the link inside dispatch(), and the restart that hold takes and runs from
     *        inside itself. Their slots are free meanwhile and can be filled again behind them.
     */
    static const size_t JOBS_IN_DISPATCH = 2;

    /**
     * @brief What the composition root gives the completion queue: the bound, exactly.
     *
     * Every producer submits from the control loop, and the loop drains this queue before it
     * submits, so the outcomes owed at one drain are at most the requests that could be waiting
     * plus the two in dispatch - every slot filled again behind them. No room beyond that is
     * reserved: a margin over an exact count buys nothing, and the count is walked by a test.
     */
    static const size_t COMPLETION_QUEUE_DEPTH = MAX_IN_FLIGHT + JOBS_IN_DISPATCH;

    // Tautological while the depth stays derived; it catches exactly one real mistake - the
    // depth replaced by a literal that no longer follows the slots and the ring.
    static_assert(COMPLETION_QUEUE_DEPTH >= MAX_IN_FLIGHT + JOBS_IN_DISPATCH,
                  "every request that can wait at once, and the two in dispatch, must have room "
                  "for their completions");

    Outbox(IHybridAPIService* api,
           ITaskScheduler* scheduler,
           IMessageQueue<OutboxCompletion>* completions,
           ILogger* logger = nullptr,
           IDebugSystem* debugSystem = nullptr,
           uint32_t stackBytes = DEFAULT_STACK_BYTES,
           uint8_t priority = 1,
           uint8_t maxAttempts = DEFAULT_MAX_ATTEMPTS,
           uint32_t backoffBaseMs = DEFAULT_BACKOFF_BASE_MS)
        : _api(api),
          _scheduler(scheduler),
          _completions(completions),
          _logger(logger),
          _debugSystem(debugSystem),
          _stackBytes(stackBytes),
          _priority(priority),
          _maxAttempts(maxAttempts > 0 ? maxAttempts : 1),
          _backoffBaseMs(backoffBaseMs)
    {
        _events.resize(EVENT_RING_CAPACITY);
    }

    ~Outbox() override
    {
        delete _sensorSlot;
        delete _deviceSlot;
        delete _diagnosticSlot;
    }

    Outbox(const Outbox&) = delete;
    Outbox& operator=(const Outbox&) = delete;

    // --- IOutbox: submitting -------------------------------------------------------------------

    uint32_t submitSensorUpdate(const UpdateDto& update) override
    {
        return submitState(OutboxKind::SENSOR_UPDATE, new UpdateDto(update), nullptr);
    }

    uint32_t submitDeviceUpdate(const UpdateDto& update) override
    {
        return submitState(OutboxKind::DEVICE_UPDATE, new UpdateDto(update), nullptr);
    }

    uint32_t submitDiagnostic(const DiagnosticDto& diagnostic) override
    {
        return submitState(OutboxKind::DIAGNOSTIC, nullptr, new DiagnosticDto(diagnostic));
    }

    uint32_t submitRuleListRequest() override
    {
        return submitState(OutboxKind::RULE_LIST, nullptr, nullptr);
    }

    uint32_t submitRuleStateBulk(const std::string& json) override
    {
        return submitEvent(OutboxKind::RULE_STATE_BULK, std::string(), json);
    }

    uint32_t submitRuleStateByUid(const std::string& ruleUid, const std::string& json) override
    {
        return submitEvent(OutboxKind::RULE_STATE_BY_UID, ruleUid, json);
    }

    uint32_t submitRuleSensorOffline(const std::string& json) override
    {
        return submitEvent(OutboxKind::RULE_SENSOR_OFFLINE, std::string(), json);
    }

    uint32_t submitDeviceCommandFetch() override
    {
        return submitState(OutboxKind::DEVICE_COMMAND_FETCH, nullptr, nullptr);
    }

    uint32_t submitModuleCommandFetch() override
    {
        return submitState(OutboxKind::MODULE_COMMAND_FETCH, nullptr, nullptr);
    }

    uint32_t submitSensorTypesRequest() override
    {
        return submitState(OutboxKind::SENSOR_TYPES_FETCH, nullptr, nullptr);
    }

    uint32_t submitWifiReconnect() override
    {
        return submitState(OutboxKind::WIFI_RECONNECT, nullptr, nullptr);
    }

    uint32_t submitWifiReconnectForALostLink() override
    {
        return submitState(OutboxKind::WIFI_RECONNECT, nullptr, nullptr, true);
    }

    uint32_t submitLinkProbe() override
    {
        return submitState(OutboxKind::LINK_PROBE, nullptr, nullptr);
    }

    uint32_t submitWifiRoam(const uint8_t* bssid, uint8_t channel) override
    {
        if (bssid == nullptr)
        {
            return kRejected;
        }

        const uint32_t ticket = nextTicket();
        bool collapsed = false;

        {
            std::lock_guard<std::mutex> guard(_mutex);
            collapsed = _wifiRoamPending;
            _wifiRoamPending = true;
            _wifiRoamTicket = ticket;
            memcpy(_wifiRoamBssid, bssid, sizeof(_wifiRoamBssid));
            _wifiRoamChannel = channel;
        }

        // Not through submitState(): this is the one state request that carries something, and the
        // target has to be stored under the same lock as the flag that says a target is waiting.
        if (collapsed)
        {
            _collapsedCount++;
            log("OUTBOX: kind %u collapsed - a fresher one replaced the request still waiting; "
                "total collapsed %u",
                (unsigned)OutboxKind::WIFI_ROAM, (unsigned)_collapsedCount);
        }

        ensureTaskStarted();
        return ticket;
    }

    uint32_t submitCommandResult(const CommandResultDto& result, ApiPriority priority) override
    {
        std::unique_ptr<CommandReportDto> report(new CommandReportDto());
        report->commandResult = result;
        report->priority = priority;
        return submitReport(OutboxKind::COMMAND_RESULT, std::move(report));
    }

    uint32_t submitDeviceCommandResult(const DeviceCommandUpdateDto& update) override
    {
        std::unique_ptr<CommandReportDto> report(new CommandReportDto());
        report->deviceCommandUpdate = update;
        return submitReport(OutboxKind::DEVICE_COMMAND_RESULT, std::move(report));
    }

    uint32_t submitModuleCommandResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        std::unique_ptr<CommandReportDto> report(new CommandReportDto());
        report->moduleCommandUpdates = updates;
        return submitReport(OutboxKind::MODULE_COMMAND_RESULT, std::move(report));
    }

    uint32_t submitRawIoResult(const RawIoResultDto& result) override
    {
        std::unique_ptr<CommandReportDto> report(new CommandReportDto());
        report->rawIoResult = result;
        return submitReport(OutboxKind::RAW_IO_RESULT, std::move(report));
    }

    uint32_t submitModuleStructure(const ModuleStructure& structure, ApiPriority priority) override
    {
        std::unique_ptr<CommandReportDto> report(new CommandReportDto());
        report->moduleStructure = structure;
        report->priority = priority;
        return submitReport(OutboxKind::MODULE_STRUCTURE, std::move(report));
    }

    // --- IOutbox: collecting outcomes -----------------------------------------------------------

    bool pollCompletion(OutboxCompletion& out) override
    {
        return (_completions != nullptr) && _completions->pop(out);
    }

    bool takeRuleListResponse(std::vector<Rule>& out) override
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (!_ruleListResponseReady)
        {
            return false;
        }
        out.swap(_ruleListResponse);
        _ruleListResponse.clear();
        _ruleListResponseReady = false;
        return true;
    }

    bool takeDeviceCommandResponse(std::vector<DeviceCommandDto>& out) override
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (!_deviceCommandResponseReady)
        {
            return false;
        }
        out.swap(_deviceCommandResponse);
        _deviceCommandResponse.clear();
        _deviceCommandResponseReady = false;
        return true;
    }

    bool takeModuleCommandResponse(std::vector<ModuleCommandDto>& out) override
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (!_moduleCommandResponseReady)
        {
            return false;
        }
        out.swap(_moduleCommandResponse);
        _moduleCommandResponse.clear();
        _moduleCommandResponseReady = false;
        return true;
    }

    bool takeSensorTypesResponse(std::vector<SensorType>& out) override
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (!_sensorTypesResponseReady)
        {
            return false;
        }
        out.swap(_sensorTypesResponse);
        _sensorTypesResponse.clear();
        _sensorTypesResponseReady = false;
        return true;
    }

    // --- IOutbox: observability -----------------------------------------------------------------

    uint32_t collapsedCount() const override { return _collapsedCount; }
    uint32_t refusedEventCount() const override { return _refusedEventCount; }
    uint32_t undeliveredCount() const override { return _undeliveredCount; }
    uint32_t lostCompletionCount() const override { return _lostCompletionCount; }

    /** @brief Requests the network task delivered. */
    uint32_t deliveredCount() const { return _deliveredCount; }

    bool ensureNetworkTask() override
    {
        if (!_taskStarted && pendingCount() > 0)
        {
            ensureTaskStarted();
        }
        return _taskStarted;
    }

    /** @brief True once the network task has actually been created. */
    bool taskStarted() const { return _taskStarted; }

    /** @brief How many times creating the network task was refused, most likely for want of heap. */
    uint32_t taskStartFailureCount() const { return _taskStartFailureCount; }

    /** @brief Requests waiting right now, across every slot and the event ring. */
    size_t pendingCount() const
    {
        std::lock_guard<std::mutex> guard(_mutex);
        size_t pending = _eventCount;
        pending += (_sensorSlot != nullptr) ? 1u : 0u;
        pending += (_deviceSlot != nullptr) ? 1u : 0u;
        pending += (_diagnosticSlot != nullptr) ? 1u : 0u;
        pending += _ruleListPending ? 1u : 0u;
        pending += _deviceCommandPending ? 1u : 0u;
        pending += _moduleCommandPending ? 1u : 0u;
        pending += _sensorTypesPending ? 1u : 0u;
        pending += _wifiReconnectPending ? 1u : 0u;
        pending += _wifiRoamPending ? 1u : 0u;
        pending += _linkProbePending ? 1u : 0u;
        return pending;
    }

    /**
     * @brief The radio, for the restart job and for the link gate.
     *
     * Optional. Without one a WIFI_RECONNECT completes undelivered, and requests are attempted
     * whatever the link is doing.
     */
    void setWifiAdapter(IWifiAdapter* wifi) { _wifi = wifi; }

    /**
     * @brief The radio again, for the one job that names an access point.
     *
     * Optional and separate from the adapter above: what a move needs of the radio is not what the
     * link gate needs of it. Without one a WIFI_ROAM completes undelivered.
     */
    void setRoamRadio(IWifiRoamRadio* radio) { _roamRadio = radio; }

    /**
     * @brief What the probe of the link asks with, and how it is taken.
     *
     * Optional. Without a probe a LINK_PROBE comes back saying the gateway could not be asked - which
     * decides nothing about the link, rather than deciding it wrongly.
     */
    void setLinkProbe(ILinkProbe* probe, const LinkProbePlan& plan)
    {
        _linkProbe = probe;
        _linkProbePlan = plan;
    }

    /**
     * @brief Where the network task's creation, or its refusal, is recorded for the link's supervision.
     *
     * Optional: a refusal is counted and written to the log either way.
     */
    void setStatusBoard(ModuleStatusBoard* statusBoard) { _statusBoard = statusBoard; }

    /**
     * @brief The MQTT connect state machine this task is to beat between its requests.
     *
     * Optional. Without one the broker is nobody's business here, as before there was a link to
     * keep.
     */
    void setMqttLink(IMqttLinkMachine* link) { _mqttLink = link; }

    /**
     * @brief Where the network task says what it is doing: one attempt of a request of a kind,
     *        the radio restart, the MQTT connect act.
     *
     * Optional, and observation only - one store at each boundary of an act, read by whoever has
     * to name the work a heap low fell on. Nothing here depends on it.
     */
    void setActBoard(BackgroundActBoard* acts) { _acts = acts; }

    /**
     * @brief Run the network task's body on the caller instead of on a task of its own.
     *
     * For hosts that drive the scheduler themselves. Never called on the device.
     */
    void runNetworkTaskBody() { runNetworkTask(); }

private:
    struct Event {
        OutboxKind kind = OutboxKind::RULE_STATE_BULK;
        uint32_t ticket = 0;
        std::string uid;
        std::string json;
        // Only the five command reports allocate one; every other event leaves it null and pays a
        // pointer for it. The ring is eight deep and a report holds a whole module structure, so
        // carrying the payload by value in every slot would cost the ring's size in idle RAM.
        std::unique_ptr<CommandReportDto> report;
    };

    struct Job {
        OutboxKind kind = OutboxKind::SENSOR_UPDATE;
        uint32_t ticket = 0;
        UpdateDto* update = nullptr;
        DiagnosticDto* diagnostic = nullptr;
        std::string uid;
        std::string json;
        std::unique_ptr<CommandReportDto> report;
        // WIFI_ROAM only: the access point to move to, copied out of its slot with the ticket.
        uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
        uint8_t channel = 0;
        // WIFI_RECONNECT only: asked for a lost link, so not run if the link is back.
        bool forALostLink = false;
    };

    // --- submission ----------------------------------------------------------------------------

    uint32_t submitState(OutboxKind kind, UpdateDto* update, DiagnosticDto* diagnostic,
                         bool forALostLink = false)
    {
        const uint32_t ticket = nextTicket();

        UpdateDto* displacedUpdate = nullptr;
        DiagnosticDto* displacedDiagnostic = nullptr;
        bool collapsed = false;
        bool stored = true;

        {
            std::lock_guard<std::mutex> guard(_mutex);
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
            switch (kind)
            {
            case OutboxKind::SENSOR_UPDATE:
                displacedUpdate = _sensorSlot;
                _sensorSlot = update;
                _sensorTicket = ticket;
                collapsed = (displacedUpdate != nullptr);
                break;
            case OutboxKind::DEVICE_UPDATE:
                displacedUpdate = _deviceSlot;
                _deviceSlot = update;
                _deviceTicket = ticket;
                collapsed = (displacedUpdate != nullptr);
                break;
            case OutboxKind::DIAGNOSTIC:
                displacedDiagnostic = _diagnosticSlot;
                _diagnosticSlot = diagnostic;
                _diagnosticTicket = ticket;
                collapsed = (displacedDiagnostic != nullptr);
                break;
            case OutboxKind::RULE_LIST:
                collapsed = _ruleListPending;
                _ruleListPending = true;
                _ruleListTicket = ticket;
                break;
            case OutboxKind::DEVICE_COMMAND_FETCH:
                collapsed = _deviceCommandPending;
                _deviceCommandPending = true;
                _deviceCommandTicket = ticket;
                break;
            case OutboxKind::MODULE_COMMAND_FETCH:
                collapsed = _moduleCommandPending;
                _moduleCommandPending = true;
                _moduleCommandTicket = ticket;
                break;
            case OutboxKind::SENSOR_TYPES_FETCH:
                collapsed = _sensorTypesPending;
                _sensorTypesPending = true;
                _sensorTypesTicket = ticket;
                break;
            case OutboxKind::WIFI_RECONNECT:
                collapsed = _wifiReconnectPending;
                // One restart for both asks: conditional only if every ask folded into it was.
                _wifiReconnectForALostLink = forALostLink && (!collapsed || _wifiReconnectForALostLink);
                _wifiReconnectPending = true;
                _wifiReconnectTicket = ticket;
                break;
            case OutboxKind::LINK_PROBE:
                collapsed = _linkProbePending;
                _linkProbePending = true;
                _linkProbeTicket = ticket;
                break;

            // No default, and the missing-case diagnostic is made an error for the extent of the
            // switch - on its own it is a warning the device build does not enable. A kind added
            // later and not given a slot here must be a compile error, not a live ticket for a
            // request that was never stored - the producer would wait for a completion that can
            // never arrive. attemptOnce() below is written the same way.
            //
            // WIFI_ROAM is stored by submitWifiRoam() and not here, because it carries a target;
            // reaching this function with it is a refusal, which is what an unstored request owes
            // its producer.
            case OutboxKind::WIFI_ROAM:
            case OutboxKind::RULE_STATE_BULK:
            case OutboxKind::RULE_STATE_BY_UID:
            case OutboxKind::RULE_SENSOR_OFFLINE:
            case OutboxKind::COMMAND_RESULT:
            case OutboxKind::DEVICE_COMMAND_RESULT:
            case OutboxKind::MODULE_COMMAND_RESULT:
            case OutboxKind::RAW_IO_RESULT:
            case OutboxKind::MODULE_STRUCTURE:
                stored = false;
                break;
            }
#pragma GCC diagnostic pop
        }

        if (!stored)
        {
            delete update;
            delete diagnostic;
            return kRejected;
        }

        // Freed outside the lock: a DTO's destructor walks vectors of std::string, and the lock
        // exists to be short enough that a producer never notices it.
        delete displacedUpdate;
        delete displacedDiagnostic;

        if (collapsed)
        {
            _collapsedCount++;
            log("OUTBOX: kind %u collapsed - a fresher one replaced the request still waiting; "
                "total collapsed %u",
                (unsigned)kind, (unsigned)_collapsedCount);
        }

        ensureTaskStarted();
        return ticket;
    }

    uint32_t submitReport(OutboxKind kind, std::unique_ptr<CommandReportDto> report)
    {
        return submitEvent(kind, std::string(), std::string(), std::move(report));
    }

    uint32_t submitEvent(OutboxKind kind,
                         const std::string& uid,
                         const std::string& json,
                         std::unique_ptr<CommandReportDto> report = nullptr)
    {
        const uint32_t ticket = nextTicket();
        bool accepted = false;

        {
            std::lock_guard<std::mutex> guard(_mutex);
            if (_eventCount < EVENT_RING_CAPACITY)
            {
                const size_t index = (_eventHead + _eventCount) % EVENT_RING_CAPACITY;
                _events[index].kind = kind;
                _events[index].ticket = ticket;
                _events[index].uid = uid;
                _events[index].json = json;
                _events[index].report = std::move(report);
                _eventCount++;
                accepted = true;
            }
        }

        if (!accepted)
        {
            // Refused at submission rather than dropped later, so that the producer finds out
            // now - while it still holds the state it would need to build the message again.
            _refusedEventCount++;
            log("OUTBOX: event kind %u refused, ring full (capacity %u); total refused %u",
                (unsigned)kind, (unsigned)EVENT_RING_CAPACITY, (unsigned)_refusedEventCount);
            return kRejected;
        }

        ensureTaskStarted();
        return ticket;
    }

    uint32_t nextTicket()
    {
        // Wraps past UINT32_MAX back to 1, never to kRejected. At one request per second that
        // takes 136 years; the guard is here so the invariant is a property of the code rather
        // than of the arithmetic.
        std::lock_guard<std::mutex> guard(_ticketMutex);
        _lastTicket++;
        if (_lastTicket == kRejected)
        {
            _lastTicket = 1;
        }
        return _lastTicket;
    }

    // --- the network task ----------------------------------------------------------------------

    void ensureTaskStarted()
    {
        if (_taskStarted || _scheduler == nullptr)
        {
            return;
        }

        // Started on first use rather than in the composition root: module modes that never send
        // anything should not pay for the stack. The flag is set from the result, not before the
        // call, so that a creation refused for want of heap is retried on the next submission - or
        // on ensureNetworkTask() - instead of silencing the network until the next reboot.
        if (_scheduler->createTask("outbox-net", _stackBytes, _priority, [this]() { runNetworkTask(); }))
        {
            _taskStarted = true;
            if (_statusBoard != nullptr)
            {
                _statusBoard->noteNetworkTask(true);
            }
            return;
        }

        if (_statusBoard != nullptr)
        {
            _statusBoard->noteNetworkTask(false);
        }
        _taskStartFailureCount++;
        log("OUTBOX: could not create the network task (%u bytes of stack); will try again on the "
            "next submission or supervision tick; failures so far %u",
            (unsigned)_stackBytes, (unsigned)_taskStartFailureCount);
    }

    void runNetworkTask()
    {
        // What the task watchdog measures is the gap between two feeds, not how long a request
        // takes. This task feeds at the top of every iteration, after every attempt and after
        // every backoff, so the worst gap is one attempt - not the whole retry budget.
        //
        // One attempt, worst case, is two things added together:
        //
        //   - up to ~35 s waiting for RestAPIService::_apiMutex. The wait is bounded by one
        //     holder's single call: FreeRTOS mutexes queue equal-priority waiters FIFO, so this
        //     task sits behind at most one full hold, and every holder makes one attempt per
        //     lock take (the callers reachable from the control loop feed their own watchdog
        //     entry between attempts and release the lock in between).
        //   - up to ~37 s for the call itself: 30 s of POST timeout plus the 2 s TestConnection
        //     ping that sendDiagnosticData() makes first, plus the connect.
        //
        // ~72 s is the WDT_WORST_LEGAL_PAUSE_S entry this task contributes to the watchdog
        // threshold calculation in constants.h; WDT_TIMEOUT_S is twice that. The margin holds
        // only while the methods this task drives make a single attempt each - retry loops
        // inside the transport would multiply with this task's own budget and eat it.
        //
        // Worth knowing before changing any of those numbers: main.cpp arms the watchdog with
        // esp_task_wdt_init(..., true) - panic on expiry, i.e. subscribing this task gives it
        // a reboot path of its own.
        //
        // The link jobs stay under the same entry. The radio restart feeds from inside its own
        // loop (WifiAdapter::reconnect() resets the watchdog after every association attempt
        // that fails with time left, and the task that calls it is now this one). The attempt
        // that ends its minute is not followed by a feed but by a scan of what is in range, so
        // its unfed unit is that last attempt plus the scan - about 25 s, under a REST attempt.
        // The move to another access point waits for one association, bounded by the timeout in
        // WifiAdapter::roamTo() - well inside the same entry, and a single unfed unit. So does the
        // probe of the link: one wait for the gateway and at most a few one-second echoes, bounded
        // by the plan it is given (boardLinkProbePlan() on the device).
        // The MQTT act is beaten from here every LINK_BEAT_MS, with a feed on every beat.
        _scheduler->watchdogSubscribeCurrentTask();

        while (true)
        {
            _scheduler->watchdogFeed();

            Job job;

            // The link comes first, in this order: a radio restart that has been asked for, then
            // the broker socket, then everything that needs both. A request made before the
            // link is back only spends its attempts, and a handshake made while the socket is
            // being rebuilt asks the heap for both at once.
            if (takeLinkJob(job))
            {
                dispatch(job);
                continue;
            }

            if (beatMqttLink())
            {
                // An act is in progress: the task is its until it settles.
                _scheduler->delayMs(LINK_BEAT_MS);
                continue;
            }

            if (linkIsUp() && takeNextJob(job))
            {
                dispatch(job);
            }
            else
            {
                _scheduler->delayMs(IDLE_POLL_MS);
            }
        }
    }

    /**
     * @brief The act of the link waiting, if one is: served ahead of every request.
     *
     * The restart first. A module whose link is down has nothing to gain from a better access
     * point, and the restart's own association may well land on one anyway. The probe last: what it
     * finds is about the association the other two leave behind.
     */
    bool takeLinkJob(Job& job)
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_wifiReconnectPending)
        {
            job.kind = OutboxKind::WIFI_RECONNECT;
            job.ticket = _wifiReconnectTicket;
            job.forALostLink = _wifiReconnectForALostLink;
            _wifiReconnectPending = false;
            return true;
        }
        if (_wifiRoamPending)
        {
            job.kind = OutboxKind::WIFI_ROAM;
            job.ticket = _wifiRoamTicket;
            memcpy(job.bssid, _wifiRoamBssid, sizeof(job.bssid));
            job.channel = _wifiRoamChannel;
            _wifiRoamPending = false;
            return true;
        }
        if (_linkProbePending)
        {
            job.kind = OutboxKind::LINK_PROBE;
            job.ticket = _linkProbeTicket;
            _linkProbePending = false;
            return true;
        }
        return false;
    }

    /**
     * @brief One beat of the MQTT connect state machine, with the act's edges on the log.
     * @return true while an act is in progress
     */
    bool beatMqttLink()
    {
        if (_mqttLink == nullptr)
        {
            return false;
        }

        const bool inProgress = _mqttLink->serviceLinkOnce();
        if (inProgress != _mqttActInProgress)
        {
            _mqttActInProgress = inProgress;
            log(inProgress ? "OUTBOX: mqtt connect act started, requests wait"
                           : "OUTBOX: mqtt connect act over");
        }
        noteMqttAct(inProgress);
        return inProgress;
    }

    // --- what this task is doing, for the act board -----------------------------------------------

    void noteAttemptStarted(OutboxKind kind)
    {
        if (_acts != nullptr)
        {
            _acts->actStarted(BackgroundActBoard::NETWORK, BackgroundActs::ofOutboxKind(kind));
            // The slot holds one act. A connect act this attempt runs across is written again by
            // the first beat that finds it still in progress.
            _mqttActOnTheBoard = false;
        }
    }

    void noteAttemptFinished(OutboxKind kind)
    {
        if (_acts != nullptr)
        {
            _acts->actFinished(BackgroundActBoard::NETWORK, BackgroundActs::ofOutboxKind(kind));
        }
    }

    /** @brief Written on the act's edges, not on its beats: a beat that changes nothing is free. */
    void noteMqttAct(bool inProgress)
    {
        if (_acts == nullptr || inProgress == _mqttActOnTheBoard)
        {
            return;
        }
        _mqttActOnTheBoard = inProgress;
        if (inProgress)
        {
            _acts->actStarted(BackgroundActBoard::NETWORK, BackgroundActs::NET_MQTT_CONNECT);
        }
        else
        {
            _acts->actFinished(BackgroundActBoard::NETWORK, BackgroundActs::NET_MQTT_CONNECT);
        }
    }

    bool linkIsUp() const
    {
        return _wifi == nullptr || _wifi->isConnected();
    }

    /**
     * @brief Wait for the link to come back before the next attempt of a request in flight.
     *
     * A request whose link dropped under it is not given up on - its producer may not have a
     * second copy to send - and it does not spend its remaining attempts on a radio that is off
     * either. It waits here, and the wait serves the link: the restart that brings it back is a
     * job of this queue and is taken from inside this loop, and the MQTT machine is beaten so it
     * settles into idle rather than counting a timeout against a link that is not there.
     */
    void holdForLink()
    {
        while (!linkIsUp())
        {
            _scheduler->watchdogFeed();

            Job link;
            if (takeLinkJob(link))
            {
                dispatch(link);
                continue;
            }

            beatMqttLink();
            _scheduler->delayMs(IDLE_POLL_MS);
        }
    }

    /**
     * @brief Take the next request, rotating over the sources so none starves the others.
     *
     * The cursor matters. Serving events first would let a burst of rule acknowledgements hold
     * telemetry back for as long as the ring takes to drain; serving state first would let a
     * producer on a short period keep an event waiting indefinitely. Rotating costs nothing and
     * removes both.
     */
    bool takeNextJob(Job& job)
    {
        std::lock_guard<std::mutex> guard(_mutex);

        for (uint8_t step = 0; step < kSourceCount; ++step)
        {
            const uint8_t source = static_cast<uint8_t>((_cursor + step) % kSourceCount);

            if (source == kSourceEvents && _eventCount > 0)
            {
                Event& event = _events[_eventHead];
                job.kind = event.kind;
                job.ticket = event.ticket;
                job.uid.swap(event.uid);
                job.json.swap(event.json);
                job.report = std::move(event.report);
                event.uid.clear();
                event.json.clear();
                _eventHead = (_eventHead + 1) % EVENT_RING_CAPACITY;
                _eventCount--;
                _cursor = static_cast<uint8_t>((source + 1) % kSourceCount);
                return true;
            }

            if (source == kSourceSensor && _sensorSlot != nullptr)
            {
                job.kind = OutboxKind::SENSOR_UPDATE;
                job.ticket = _sensorTicket;
                job.update = _sensorSlot;
                _sensorSlot = nullptr;
                _cursor = static_cast<uint8_t>((source + 1) % kSourceCount);
                return true;
            }

            if (source == kSourceDevice && _deviceSlot != nullptr)
            {
                job.kind = OutboxKind::DEVICE_UPDATE;
                job.ticket = _deviceTicket;
                job.update = _deviceSlot;
                _deviceSlot = nullptr;
                _cursor = static_cast<uint8_t>((source + 1) % kSourceCount);
                return true;
            }

            if (source == kSourceDiagnostic && _diagnosticSlot != nullptr)
            {
                job.kind = OutboxKind::DIAGNOSTIC;
                job.ticket = _diagnosticTicket;
                job.diagnostic = _diagnosticSlot;
                _diagnosticSlot = nullptr;
                _cursor = static_cast<uint8_t>((source + 1) % kSourceCount);
                return true;
            }

            if (source == kSourceRuleList && _ruleListPending)
            {
                job.kind = OutboxKind::RULE_LIST;
                job.ticket = _ruleListTicket;
                _ruleListPending = false;
                _cursor = static_cast<uint8_t>((source + 1) % kSourceCount);
                return true;
            }

            if (source == kSourceDeviceCommand && _deviceCommandPending)
            {
                job.kind = OutboxKind::DEVICE_COMMAND_FETCH;
                job.ticket = _deviceCommandTicket;
                _deviceCommandPending = false;
                _cursor = static_cast<uint8_t>((source + 1) % kSourceCount);
                return true;
            }

            if (source == kSourceModuleCommand && _moduleCommandPending)
            {
                job.kind = OutboxKind::MODULE_COMMAND_FETCH;
                job.ticket = _moduleCommandTicket;
                _moduleCommandPending = false;
                _cursor = static_cast<uint8_t>((source + 1) % kSourceCount);
                return true;
            }

            if (source == kSourceSensorTypes && _sensorTypesPending)
            {
                job.kind = OutboxKind::SENSOR_TYPES_FETCH;
                job.ticket = _sensorTypesTicket;
                _sensorTypesPending = false;
                _cursor = static_cast<uint8_t>((source + 1) % kSourceCount);
                return true;
            }
        }

        return false;
    }

    void dispatch(Job& job)
    {
        OutboxCompletion completion;
        completion.ticket = job.ticket;
        completion.kind = job.kind;

        // The radio restart is one attempt: it loops over the known networks for a minute on its
        // own, and a second minute would only put off the producer's next decision. The move to
        // another access point is one attempt for the same reason - a second one would be made
        // against a look that is already stale, and the producer looks again anyway. The probe of
        // the link is one attempt because its answer is the outcome, whatever it is. Everything
        // else gets the retry budget.
        const bool isLinkJob = (job.kind == OutboxKind::WIFI_RECONNECT || job.kind == OutboxKind::WIFI_ROAM ||
                                job.kind == OutboxKind::LINK_PROBE);
        const uint8_t attemptBudget = isLinkJob ? 1 : _maxAttempts;

        for (uint8_t attempt = 1; attempt <= attemptBudget; ++attempt)
        {
            if (!isLinkJob)
            {
                holdForLink();
            }

            completion.attempts = attempt;

            // The attempt is the act: it is what holds the handshake, the body and the answer.
            // The hold above and the backoff below hold nothing but the job itself.
            noteAttemptStarted(job.kind);
            const bool attemptSucceeded = attemptOnce(job, completion);
            noteAttemptFinished(job.kind);

            if (attemptSucceeded)
            {
                completion.delivered = true;
                break;
            }

            _scheduler->watchdogFeed();

            if (attempt < attemptBudget)
            {
                _scheduler->delayMs(backoffMs(attempt));
                _scheduler->watchdogFeed();
            }
        }

        if (completion.delivered)
        {
            _deliveredCount++;
        }
        else
        {
            _undeliveredCount++;
            log("OUTBOX: kind %u ticket %u not delivered after %u attempts; total undelivered %u",
                (unsigned)job.kind, (unsigned)job.ticket, (unsigned)completion.attempts,
                (unsigned)_undeliveredCount);
        }

        delete job.update;
        delete job.diagnostic;
        job.update = nullptr;
        job.diagnostic = nullptr;

        if (_completions != nullptr && !_completions->push(completion))
        {
            // A producer waiting on this ticket will never hear about it. DeviceService would keep
            // a device's rule context for one more update (safe); SyncRulesUseCase would keep
            // _requestTicket set and stop asking for rules (not safe). The queue is sized so this
            // cannot happen - see globals.h - and the counter is here so that "cannot happen"
            // stays checkable rather than assumed.
            _lostCompletionCount++;
            log("OUTBOX: completion queue full, lost the outcome of kind %u ticket %u; total lost %u",
                (unsigned)job.kind, (unsigned)job.ticket, (unsigned)_lostCompletionCount);
        }
    }

    bool attemptOnce(const Job& job, OutboxCompletion& completion)
    {
        // The two jobs that are not requests: they talk to the radio, not to the server, and their
        // outcome is whether the module is associated - and, for the move, with which access point
        // - when they return.
        if (job.kind == OutboxKind::WIFI_RECONNECT)
        {
            if (job.forALostLink && _wifi != nullptr && _wifi->isConnected())
            {
                // Asked for while the link was down and reached after it came back by itself - behind
                // a request's attempt, or a network task the heap refused for a while. Restarting the
                // radio now would take a working link down for up to a minute.
                log("OUTBOX: radio restart ticket %u not run - the link it was asked for is back",
                    (unsigned)job.ticket);
                // No attempt was made, and the completion says so: it is delivered - the station is
                // associated - but it is not a restart that took place.
                completion.attempts = 0;
                return true;
            }
            return (_wifi != nullptr) && _wifi->reconnect();
        }

        if (job.kind == OutboxKind::WIFI_ROAM)
        {
            return (_roamRadio != nullptr) && _roamRadio->roamTo(job.bssid, job.channel);
        }

        // The probe always runs to its end; what it found is the outcome, and travels back as such.
        if (job.kind == OutboxKind::LINK_PROBE)
        {
            LinkProbeResult found;
            found.gateway = LinkReach::UNAVAILABLE;
            if (_linkProbe != nullptr)
            {
                found = runLinkProbe(*_linkProbe, _linkProbePlan);
            }
            completion.linkProbe = found.encode();
            return true;
        }

        if (_api == nullptr)
        {
            return false;
        }

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
        switch (job.kind)
        {
        case OutboxKind::SENSOR_UPDATE:
        case OutboxKind::DEVICE_UPDATE:
            return (job.update != nullptr) && _api->sendModuleUpdateData(*job.update);

        case OutboxKind::DIAGNOSTIC:
            return (job.diagnostic != nullptr) && _api->sendDiagnosticData(*job.diagnostic);

        case OutboxKind::RULE_LIST:
            return fetchRuleList(completion);

        case OutboxKind::RULE_STATE_BULK:
            return _api->sendRuleStateCommand(job.json.c_str());

        case OutboxKind::RULE_STATE_BY_UID:
            return _api->sendByUidRuleStateCommand(job.uid.c_str(), job.json.c_str());

        case OutboxKind::RULE_SENSOR_OFFLINE:
            return _api->sendRuleSensorOfflineEvent(job.json.c_str());

        case OutboxKind::DEVICE_COMMAND_FETCH:
            return fetchDeviceCommands(completion);

        case OutboxKind::MODULE_COMMAND_FETCH:
            return fetchModuleCommands(completion);

        case OutboxKind::SENSOR_TYPES_FETCH:
            return fetchSensorTypes(completion);

        // The five reports. Each hands the DTO straight to the corresponding API method, so the
        // producer decides what the backend sees and this task only decides when.
        case OutboxKind::COMMAND_RESULT:
            return (job.report != nullptr) &&
                   _api->sendCommandResult(job.report->commandResult, job.report->priority);

        case OutboxKind::DEVICE_COMMAND_RESULT:
            return (job.report != nullptr) &&
                   _api->sendDeviceCommandExecutionResult(job.report->deviceCommandUpdate);

        case OutboxKind::MODULE_COMMAND_RESULT:
            return (job.report != nullptr) &&
                   _api->sendModuleCommandExecutionResult(job.report->moduleCommandUpdates);

        case OutboxKind::RAW_IO_RESULT:
            return (job.report != nullptr) && _api->sendRawIoResult(job.report->rawIoResult);

        case OutboxKind::MODULE_STRUCTURE:
            return (job.report != nullptr) &&
                   _api->sendToServerModuleStructure(job.report->moduleStructure,
                                                     job.report->priority);

        // Handled above the switch; listed so that a kind left out is a compile error here too.
        case OutboxKind::WIFI_RECONNECT:
        case OutboxKind::WIFI_ROAM:
        case OutboxKind::LINK_PROBE:
            break;
        }
#pragma GCC diagnostic pop

        return false;
    }

    bool fetchRuleList(OutboxCompletion& completion)
    {
        // httpFailed is what separates "the server said there are no rules" from "the server said
        // nothing, or said something this module could not read". Without it an empty list would
        // either be retried four times on every module that legitimately has no rules, or never
        // retried at all when the link was down - and, worse, a failed fetch would reach
        // SyncRulesUseCase as an answer, and be recorded as a synchronization that succeeded
        // against a response the module never understood. (Measured: an empty snapshot does not
        // delete rules - handleEmptyServerResponse() keeps them.)
        //
        // That flag is the whole verdict. The list is never shortened here: a snapshot is
        // authoritative, so a short one *does* delete rules, and cutting it saves nothing because
        // RestAPIService::getRuleList() has already built the whole vector out of the whole
        // response body. Bounding that allocation needs a streaming parser, not a cap here.
        bool httpFailed = false;
        bool readWhole = false;
        std::vector<Rule> rules = _api->getRuleList(&httpFailed, &readWhole);

        if (httpFailed)
        {
            return false;
        }

        const size_t received = rules.size();

        {
            std::lock_guard<std::mutex> guard(_mutex);
            _ruleListResponse.swap(rules);
            _ruleListResponseReady = true;
        }

        completion.responseItemCount =
            static_cast<uint16_t>((received > 0xFFFFu) ? 0xFFFFu : received);
        // Carried to the consumer because it decides whether the answer may be read as the
        // complete set of the account's rules - see OutboxCompletion::responseReadWhole.
        completion.responseReadWhole = readWhole;
        return true;
    }

    /**
     * @brief GET the pending device commands and leave them in the response slot.
     *
     * `httpFailed` carries the same weight it does for the rule list, and for a sharper reason:
     * CommandService executes the command out of the MQTT payload when the fetch failed, and does
     * nothing at all when the server answered that there is nothing pending. Collapsing those two
     * into "empty list" would either execute a stale payload every time a module has no commands,
     * or lose the fallback entirely.
     */
    bool fetchDeviceCommands(OutboxCompletion& completion)
    {
        bool httpFailed = false;
        std::vector<DeviceCommandDto> commands = _api->getDeviceCommand(&httpFailed);

        if (httpFailed)
        {
            return false;
        }

        const size_t received = commands.size();

        {
            std::lock_guard<std::mutex> guard(_mutex);
            _deviceCommandResponse.swap(commands);
            _deviceCommandResponseReady = true;
        }

        completion.responseItemCount =
            static_cast<uint16_t>((received > 0xFFFFu) ? 0xFFFFu : received);
        return true;
    }

    bool fetchModuleCommands(OutboxCompletion& completion)
    {
        bool httpFailed = false;
        std::vector<ModuleCommandDto> commands = _api->getModuleCommand(&httpFailed);

        if (httpFailed)
        {
            return false;
        }

        const size_t received = commands.size();

        {
            std::lock_guard<std::mutex> guard(_mutex);
            _moduleCommandResponse.swap(commands);
            _moduleCommandResponseReady = true;
        }

        completion.responseItemCount =
            static_cast<uint16_t>((received > 0xFFFFu) ? 0xFFFFu : received);
        return true;
    }

    /**
     * @brief GET the sensor type catalog and leave it in the response slot.
     *
     * `httpFailed` again, and again because the two outcomes it separates are acted on
     * differently. SyncSensorTypesUseCase answers an empty catalog from a server that replied by
     * keeping whatever it has in local storage and calling the sync a success
     * (handleEmptyServerResponse()); a fetch that never reached the server is not a statement about
     * the catalog at all, and reporting it as an empty one would let a network outage be recorded
     * as a successful synchronization against no types.
     */
    bool fetchSensorTypes(OutboxCompletion& completion)
    {
        bool httpFailed = false;
        std::vector<SensorType> sensorTypes = _api->getSensorTypesList(&httpFailed);

        if (httpFailed)
        {
            return false;
        }

        const size_t received = sensorTypes.size();

        {
            std::lock_guard<std::mutex> guard(_mutex);
            _sensorTypesResponse.swap(sensorTypes);
            _sensorTypesResponseReady = true;
        }

        completion.responseItemCount =
            static_cast<uint16_t>((received > 0xFFFFu) ? 0xFFFFu : received);
        return true;
    }

    uint32_t backoffMs(uint8_t attempt) const
    {
        uint32_t wait = _backoffBaseMs;
        for (uint8_t i = 1; i < attempt; ++i)
        {
            wait *= BACKOFF_FACTOR;
        }
        return wait;
    }

    void log(const char* format, ...)
    {
        if (_logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::NET_DEBUG))
        {
            return;
        }

        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        _logger->println(DebugFeatures::NET_DEBUG, buffer);
    }

    static const uint8_t kSourceEvents = 0;
    static const uint8_t kSourceSensor = 1;
    static const uint8_t kSourceDevice = 2;
    static const uint8_t kSourceDiagnostic = 3;
    static const uint8_t kSourceRuleList = 4;
    static const uint8_t kSourceDeviceCommand = 5;
    static const uint8_t kSourceModuleCommand = 6;
    static const uint8_t kSourceSensorTypes = 7;
    static const uint8_t kSourceCount = 8;

    IHybridAPIService* _api;
    ITaskScheduler* _scheduler;
    IMessageQueue<OutboxCompletion>* _completions;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IWifiAdapter* _wifi = nullptr;
    IWifiRoamRadio* _roamRadio = nullptr;
    IMqttLinkMachine* _mqttLink = nullptr;
    ILinkProbe* _linkProbe = nullptr;
    LinkProbePlan _linkProbePlan;
    ModuleStatusBoard* _statusBoard = nullptr;
    // Written and read on the network task only, outside _mutex. The slots below, the restart
    // flag and its ticket among them, are written by their producers and taken by the network
    // task under _mutex.
    bool _mqttActInProgress = false;

    BackgroundActBoard* _acts = nullptr;
    // Whether the word on the board is the connect act's. Network task only, like the flag above.
    bool _mqttActOnTheBoard = false;

    uint32_t _stackBytes;
    uint8_t _priority;
    uint8_t _maxAttempts;
    uint32_t _backoffBaseMs;

    mutable std::mutex _mutex;
    std::mutex _ticketMutex;

    UpdateDto* _sensorSlot = nullptr;
    UpdateDto* _deviceSlot = nullptr;
    DiagnosticDto* _diagnosticSlot = nullptr;
    bool _ruleListPending = false;
    bool _deviceCommandPending = false;
    bool _moduleCommandPending = false;
    bool _sensorTypesPending = false;
    bool _wifiReconnectPending = false;
    // The restart waiting was asked for a lost link only: see submitWifiReconnectForALostLink().
    bool _wifiReconnectForALostLink = false;
    bool _wifiRoamPending = false;
    uint8_t _wifiRoamBssid[6] = {0, 0, 0, 0, 0, 0};
    uint8_t _wifiRoamChannel = 0;
    bool _linkProbePending = false;

    uint32_t _sensorTicket = 0;
    uint32_t _deviceTicket = 0;
    uint32_t _diagnosticTicket = 0;
    uint32_t _ruleListTicket = 0;
    uint32_t _deviceCommandTicket = 0;
    uint32_t _moduleCommandTicket = 0;
    uint32_t _sensorTypesTicket = 0;
    uint32_t _wifiReconnectTicket = 0;
    uint32_t _wifiRoamTicket = 0;
    uint32_t _linkProbeTicket = 0;

    std::vector<Event> _events;
    size_t _eventHead = 0;
    size_t _eventCount = 0;

    std::vector<Rule> _ruleListResponse;
    bool _ruleListResponseReady = false;

    std::vector<DeviceCommandDto> _deviceCommandResponse;
    bool _deviceCommandResponseReady = false;

    std::vector<ModuleCommandDto> _moduleCommandResponse;
    bool _moduleCommandResponseReady = false;

    std::vector<SensorType> _sensorTypesResponse;
    bool _sensorTypesResponseReady = false;

    uint8_t _cursor = 0;
    uint32_t _lastTicket = 0;

    bool _taskStarted = false;
    uint32_t _taskStartFailureCount = 0;
    uint32_t _collapsedCount = 0;
    uint32_t _refusedEventCount = 0;
    uint32_t _undeliveredCount = 0;
    uint32_t _deliveredCount = 0;
    uint32_t _lostCompletionCount = 0;
};
