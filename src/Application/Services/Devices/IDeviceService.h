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
    
    // Device management
    virtual Device* getDeviceByUid(const char* deviceUid) = 0;
    virtual std::vector<Device*> getAllDevices() = 0;
    virtual bool updateDeviceState(const char* deviceUid, DeviceStateEnum state) = 0;
    virtual bool setDeviceValue(const char* deviceUid, float value) = 0;
    
    // Status generation
    virtual UpdateDto createUpdateDto() = 0;
    
    // Device control
    virtual bool turnOnDevice(const char* deviceUid) = 0;
    virtual bool turnOffDevice(const char* deviceUid) = 0;
    virtual bool isDeviceOn(const char* deviceUid) = 0;
    
    // Service routine
    virtual void processDevices() = 0;
    virtual void updateDeviceSensorValues() = 0;
};
