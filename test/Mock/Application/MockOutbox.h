#pragma once

#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Data/Entities/WifiNode.h"

#include <cstring>
#include <string>
#include <vector>

/**
 * @brief An outbox that records what was submitted and hands back what the test tells it to.
 *
 * A stand-in for the *queue*, not for what the queue does. It carries no retry policy, no backoff
 * and no rule about what an empty response means - those live in Outbox and are tested against the
 * real class in test_outbox.cpp and, end to end with a real network task, in
 * test_s8_loop_with_command.cpp. What this mock exists for is the other side of the contract: what
 * a producer submits, and what it does when a completion arrives.
 */
class MockOutbox : public IOutbox {
public:
    // --- what was submitted -------------------------------------------------------------------

    uint32_t deviceCommandFetches = 0;
    uint32_t moduleCommandFetches = 0;
    uint32_t ruleListRequests = 0;
    uint32_t sensorTypesRequests = 0;
    uint32_t wifiReconnectRequests = 0;
    uint32_t ensureNetworkTaskCalls = 0;
    // Of wifiReconnectRequests, the ones asked for a lost link.
    uint32_t wifiReconnectForALostLinkRequests = 0;
    uint32_t linkProbeRequests = 0;
    /** Every access point a move was asked for, in the order the moves were submitted. */
    std::vector<WifiNode> wifiRoamTargets;

    std::vector<CommandResultDto> commandResults;
    std::vector<ApiPriority> commandResultPriorities;
    std::vector<DeviceCommandUpdateDto> deviceCommandResults;
    std::vector<std::vector<ModuleCommandUpdateDto>> moduleCommandResults;
    std::vector<RawIoResultDto> rawIoResults;
    uint32_t moduleStructureSends = 0;
    ApiPriority lastModuleStructurePriority = ApiPriority::UNDEFINED;
    std::vector<std::string> ruleStateUids;
    std::vector<std::string> ruleStateBodies;

    /** Make the next `n` submissions of any kind come back as a refusal, as a full ring does. */
    uint32_t refusalsLeft = 0;

    // --- what the test hands back --------------------------------------------------------------

    std::vector<DeviceCommandDto> deviceCommandResponse;
    bool deviceCommandResponseReady = false;
    std::vector<ModuleCommandDto> moduleCommandResponse;
    bool moduleCommandResponseReady = false;
    std::vector<Rule> ruleListResponse;
    bool ruleListResponseReady = false;
    std::vector<SensorType> sensorTypesResponse;
    bool sensorTypesResponseReady = false;

    std::vector<OutboxCompletion> completions;

    /** @brief Queue a completion for the control loop to pick up. */
    void pushCompletion(OutboxKind kind, uint32_t ticket, bool delivered)
    {
        OutboxCompletion completion;
        completion.kind = kind;
        completion.ticket = ticket;
        completion.delivered = delivered;
        completion.attempts = 1;
        completions.push_back(completion);
    }

    // --- IOutbox --------------------------------------------------------------------------------

    uint32_t submitSensorUpdate(const UpdateDto&) override { return ticket(); }
    uint32_t submitDeviceUpdate(const UpdateDto&) override { return ticket(); }
    uint32_t submitDiagnostic(const DiagnosticDto&) override { return ticket(); }

    uint32_t submitRuleListRequest() override
    {
        ruleListRequests++;
        return ticket();
    }

    uint32_t submitRuleStateBulk(const std::string&) override { return ticket(); }

    uint32_t submitRuleStateByUid(const std::string& ruleUid, const std::string& json) override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            ruleStateUids.push_back(ruleUid);
            ruleStateBodies.push_back(json);
        }
        return issued;
    }

    uint32_t submitRuleSensorOffline(const std::string&) override { return ticket(); }

    uint32_t submitDeviceCommandFetch() override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            deviceCommandFetches++;
        }
        return issued;
    }

    uint32_t submitModuleCommandFetch() override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            moduleCommandFetches++;
        }
        return issued;
    }

    uint32_t submitSensorTypesRequest() override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            sensorTypesRequests++;
        }
        return issued;
    }

    uint32_t submitWifiReconnect() override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            wifiReconnectRequests++;
        }
        return issued;
    }

    uint32_t submitWifiReconnectForALostLink() override
    {
        const uint32_t issued = submitWifiReconnect();
        if (issued != kRejected)
        {
            wifiReconnectForALostLinkRequests++;
        }
        return issued;
    }

    bool ensureNetworkTask() override
    {
        ensureNetworkTaskCalls++;
        return true;
    }

    uint32_t submitLinkProbe() override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            linkProbeRequests++;
        }
        return issued;
    }

    uint32_t submitWifiRoam(const uint8_t* bssid, uint8_t channel) override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected && bssid != nullptr)
        {
            WifiNode target;
            memcpy(target.bssid, bssid, sizeof(target.bssid));
            target.channel = channel;
            target.rssi = 0;
            wifiRoamTargets.push_back(target);
        }
        return issued;
    }

    uint32_t submitCommandResult(const CommandResultDto& result, ApiPriority priority) override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            commandResults.push_back(result);
            commandResultPriorities.push_back(priority);
        }
        return issued;
    }

    uint32_t submitDeviceCommandResult(const DeviceCommandUpdateDto& update) override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            deviceCommandResults.push_back(update);
        }
        return issued;
    }

    uint32_t submitModuleCommandResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            moduleCommandResults.push_back(updates);
        }
        return issued;
    }

    uint32_t submitRawIoResult(const RawIoResultDto& result) override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            rawIoResults.push_back(result);
        }
        return issued;
    }

    uint32_t submitModuleStructure(const ModuleStructure&, ApiPriority priority) override
    {
        const uint32_t issued = ticket();
        if (issued != kRejected)
        {
            moduleStructureSends++;
            lastModuleStructurePriority = priority;
        }
        return issued;
    }

    bool pollCompletion(OutboxCompletion& out) override
    {
        if (completions.empty())
        {
            return false;
        }
        out = completions.front();
        completions.erase(completions.begin());
        return true;
    }

    bool takeRuleListResponse(std::vector<Rule>& out) override
    {
        if (!ruleListResponseReady)
        {
            return false;
        }
        out = ruleListResponse;
        ruleListResponse.clear();
        ruleListResponseReady = false;
        return true;
    }

    bool takeDeviceCommandResponse(std::vector<DeviceCommandDto>& out) override
    {
        if (!deviceCommandResponseReady)
        {
            return false;
        }
        out = deviceCommandResponse;
        deviceCommandResponse.clear();
        deviceCommandResponseReady = false;
        return true;
    }

    bool takeModuleCommandResponse(std::vector<ModuleCommandDto>& out) override
    {
        if (!moduleCommandResponseReady)
        {
            return false;
        }
        out = moduleCommandResponse;
        moduleCommandResponse.clear();
        moduleCommandResponseReady = false;
        return true;
    }

    bool takeSensorTypesResponse(std::vector<SensorType>& out) override
    {
        if (!sensorTypesResponseReady)
        {
            return false;
        }
        out = sensorTypesResponse;
        sensorTypesResponse.clear();
        sensorTypesResponseReady = false;
        return true;
    }

    uint32_t collapsedCount() const override { return 0; }
    uint32_t refusedEventCount() const override { return _refusedCount; }
    uint32_t undeliveredCount() const override { return 0; }
    uint32_t lostCompletionCount() const override { return 0; }

    /** @brief The last ticket handed out, for a producer that has to recognise its own answer. */
    uint32_t lastIssuedTicket() const { return _lastTicket; }

private:
    uint32_t ticket()
    {
        if (refusalsLeft > 0)
        {
            refusalsLeft--;
            _refusedCount++;
            return kRejected;
        }
        return ++_lastTicket;
    }

    uint32_t _lastTicket = 0;
    uint32_t _refusedCount = 0;
};
