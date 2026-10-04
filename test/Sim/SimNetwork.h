#pragma once

#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"
#include "Sim/VirtualClock.h"

#include <cstdint>
#include <string>

namespace hostsim {

/**
 * @brief A server that answers after a chosen amount of virtual time.
 *
 * Deliberately the smallest thing S1 needs, and deliberately protocol-free: it takes an opaque
 * label and an opaque payload and returns after the configured latency. It models no HTTP, no
 * MQTT topic, no JSON - inventing any of those here would be inventing a contract with the
 * backend, which is not ours to define.
 *
 * The important part is how it waits. send() blocks the *calling task* through the scheduler,
 * exactly as a synchronous socket call blocks the task that made it on the device: other tasks
 * keep running, the caller does not. That is what makes the result meaningful - if the caller
 * is the control loop, the control loop stops.
 */
class SimNetwork {
public:
    struct Response {
        bool ok = false;
        uint32_t latencyMs = 0;
    };

    SimNetwork(VirtualClock& clock, ITaskScheduler& scheduler)
        : _clock(clock), _scheduler(scheduler)
    {
    }

    /** @brief How long the server takes to answer, in virtual milliseconds. */
    void setResponseLatencyMs(uint32_t ms) { _latencyMs = ms; }

    /** @brief When false, the call still costs its latency and then reports failure. */
    void setReachable(bool reachable) { _reachable = reachable; }

    Response send(const char* label, const char* payload)
    {
        _sendCount++;
        _lastLabel = (label != nullptr) ? label : "";
        _lastPayload = (payload != nullptr) ? payload : "";
        _lastSendStartedAtMs = _clock.nowMs();

        _scheduler.delayMs(_latencyMs);

        _lastSendFinishedAtMs = _clock.nowMs();
        _completedSendCount++;
        _lastCompletedLatencyMs = _lastSendFinishedAtMs - _lastSendStartedAtMs;

        Response response;
        response.ok = _reachable;
        response.latencyMs = _latencyMs;
        return response;
    }

    /** @brief Sends that were started, including one still in flight when observation ended. */
    uint32_t sendCount() const { return _sendCount; }

    /** @brief Sends that got their answer. */
    uint32_t completedSendCount() const { return _completedSendCount; }

    /** @brief Virtual milliseconds the last completed send took, start to answer. */
    uint64_t lastCompletedLatencyMs() const { return _lastCompletedLatencyMs; }

    uint64_t lastSendStartedAtMs() const { return _lastSendStartedAtMs; }
    uint64_t lastSendFinishedAtMs() const { return _lastSendFinishedAtMs; }
    const std::string& lastLabel() const { return _lastLabel; }
    const std::string& lastPayload() const { return _lastPayload; }

private:
    VirtualClock& _clock;
    ITaskScheduler& _scheduler;
    uint32_t _latencyMs = 0;
    bool _reachable = true;
    uint32_t _sendCount = 0;
    uint32_t _completedSendCount = 0;
    uint64_t _lastCompletedLatencyMs = 0;
    uint64_t _lastSendStartedAtMs = 0;
    uint64_t _lastSendFinishedAtMs = 0;
    std::string _lastLabel;
    std::string _lastPayload;
};

} // namespace hostsim
