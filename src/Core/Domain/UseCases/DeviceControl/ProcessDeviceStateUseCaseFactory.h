#pragma once

#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCaseFactory.h"
#include "ProcessDeviceStateUseCase.h"
#include "ProcessAnalogDeviceStateUseCase.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"

class ProcessDeviceStateUseCaseFactory : public IProcessDeviceStateUseCaseFactory {
public:
    ProcessDeviceStateUseCaseFactory() = default;
    ~ProcessDeviceStateUseCaseFactory() override = default;
    IProcessDeviceStateUseCase* create(int moduleType) override {
        switch (moduleType) {
            case MODULE_TYPE_ANALOG_INPUT_DEVICE:
            case MODULE_TYPE_HYDRO:
                return new ProcessAnalogDeviceStateUseCase();
            case MODULE_TYPE_DEVICE:
            case MODULE_TYPE_SENSOR:
            default:
                return new ProcessDeviceStateUseCase();
        }
    }
};
