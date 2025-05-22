#pragma once

#include <vector>
#include "Core/Domain/Data/Entities/Device.h"
#include "Application/DTOs/UpdateDto.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"

/**
 * @brief Interface for device management services
 */
class IDeviceService {
public:
    virtual ~IDeviceService() = default;
    
    // Initialization
    virtual bool init() = 0;

    //loop
    
    //call back for mqtt
};
