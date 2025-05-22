#include "DeviceService.h"
#include <cstring>
#include <algorithm>
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCaseFactory.h"

DeviceService::DeviceService(IModuleConfig* moduleConfig, 
                            ModuleStructure* moduleStructure,
                            IDeviceModel* deviceModel,
                            INTPTimeAdapter* timeAdapter,
                            IArduinoAdapter* arduinoAdapter,
                            IProcessDeviceStateUseCaseFactory* deviceStateUseCaseFactory) 
    : moduleConfig(moduleConfig), 
      moduleStructure(moduleStructure),
      deviceModel(deviceModel),
      timeAdapter(timeAdapter),
      arduinoAdapter(arduinoAdapter),
      deviceStateUseCaseFactory(deviceStateUseCaseFactory),
      processDeviceStateUseCase(nullptr) {
}

DeviceService::~DeviceService() {
    if (processDeviceStateUseCase) {
        delete processDeviceStateUseCase;
        processDeviceStateUseCase = nullptr;
    }
}

bool DeviceService::init() {
    bool result = deviceModel->init();
    if (!result) {
        return false;
    }
    
    if (processDeviceStateUseCase) {
        delete processDeviceStateUseCase;
        processDeviceStateUseCase = nullptr;
    }
    
    int moduleType = moduleConfig->getModuleType();
    processDeviceStateUseCase = deviceStateUseCaseFactory->create(moduleType);
    
    return processDeviceStateUseCase != nullptr;
}

void DeviceService::loop() {
    if (processDeviceStateUseCase) {
        processDeviceStateUseCase->loop();
    }
}