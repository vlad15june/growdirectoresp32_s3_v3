#pragma once

#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCaseFactory.h"
#include "ProcessDeviceStateUseCase.h"
#include "ProcessAnalogDeviceStateUseCase.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"

class ProcessDeviceStateUseCaseFactory : public IProcessDeviceStateUseCaseFactory {
public:
    ProcessDeviceStateUseCaseFactory() = default;
    ~ProcessDeviceStateUseCaseFactory() override = default;
    IProcessDeviceStateUseCase* create(DeviceTypeEnum type) override {
        switch (type) {
            case DEVICE_TYPE_ANALOG:
                return new ProcessAnalogDeviceStateUseCase();
            case DEVICE_TYPE_DIGITAL:
            default:
                return new ProcessDeviceStateUseCase();
        }
    }
};
