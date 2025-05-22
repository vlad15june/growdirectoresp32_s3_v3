#pragma once

#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCase.h"

class ProcessAnalogDeviceStateUseCase : public IProcessDeviceStateUseCase {
public:
    ProcessAnalogDeviceStateUseCase() = default;
    ~ProcessAnalogDeviceStateUseCase() override = default;

    void loop() override;
};
