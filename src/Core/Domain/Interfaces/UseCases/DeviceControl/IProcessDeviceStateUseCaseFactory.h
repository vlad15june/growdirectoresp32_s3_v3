#pragma once

#include "IProcessDeviceStateUseCase.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"

class IProcessDeviceStateUseCaseFactory {
public:
    virtual ~IProcessDeviceStateUseCaseFactory() = default;
    virtual IProcessDeviceStateUseCase* create(DeviceTypeEnum type) = 0;
};
