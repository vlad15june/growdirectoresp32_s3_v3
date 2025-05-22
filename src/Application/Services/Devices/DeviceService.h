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
#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCase.h"
#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCaseFactory.h"

/**
 * @brief Implementation of the device service that manages devices
 */
class DeviceService : public IDeviceService {
private:
    IModuleConfig* moduleConfig;
    ModuleStructure* moduleStructure;
    IDeviceModel* deviceModel;
    
    INTPTimeAdapter* timeAdapter;
    
    IArduinoAdapter* arduinoAdapter;
    
    IProcessDeviceStateUseCaseFactory* deviceStateUseCaseFactory;
    
    IProcessDeviceStateUseCase* processDeviceStateUseCase;

public:
    /**
     * @brief Constructor for DeviceService
     * @param moduleConfig Configuration for the module
     * @param moduleStructure Structure of the module
     * @param deviceModel Model for device management
     * @param timeAdapter Adapter for time operations
     * @param arduinoAdapter Adapter for Arduino operations
     * @param deviceStateUseCaseFactory Factory for creating device state processors
     */
    DeviceService(IModuleConfig* moduleConfig, 
                  ModuleStructure* moduleStructure,
                  IDeviceModel* deviceModel,
                  INTPTimeAdapter* timeAdapter,
                  IArduinoAdapter* arduinoAdapter,
                  IProcessDeviceStateUseCaseFactory* deviceStateUseCaseFactory);

    ~DeviceService();
    
    bool init() override;
    void loop() override;
};
