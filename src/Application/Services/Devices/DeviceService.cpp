#include "DeviceService.h"
#include <cstring>
#include <algorithm>

DeviceService::DeviceService(IModuleConfig* moduleConfig, 
                            ModuleStructure* moduleStructure,
                            IDeviceModel* deviceModel,
                            INTPTimeAdapter* timeAdapter,
                            IArduinoAdapter* arduinoAdapter) 
    : moduleConfig(moduleConfig), 
      moduleStructure(moduleStructure),
      deviceModel(deviceModel),
      timeAdapter(timeAdapter),
      arduinoAdapter(arduinoAdapter) {
}

DeviceService::~DeviceService() {
    // DeviceModel handles device cleanup
}

bool DeviceService::init() {
    return deviceModel->init();
}