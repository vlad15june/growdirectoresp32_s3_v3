#include "DeviceService.h"
#include <cstring>
#include <algorithm>

DeviceService::DeviceService(IModuleConfig* moduleConfig, 
                            ModuleStructure* moduleStructure, 
                            INTPTimeAdapter* timeAdapter,
                            IArduinoAdapter* arduinoAdapter) 
    : moduleConfig(moduleConfig), 
      moduleStructure(moduleStructure),
      timeAdapter(timeAdapter),
      arduinoAdapter(arduinoAdapter) {
    // Initialize containers
    devices.reserve(moduleConfig->getDeviceCount());
}

DeviceService::~DeviceService() {
    // Clean up allocated devices
    for (auto device : devices) {
        delete device;
    }
    devices.clear();
}

bool DeviceService::init() {
    initDevices();
    return true;
}

void DeviceService::initDevices() {
    // Clear any existing devices
    for (auto device : devices) {
        delete device;
    }
    devices.clear();
    
    // Initialize devices based on module type
    int moduleType = moduleConfig->getModuleType();
    int moduleSubType = moduleConfig->getModuleSubType();
    
    if (moduleType == MODULE_TYPE_DEVICE) {
        // For relay/digital output devices
        auto relays = moduleStructure->getAllRelays();
        for (auto relay : relays) {
            DeviceDigitalOut* device = new DeviceDigitalOut();
            device->deviceUid = relay->uid.c_str();
            device->state = DEVICE_STATE_OFF;
            device->isError = false;
            device->deviceType = DEVICE_TYPE_DIGITAL;  // Set device type
            // Initialize other properties
            devices.push_back(device);
        }
    }
    else if (moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE) {
        // For analog output devices
        auto pumps = moduleStructure->getAllPumps();
        for (auto pump : pumps) {
            DeviceAnalogOut* device = new DeviceAnalogOut();
            device->deviceUid = pump->uid.c_str();
            device->state = DEVICE_STATE_OFF;
            device->isError = false;
            device->deviceType = DEVICE_TYPE_ANALOG;  // Set device type
            // Initialize other properties
            devices.push_back(device);
        }
    }
}

Device* DeviceService::getDeviceByUid(const char* deviceUid) {
    for (auto device : devices) {
        if (strcmp(device->deviceUid, deviceUid) == 0) {
            return device;
        }
    }
    return nullptr;
}

std::vector<Device*> DeviceService::getAllDevices() {
    return devices;
}

bool DeviceService::updateDeviceState(const char* deviceUid, DeviceStateEnum state) {
    Device* device = getDeviceByUid(deviceUid);
    if (device != nullptr) {
        device->state = state;
        
        // Update switched time using INTPTimeAdapter
        device->switchedAt = timeAdapter->getFormattedTime();
        
        // Update time on/off based on state
        uint32_t currentMillis = arduinoAdapter->millis();
        if (state == DEVICE_STATE_ON) {
            device->timeOn = currentMillis;
        } else if (state == DEVICE_STATE_OFF || state == DEVICE_STATE_OFF_BY_POWER_ON) {
            device->timeOff = currentMillis;
        }
        
        return true;
    }
    return false;
}

bool DeviceService::setDeviceValue(const char* deviceUid, float value) {
    Device* device = getDeviceByUid(deviceUid);
    if (device == nullptr) return false;
    
    // Check if this is an analog device using the device type
    if (device->deviceType != DEVICE_TYPE_ANALOG) return false;
    
    // Now we can safely cast to DeviceAnalogOut since we know the device type
    DeviceAnalogOut* analogDevice = static_cast<DeviceAnalogOut*>(device);
    
    // Set the analog value (0-100%)
    analogDevice->analogPinValue = value;
    return true;
}

UpdateDto DeviceService::createUpdateDto() {
    UpdateDto updateDto;
    
    // Set basic module information
    updateDto.growdirectorUid = moduleConfig->getModuleUid();
    updateDto.millis = arduinoAdapter->millis();
    
    // Get time as ISO string using INTPTimeAdapter
    updateDto.time = timeAdapter->getFormattedTime();
    
    // Add account information
    char accountUid[BUFSIZE];
    moduleConfig->getSettings()->getAccountUid(accountUid);
    updateDto.accountUid = accountUid;
    
    char email[BUFSIZE];
    moduleConfig->getSettings()->getUser(email);
    updateDto.email = email;
    
    // Create device DTOs
    for (auto device : devices) {
        DeviceDto deviceDto = createDeviceDto(device);
        updateDto.devices.push_back(deviceDto);
    }
    
    return updateDto;
}

DeviceDto DeviceService::createDeviceDto(Device* device) {
    DeviceDto dto;
    
    // Copy basic properties
    dto.uid = device->deviceUid;
    dto.state = getDeviceStateName(device->state);
    dto.state_hw = (device->state == DEVICE_STATE_ON) ? "on" : "off";
    dto.switchedAt = device->switchedAt ? device->switchedAt : "";
    dto.isAuto = device->isAuto;
    dto.isManual = device->isManual;
    dto.sensorUid = device->sensorUid ? device->sensorUid : "";
    dto.sensorValue = device->sensorValue;
    
    // Rule information
    dto.ruleUid = device->ruleUid ? device->ruleUid : "";
    dto.ruleType = device->ruleType ? device->ruleType : "";
    dto.ruleVersion = device->ruleVersion;
    dto.ruleScheduleStartAt = device->ruleScheduleStartAt ? device->ruleScheduleStartAt : "";
    dto.ruleScheduleDuration = device->ruleScheduleDuration;
    dto.sensorValueLow = device->sensorValueLow;
    dto.sensorValueHigh = device->sensorValueHigh;
    
    // Rule timing
    dto.ruleSensorStartAt = device->ruleSensorStartAt ? device->ruleSensorStartAt : "";
    dto.ruleSensorStopAt = device->ruleSensorStopAt ? device->ruleSensorStopAt : "";
    dto.ruleSensorValueStart = device->ruleSensorValueStart;
    dto.ruleSensorValueStop = device->ruleSensorValueStop;
    dto.ruleStopReason = device->ruleStopReason ? device->ruleStopReason : "";
    
    // Duration calculations
    uint32_t currentMillis = arduinoAdapter->millis();
    if (device->state == DEVICE_STATE_ON && device->timeOn > 0) {
        dto.secondsEnabledFor = (currentMillis - device->timeOn) / 1000;
    } else {
        dto.secondsEnabledFor = 0;
    }
    
    if (device->state != DEVICE_STATE_ON && device->timeOff > 0) {
        dto.secondsDisabledFor = (currentMillis - device->timeOff) / 1000;
    } else {
        dto.secondsDisabledFor = 0;
    }
    
    // Switch back timing
    if (device->needToSwitchBack && device->durationOn > 0 && device->state == DEVICE_STATE_ON) {
        uint32_t switchBackTime = device->timeOn + device->durationOn;
        if (switchBackTime > currentMillis) {
            dto.secondsToSwitchBack = (switchBackTime - currentMillis) / 1000;
        } else {
            dto.secondsToSwitchBack = 0;
        }
    } else {
        dto.secondsToSwitchBack = 0;
    }
    
    // Digital device specific properties
    if (device->deviceType == DEVICE_TYPE_DIGITAL) {
        DeviceDigitalOut* digitalDevice = static_cast<DeviceDigitalOut*>(device);
        dto.isRuleTurnOnDevice = digitalDevice->isRuleTurnOnDevice;
        dto.timeToSetPoint = digitalDevice->timeToSetPoint;
        dto.cyclesToSetPoint = digitalDevice->cyclesToSetPoint;
        dto.currentCycle = digitalDevice->currentCycle;
    }
    
    // Command ID
    if (device->commandId > 0) {
        dto.commandId = std::to_string(device->commandId);
    }
    
    return dto;
}

bool DeviceService::turnOnDevice(const char* deviceUid) {
    return updateDeviceState(deviceUid, DEVICE_STATE_ON);
}

bool DeviceService::turnOffDevice(const char* deviceUid) {
    return updateDeviceState(deviceUid, DEVICE_STATE_OFF);
}

bool DeviceService::isDeviceOn(const char* deviceUid) {
    Device* device = getDeviceByUid(deviceUid);
    if (device != nullptr) {
        return device->state == DEVICE_STATE_ON;
    }
    return false;
}

void DeviceService::processDevices() {
    uint32_t currentMillis = arduinoAdapter->millis();
    
    for (auto device : devices) {
        // Check if device needs to switch back
        if (device->needToSwitchBack && device->state == DEVICE_STATE_ON && 
            device->timeOn > 0 && device->durationOn > 0) {
            if (currentMillis - device->timeOn >= device->durationOn) {
                device->state = DEVICE_STATE_OFF;
                device->timeOff = currentMillis;
                
                // Update switched time using INTPTimeAdapter
                device->switchedAt = timeAdapter->getFormattedTime();
            }
        }
        
        // Process digital device specifics
        if (device->deviceType == DEVICE_TYPE_DIGITAL) {
            DeviceDigitalOut* digitalDevice = static_cast<DeviceDigitalOut*>(device);
            
            // Handle step-based operations if needed
            if (digitalDevice->state == DEVICE_STATE_STEP_ON) {
                // Logic for step-on state processing
            } else if (digitalDevice->state == DEVICE_STATE_STEP_WAITING) {
                // Logic for step-waiting state processing
            }
            
            // Handle delay operations
            if (digitalDevice->state == DEVICE_STATE_DELAY_ON && 
                digitalDevice->delayStartTime > 0 &&
                currentMillis - digitalDevice->delayStartTime >= digitalDevice->timeToSetPoint) {
                digitalDevice->state = DEVICE_STATE_ON;
                digitalDevice->timeOn = currentMillis;
                
                // Update switched time using INTPTimeAdapter
                digitalDevice->switchedAt = timeAdapter->getFormattedTime();
            }
            else if (digitalDevice->state == DEVICE_STATE_DELAY_OFF && 
                digitalDevice->delayStartTime > 0 &&
                currentMillis - digitalDevice->delayStartTime >= digitalDevice->timeToSetPoint) {
                digitalDevice->state = DEVICE_STATE_OFF;
                digitalDevice->timeOff = currentMillis;
                
                // Update switched time using INTPTimeAdapter
                digitalDevice->switchedAt = timeAdapter->getFormattedTime();
            }
        }
    }
}

void DeviceService::updateDeviceSensorValues() {
    for (auto device : devices) {
        if (device->sensorUid == nullptr || strlen(device->sensorUid) == 0) {
            continue;
        }
        
        // Find the sensor by UID
        Sensor* sensor = moduleStructure->findSensorByUid(device->sensorUid);
        if (sensor != nullptr) {
            // Here you would read the actual sensor value
            // For now we just update the timestamp
            device->sensorValueTime = arduinoAdapter->millis();
        }
    }
}
