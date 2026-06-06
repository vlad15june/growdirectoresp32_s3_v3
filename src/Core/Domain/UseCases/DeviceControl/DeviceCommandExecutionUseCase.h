#pragma once

#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include <string>
#include <vector>
#include <cstdarg>

/**
 * @brief Implementation of device command execution use case
 */
class DeviceCommandExecutionUseCase : public IDeviceCommandExecutionUseCase
{
private:
    IDeviceModel *deviceModel;
    IArduinoAdapter *arduinoAdapter;
    ILogger *logger;
    IDebugSystem *debugSystem;
    ModuleStructure *moduleStructure;
    IModuleConfig *moduleConfig;

public:
    /**
     * @brief Constructor
     * @param deviceModel Device model for accessing devices
     * @param arduinoAdapter Arduino adapter for hardware control
     * @param moduleStructure Module structure for pin mapping
     * @param logger Logger instance
     * @param debugSystem Debug system instance
     * @param moduleConfig Module configuration for relay/pump pin mapping
     */
    DeviceCommandExecutionUseCase(
        IDeviceModel *deviceModel,
        IArduinoAdapter *arduinoAdapter,
        ModuleStructure *moduleStructure,
        ILogger *logger,
        IDebugSystem *debugSystem,
        IModuleConfig *moduleConfig)
        : deviceModel(deviceModel),
          arduinoAdapter(arduinoAdapter),
          moduleStructure(moduleStructure),
          logger(logger),
          debugSystem(debugSystem),
          moduleConfig(moduleConfig) {}

    /**
     * @brief Execute a device command
     * @param deviceUid Device unique identifier
     * @param requestedState Requested state (on/off)
     * @param commandId Command identifier
     * @return true if command was executed successfully, false otherwise
     */
    bool execute(const std::string &deviceUid, const std::string &requestedState, int commandId, bool needToSwitchBack = false, int secondsToSwitchBack = 0, const std::string &switchedAt = "") override
    {
        log("DeviceCommandExecutionUseCase: Executing command - deviceUid: %s, state: %s, commandId: %d, needToSwitchBack: %s, secondsToSwitchBack: %d",
            deviceUid.c_str(), requestedState.c_str(), commandId, needToSwitchBack ? "true" : "false", secondsToSwitchBack);

        // Find the device
        Device *device = deviceModel->getDeviceByUid(deviceUid.c_str());
        if (!device)
        {
            log("DeviceCommandExecutionUseCase: Device not found: %s", deviceUid.c_str());
            return false;
        }

        // Resolve the channel index for this device
        int index = findDeviceIndex(deviceUid);
        if (index < 0)
        {
            log("DeviceCommandExecutionUseCase: Channel not found for device: %s", deviceUid.c_str());
            return false;
        }

        bool turnOn = (requestedState == "on" || requestedState == "On" || requestedState == "ON");

        if (executeHardwareCommand(device, index, turnOn))
        {
            // Update device state
            device->state = turnOn ? DEVICE_STATE_ON : DEVICE_STATE_OFF;
            device->isManual = true;
            device->isAuto = false;
            device->workManual = true;
            device->commandId = commandId;
            device->needToSwitchBack = needToSwitchBack;

            if (!switchedAt.empty())
            {
                device->switchedAtStorage = switchedAt;
                device->switchedAt = device->switchedAtStorage.c_str();
            }

            // Set switch timestamp and switch-back duration
            unsigned long currentTime = arduinoAdapter->millis();
            if (turnOn)
            {
                device->timeOn = currentTime;
                device->durationOn = static_cast<uint32_t>(secondsToSwitchBack) * 1000;
            }
            else
            {
                device->timeOff = currentTime;
                device->durationOff = static_cast<uint32_t>(secondsToSwitchBack) * 1000;
            }

            log("DeviceCommandExecutionUseCase: Command executed successfully - device: %s, state: %s",
                deviceUid.c_str(), turnOn ? "ON" : "OFF");

            return true;
        }

        return false;
    }

private:
    /**
     * @brief Find the hardware pin for a device
     * @param deviceUid Device UID to search for
     * @return Pin number if found, -1 otherwise
     */
    int findDevicePin(const std::string &deviceUid)
    {
        // Search through all nodes in module structure
        for (const auto &node : moduleStructure->nodes)
        {
            // Search relays
            for (const auto &relay : node.relays)
            {
                if (relay.uid == deviceUid)
                {
                    return relay.pin;
                }
            }

            // Search pumps (for analog devices)
            for (const auto &pump : node.pumps)
            {
                if (pump.uid == deviceUid)
                {
                    return pump.pin;
                }
            }
        }

        return -1; // Pin not found
    }

    int findDeviceIndex(const std::string &deviceUid)
    {
        std::vector<Device *> devices = deviceModel->getAllDevices();
        for (size_t i = 0; i < devices.size(); i++)
        {
            if (devices[i] && devices[i]->deviceUid && deviceUid == devices[i]->deviceUid)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int resolveRelayPin(int index)
    {
        if (!moduleConfig || index < 0 || index >= moduleConfig->getDeviceCount())
        {
            return -1;
        }
        uint8_t *pins = moduleConfig->getRelayPins();
        return pins ? pins[index] : -1;
    }

    int resolvePumpPin(int index)
    {
        if (!moduleConfig || index < 0 || index >= moduleConfig->getDeviceCount())
        {
            return -1;
        }
        uint8_t *pins = moduleConfig->getPumpPins();
        return pins ? pins[index] : -1;
    }

    /**
     * @brief Execute hardware command based on device type
     * @param device Device to control
     * @param pin Hardware pin number
     * @param turnOn Whether to turn device on or off
     * @return true if command was executed successfully
     */
    bool executeHardwareCommand(Device *device, int index, bool turnOn)
    {
        try
        {
            if (device->deviceType == DEVICE_TYPE_DIGITAL)
            {
                int pin = resolveRelayPin(index);
                if (pin < 0)
                {
                    log("DeviceCommandExecutionUseCase: Relay pin not found for channel: %d", index);
                    return false;
                }

                uint8_t value = turnOn ? arduinoAdapter->getDigitalHigh() : arduinoAdapter->getDigitalLow();
                arduinoAdapter->digitalWrite(pin, value);

                logDrive(device, index, pin, turnOn);
            }
            else if (device->deviceType == DEVICE_TYPE_ANALOG)
            {
                int pin = findDevicePin(device->deviceUid);
                if (pin < 0)
                {
                    log("DeviceCommandExecutionUseCase: Analog pin not found for device: %s", device->deviceUid);
                    return false;
                }

                DeviceAnalogOut *analogDevice = static_cast<DeviceAnalogOut *>(device);

                int analogValue = turnOn ? 255 : 0;

                if (turnOn && analogDevice->analogPinValueWhenTarget > 0)
                {
                    analogValue = static_cast<int>(analogDevice->analogPinValueWhenTarget);
                }

                arduinoAdapter->analogWrite(pin, analogValue);

                log("DeviceCommandExecutionUseCase: Analog write - pin: %d, value: %d", pin, analogValue);
            }
            else if (device->deviceType == DEVICE_TYPE_PUMP)
            {
                if (turnOn)
                {
                    log("DeviceCommandExecutionUseCase: Turning on pump, checking for other active pumps");

                    if (!turnOffAllOtherPumps(device->deviceUid))
                    {
                        log("DeviceCommandExecutionUseCase: Failed to turn off other pumps");
                        return false;
                    }
                }

                int pin = resolvePumpPin(index);
                if (pin < 0)
                {
                    log("DeviceCommandExecutionUseCase: Pump pin not found for channel: %d", index);
                    return false;
                }

                uint8_t value = turnOn ? arduinoAdapter->getDigitalHigh() : arduinoAdapter->getDigitalLow();
                arduinoAdapter->digitalWrite(pin, value);

                logDrive(device, index, pin, turnOn);
            }
            else
            {
                log("DeviceCommandExecutionUseCase: Unknown device type for device: %s", device->deviceUid);
                return false;
            }

            return true;
        }
        catch (...)
        {
            log("DeviceCommandExecutionUseCase: Exception occurred while executing hardware command");
            return false;
        }
    }

    void logDrive(Device *device, int index, int pin, bool turnOn)
    {
        log("DEVICE_COMMAND: drive uid=%s ch=%d pin=%d on=%d",
            (device && device->deviceUid) ? device->deviceUid : "", index, pin, turnOn ? 1 : 0);
    }

    /**
     * @brief Check if device is a pump device
     * @param device Device to check
     * @return true if device is a pump
     */
    bool isPumpDevice(Device *device)
    {
        return (device && device->deviceType == DEVICE_TYPE_PUMP);
    }

    /**
     * @brief Turn off all pumps except the specified one
     * @param excludeUid UID of pump to exclude from turning off
     * @return true if all other pumps were turned off successfully
     */
    bool turnOffAllOtherPumps(const std::string &excludeUid)
    {
        bool allSuccess = true;
        
        // Get all devices and filter for pumps
        std::vector<Device*> allDevices = deviceModel->getAllDevices();
        
        for (Device* device : allDevices)
        {
            if (device && 
                device->deviceType == DEVICE_TYPE_PUMP && 
                strcmp(device->deviceUid, excludeUid.c_str()) != 0 &&
                device->state == DEVICE_STATE_ON)
            {
                log("DeviceCommandExecutionUseCase: Turning off pump: %s", device->deviceUid);

                int index = findDeviceIndex(device->deviceUid);
                int pin = resolvePumpPin(index);
                if (pin >= 0)
                {
                    arduinoAdapter->digitalWrite(pin, arduinoAdapter->getDigitalLow());

                    device->state = DEVICE_STATE_OFF;
                    device->timeOff = arduinoAdapter->millis();

                    logDrive(device, index, pin, false);
                }
                else
                {
                    log("DeviceCommandExecutionUseCase: Pin not found for pump: %s", device->deviceUid);
                    allSuccess = false;
                }
            }
        }
        
        return allSuccess;
    }

    /**
     * @brief Log with debug feature check
     */
    void log(const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::DEVICE_COMMAND_DEBUG) && logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* debugFeature = debugSystem->getFeature(DebugFeatures::DEVICE_COMMAND_DEBUG);
            if (debugFeature)
            {
                logger->println(DebugFeatures::DEVICE_COMMAND_DEBUG, buffer, debugFeature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::DEVICE_COMMAND_DEBUG, buffer);
            }
        }
    }
};
