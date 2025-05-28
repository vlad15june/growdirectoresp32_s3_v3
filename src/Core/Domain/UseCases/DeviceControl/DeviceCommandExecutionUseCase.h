#pragma once

#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include <string>
#include <cstring>
#include <cstdarg>
#include <cstdio>

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

public:
    /**
     * @brief Constructor
     * @param deviceModel Device model for accessing devices
     * @param arduinoAdapter Arduino adapter for hardware control
     * @param moduleStructure Module structure for pin mapping
     * @param logger Logger instance
     * @param debugSystem Debug system instance
     */
    DeviceCommandExecutionUseCase(
        IDeviceModel *deviceModel,
        IArduinoAdapter *arduinoAdapter,
        ModuleStructure *moduleStructure,
        ILogger *logger,
        IDebugSystem *debugSystem)
        : deviceModel(deviceModel),
          arduinoAdapter(arduinoAdapter),
          moduleStructure(moduleStructure),
          logger(logger),
          debugSystem(debugSystem) {}

    /**
     * @brief Execute a device command
     * @param deviceUid Device unique identifier
     * @param requestedState Requested state (on/off)
     * @param commandId Command identifier
     * @return true if command was executed successfully, false otherwise
     */
    bool execute(const std::string &deviceUid, const std::string &requestedState, int commandId) override
    {
        log("DeviceCommandExecutionUseCase: Executing command - deviceUid: %s, state: %s, commandId: %d",
            deviceUid.c_str(), requestedState.c_str(), commandId);

        // Find the device
        Device *device = deviceModel->getDeviceByUid(deviceUid.c_str());
        if (!device)
        {
            log("DeviceCommandExecutionUseCase: Device not found: %s", deviceUid.c_str());
            return false;
        }

        // Find the hardware pin for this device
        int pin = findDevicePin(deviceUid);
        if (pin < 0)
        {
            log("DeviceCommandExecutionUseCase: Pin not found for device: %s", deviceUid.c_str());
            return false;
        }

        // Parse requested state
        bool turnOn = (requestedState == "on" || requestedState == "On" || requestedState == "ON");

        // Execute the hardware command
        if (executeHardwareCommand(device, pin, turnOn))
        {
            // Update device state
            device->state = turnOn ? DEVICE_STATE_ON : DEVICE_STATE_OFF;
            device->isManual = true;
            device->isAuto = false;
            device->commandId = commandId;

            // Set switch timestamp
            unsigned long currentTime = arduinoAdapter->millis();
            if (turnOn)
            {
                device->timeOn = currentTime;
            }
            else
            {
                device->timeOff = currentTime;
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

    /**
     * @brief Execute hardware command based on device type
     * @param device Device to control
     * @param pin Hardware pin number
     * @param turnOn Whether to turn device on or off
     * @return true if command was executed successfully
     */
    bool executeHardwareCommand(Device *device, int pin, bool turnOn)
    {
        try
        {
            if (device->deviceType == DEVICE_TYPE_DIGITAL)
            {
                // Digital device (relay)
                uint8_t value = turnOn ? arduinoAdapter->getDigitalHigh() : arduinoAdapter->getDigitalLow();
                arduinoAdapter->digitalWrite(pin, value);

                log("DeviceCommandExecutionUseCase: Digital write - pin: %d, value: %d", pin, value);
            }
            else if (device->deviceType == DEVICE_TYPE_ANALOG)
            {
                // Analog device (PWM)
                DeviceAnalogOut *analogDevice = static_cast<DeviceAnalogOut *>(device);
                int analogValue = turnOn ? 255 : 0; // Full on or off for simple command

                // You might want to use specific analog values based on device configuration
                if (turnOn && analogDevice->analogPinValueWhenTarget > 0)
                {
                    analogValue = static_cast<int>(analogDevice->analogPinValueWhenTarget);
                }

                arduinoAdapter->analogWrite(pin, analogValue);

                log("DeviceCommandExecutionUseCase: Analog write - pin: %d, value: %d", pin, analogValue);
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

    /**
     * @brief Log with debug feature check
     */
    void log(const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::DEVICE_COMMAND_DEBUG))
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(DebugFeatures::DEVICE_COMMAND_DEBUG, buffer);
        }
    }
};
