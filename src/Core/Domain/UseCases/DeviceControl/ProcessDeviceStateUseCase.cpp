#include "ProcessDeviceStateUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include <cstdio>

ProcessDeviceStateUseCase::ProcessDeviceStateUseCase(
    ILogger *logger,
    IDebugSystem *debugSystem,
    IModuleConfig *moduleConfig,
    IArduinoAdapter *arduinoAdapter,
    IDeviceModel *deviceModel)
    : logger(logger),
      debugSystem(debugSystem),
      moduleConfig(moduleConfig),
      arduinoAdapter(arduinoAdapter),
      deviceModel(deviceModel)
{
    int count = moduleConfig ? moduleConfig->getDeviceCount() : 0;
    uint8_t inactive = arduinoAdapter ? arduinoAdapter->getDigitalLow() : 0;
    lastRelayLevel.assign(count, static_cast<int8_t>(inactive));
}

void ProcessDeviceStateUseCase::loop()
{
    controlDeviceState();
}

void ProcessDeviceStateUseCase::controlDeviceState()
{
    const auto& devices = deviceModel->allDevices();
    for (int i = 0; i < devices.size(); i++)
    {
        processDeviceState(devices[i], i);
    }
}

void ProcessDeviceStateUseCase::processDeviceState(Device *device, int index)
{
    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
    {
        logger->printf("CONTROL: Channel %d, isResquestOn=%d, state=%s\n",
                       index,
                       (static_cast<DeviceDigitalOut *>(device)->isResquestOn ? 1 : 0),
                       getDeviceStateName(device->state));
    }

    int moduleType = moduleConfig->getModuleType();

    switch (device->state)
    {
    case DEVICE_STATE_DELAY_ON:
        processDelayOn(device, index);
        break;

    case DEVICE_STATE_DELAY_OFF:
        processDelayOff(device, index);
        break;

    case DEVICE_STATE_REQUEST_ON:
        processRequestOn(device, index, moduleType);
        break;

    case DEVICE_STATE_REQUEST_STEP_ON:
        processRequestStepOn(device, index, moduleType);
        break;

    case DEVICE_STATE_ON:
        processOn(device, index);
        break;

    case DEVICE_STATE_STEP_ON:
        processStepOn(device, index);
        break;

    case DEVICE_STATE_STEP_WAITING:
        processStepWaiting(device, index);
        break;

    case DEVICE_STATE_OFF:
        processOff(device, index);
        break;

    default:
        // No action for other states
        break;
    }
}

void ProcessDeviceStateUseCase::processDelayOn(Device *device, int index)
{
    // During delay state, the device remains off
    switchRelayOff(device, index);
    // No state change here - the state change will be handled in the rule processing
}

void ProcessDeviceStateUseCase::processDelayOff(Device *device, int index)
{
    // During delayOff state, the device remains on while waiting to switch off
    switchRelayOn(device, index);
    // No state change here - the state change will be handled in the rule processing
}

void ProcessDeviceStateUseCase::processRequestOn(Device *device, int index, int moduleType)
{
    if (moduleType == MODULE_TYPE_HYDRO)
    {
        int countOffDevices = countDevicesInOffState();

        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            logger->printf("CONTROL: Channel %d, countOffDevices=%d\n", index, countOffDevices);
        }

        int deviceCount = deviceModel->getDeviceCount();

        if (countOffDevices == deviceCount)
        {
            if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
            {
                logger->printf("CONTROL: Channel %d, turn ON from queue\n", index);
            }

            char buffer[96];
            snprintf(buffer, sizeof(buffer), "RELAY_DRIVE: hydro_admit ch=%d countOff=%d deviceCount=%d",
                     index, countOffDevices, deviceCount);
            logRelay(buffer);

            device->state = DEVICE_STATE_ON;
            device->timeOn = arduinoAdapter->millis();
            switchRelayOn(device, index);
        }
    }
    else if (moduleType == MODULE_TYPE_DEVICE)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            logger->printf("CONTROL: Channel %d, turn ON\n", index);
        }

        device->state = DEVICE_STATE_ON;
        device->timeOn = arduinoAdapter->millis();
        switchRelayOn(device, index);
    }
}

void ProcessDeviceStateUseCase::processRequestStepOn(Device *device, int index, int moduleType)
{
    if (moduleType == MODULE_TYPE_HYDRO)
    {
        int countOffDevices = countDevicesInOffState();

        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            logger->printf("CONTROL: Channel %d, countOffDevices=%d\n", index, countOffDevices);
        }

        int deviceCount = deviceModel->getDeviceCount();

        if (countOffDevices == deviceCount)
        {
            if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
            {
                logger->printf("CONTROL: Channel %d, turn ON from queue\n", index);
            }

            char buffer[96];
            snprintf(buffer, sizeof(buffer), "RELAY_DRIVE: hydro_admit ch=%d countOff=%d deviceCount=%d",
                     index, countOffDevices, deviceCount);
            logRelay(buffer);

            device->state = DEVICE_STATE_STEP_ON;
            device->timeOn = arduinoAdapter->millis();
            switchRelayOn(device, index);
        }
    }
    else if (moduleType == MODULE_TYPE_DEVICE)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            logger->printf("CONTROL: Channel %d, turn ON\n", index);
        }

        device->state = DEVICE_STATE_STEP_ON;
        device->timeOn = arduinoAdapter->millis();
        switchRelayOn(device, index);
    }
}

void ProcessDeviceStateUseCase::processOn(Device *device, int index)
{
    if (device->needToSwitchBack)
    {
        unsigned long currentDuration = arduinoAdapter->millis() - device->timeOn;

        if (currentDuration < device->durationOn)
        {
            switchRelayOn(device, index);
        }
        else
        {
            if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
            {
                logger->printf("CONTROL: Channel %d, OFF, (millis() - timeOn)=%lu >= durationOn=%lu) ruleUid = %s\n",
                               index, currentDuration, device->durationOn, device->ruleUid);
            }

            device->state = DEVICE_STATE_OFF;

            if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
            {
                logger->printf("CONTROL: Channel %d, state = %s\n", index, getDeviceStateName(device->state));
            }

            switchRelayOff(device, index);
            device->isAuto = false;
            device->isManual = false;
            device->ruleStopReason = "time_reach";
            if (device->deviceType != DeviceTypeEnum::DEVICE_TYPE_ANALOG)
            {
                static_cast<DeviceDigitalOut *>(device)->isRuleTurnOnDevice = false;
            }
        }
    }
    else
    {
        switchRelayOn(device, index);
    }
}

void ProcessDeviceStateUseCase::processStepOn(Device *device, int index)
{
    unsigned long currentDuration = arduinoAdapter->millis() - device->timeOn;

    if (currentDuration < device->durationOn)
    {
        switchRelayOn(device, index);
    }
    else
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            logger->printf("CONTROL: Channel %d, OFF, (millis() - timeOn)=%lu >= durationOn=%lu) ruleUid = %s\n",
                           index, currentDuration, device->durationOn, device->ruleUid);
        }

        device->state = DEVICE_STATE_STEP_WAITING;

        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            logger->printf("CONTROL: Channel %d, state = %s\n", index, getDeviceStateName(device->state));
        }

        switchRelayOff(device, index);
    }
}

void ProcessDeviceStateUseCase::processStepWaiting(Device *device, int index)
{
    DeviceDigitalOut *digitalDevice = static_cast<DeviceDigitalOut *>(device);
    unsigned long currentDurationMix = arduinoAdapter->millis() - (device->timeOn + device->durationOn);

    if (currentDurationMix > digitalDevice->mixTime)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            logger->printf("CONTROL: Mix stop, currentDurationMix=%lu, mixTime=%d \n",
                           currentDurationMix, digitalDevice->mixTime);
        }

        if (digitalDevice->isRuleTurnOnDevice)
        {
            device->state = DEVICE_STATE_REQUEST_STEP_ON;
            digitalDevice->currentCycle++;

            if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
            {
                logger->printf("CONTROL: Channel %d, state =stepWaiting->%s, isRuleTurnOnDevice = true\n",
                               index, getDeviceStateName(device->state));
            }

            // TODO: Handle history recording
            // needStoreDeviceHistory = true;
            // needSendToServer = true;
            // device->switchedAt = getLocalTime();
        }
        else
        {
            device->state = DEVICE_STATE_OFF;

            if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
            {
                logger->printf("CONTROL: Channel %d, state = stepWaiting->%s, isRuleTurnOnDevice = false\n",
                               index, getDeviceStateName(device->state));
            }

            switchRelayOff(device, index);

            // TODO: Handle history recording
            // needStoreDeviceHistory = true;
            // needSendToServer = true;
            // device->switchedAt = getLocalTime();

            device->isAuto = false;
            device->isManual = false;
        }
    }
    else
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            logger->printf("CONTROL: Mix continue, currentDurationMix=%lu, mixTime=%d \n",
                           currentDurationMix, digitalDevice->mixTime);
        }

        switchRelayOff(device, index);
    }
}

void ProcessDeviceStateUseCase::processOff(Device *device, int index)
{
    switchRelayOff(device, index);
    device->isAuto = false;
    device->isManual = false;
}

// Helper methods

void ProcessDeviceStateUseCase::switchRelayOn(Device *device, int index)
{
    setRelayLevel(device, index, arduinoAdapter->getDigitalHigh(), "on");
}

void ProcessDeviceStateUseCase::switchRelayOff(Device *device, int index)
{
    setRelayLevel(device, index, arduinoAdapter->getDigitalLow(), "off");
}

void ProcessDeviceStateUseCase::setRelayLevel(Device *device, int index, uint8_t level, const char *action)
{
    if (!isChannelInRange(index))
    {
        return;
    }

    if (index >= static_cast<int>(lastRelayLevel.size()))
    {
        lastRelayLevel.resize(index + 1, static_cast<int8_t>(arduinoAdapter->getDigitalLow()));
    }

    if (lastRelayLevel[index] == static_cast<int8_t>(level))
    {
        return;
    }

    uint8_t pin = resolvePin(index);
    arduinoAdapter->digitalWrite(pin, level);
    lastRelayLevel[index] = static_cast<int8_t>(level);

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "RELAY_DRIVE: %s ch=%d pin=%d level=%d ruleUid=%s",
             action, index, pin, level, (device && device->ruleUid) ? device->ruleUid : "");
    logRelay(buffer);
}

uint8_t ProcessDeviceStateUseCase::resolvePin(int index)
{
    uint8_t *pins = (moduleConfig->getModuleType() == MODULE_TYPE_HYDRO)
                        ? moduleConfig->getPumpPins()
                        : moduleConfig->getRelayPins();
    return pins[index];
}

bool ProcessDeviceStateUseCase::isChannelInRange(int index)
{
    return index >= 0 && index < moduleConfig->getDeviceCount();
}

int ProcessDeviceStateUseCase::countDevicesInOffState()
{
    int count = 0;
    const auto& devices = deviceModel->allDevices();

    for (size_t i = 0; i < devices.size(); i++)
    {
        DeviceStateEnum state = devices[i]->state;
        if (state != DEVICE_STATE_ON && state != DEVICE_STATE_STEP_ON)
        {
            count++;
        }
    }

    return count;
}

void ProcessDeviceStateUseCase::logRelay(const char *message)
{
    if (!logger || !debugSystem || !debugSystem->isFeatureEnabled(DebugFeatures::RELAY_DRIVE_DEBUG))
    {
        return;
    }

    IDebugFeature *feature = debugSystem->getFeature(DebugFeatures::RELAY_DRIVE_DEBUG);
    if (feature)
    {
        logger->println(DebugFeatures::RELAY_DRIVE_DEBUG, message, feature->getLogChannel());
    }
    else
    {
        logger->println(DebugFeatures::RELAY_DRIVE_DEBUG, message);
    }
}
