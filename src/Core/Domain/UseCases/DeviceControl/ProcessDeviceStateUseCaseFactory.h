#pragma once

#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCaseFactory.h"
#include "ProcessDeviceStateUseCase.h"
#include "ProcessAnalogDeviceStateUseCase.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"

class ProcessDeviceStateUseCaseFactory : public IProcessDeviceStateUseCaseFactory {
private:
    ILogger* logger;
    IDebugSystem* debugSystem;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    IDeviceModel* deviceModel;

public:
    ProcessDeviceStateUseCaseFactory(
        ILogger* logger, 
        IDebugSystem* debugSystem, 
        IModuleConfig* moduleConfig,
        IArduinoAdapter* arduinoAdapter,
        IDeviceModel* deviceModel)
        : logger(logger),
          debugSystem(debugSystem),
          moduleConfig(moduleConfig),
          arduinoAdapter(arduinoAdapter),
          deviceModel(deviceModel) {
    }
    
    ~ProcessDeviceStateUseCaseFactory() override = default;
    
    IProcessDeviceStateUseCase* create(int moduleType) override {
        switch (moduleType) {
            case MODULE_TYPE_ANALOG_INPUT_DEVICE:
                return new ProcessAnalogDeviceStateUseCase(
                    logger, debugSystem, moduleConfig, arduinoAdapter, deviceModel);
            case MODULE_TYPE_DEVICE:
            case MODULE_TYPE_HYDRO:
            case MODULE_TYPE_SENSOR:
            default:
                return new ProcessDeviceStateUseCase(
                    logger, debugSystem, moduleConfig, arduinoAdapter, deviceModel);
        }
    }
};
