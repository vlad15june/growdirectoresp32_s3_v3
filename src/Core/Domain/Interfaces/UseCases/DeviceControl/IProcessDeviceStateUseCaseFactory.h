#pragma once

#include "IProcessDeviceStateUseCase.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"

class IProcessDeviceStateUseCaseFactory {
public:
    virtual ~IProcessDeviceStateUseCaseFactory() = default;
    virtual IProcessDeviceStateUseCase* create(int moduleType) = 0;
};
