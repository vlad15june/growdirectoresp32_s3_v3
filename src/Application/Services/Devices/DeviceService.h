#pragma once

#include <vector>
#include <memory>
#include <ctime>
#include "IDeviceService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Application/DTOs/UpdateDto.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

/**
 * @brief Implementation of the device service that manages devices
 */
class DeviceService : public IDeviceService {
private:
    IModuleConfig* moduleConfig;
    ModuleStructure* moduleStructure;
    IDeviceModel* deviceModel;
    
    // Time adapter for time operations
    INTPTimeAdapter* timeAdapter;
    
    // Arduino adapter for platform-specific operations
    IArduinoAdapter* arduinoAdapter;

public:
    /**
     * @brief Constructor for DeviceService
     * @param moduleConfig Configuration for the module
     * @param moduleStructure Structure of the module
     * @param deviceModel Model for device management
     * @param timeAdapter Adapter for time operations
     * @param arduinoAdapter Adapter for Arduino operations
     */
    DeviceService(IModuleConfig* moduleConfig, 
                  ModuleStructure* moduleStructure,
                  IDeviceModel* deviceModel,
                  INTPTimeAdapter* timeAdapter,
                  IArduinoAdapter* arduinoAdapter);
    
    /**
     * @brief Destructor for DeviceService
     */
    ~DeviceService();
    
    // Implementation of IDeviceService methods
    bool init() override;
};
