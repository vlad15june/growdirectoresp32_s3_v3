#pragma once

#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCase.h"
#include "Core/Domain/Data/Entities/Device.h"
#include <vector>
#include <cstdint>

class ILogger;
class IDebugSystem;
class IModuleConfig;
class IArduinoAdapter;
class IDeviceModel;

class ProcessDeviceStateUseCase : public IProcessDeviceStateUseCase {
private:
    ILogger* logger;
    IDebugSystem* debugSystem;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    IDeviceModel* deviceModel;

    std::vector<int8_t> lastRelayLevel;

    // Methods to process different device states
    void processDelayOn(Device* device, int index);
    void processDelayOff(Device* device, int index);
    void processRequestOn(Device* device, int index, int moduleType);
    void processRequestStepOn(Device* device, int index, int moduleType);
    void processOn(Device* device, int index);
    void processStepOn(Device* device, int index);
    void processStepWaiting(Device* device, int index);
    void processOff(Device* device, int index);
    
    // Helper methods
    void processDeviceState(Device* device, int index);
    void controlDeviceState();
    void switchRelayOn(Device* device, int index);
    void switchRelayOff(Device* device, int index);
    void setRelayLevel(Device* device, int index, uint8_t level, const char* action);
    uint8_t resolvePin(int index);
    bool isChannelInRange(int index);
    int countDevicesInOffState();
    void logRelay(const char* message);
    
public:
    ProcessDeviceStateUseCase(
        ILogger* logger, 
        IDebugSystem* debugSystem, 
        IModuleConfig* moduleConfig,
        IArduinoAdapter* arduinoAdapter,
        IDeviceModel* deviceModel);
        
    ~ProcessDeviceStateUseCase() override = default;
    
    void loop() override;
};
