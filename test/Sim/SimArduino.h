#pragma once

#include <cstdint>

#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"
#include "Sim/VirtualClock.h"

#include <map>

namespace hostsim {

/**
 * @brief IArduinoAdapter on top of the virtual clock.
 *
 * This does not replace MockArduinoAdapter and is not meant to. That mock answers "was this
 * called"; this one answers "what happens if". They are different levels and both are needed:
 * the counting mocks stay where a call count is the whole question.
 *
 * millis() and micros() read the virtual clock. delay() does not sleep - when a scheduler is
 * attached it blocks the calling task like vTaskDelay() does, letting other tasks run; without
 * one it simply moves time forward.
 */
class SimArduino : public IArduinoAdapter {
public:
    SimArduino(VirtualClock& clock, ITaskScheduler* scheduler = nullptr)
        : _clock(clock), _scheduler(scheduler)
    {
    }

    // --- Constants ------------------------------------------------------------------------
    uint8_t getPinModeInput() const override { return 0; }
    uint8_t getPinModeOutput() const override { return 1; }
    uint8_t getPinModeInputPullup() const override { return 2; }
    uint8_t getDigitalLow() const override { return 0; }
    uint8_t getDigitalHigh() const override { return 1; }

    // --- Time -----------------------------------------------------------------------------
    void delay(unsigned long ms) override
    {
        delayCount++;
        lastDelayMs = ms;
        if (_scheduler != nullptr)
        {
            _scheduler->delayMs(static_cast<uint32_t>(ms));
        }
        else
        {
            _clock.advanceMs(ms);
        }
    }

    void delayMicroseconds(unsigned int us) override
    {
        delayMicrosecondsCount++;
        lastDelayUs = us;
        // Sub-millisecond waits are short enough to be modelled as pure time, not as a task
        // switch: on the device delayMicroseconds() busy-waits and does not yield either.
        _clock.advanceUs(us);
    }

    unsigned long millis() override { return static_cast<unsigned long>(_clock.nowMs()); }
    unsigned long micros() override { return static_cast<unsigned long>(_clock.nowUs()); }

    // --- Digital I/O ----------------------------------------------------------------------
    void pinMode(uint8_t pin, uint8_t mode) override { pinModes[pin] = mode; }

    void digitalWrite(uint8_t pin, uint8_t val) override
    {
        digitalOutputs[pin] = val;
        digitalWriteCounts[pin]++;
        lastWriteAtMs[pin] = _clock.nowMs();
    }

    int digitalRead(uint8_t pin) override
    {
        std::map<uint8_t, int>::const_iterator it = digitalInputs.find(pin);
        return (it != digitalInputs.end()) ? it->second : 0;
    }

    // --- Analog I/O -----------------------------------------------------------------------
    int analogRead(uint8_t pin) override
    {
        std::map<uint8_t, int>::const_iterator it = analogInputs.find(pin);
        return (it != analogInputs.end()) ? it->second : 0;
    }

    void analogWrite(uint8_t pin, int val) override { analogOutputs[pin] = val; }

    // --- LEDC PWM -------------------------------------------------------------------------
    void ledcSetupChannel(uint8_t channel, double freq, uint8_t resolution) override
    {
        ledcFreq[channel] = freq;
        ledcResolution[channel] = resolution;
    }
    void ledcAttach(uint8_t pin, uint8_t channel) override { ledcPinToChannel[pin] = channel; }
    void ledcDetach(uint8_t pin) override { ledcPinToChannel.erase(pin); }
    void ledcWriteChannel(uint8_t channel, uint32_t duty) override { ledcDuty[channel] = duty; }
    uint32_t ledcReadChannel(uint8_t channel) override
    {
        std::map<uint8_t, uint32_t>::const_iterator it = ledcDuty.find(channel);
        return (it != ledcDuty.end()) ? it->second : 0;
    }

    // --- Serial ---------------------------------------------------------------------------
    void serialBegin(unsigned long baud) override { serialBaud = baud; }
    size_t serialPrint(const char*) override { return 0; }
    size_t serialPrintln(const char*) override { return 0; }
    int serialAvailable() override { return 0; }
    int serialRead() override { return -1; }

    // --- System ---------------------------------------------------------------------------
    void restart() override
    {
        restartCount++;
        restartedAtMs = _clock.nowMs();
    }
    void hardReset() override { hardResetCount++; }
    int getResetReason() override { return resetReasonValue; }
    void feedTaskWatchdog() override
    {
        watchdogFeedCount++;
        lastWatchdogFeedMs = _clock.nowMs();
        // On the device this is esp_task_wdt_reset(): it feeds the watchdog entry of whichever
        // task made the call. The scheduler records exactly that - the feed lands on the
        // current task's name - so a watchdog model sees what the hardware would see.
        if (_scheduler != nullptr)
        {
            _scheduler->watchdogFeed();
        }
    }

    // --- Memory ---------------------------------------------------------------------------
    uint32_t getFreeHeap() override { return freeHeapValue; }
    uint32_t getMinFreeHeap() override { return minFreeHeapValue; }
    uint32_t getHeapSize() override { return heapSizeValue; }
    uint32_t getMaxAllocHeap() override { return maxAllocHeapValue; }
    uint32_t getPsramSize() override { return psramSizeValue; }
    uint32_t getFreePsram() override { return psramFreeValue; }
    uint32_t getMaxAllocPsram() override { return maxAllocPsramValue; }

    // --- Observable state -----------------------------------------------------------------
    std::map<uint8_t, uint8_t> pinModes;
    std::map<uint8_t, uint8_t> digitalOutputs;
    std::map<uint8_t, int> digitalWriteCounts;
    std::map<uint8_t, uint64_t> lastWriteAtMs;
    std::map<uint8_t, int> digitalInputs;
    std::map<uint8_t, int> analogInputs;
    std::map<uint8_t, int> analogOutputs;
    std::map<uint8_t, double> ledcFreq;
    std::map<uint8_t, uint8_t> ledcResolution;
    std::map<uint8_t, uint8_t> ledcPinToChannel;
    std::map<uint8_t, uint32_t> ledcDuty;

    int delayCount = 0;
    int delayMicrosecondsCount = 0;
    unsigned long lastDelayMs = 0;
    unsigned long lastDelayUs = 0;
    int restartCount = 0;
    uint64_t restartedAtMs = 0;
    int hardResetCount = 0;
    int resetReasonValue = 0;
    int watchdogFeedCount = 0;
    uint64_t lastWatchdogFeedMs = 0;
    unsigned long serialBaud = 0;

    uint32_t freeHeapValue = 200000;
    uint32_t minFreeHeapValue = 150000;
    uint32_t heapSizeValue = 320000;
    uint32_t maxAllocHeapValue = 100000;
    uint32_t psramSizeValue = 8388608;
    uint32_t psramFreeValue = 8000000;
    uint32_t maxAllocPsramValue = 4194304;

private:
    VirtualClock& _clock;
    ITaskScheduler* _scheduler;
};

} // namespace hostsim
