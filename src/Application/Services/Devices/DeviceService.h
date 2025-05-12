#pragma once

#include <vector>
#include <memory>
#include <ctime>
#include "IDeviceService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
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
    std::vector<Device*> devices;
    
    // Time adapter for time operations
    INTPTimeAdapter* timeAdapter;
    
    // Arduino adapter for platform-specific operations
    IArduinoAdapter* arduinoAdapter;
    
    // Helper methods
    DeviceDto createDeviceDto(Device* device);
    void initDevices();

public:
    /**
     * @brief Constructor for DeviceService
     * @param moduleConfig Configuration for the module
     * @param moduleStructure Structure of the module
     * @param timeAdapter Adapter for time operations
     * @param arduinoAdapter Adapter for Arduino operations
     */
    DeviceService(IModuleConfig* moduleConfig, 
                  ModuleStructure* moduleStructure, 
                  INTPTimeAdapter* timeAdapter,
                  IArduinoAdapter* arduinoAdapter);
    
    /**
     * @brief Destructor for DeviceService
     */
    ~DeviceService();
    
    // Implementation of IDeviceService methods
    bool init() override;
    Device* getDeviceByUid(const char* deviceUid) override;
    std::vector<Device*> getAllDevices() override;
    bool updateDeviceState(const char* deviceUid, DeviceStateEnum state) override;
    bool setDeviceValue(const char* deviceUid, float value) override;
    UpdateDto createUpdateDto() override;
    bool turnOnDevice(const char* deviceUid) override;
    bool turnOffDevice(const char* deviceUid) override;
    bool isDeviceOn(const char* deviceUid) override;
    void processDevices() override;
    void updateDeviceSensorValues() override;
};
