#pragma once

#include "Application/Interfaces/ICommandService.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <cstdarg>
#include <cstdio>

/**
 * @brief Implementation of command processing service
 */
class CommandService : public ICommandService
{
private:
    IDeviceCommandExecutionUseCase *deviceCommandUseCase;
    IHybridAPIService *hybridApiService;
    ILogger *logger;
    IDebugSystem *debugSystem;

public:
    /**
     * @brief Constructor
     * @param deviceCommandUseCase Use case for device command execution
     * @param hybridApiService Hybrid API service for server communication
     * @param logger Logger instance
     * @param debugSystem Debug system instance
     */
    CommandService(
        IDeviceCommandExecutionUseCase *deviceCommandUseCase,
        IHybridAPIService *hybridApiService,
        ILogger *logger,
        IDebugSystem *debugSystem)
        : deviceCommandUseCase(deviceCommandUseCase),
          hybridApiService(hybridApiService),
          logger(logger),
          debugSystem(debugSystem) {}

    /**
     * @brief Process a command DTO
     * @param command Command to process
     * @return true if command was processed successfully, false otherwise
     */
    bool process(const CommandDto &command) override
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Processing command type: %s", command.type.c_str());

        // Route command based on type
        if (command.isDeviceCommand())
        {
            return processDeviceCommand(command);
        }
        else if (command.isRuleCommand())
        {
            return processRuleCommand(command);
        }
        else if (command.isModuleCommand())
        {
            return processModuleCommand(command);
        }
        else if (command.isSensorTypeCommand())
        {
            return processSensorTypeCommand(command);
        }
        else
        {
            logError("CommandService: Unknown command type: %s", command.type.c_str());
            return false;
        }
    }

private:
    /**
     * @brief Process device command notification
     */
    bool processDeviceCommand(const CommandDto &command)
    {
        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Processing device command - deviceUid: %s, state: %s, commandId: %d",
            command.deviceUid.c_str(), command.requestedState.c_str(), command.commandId);

        // Get device command from server
        if (!hybridApiService)
        {
            logError("CommandService: HybridAPIService not available");
            return false;
        }

        DeviceCommandDto deviceCommand = hybridApiService->getDeviceCommand();
        
        // Check if we received a valid command
        if (deviceCommand.uid.empty())
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: No device command received from server");
            return true; // Not an error, just no command available
        }

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Received device command from server - deviceUid: %s, state: %s, commandId: %d",
            deviceCommand.uid.c_str(), deviceCommand.requestedState.c_str(), deviceCommand.id);

        // Execute the device command using the use case
        if (deviceCommandUseCase)
        {
            bool executionResult = deviceCommandUseCase->execute(
                deviceCommand.uid, 
                deviceCommand.requestedState, 
                deviceCommand.id
            );

            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: Device command execution %s",
                executionResult ? "successful" : "failed");

            DeviceCommandUpdateDto updateDto;
            updateDto.uid = deviceCommand.uid;
            updateDto.status = executionResult ? "done" : "error";
            if (!executionResult)
            {
                updateDto.errorMessage = "Device command execution failed";
            }
            hybridApiService->sendDeviceCommandExecutionResult(updateDto);

            return executionResult;
        }

        logError("CommandService: DeviceCommandExecutionUseCase not available");
        return false;
    }

    /**
     * @brief Process rule command notification
     */
    bool processRuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::RULE_DEBUG,
            "CommandService: Processing rule command - ruleUid: %s", command.ruleUid.c_str());

        // TODO: Implement rule command processing
        // This would involve parsing the rulesPayload and updating rules
        logInfo("CommandService: Rule command processing not yet implemented");

        return true; // Return true for now to indicate command was handled
    }

    /**
     * @brief Process module command notification
     */
    bool processModuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Processing module command - command: %s, version: %s",
            command.command.c_str(), command.version.c_str());

        if (command.command == "UpdateFirmware")
        {
            logInfo("CommandService: Firmware update requested - version: %s, url: %s",
                    command.version.c_str(), command.url.c_str());
        }

        return true; // Return true for now to indicate command was handled
    }

    /**
     * @brief Process sensor type command notification
     */
    bool processSensorTypeCommand(const CommandDto &command)
    {
        log(DebugFeatures::SETUP_SENSOR_DEBUG,
            "CommandService: Processing sensor type command - sensorUid: %s, type: %s",
            command.sensorUid.c_str(), command.sensorType.c_str());

        // TODO: Implement sensor type configuration logic
        logInfo("CommandService: Sensor type command processing not yet implemented");

        return true; // Return true for now to indicate command was handled
    }

    /**
     * @brief Log with debug feature check
     */
    void log(const char *feature, const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(feature))
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(feature, buffer);
        }
    }

    /**
     * @brief Log error message
     */
    void logError(const char *format, ...)
    {
        if (logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println("ERROR", buffer);
        }
    }

    /**
     * @brief Log info message
     */
    void logInfo(const char *format, ...)
    {
        if (logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println("INFO", buffer);
        }
    }
};
