#pragma once

#include <vector>
#include "Core/Domain/Data/Entities/Device.h"
#include "Application/DTOs/UpdateDto.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"

class IDeviceService {
public:
    virtual ~IDeviceService() = default;
    
    virtual bool init() = 0;
    virtual void loop() = 0;

    //call back for mqtt
};
