#pragma once

#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCase.h"
#include "Core/Domain/Data/Entities/Device.h"

class ILogger;
class IDebugSystem;
class IModuleConfig;
class IArduinoAdapter;
class IDeviceModel;

class ProcessAnalogDeviceStateUseCase : public IProcessDeviceStateUseCase {
private:
    ILogger* logger;
    IDebugSystem* debugSystem;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    IDeviceModel* deviceModel;
    
    // Constant for converting percentage to analog write value
    const float PERCENT_TO_ANALOG_WRITE_VALUE = 255.0 / 100.0;
    
    // State tracking variables
    bool needStoreDeviceHistory = false;
    bool needSendToServer = false;
    
    // Helper methods
    void setAnalogInputDeviceState(DeviceAnalogOut* device, int index);
    void controlAnalogInputDeviceState();
    void ledON(int index);
    void ledOFF(int index);
    const char* getLocalTime();
    
public:
    ProcessAnalogDeviceStateUseCase(
        ILogger* logger, 
        IDebugSystem* debugSystem, 
        IModuleConfig* moduleConfig,
        IArduinoAdapter* arduinoAdapter,
        IDeviceModel* deviceModel);
        
    ~ProcessAnalogDeviceStateUseCase() override = default;
    
    void loop() override;
};
