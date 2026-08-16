#pragma once

#include <string>

/**
 * @brief Persisted progress of a STEP rule cycle counter (GDV1-3811).
 *
 * The counter itself lives on the device entity (DeviceDigitalOut::currentCycle) and is lost on
 * every reboot. This record is what survives a restart: the counter plus the identity of what it
 * counts, so a stored value is only ever restored for the very same rule on the very same device
 * with the very same configured limit.
 */
struct StepCycleCounter {
    std::string deviceUid;
    std::string ruleUid;
    int cyclesToSetPoint = 0;
    int currentCycle = 0;
};
