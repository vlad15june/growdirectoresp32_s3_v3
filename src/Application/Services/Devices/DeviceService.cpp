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
    if (deviceModel->allDevices().empty()) {
        return;
    }

    if (processDeviceStateUseCase) {
        processDeviceStateUseCase->loop();
    }

    refreshSwitchedAtForChangedDevices();

    // Either route out is enough. With an outbox this service does not hold an API pointer at
    // all on the device - it submits and the network task makes the call.
    if (!hybridApiService && !outbox) {
        return;
    }

    unsigned long currentTime = arduinoAdapter->millis();

    bool stateChanged = deviceStateChangedSinceReported();
    bool periodicDue = (currentTime - lastDeviceUpdateTime >= DEVICE_UPDATE_INTERVAL);
    const bool allowed = (bootGates == nullptr) || bootGates->allows(BootGate::DEVICE_UPDATE);
    if ((stateChanged || periodicDue) && !allowed && !updateHeld) {
        // The update this iteration would have handed over - and the outbox would have kept until
        // the link was back - is held by the gate instead. It goes when the gate opens.
        updateHeld = true;
        updateHeldSinceMs = currentTime;
    }

    if ((stateChanged || periodicDue) && allowed) {
        if (stateChanged) {
            log("DeviceService: Device state changed, sending update immediately");
        }

        if (outbox) {
            // Which devices this update speaks for has to be decided now, not when the completion
            // comes back: by then a rule may have switched something else on and set its
            // needReportingRuleUid, and clearing that would erase a reason the server never heard.
            std::vector<std::string> reported = devicesAwaitingRuleReport();

            const uint32_t ticket = outbox->submitDeviceUpdate(createDeviceUpdateDto());
            if (ticket != IOutbox::kRejected) {
                pendingUpdateTicket = ticket;
                pendingReportedRuleUids.swap(reported);
                // Only on an accepted hand-off. Recording the state before knowing whether the
                // send was taken would leave "nothing has changed" on file after a refused
                // submission, and the change would never be re-sent on its own account.
                captureReportedDeviceStates();
            }
            lastDeviceUpdateTime = arduinoAdapter->millis();
            if (updateHeld) {
                // Held, the interval keeps the phase the held update would have given it: a return of
                // the link sends as many updates as an outage the gate did not see, the held one
                // fresher.
                lastDeviceUpdateTime -= (lastDeviceUpdateTime - updateHeldSinceMs) % DEVICE_UPDATE_INTERVAL;
                updateHeld = false;
            }
        } else {
            bool sent = sendDeviceUpdate();
            captureReportedDeviceStates();
            if (sent) {
                clearReportedRuleContext();
            }
        }
    }
}

void DeviceService::onOutboxCompletion(const OutboxCompletion& completion) {
    if (completion.kind != OutboxKind::DEVICE_UPDATE) {
        return;
    }

    if (pendingUpdateTicket == IOutbox::kRejected || completion.ticket != pendingUpdateTicket) {
        // An older update that a fresher one replaced in the outbox, or someone else's ticket.
        return;
    }

    pendingUpdateTicket = IOutbox::kRejected;

    std::vector<std::string> reported;
    reported.swap(pendingReportedRuleUids);

    if (!completion.delivered) {
        // The whole reason this service waits for a completion instead of treating the hand-off
        // as the send. clearReportedRuleContext() erases workByRule, ruleUid, ruleStopReason and
        // needReportingRuleUid - the only record of why a pump was switched on. Erasing it on a
        // message that never arrived leaves the server with an unexplained device state and
        // nowhere to recover the explanation from. Kept here, it goes out with the next update.
        log("DeviceService: Device update was not delivered, keeping the rule context of %d device(s)",
            (int)reported.size());
        return;
    }

    clearReportedRuleContext(reported);
}

std::vector<std::string> DeviceService::devicesAwaitingRuleReport() const {
    std::vector<std::string> uids;
    const auto& devices = deviceModel->allDevices();
    for (auto device : devices) {
        if (device && device->needReportingRuleUid && device->deviceUid) {
            uids.push_back(device->deviceUid);
        }
    }
    return uids;
}

void DeviceService::clearReportedRuleContext(const std::vector<std::string>& deviceUids) {
    if (deviceUids.empty()) {
        return;
    }

    const auto& devices = deviceModel->allDevices();
    for (auto device : devices) {
        if (!device || !device->needReportingRuleUid || !device->deviceUid) {
            continue;
        }

        const std::string uid = device->deviceUid;
        bool wasReported = false;
        for (const auto& reportedUid : deviceUids) {
            if (reportedUid == uid) {
                wasReported = true;
                break;
            }
        }
        if (!wasReported) {
            continue;
        }

        device->workByRule = false;
        device->ruleUid = nullptr;
        device->ruleStopReason = nullptr;
        device->needReportingRuleUid = false;
        if (device->deviceType != DEVICE_TYPE_ANALOG) {
            static_cast<DeviceDigitalOut*>(device)->isRuleTurnOnDevice = false;
        }

        log("DeviceService: Cleared rule context after reporting for %s", uid.c_str());
    }
}

void DeviceService::clearReportedRuleContext() {
    const auto& devices = deviceModel->allDevices();
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

    const auto& devices = deviceModel->allDevices();

    // Fast path: the same devices in the same order as last iteration. One pass, comparing by
    // index; the only work on a settled iteration is reads and enum comparisons.
    size_t trackedIndex = 0;
    bool aligned = true;
    for (auto device : devices) {
        if (!device || !device->deviceUid) {
            continue;
        }
        if (trackedIndex >= prevDeviceStates.size() ||
            prevDeviceStates[trackedIndex].uid != device->deviceUid) {
            aligned = false;
            break;
        }
        TrackedDeviceState& tracked = prevDeviceStates[trackedIndex++];
        if (tracked.state != device->state) {
            stampSwitchedAt(device);
            tracked.state = device->state;
        }
    }
    if (aligned) {
        // Entries past trackedIndex belong to devices currently absent from the list; they are
        // deliberately kept, so a device that comes back is compared against its last known
        // state instead of being treated as never seen.
        return;
    }

    // The device list changed shape - rebuild the tracking list in the new order, carrying
    // every last seen state over by UID. Rare (reconfiguration), allowed to allocate.
    std::vector<TrackedDeviceState> rebuilt;
    rebuilt.reserve(prevDeviceStates.size() + devices.size());
    for (auto device : devices) {
        if (!device || !device->deviceUid) {
            continue;
        }
        TrackedDeviceState tracked;
        tracked.uid = device->deviceUid;
        tracked.state = device->state;
        for (const auto& previous : prevDeviceStates) {
            if (previous.uid == device->deviceUid) {
                if (previous.state != device->state) {
                    stampSwitchedAt(device);
                }
                break;
            }
        }
        rebuilt.push_back(tracked);
    }
    for (const auto& previous : prevDeviceStates) {
        bool stillPresent = false;
        for (const auto& current : rebuilt) {
            if (current.uid == previous.uid) {
                stillPresent = true;
                break;
            }
        }
        if (!stillPresent) {
            rebuilt.push_back(previous);
        }
    }
    prevDeviceStates.swap(rebuilt);
}

void DeviceService::stampSwitchedAt(Device* device) {
    const char* now = timeAdapter->getFormattedTime();
    if (now) {
        device->switchedAtStorage = now;
        device->switchedAt = device->switchedAtStorage.c_str();
    }
}

bool DeviceService::deviceStateChangedSinceReported() const {
    if (!reportedStatesValid) {
        // Nothing was ever reported: the first picture the server gets is always news.
        return true;
    }

    const auto& devices = deviceModel->allDevices();
    size_t reportedIndex = 0;
    for (auto device : devices) {
        if (!device) {
            continue;
        }
        if (reportedIndex >= reportedDeviceStates.size()) {
            return true;
        }
        const ReportedDeviceState& reported = reportedDeviceStates[reportedIndex++];
        if (reported.uid != (device->deviceUid ? device->deviceUid : "")) {
            return true;
        }
        if (reported.state != device->state ||
            reported.isAuto != device->isAuto ||
            reported.isManual != device->isManual ||
            reported.workByRule != device->workByRule ||
            reported.workManual != device->workManual ||
            reported.needToSwitchBack != device->needToSwitchBack ||
            reported.commandId != device->commandId) {
            return true;
        }
        if (reported.ruleUid != (device->ruleUid ? device->ruleUid : "")) {
            return true;
        }
        if (reported.switchedAt != (device->switchedAt ? device->switchedAt : "")) {
            return true;
        }
    }
    return reportedIndex != reportedDeviceStates.size();
}

void DeviceService::captureReportedDeviceStates() {
    const auto& devices = deviceModel->allDevices();
    size_t reportedIndex = 0;
    for (auto device : devices) {
        if (!device) {
            continue;
        }
        if (reportedIndex >= reportedDeviceStates.size()) {
            reportedDeviceStates.push_back(ReportedDeviceState());
        }
        ReportedDeviceState& reported = reportedDeviceStates[reportedIndex++];
        reported.uid = device->deviceUid ? device->deviceUid : "";
        reported.state = device->state;
        reported.isAuto = device->isAuto;
        reported.isManual = device->isManual;
        reported.workByRule = device->workByRule;
        reported.workManual = device->workManual;
        reported.needToSwitchBack = device->needToSwitchBack;
        reported.commandId = device->commandId;
        reported.ruleUid = device->ruleUid ? device->ruleUid : "";
        reported.switchedAt = device->switchedAt ? device->switchedAt : "";
    }
    reportedDeviceStates.resize(reportedIndex);
    reportedStatesValid = true;
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
    
    const auto& devices = deviceModel->allDevices();
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