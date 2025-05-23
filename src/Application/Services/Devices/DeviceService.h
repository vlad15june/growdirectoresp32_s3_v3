#pragma once

#include <vector>
#include <memory>
#include <ctime>
#include <cstdarg>
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
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class DeviceService : public IDeviceService {
private:
    IModuleConfig* moduleConfig;
    ModuleStructure* moduleStructure;
    IDeviceModel* deviceModel;
    INTPTimeAdapter* timeAdapter;
    IArduinoAdapter* arduinoAdapter;
    IProcessDeviceStateUseCaseFactory* deviceStateUseCaseFactory;
    IProcessDeviceStateUseCase* processDeviceStateUseCase;
    IHybridAPIService* hybridApiService;
    ILogger* logger;
    IDebugSystem* debugSystem;
    
    const unsigned long DEVICE_UPDATE_INTERVAL = 30000;
    unsigned long lastDeviceUpdateTime = 0;
    
    UpdateDto createDeviceUpdateDto();
    
    void log(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
    
            logger->println(DebugFeatures::CONTROL_DEBUG, buffer);
        }
    }
    
public:
    DeviceService(IModuleConfig* moduleConfig, 
                  ModuleStructure* moduleStructure,
                  IDeviceModel* deviceModel,
                  INTPTimeAdapter* timeAdapter,
                  IArduinoAdapter* arduinoAdapter,
                  IProcessDeviceStateUseCaseFactory* deviceStateUseCaseFactory,
                  IHybridAPIService* hybridApiService = nullptr,
                  ILogger* logger = nullptr,
                  IDebugSystem* debugSystem = nullptr);

    ~DeviceService();
    
    bool init() override;
    void loop() override;
};
