#ifndef MOCK_CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H
#define MOCK_CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/ICheckFirmwareUpdateOnStartupUseCase.h"

class MockCheckFirmwareUpdateOnStartupUseCase : public ICheckFirmwareUpdateOnStartupUseCase {
public:
    bool runCalled = false;
    bool runReturnValue = false;

    void reset() {
        runCalled = false;
        runReturnValue = false;
    }

    bool run() override {
        runCalled = true;
        return runReturnValue;
    }
};

#endif // MOCK_CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H
