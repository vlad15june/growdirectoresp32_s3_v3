#include "ProcessAnalogDeviceStateUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"

ProcessAnalogDeviceStateUseCase::ProcessAnalogDeviceStateUseCase(
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
}

void ProcessAnalogDeviceStateUseCase::loop()
{
    // Process all analog devices
    controlAnalogInputDeviceState();
}

void ProcessAnalogDeviceStateUseCase::controlAnalogInputDeviceState()
{
    auto devices = deviceModel->getAllDevices();

    for (int i = 0; i < devices.size(); i++)
    {
        if (devices[i]->deviceType == DEVICE_TYPE_ANALOG)
        {
            DeviceAnalogOut *analogDevice = static_cast<DeviceAnalogOut *>(devices[i]);
            setAnalogInputDeviceState(analogDevice, i);
        }
        // TODO: Handle device history and server updates
        // if (needStoreDeviceHistory) {
        //     storeDeviceHistory();
        //     needStoreDeviceHistory = false;
        // }

        // if (needSendToServer) {
        //     sendDataToServer();
        //     needSendToServer = false;
        // }
    }
}

void ProcessAnalogDeviceStateUseCase::setAnalogInputDeviceState(DeviceAnalogOut *device, int index)
{
    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
    {
        logger->printf("CONTROL: setAnalogInputDeviceState \n");
        logger->printf("CONTROL: createdDate %s\n", device->createdDate);
    }

    // Get device pin configuration
    // TODO: Implement a proper way to get pin configuration
    int pin = 0;           // This should come from the device configuration
    int analogPin = 0;     // This should come from the device configuration
    bool pinValue = false; // Current pin state

    if (device->direction == RuleDirection::UP)
    {
        if (device->state == DEVICE_STATE_ON)
        {
            if (device->needToSwitchBack)
            {
                unsigned long currentDuration = arduinoAdapter->millis() - device->timeOn;

                if (currentDuration < device->durationOn)
                {
                    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                    {
                        logger->printf("CONTROL: Channel %d, pin %d ON, (millis() - timeOn)=%lu < durationOn=%lu), ruleUid = %s \n",
                                       index, pin, currentDuration, device->durationOn, device->ruleUid);
                    }

                    if (!pinValue)
                    {
                        needStoreDeviceHistory = true;
                        needSendToServer = true;
                        device->switchedAt = getLocalTime();
                    }

                    pinValue = true;
                    ledON(index);

                    float analogWriteValue = device->analogPinValue * PERCENT_TO_ANALOG_WRITE_VALUE;
                    int analogWriteValueInt = (int)analogWriteValue;

                    arduinoAdapter->analogWrite(analogPin, analogWriteValueInt);
                    arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalHigh());

                    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                    {
                        logger->printf("CONTROL: analogValue = %d /n", analogWriteValueInt);
                    }
                }
                else
                {
                    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                    {
                        logger->printf("CONTROL: Channel %d, pin %d OFF, (millis() - timeOn)=%lu >= durationOn=%lu) ruleUid = %s\n",
                                       index, pin, currentDuration, device->durationOn, device->ruleUid);
                    }

                    if (pinValue)
                    {
                        needStoreDeviceHistory = true;
                        needSendToServer = true;
                        device->switchedAt = getLocalTime();
                    }

                    pinValue = false;
                    ledOFF(index);

                    arduinoAdapter->analogWrite(analogPin, 0);
                    arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalLow());

                    // Check relaxation time before switching to Idle
                    unsigned long currentDurationMix = arduinoAdapter->millis() - (device->timeOn + device->durationOn);
                    int mixTime = 0; // This should come from device configuration

                    if (currentDurationMix > mixTime)
                    {
                        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                        {
                            logger->printf("CONTROL: Mix stop, currentDurationMix=%lu, mixTime=%d \n",
                                           currentDurationMix, mixTime);
                        }

                        device->isAuto = false;
                        device->isManual = false;
                    }
                    else
                    {
                        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                        {
                            logger->printf("CONTROL: Mix continue, currentDurationMix=%lu, mixTime=%d \n",
                                           currentDurationMix, mixTime);
                        }
                    }
                }
            }
            else
            {
                // Continuous operation
                if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                {
                    logger->printf("CONTROL: Channel %d, pin %d ON, RUN continuously, ruleUid = %s\n",
                                   index, pin, device->ruleUid);
                }

                if (!pinValue)
                {
                    needStoreDeviceHistory = true;
                    needSendToServer = true;
                    device->switchedAt = getLocalTime();
                }

                pinValue = true;
                ledON(index);

                float analogWriteValue = device->analogPinValue * PERCENT_TO_ANALOG_WRITE_VALUE;
                int analogWriteValueInt = (int)analogWriteValue;

                arduinoAdapter->analogWrite(analogPin, analogWriteValueInt);
                arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalHigh());

                if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                {
                    logger->printf("CONTROL: analogValue = %d /n", analogWriteValueInt);
                }

                device->isAuto = false;
                device->isManual = false;
            }
        }

        if (device->state == DEVICE_STATE_OFF)
        {
            if (device->needToSwitchBack)
            {
                unsigned long currentDuration = arduinoAdapter->millis() - device->timeOff;

                if (currentDuration < device->durationOff)
                {
                    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                    {
                        logger->printf("CONTROL: Channel %d, pin %d OFF, (millis() - timeOff)=%lu < durationOff=%lu) ruleUid = %s\n",
                                       index, pin, currentDuration, device->durationOff, device->ruleUid);
                    }

                    if (pinValue)
                    {
                        needStoreDeviceHistory = true;
                        needSendToServer = true;
                        device->switchedAt = getLocalTime();
                    }

                    pinValue = false;
                    ledOFF(index);

                    arduinoAdapter->analogWrite(analogPin, 0);
                    arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalLow());
                }
                else
                {
                    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                    {
                        logger->printf("CONTROL: Channel %d, pin %d ON, (millis() - timeOff)=%lu >= durationOff=%lu) ruleUid = %s\n",
                                       index, pin, currentDuration, device->durationOff, device->ruleUid);
                    }

                    if (!pinValue)
                    {
                        needStoreDeviceHistory = true;
                        needSendToServer = true;
                        device->switchedAt = getLocalTime();
                    }

                    pinValue = true;
                    ledON(index);

                    float analogWriteValue = device->analogPinValue * PERCENT_TO_ANALOG_WRITE_VALUE;
                    int analogWriteValueInt = (int)analogWriteValue;

                    arduinoAdapter->analogWrite(analogPin, analogWriteValueInt);
                    arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalHigh());

                    if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                    {
                        logger->printf("CONTROL: analogValue = %d /n", analogWriteValueInt);
                    }

                    device->isAuto = false;
                    device->isManual = false;
                }
            }
            else
            {
                // Continuous OFF state
                if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
                {
                    logger->printf("CONTROL: Channel %d, pin %d OFF, RUN continuously, ruleUid = %s\n",
                                   index, pin, device->ruleUid);
                }

                if (pinValue)
                {
                    needStoreDeviceHistory = true;
                    needSendToServer = true;
                    device->switchedAt = getLocalTime();
                }

                pinValue = false;
                ledOFF(index);

                arduinoAdapter->analogWrite(analogPin, 0);
                arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalLow());

                device->isAuto = false;
                device->isManual = false;
            }
        }
    }
}

void ProcessAnalogDeviceStateUseCase::ledON(int index)
{
    // TODO: Implement LED control or use a dedicated service
}

void ProcessAnalogDeviceStateUseCase::ledOFF(int index)
{
    // TODO: Implement LED control or use a dedicated service
}

const char *ProcessAnalogDeviceStateUseCase::getLocalTime()
{
    // TODO: Implement proper time handling
    return ""; // Placeholder
}
