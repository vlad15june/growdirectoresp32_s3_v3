#ifndef STEP_CYCLE_STORE_H
#define STEP_CYCLE_STORE_H

#include "Core/Domain/Interfaces/IStepCycleStore.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdint.h>

/**
 * @brief One stored cycle counter. Fixed size and no std::string: the table is meant to live in
 *        memory that is not managed by the allocator (RTC memory of the clock).
 */
struct StepCycleSlot
{
    uint32_t marker;
    char deviceUid[24];
    char ruleUid[40];
    uint16_t cyclesToSetPoint;
    uint16_t currentCycle;
};

/**
 * @brief The whole table: one slot per digital channel of the module (deviceCount is 4 at most).
 *
 * WHERE the table lives is decided by whoever declares it, not by this class. In the firmware it
 * is declared in globals.h in the RTC memory of the built-in clock (`RTC_NOINIT_ATTR`), which
 * survives every restart the module makes by itself - the nightly preventive one, the technical
 * one after an offline period, an OTA - and, unlike the EEPROM, has no write endurance limit.
 * A power cut still clears it: the on-chip RTC memory is not battery backed. TO FINISH LATER: the
 * board also carries an EXTERNAL clock (RV-3028) with a battery, and its memory survives a power
 * cut - once that chip has a driver (`Rtc3028Adapter`, planned; today only the jig stubs
 * `#init_rtc3028` / `#read_rtc3028` in BoardTest.h), declare the table in its battery-backed
 * memory instead. Nothing else here or in the rules has to change.
 */
struct StepCycleTable
{
    static const int SLOTS = 4;
    StepCycleSlot slots[SLOTS];
};

/**
 * @brief Cycle counters of STEP rules across a reboot (GDV1-3811), stored in a fixed table.
 *
 * The counter itself lives on the device entity (DeviceDigitalOut::currentCycle) and dies with the
 * RAM; this store is what a restart does not take away, so the protective shutdown keeps counting
 * the configured number of cycles instead of getting a fresh budget after every restart.
 *
 * Not in the EEPROM by review decision (PR #20, 2026-08-13): a counter written once per completed
 * cycle would spend the finite write endurance of the EEPROM.
 */
class StepCycleStore : public IStepCycleStore
{
private:
    static const uint32_t SLOT_MARKER = 0x53544350; // "STCP" - the table is not zeroed at boot

    StepCycleTable &_table;
    ILogger *_logger;
    IDebugSystem *_debugSystem;

    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr &&
            _debugSystem->isFeatureEnabled(DebugFeatures::RULE_DEBUG))
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::RULE_DEBUG, buffer);
        }
    }

    // A slot is trusted only when it looks like something this code wrote: after a power cut the
    // RTC memory holds whatever was there, and the marker alone can match by accident.
    static bool isValidSlot(const StepCycleSlot &slot)
    {
        return slot.marker == SLOT_MARKER &&
               strnlen(slot.deviceUid, sizeof(slot.deviceUid)) > 0 &&
               strnlen(slot.deviceUid, sizeof(slot.deviceUid)) < sizeof(slot.deviceUid) &&
               strnlen(slot.ruleUid, sizeof(slot.ruleUid)) < sizeof(slot.ruleUid) &&
               slot.cyclesToSetPoint > 0 &&
               slot.currentCycle <= slot.cyclesToSetPoint;
    }

    int findSlot(const char *deviceUid)
    {
        if (deviceUid == nullptr || deviceUid[0] == 0)
        {
            return -1;
        }

        for (int slot = 0; slot < StepCycleTable::SLOTS; slot++)
        {
            if (isValidSlot(_table.slots[slot]) &&
                strncmp(_table.slots[slot].deviceUid, deviceUid, sizeof(_table.slots[slot].deviceUid)) == 0)
            {
                return slot;
            }
        }

        return -1;
    }

    int findFreeSlot()
    {
        for (int slot = 0; slot < StepCycleTable::SLOTS; slot++)
        {
            if (!isValidSlot(_table.slots[slot]))
            {
                return slot;
            }
        }

        return -1;
    }

    static void copyString(char *destination, size_t size, const std::string &value)
    {
        strncpy(destination, value.c_str(), size - 1);
        destination[size - 1] = 0;
    }

public:
    StepCycleStore(StepCycleTable &table, ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr)
        : _table(table), _logger(logger), _debugSystem(debugSystem)
    {
    }

    bool getStepCycleCounter(const char *deviceUid, StepCycleCounter &counter) override
    {
        int slot = findSlot(deviceUid);
        if (slot < 0)
        {
            return false;
        }

        const StepCycleSlot &stored = _table.slots[slot];
        counter.deviceUid = stored.deviceUid;
        counter.ruleUid = stored.ruleUid;
        counter.cyclesToSetPoint = stored.cyclesToSetPoint;
        counter.currentCycle = stored.currentCycle;
        return true;
    }

    void setStepCycleCounter(const StepCycleCounter &counter) override
    {
        if (counter.deviceUid.empty() || counter.cyclesToSetPoint <= 0)
        {
            return;
        }

        int slot = findSlot(counter.deviceUid.c_str());
        if (slot < 0)
        {
            slot = findFreeSlot();
        }
        if (slot < 0)
        {
            log("STEP_CYCLE_STORE: no free slot for deviceUid=%s", counter.deviceUid.c_str());
            return;
        }

        StepCycleSlot &target = _table.slots[slot];
        target.marker = SLOT_MARKER;
        copyString(target.deviceUid, sizeof(target.deviceUid), counter.deviceUid);
        copyString(target.ruleUid, sizeof(target.ruleUid), counter.ruleUid);
        target.cyclesToSetPoint = static_cast<uint16_t>(counter.cyclesToSetPoint);
        target.currentCycle = static_cast<uint16_t>(counter.currentCycle);
    }

    void clearStepCycleCounter(const char *deviceUid) override
    {
        int slot = findSlot(deviceUid);
        if (slot < 0)
        {
            return;
        }

        memset(&_table.slots[slot], 0, sizeof(StepCycleSlot));
    }

    // Called once at boot: a power cut leaves the RTC memory with whatever bytes were there, and
    // a slot that does not look like ours is not worth keeping around.
    void dropInvalidSlots()
    {
        for (int slot = 0; slot < StepCycleTable::SLOTS; slot++)
        {
            if (!isValidSlot(_table.slots[slot]))
            {
                memset(&_table.slots[slot], 0, sizeof(StepCycleSlot));
            }
        }
    }
};

#endif // STEP_CYCLE_STORE_H
