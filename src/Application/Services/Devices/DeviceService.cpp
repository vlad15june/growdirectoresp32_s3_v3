#include "DeviceService.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCaseFactory.h"

DeviceService::DeviceService(IModuleConfig* moduleConfig, 
                            ModuleStructure* moduleStructure,
                            IDeviceModel* deviceModel,
                            INTPTimeAdapter* timeAdapter,
                            IArduinoAdapter* arduinoAdapter,
                            IProcessDeviceStateUseCaseFactory* deviceStateUseCaseFactory,
                            IHybridAPIService* hybridApiService,
                            ILogger* logger,
                            IDebugSystem* debugSystem) 
    : moduleConfig(moduleConfig), 
      moduleStructure(moduleStructure),
      deviceModel(deviceModel),
      timeAdapter(timeAdapter),
      arduinoAdapter(arduinoAdapter),
      deviceStateUseCaseFactory(deviceStateUseCaseFactory),
      processDeviceStateUseCase(nullptr),
      hybridApiService(hybridApiService),
      logger(logger),
      debugSystem(debugSystem),
      lastDeviceUpdateTime(0) {
}

DeviceService::~DeviceService() {
    if (processDeviceStateUseCase) {
        delete processDeviceStateUseCase;
        processDeviceStateUseCase = nullptr;
    }
}

bool DeviceService::init() {
    log("DeviceService: Initializing");
    bool result = deviceModel->init();
    if (!result) {
        log("DeviceService: Failed to initialize device model");
        return false;
    }
    
    if (processDeviceStateUseCase) {
        delete processDeviceStateUseCase;
        processDeviceStateUseCase = nullptr;
    }
    
    int moduleType = moduleConfig->getModuleType();
    processDeviceStateUseCase = deviceStateUseCaseFactory->create(moduleType);
    
    lastDeviceUpdateTime = arduinoAdapter->millis();
    
    log("DeviceService: Initialization %s", processDeviceStateUseCase ? "successful" : "failed");
    return processDeviceStateUseCase != nullptr;
}

void DeviceService::loop() {
    if (deviceModel->getAllDevices().empty()) {
        return;
    }
    
    if (processDeviceStateUseCase) {
        processDeviceStateUseCase->loop();
    }

    refreshSwitchedAtForChangedDevices();

    if (!hybridApiService) {
        return;
    }

    unsigned long currentTime = arduinoAdapter->millis();
    std::string signature = buildDeviceStateSignature();

    bool stateChanged = (signature != lastDeviceStateSignature);
    bool periodicDue = (currentTime - lastDeviceUpdateTime >= DEVICE_UPDATE_INTERVAL);

    if (stateChanged || periodicDue) {
        if (stateChanged) {
            log("DeviceService: Device state changed, sending update immediately");
        }
        bool sent = sendDeviceUpdate();
        lastDeviceStateSignature = signature;
        if (sent) {
            clearReportedRuleContext();
        }
    }
}

void DeviceService::clearReportedRuleContext() {
    auto devices = deviceModel->getAllDevices();
    for (auto device : devices) {
        if (!device || !device->needReportingRuleUid) {
            continue;
        }

        device->workByRule = false;
        device->ruleUid = nullptr;
        device->ruleStopReason = nullptr;
        device->needReportingRuleUid = false;
        if (device->deviceType != DEVICE_TYPE_ANALOG) {
            static_cast<DeviceDigitalOut*>(device)->isRuleTurnOnDevice = false;
        }

        log("DeviceService: Cleared rule context after reporting for %s",
            device->deviceUid ? device->deviceUid : "");
    }
}

bool DeviceService::sendDeviceUpdate() {
    if (!hybridApiService) {
        return false;
    }

    log("DeviceService: Sending device update to server");
    UpdateDto updateDto = createDeviceUpdateDto();

    bool sent = hybridApiService->sendModuleUpdateData(updateDto);

    lastDeviceUpdateTime = arduinoAdapter->millis();
    return sent;
}

void DeviceService::refreshSwitchedAtForChangedDevices() {
    if (!timeAdapter) {
        return;
    }

    auto devices = deviceModel->getAllDevices();
    for (auto device : devices) {
        if (!device || !device->deviceUid) {
            continue;
        }

        std::string uid = device->deviceUid;
        auto it = prevDeviceStates.find(uid);

        if (it != prevDeviceStates.end() && it->second != device->state) {
            const char* now = timeAdapter->getFormattedTime();
            if (now) {
                device->switchedAtStorage = now;
                device->switchedAt = device->switchedAtStorage.c_str();
            }
        }

        prevDeviceStates[uid] = device->state;
    }
}

std::string DeviceService::buildDeviceStateSignature() {
    std::string signature;
    auto devices = deviceModel->getAllDevices();
    for (auto device : devices) {
        if (!device) {
            continue;
        }
        signature += device->deviceUid ? device->deviceUid : "";
        signature += ":";
        signature += std::to_string(static_cast<int>(device->state));
        signature += ":";
        signature += device->isAuto ? "1" : "0";
        signature += device->isManual ? "1" : "0";
        signature += device->workByRule ? "1" : "0";
        signature += device->workManual ? "1" : "0";
        signature += device->needToSwitchBack ? "1" : "0";
        signature += ":";
        signature += std::to_string(device->commandId);
        signature += ":";
        signature += device->ruleUid ? device->ruleUid : "";
        signature += ":";
        signature += device->switchedAt ? device->switchedAt : "";
        signature += ";";
    }
    return signature;
}

UpdateDto DeviceService::createDeviceUpdateDto() {
    UpdateDto dto;
    
    dto.growdirectorUid = moduleConfig->getModuleUid();
    dto.time = timeAdapter->getFormattedTime();
    dto.millis = arduinoAdapter->millis();
    
    ISettings* settings = moduleConfig->getSettings();
    if (settings) {
        char accountUidBuffer[256];
        settings->getAccountUid(accountUidBuffer);
        dto.accountUid = accountUidBuffer;
        
        char userEmailBuffer[256];
        settings->getUser(userEmailBuffer);
        dto.email = userEmailBuffer;
    }
    
    auto devices = deviceModel->getAllDevices();
    for (auto device : devices) {
        DeviceDto deviceDto;
        
        deviceDto.uid = device->deviceUid;
        deviceDto.state = getDeviceStateName(device->state);
        deviceDto.switchedAt = device->switchedAt ? device->switchedAt : "";
        
        bool isOn = false;
        if (device->state == DEVICE_STATE_ON || 
            device->state == DEVICE_STATE_STEP_ON) {
            isOn = true;
        }
        deviceDto.state_hw = isOn ? "on" : "off";
        
        deviceDto.isAuto = device->workByRule;
        deviceDto.isManual = device->workManual;
        
        uint32_t secondsToSwitchBack = 0;
        uint32_t secondsEnabledFor = 0;
        
        if (isOn && device->needToSwitchBack) {
            unsigned long currentDuration = arduinoAdapter->millis() - device->timeOn;
            if (currentDuration < device->durationOn) {
                secondsToSwitchBack = (device->durationOn - currentDuration) / 1000;
            }
            secondsEnabledFor = device->durationOn / 1000;
        }
        
        if (device->workByRule) {
            if (device->ruleUid) {
                deviceDto.ruleUid = device->ruleUid;

                if (device->workflowUid && device->workflowUid[0]) {
                    deviceDto.workflowUid = device->workflowUid;
                    deviceDto.workflowVersion = device->workflowVersion;
                }
                
                switch (device->ruleType) {
                    case RuleType::STEP:
                        deviceDto.ruleType = "step";
                        break;
                    case RuleType::SENSOR:
                        deviceDto.ruleType = "sensor";
                        break;
                    case RuleType::SCHEDULE:
                        deviceDto.ruleType = "schedule";
                        break;
                    case RuleType::EC:
                        deviceDto.ruleType = "EC";
                        break;
                    case RuleType::PH:
                        deviceDto.ruleType = "pH";
                        break;
                    case RuleType::LOOP:
                        deviceDto.ruleType = "loop";
                        break;
                    default:
                        deviceDto.ruleType = "";
                        break;
                }
            }
            
            if (device->deviceType == DEVICE_TYPE_DIGITAL) {
                DeviceDigitalOut* digitalDevice = static_cast<DeviceDigitalOut*>(device);
                deviceDto.isRuleTurnOnDevice = digitalDevice->isRuleTurnOnDevice;
                
                if (device->state != DEVICE_STATE_DELAY_ON && !digitalDevice->isRuleTurnOnDevice) {
                    if (device->ruleSensorStopAt) {
                        deviceDto.ruleSensorStopAt = device->ruleSensorStopAt;
                    }
                    deviceDto.ruleSensorValueStop = device->ruleSensorValueStop;
                    deviceDto.ruleStopReason = device->ruleStopReason ? device->ruleStopReason : "";
                }
                
                if (device->ruleSensorStartAt) {
                    deviceDto.ruleSensorStartAt = device->ruleSensorStartAt;
                }
                
                deviceDto.ruleVersion = device->ruleVersion;
                
                if (device->ruleType == RuleType::PH || 
                    device->ruleType == RuleType::EC || 
                    device->ruleType == RuleType::SENSOR || 
                    device->ruleType == RuleType::STEP) {
                    
                    if (device->sensorUid) {
                        deviceDto.sensorUid = device->sensorUid;
                    }
                    deviceDto.sensorValueLow = device->sensorValueLow;
                    deviceDto.sensorValueHigh = device->sensorValueHigh;
                    deviceDto.sensorValue = device->sensorValue;
                    deviceDto.ruleSensorValueStart = device->ruleSensorValueStart;
                    
                    if (device->ruleType == RuleType::SENSOR) {
                        if (digitalDevice->timeToSetPoint > 0) {
                            deviceDto.timeToSetPoint = digitalDevice->timeToSetPoint;
                        }
                    }
                    
                    if (device->ruleType == RuleType::PH || 
                        device->ruleType == RuleType::EC || 
                        device->ruleType == RuleType::STEP) {
                        
                        if (digitalDevice->cyclesToSetPoint > 0) {
                            deviceDto.cyclesToSetPoint = digitalDevice->cyclesToSetPoint;
                            deviceDto.currentCycle = digitalDevice->currentCycle;
                        }
                    }
                }
                
                if (device->ruleType == RuleType::SCHEDULE) {
                    deviceDto.ruleScheduleStartAt = device->ruleScheduleStartAt ? device->ruleScheduleStartAt : "";
                    deviceDto.ruleScheduleDuration = device->ruleScheduleDuration;
                    deviceDto.secondsToSwitchBack = secondsToSwitchBack;
                    deviceDto.secondsEnabledFor = secondsEnabledFor;
                }
                
                if (device->ruleType == RuleType::PH || 
                    device->ruleType == RuleType::EC || 
                    device->ruleType == RuleType::STEP || 
                    device->ruleType == RuleType::LOOP) {
                    
                    deviceDto.secondsToSwitchBack = secondsToSwitchBack;
                    deviceDto.secondsDisabledFor = device->mixTime / 1000;
                    deviceDto.secondsEnabledFor = secondsEnabledFor;
                }
            }
        }
        
        if (device->workManual) {
            deviceDto.commandId = device->commandId;
            deviceDto.secondsToSwitchBack = secondsToSwitchBack;
            deviceDto.secondsEnabledFor = secondsEnabledFor;
        }
        
        if (device->deviceType == DEVICE_TYPE_ANALOG) {
            DeviceAnalogOut* analogDevice = static_cast<DeviceAnalogOut*>(device);
        }
        
        dto.devices.push_back(deviceDto);
    }
    
    return dto;
}