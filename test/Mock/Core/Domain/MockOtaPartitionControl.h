#pragma once

#include "Core/Domain/Interfaces/Adapters/IOtaPartitionControl.h"

/**
 * @brief Scripted bootloader bookkeeping: answers the states the test sets, counts the calls.
 *
 * The proof rule for esp_ota_*: the host records that the confirmation call was made, it does
 * not imitate what the bootloader does with it.
 */
class MockOtaPartitionControl : public IOtaPartitionControl {
public:
    OtaImageState runningState = OtaImageState::VALID;
    OtaImageState otherState = OtaImageState::UNDEFINED;
    bool markValidResult = true;

    int markValidCalls = 0;
    int runningStateCalls = 0;
    int otherStateCalls = 0;

    const char* runningLabel = "app0";
    const char* otherLabel = "app1";

    OtaImageState runningImageState() override
    {
        runningStateCalls++;
        return runningState;
    }

    OtaImageState otherImageState() override
    {
        otherStateCalls++;
        return otherState;
    }

    bool markRunningImageValid() override
    {
        markValidCalls++;
        return markValidResult;
    }

    const char* runningPartitionLabel() override { return runningLabel; }

    const char* otherPartitionLabel() override { return otherLabel; }
};
