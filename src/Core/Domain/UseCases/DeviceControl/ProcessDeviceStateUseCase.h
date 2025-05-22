#pragma once

#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCase.h"

class ProcessDeviceStateUseCase : public IProcessDeviceStateUseCase {
public:
    ProcessDeviceStateUseCase() = default;
    ~ProcessDeviceStateUseCase() override = default;
    void loop() override;
};
