#pragma once

#include <vector>
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"

/**
 * @brief Interface for device model management
 */
class IDeviceModel {
public:
    virtual ~IDeviceModel() = default;
    
    // Initialization
    virtual bool init() = 0;
    
    // Device access
    virtual Device* getDeviceByUid(const char* deviceUid) = 0;
    virtual std::vector<Device*> getAllDevices() = 0;
    virtual int getDeviceCount() const = 0;
    
    // Device operations
    virtual bool addDevice(Device* device) = 0;
    virtual bool removeDevice(const char* deviceUid) = 0;
    virtual void clearDevices() = 0;
};
