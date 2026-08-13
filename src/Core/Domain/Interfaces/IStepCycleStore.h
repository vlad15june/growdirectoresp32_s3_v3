#ifndef I_STEP_CYCLE_STORE_H
#define I_STEP_CYCLE_STORE_H

#include "Core/Domain/Data/Entities/StepCycleCounter.h"

/**
 * @brief Storage of the STEP rule cycle counters that has to survive a reboot (GDV1-3811).
 *
 * Deliberately NOT part of ISettings: settings live in the EEPROM, and the EEPROM has a finite
 * number of write cycles while this counter is written on every completed cycle of every running
 * step rule (owner, review of PR #20, 2026-08-13). The implementation keeps the records in memory
 * that outlives a restart without being flash - today the memory of the built-in clock, see
 * StepCycleStore and the table declared in globals.h.
 *
 * One record per device: the record carries the identity of what it counts, so a stale record is
 * recognised and dropped instead of being restored onto a different rule or limit.
 */
class IStepCycleStore
{
public:
    virtual bool getStepCycleCounter(const char *deviceUid, StepCycleCounter &counter) = 0;
    virtual void setStepCycleCounter(const StepCycleCounter &counter) = 0;
    virtual void clearStepCycleCounter(const char *deviceUid) = 0;
    virtual ~IStepCycleStore() = default;
};

#endif // I_STEP_CYCLE_STORE_H
