#pragma once

// A stand-in for the ESP-IDF task watchdog, on the virtual clock.
//
// What it substitutes is the hardware/SDK half: the periodic check that every subscribed task
// has fed within the threshold, and the panic when one has not. What it reads is real - the
// per-task subscription and feed timestamps HostTaskScheduler records when production code
// calls watchdogSubscribeCurrentTask()/watchdogFeed()/watchdogUnsubscribeCurrentTask(), and the
// threshold constant the device arms (constants.h). A test that holds the module inside a legal
// long operation asserts this model stays quiet; a test that freezes the control loop asserts it
// fires, and names when.
//
// The list it checks is the scheduler's list of entries, one per task, as esp_task_wdt keeps it:
// a task whose body returned without taking itself off is still on it, is never fed again, and
// fires the watchdog - however many later tasks of the same name feed their own entries.

#include "Sim/HostTaskScheduler.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hostsim {

class SimTaskWatchdog {
public:
    /** How often the model samples the feed gaps. The real watchdog is a hardware timer; a
     *  quarter second of virtual time keeps the firing moment sharp without costing steps. */
    static const uint32_t POLL_MS = 250;

    SimTaskWatchdog(HostTaskScheduler& scheduler, VirtualClock& clock, uint32_t timeoutMs)
        : _scheduler(scheduler), _clock(clock), _timeoutMs(timeoutMs)
    {
    }

    /** @brief Arm the model: it watches every task subscribed now or later. */
    void start()
    {
        _scheduler.createTask("sim-task-wdt", 64 * 1024, 1, [this]() {
            while (true)
            {
                check();
                _scheduler.delayMs(POLL_MS);
            }
        });
    }

    bool fired() const { return _fired; }
    const std::string& firedTaskName() const { return _firedTaskName; }
    uint64_t firedAtMs() const { return _firedAtMs; }
    /** @brief The starved task's feed gap at the moment of firing. */
    uint64_t firedGapMs() const { return _firedGapMs; }

    /** @brief The longest feed gap each subscribed task ever showed, fired or not. */
    uint64_t worstGapMs(const std::string& taskName) const
    {
        std::map<std::string, uint64_t>::const_iterator it = _worstGapMs.find(taskName);
        return (it == _worstGapMs.end()) ? 0 : it->second;
    }

    uint64_t worstGapMsOverall() const
    {
        uint64_t worst = 0;
        for (std::map<std::string, uint64_t>::const_iterator it = _worstGapMs.begin();
             it != _worstGapMs.end(); ++it)
        {
            if (it->second > worst)
            {
                worst = it->second;
            }
        }
        return worst;
    }

private:
    void check()
    {
        const uint64_t nowMs = _clock.nowMs();
        const std::vector<HostTaskScheduler::WatchdogEntry>& entries = _scheduler.watchdogEntries();

        for (size_t i = 0; i < entries.size(); i++)
        {
            const std::string& name = entries[i].taskName;
            const uint64_t lastMs = entries[i].lastFeedAtUs / 1000ULL;
            const uint64_t gapMs = nowMs - lastMs;

            if (gapMs > _worstGapMs[name])
            {
                _worstGapMs[name] = gapMs;
            }

            if (!_fired && gapMs > _timeoutMs)
            {
                // First expiry only: on the device this is the panic, and nothing runs after.
                _fired = true;
                _firedTaskName = name;
                _firedAtMs = nowMs;
                _firedGapMs = gapMs;
            }
        }
    }

    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
    uint32_t _timeoutMs;

    bool _fired = false;
    std::string _firedTaskName;
    uint64_t _firedAtMs = 0;
    uint64_t _firedGapMs = 0;
    std::map<std::string, uint64_t> _worstGapMs;
};

} // namespace hostsim
