#pragma once

#include "Application/DTOs/CommandReportDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

/**
 * @brief The kinds of network work the outbox carries.
 *
 * The kind decides three things at once: which API call the network task makes, which slot or
 * ring the request waits in, and which producer recognises the completion as its own.
 */
enum class OutboxKind : uint8_t {
    /** Sensor readings, POST /update. State. */
    SENSOR_UPDATE = 0,
    /** Device states, POST /update. State. */
    DEVICE_UPDATE = 1,
    /** Diagnostic snapshot, POST /diagnostic (and the MQTT topic under ApiPriority::BOTH). State. */
    DIAGNOSTIC = 2,
    /** GET /rulelist. State: asking twice in a row for the same list has no value. */
    RULE_LIST = 3,
    /** POST /rules/state, the acknowledgement for a whole rule snapshot. Event. */
    RULE_STATE_BULK = 4,
    /** POST /rules/{uid}/state, one rule changing state. Event. */
    RULE_STATE_BY_UID = 5,
    /** MQTT rule_sensor_offline event. Event. */
    RULE_SENSOR_OFFLINE = 6,
    /**
     * GET /devices_command. State.
     *
     * The MQTT message that starts this is only a signal - the authoritative list is the one the
     * server answers with, and asking twice while one ask is in flight returns the same list.
     */
    DEVICE_COMMAND_FETCH = 7,
    /** GET /module_command, for the same reason and with the same shape. State. */
    MODULE_COMMAND_FETCH = 8,
    /** The generic command acknowledgement, sendCommandResult(). Event. */
    COMMAND_RESULT = 9,
    /** One device command's outcome, sendDeviceCommandExecutionResult(). Event. */
    DEVICE_COMMAND_RESULT = 10,
    /** Module command outcomes, sendModuleCommandExecutionResult(). Event. */
    MODULE_COMMAND_RESULT = 11,
    /** The bytes a raw bus read returned, sendRawIoResult(). Event. */
    RAW_IO_RESULT = 12,
    /**
     * The module structure, sendToServerModuleStructure(). Event.
     *
     * An event and not state, for the same reason the command reports are: a structure carrying a
     * new local IP is the only message that tells the backend where to reach this module, and a
     * second structure does not make the first one redundant - it describes a different address.
     * There is nothing to collapse.
     */
    MODULE_STRUCTURE = 13,
    /**
     * GET /sensor_models, the sensor type catalog. State: asking twice in a row for the same
     * catalog has no value, exactly as for the rule list.
     */
    SENSOR_TYPES_FETCH = 14,
    /**
     * Restart the radio and associate again, IWifiAdapter::reconnect(). State: two restarts
     * queued are one restart.
     *
     * Not a message to the server but a job for the same task, and for the same reason the
     * messages are: the restart holds its caller for up to a minute, and its caller would otherwise be the
     * control loop. It goes ahead of every request in the queue - a request made over a link that
     * is being rebuilt only spends its attempts - and the outcome travels back as `delivered`.
     */
    WIFI_RECONNECT = 15,
    /**
     * Move the station to a named access point of the network it is already on,
     * IWifiRoamRadio::roamTo(). State: a second target queued replaces the first, because the
     * second was chosen from a fresher look than the first.
     *
     * A job of this task for the same reason the restart is - it holds its caller for the seconds
     * an association takes - and served ahead of the requests for a different one: every request
     * made over the link about to be dropped is a request that has to be made again.
     */
    WIFI_ROAM = 16,
    /**
     * Probe the link: ask the gateway for its address, then echo a public address (LinkProbeRun.h).
     * State: two probes queued are one probe.
     *
     * A job of this task like the restart - it waits for answers for seconds - and served with the
     * link's acts, ahead of the requests: it is asked for while requests are failing, and its answer
     * decides whether the radio is restarted under them. Its outcome travels back in `linkProbe`.
     */
    LINK_PROBE = 17,
};

/**
 * @brief What the outbox is allowed to do with a request when there is no room for it.
 *
 * Declared by the producer at submission rather than derived from the kind, because it is the
 * producer that knows whether losing this particular message loses information.
 */
enum class OutboxPolicy : uint8_t {
    /**
     * State: at most one request of this kind is ever queued, and a new one replaces the one
     * waiting. Replacement, not eviction of the oldest - three queued sensor snapshots are not
     * three times the information, they are one stale picture and two staler ones.
     */
    COLLAPSE_BY_KIND = 0,
    /**
     * Event: never silently dropped. If the event ring is full the submission is refused on the
     * spot and the producer is told so by the return value, so it can keep its own state and
     * try again.
     */
    NEVER_DROP = 1,
};

/**
 * @brief What the network task reports back about one submitted request.
 *
 * Trivially copyable on purpose: this is what travels through IMessageQueue, and the FreeRTOS
 * queue underneath copies raw bytes.
 */
struct OutboxCompletion {
    /** The ticket returned by the submit call this completion answers. */
    uint32_t ticket = 0;
    OutboxKind kind = OutboxKind::SENSOR_UPDATE;
    /** True only if the server accepted it. "Handed over" is never reported as delivered. */
    bool delivered = false;
    /** How many attempts the network task spent on it. */
    uint8_t attempts = 0;
    /**
     * Requests that carry a response - RULE_LIST, DEVICE_COMMAND_FETCH, MODULE_COMMAND_FETCH,
     * SENSOR_TYPES_FETCH:
     * how many items the response slot holds. Observability, not a verdict - an empty list from a
     * server that answered is a legitimate answer, and `delivered` is what separates it from a
     * fetch that failed.
     */
    uint16_t responseItemCount = 0;
    /**
     * RULE_LIST only: true when every rule the answer carried is in the response slot.
     *
     * A rule can be left out of an answer that arrived perfectly well - one this module cannot
     * read, one larger than it accepts, one past the number of rules it keeps - and the consumer
     * has to know, because a rule snapshot is authoritative: the merge retires every stored rule
     * the snapshot does not name. A rule missing because the module could not take it is not a
     * rule the server deleted, and treating it as one switches a device off.
     */
    bool responseReadWhole = false;
    /** LINK_PROBE only: what the probe found, LinkProbeResult::encode(). */
    uint8_t linkProbe = 0;
};

static_assert(std::is_trivially_copyable<OutboxCompletion>::value,
              "OutboxCompletion travels through a FreeRTOS queue, which copies raw bytes");

/**
 * @brief The one place in the firmware that talks to the network, seen from the producer's side.
 *
 * Producers - the sensor service, the device service, diagnostics, rule sync - do not call the
 * REST or MQTT client. They submit a request here and carry on within the same control
 * loop iteration; a single network task drains the outbox and makes the call. That is what makes
 * the worst control-loop iteration independent of how slow the server is.
 *
 * Two things make this useful rather than merely asynchronous:
 *
 *  - Every submit returns a ticket, and every finished request comes back as an OutboxCompletion
 *    that the control loop drains at the top of its iteration. A producer that must know whether
 *    the message actually arrived - DeviceService has to, because it erases a device's reason for
 *    switching on once the server has been told - learns the outcome there, on its own task, with
 *    none of its state shared across cores.
 *  - A request may carry a response. For a send that is just the status; for GET /rulelist it is
 *    the parsed rules, left in a single response slot owned by the outbox and taken by the
 *    control loop when the completion says it is there.
 */
class IOutbox {
public:
    /** @brief The ticket value returned when a submission was refused. Real tickets start at 1. */
    static const uint32_t kRejected = 0;

    virtual ~IOutbox() = default;

    // --- Producers: submitting work -----------------------------------------------------------

    /** @brief Queue a sensor update. State: replaces any sensor update still waiting. */
    virtual uint32_t submitSensorUpdate(const UpdateDto& update) = 0;

    /** @brief Queue a device update. State: replaces any device update still waiting. */
    virtual uint32_t submitDeviceUpdate(const UpdateDto& update) = 0;

    /** @brief Queue a diagnostic snapshot. State: replaces any snapshot still waiting. */
    virtual uint32_t submitDiagnostic(const DiagnosticDto& diagnostic) = 0;

    /** @brief Ask for the rule list. State: a second ask while one is pending is folded into it. */
    virtual uint32_t submitRuleListRequest() = 0;

    /** @brief Acknowledge a whole rule snapshot. Event: refused rather than dropped. */
    virtual uint32_t submitRuleStateBulk(const std::string& json) = 0;

    /** @brief Report one rule changing state. Event: refused rather than dropped. */
    virtual uint32_t submitRuleStateByUid(const std::string& ruleUid, const std::string& json) = 0;

    /** @brief Report a rule's sensor going silent. Event: refused rather than dropped. */
    virtual uint32_t submitRuleSensorOffline(const std::string& json) = 0;

    /**
     * @brief Ask for the pending device commands. State: a second ask is folded into the one
     *        already waiting, because both would return the same list.
     */
    virtual uint32_t submitDeviceCommandFetch() = 0;

    /** @brief Ask for the pending module commands. State, for the same reason. */
    virtual uint32_t submitModuleCommandFetch() = 0;

    /**
     * @brief Ask for the sensor type catalog. State: a second ask while one is pending is folded
     *        into it, because both would return the same catalog.
     */
    virtual uint32_t submitSensorTypesRequest() = 0;

    /**
     * @brief Ask for the radio to be restarted and the link re-associated.
     *
     * State: a second ask while one waits is folded into it. Served before any request, and the
     * producer that asked learns the outcome - associated or not - from the completion, on its
     * own task, exactly as it learns about a send.
     */
    virtual uint32_t submitWifiReconnect() = 0;

    /**
     * @brief The same restart, asked for because the link was lost: not run if it is back.
     *
     * Same slot, folded the same way - and a restart asked for both ways is the unconditional one.
     * When the job comes to run and the station is associated again, the radio is left alone and
     * the outcome is "associated": restarting it then would only take a working link down.
     */
    virtual uint32_t submitWifiReconnectForALostLink() = 0;

    /**
     * @brief Ask for the station to be moved to this access point of the network it is on.
     *
     * State: a target submitted while one waits replaces it. Served before any request, and the
     * producer that asked learns from the completion whether the station is there.
     *
     * @param bssid   six bytes, copied here; the caller keeps ownership of what it points at
     * @param channel the channel that access point is on
     */
    virtual uint32_t submitWifiRoam(const uint8_t* bssid, uint8_t channel) = 0;

    /**
     * @brief Ask for a probe of the link: the gateway, then a public address.
     *
     * State: a second ask while one waits is folded into it. Served with the link's acts, before any
     * request; the producer learns what the probe found from the completion's `linkProbe`.
     */
    virtual uint32_t submitLinkProbe() = 0;

    /**
     * @brief Create the network task if it is not running and something waits for it.
     *
     * The task is created on the first submission, and a creation the heap refused is tried again on
     * the next one. This is the try for a module that has nothing more to submit: control loop only.
     *
     * @return true if the network task runs
     */
    virtual bool ensureNetworkTask() = 0;

    /**
     * @brief Report a command's outcome. Event: refused rather than dropped.
     *
     * These five are events and not state on purpose. A device that executed a command and never
     * told the server leaves the backend showing a command that is still `new` and a device whose
     * real state nobody knows; a superseded telemetry snapshot loses nothing comparable.
     */
    virtual uint32_t submitCommandResult(const CommandResultDto& result, ApiPriority priority) = 0;
    virtual uint32_t submitDeviceCommandResult(const DeviceCommandUpdateDto& update) = 0;
    virtual uint32_t submitModuleCommandResult(const std::vector<ModuleCommandUpdateDto>& updates) = 0;
    virtual uint32_t submitRawIoResult(const RawIoResultDto& result) = 0;
    virtual uint32_t submitModuleStructure(const ModuleStructure& structure, ApiPriority priority) = 0;

    // --- Control loop: collecting outcomes -----------------------------------------------------

    /**
     * @brief Take one finished request's outcome, if any. Never blocks.
     * @return false when there is nothing waiting
     */
    virtual bool pollCompletion(OutboxCompletion& out) = 0;

    /**
     * @brief Take the rules fetched by the last completed RULE_LIST request.
     *
     * Valid to call once, after a RULE_LIST completion with delivered == true. The slot is left
     * empty afterwards.
     *
     * The response is never shortened. A rule snapshot is authoritative - SyncRulesUseCase hands
     * its UIDs to IRuleRepository::synchronizeRules() as the complete set, and RuleRepository
     * marks every local rule missing from it DELETING - so a snapshot cut short would delete the
     * rules that fell off the end. There is also nothing to gain by cutting it: the whole list is
     * already built above this point, in RestAPIService::getRuleList(), out of a std::string that
     * HttpClientAdapter::getString() sized to the response. Any cap here would buy a new failure
     * mode for no memory saved.
     *
     * @return false when no response is waiting
     */
    virtual bool takeRuleListResponse(std::vector<Rule>& out) = 0;

    /**
     * @brief Take the device commands fetched by the last completed DEVICE_COMMAND_FETCH.
     *
     * Valid after a DEVICE_COMMAND_FETCH completion with delivered == true. An empty list with
     * delivered == true means the server answered "nothing pending"; delivered == false means it
     * did not answer, and that is the case the MQTT-payload fallback exists for.
     *
     * @return false when no response is waiting
     */
    virtual bool takeDeviceCommandResponse(std::vector<DeviceCommandDto>& out) = 0;

    /** @brief Take the module commands fetched by the last completed MODULE_COMMAND_FETCH. */
    virtual bool takeModuleCommandResponse(std::vector<ModuleCommandDto>& out) = 0;

    /**
     * @brief Take the sensor types fetched by the last completed SENSOR_TYPES_FETCH.
     *
     * Valid after a SENSOR_TYPES_FETCH completion with delivered == true. The same separation the
     * rule list needs, and for the same reason: an empty catalog from a server that answered means
     * "this account has no sensor models", while a fetch that failed means nothing at all and must
     * not be allowed to look like an empty catalog - SyncSensorTypesUseCase would then decide what
     * to do about types it never received.
     *
     * @return false when no response is waiting
     */
    virtual bool takeSensorTypesResponse(std::vector<SensorType>& out) = 0;

    // --- Observability -------------------------------------------------------------------------

    /** @brief State requests replaced by a fresher one of the same kind before they were sent. */
    virtual uint32_t collapsedCount() const = 0;

    /** @brief Event submissions refused because the event ring was full. */
    virtual uint32_t refusedEventCount() const = 0;

    /** @brief Requests the network task gave up on after exhausting its attempts. */
    virtual uint32_t undeliveredCount() const = 0;

    /** @brief Completions the completion queue had no room for. */
    virtual uint32_t lostCompletionCount() const = 0;
};
