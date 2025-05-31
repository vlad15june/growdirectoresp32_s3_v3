#pragma once

#include "Application/Interfaces/ICommandService.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
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
    INTPTimeAdapter *timeAdapter;
    static const unsigned long commandMaxAgeSeconds = 300; // 5 minutes

public:
    /**
     * @brief Constructor
     * @param deviceCommandUseCase Use case for device command execution
     * @param hybridApiService Hybrid API service for server communication
     * @param logger Logger instance
     * @param debugSystem Debug system instance
     * @param timeAdapter Time adapter for command validation
     */
    CommandService(
        IDeviceCommandExecutionUseCase *deviceCommandUseCase,
        IHybridAPIService *hybridApiService,
        ILogger *logger,
        IDebugSystem *debugSystem,
        INTPTimeAdapter *timeAdapter = nullptr)
        : deviceCommandUseCase(deviceCommandUseCase),
          hybridApiService(hybridApiService),
          logger(logger),
          debugSystem(debugSystem),
          timeAdapter(timeAdapter) {}

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
     * @brief Check if command time is valid based on creation timestamp
     * @param createdDate ISO timestamp from server (e.g., "2022-12-16T06:02:42.262074+00:00")
     * @return true if command is within acceptable time window, false otherwise
     */
    bool checkCommandTime(const std::string& createdDate)
    {
        if (createdDate.empty())
        {
            logError("CommandService: Empty createdDate provided");
            return false;
        }

        // Get current time in seconds from time adapter
        unsigned long currentTimeSeconds = 0;
        if (timeAdapter)
        {
            unsigned long currentEpoch = timeAdapter->getEpochTime();
            if (currentEpoch > 0)
            {
                // Convert epoch to seconds of day for comparison
                currentTimeSeconds = (currentEpoch % 86400); // 86400 seconds in a day
            }
            else
            {
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Could not get current time from adapter, using fallback");
                return false; // If we can't get time, reject for safety
            }
        }
        else
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: No time adapter available for validation");
            return false; // No time adapter, reject for safety
        }

        // Parse ISO timestamp: "2022-12-16T06:02:42.262074+00:00"
        std::string createdDateStr = createdDate;
        
        // Find T separator
        size_t splitT = createdDateStr.find("T");
        if (splitT == std::string::npos)
        {
            logError("CommandService: Invalid timestamp format - missing T separator");
            return false;
        }

        // Find decimal point for milliseconds
        size_t splitPoint = createdDateStr.find(".", splitT);
        if (splitPoint == std::string::npos)
        {
            // No milliseconds, find timezone or end
            splitPoint = createdDateStr.find("+", splitT);
            if (splitPoint == std::string::npos)
            {
                splitPoint = createdDateStr.find("Z", splitT);
                if (splitPoint == std::string::npos)
                {
                    splitPoint = createdDateStr.length();
                }
            }
        }

        // Extract time portion (HH:MM:SS)
        std::string timeStamp = createdDateStr.substr(splitT + 1, splitPoint - splitT - 1);
        
        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Extracted timeStamp: %s", timeStamp.c_str());

        // Parse hours, minutes, seconds
        if (timeStamp.length() < 8) // Minimum HH:MM:SS
        {
            logError("CommandService: Invalid time format in timestamp");
            return false;
        }

        std::string hoursStr = timeStamp.substr(0, 2);
        std::string minutesStr = timeStamp.substr(3, 2);
        std::string secondsStr = timeStamp.substr(6, 2);

        int hoursInt = std::stoi(hoursStr);
        int minutesInt = std::stoi(minutesStr);
        int secondsInt = std::stoi(secondsStr);

        // Validate parsed values
        if (hoursInt < 0 || hoursInt > 23 || minutesInt < 0 || minutesInt > 59 || 
            secondsInt < 0 || secondsInt > 59)
        {
            logError("CommandService: Invalid time values - H:%d M:%d S:%d", 
                     hoursInt, minutesInt, secondsInt);
            return false;
        }

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: timeStamp = %s, createdDate h=%d, min=%d, sec=%d",
            timeStamp.c_str(), hoursInt, minutesInt, secondsInt);

        // Convert command time to seconds of day
        unsigned long commandTimeSeconds = hoursInt * 3600 + minutesInt * 60 + secondsInt;

        // Calculate time difference
        long differenceTime = static_cast<long>(currentTimeSeconds) - static_cast<long>(commandTimeSeconds);
        
        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: diff command time %ld, currentTimeSeconds %lu, commandTimeSeconds %lu",
            differenceTime, currentTimeSeconds, commandTimeSeconds);

        // Handle negative difference (command from future or day rollover)
        if (differenceTime < 0)
        {
            // Check if it's a day rollover case
            long adjustedDifference = differenceTime + 86400; // Add one day
            if (adjustedDifference > 0 && adjustedDifference <= static_cast<long>(commandMaxAgeSeconds))
            {
                differenceTime = adjustedDifference;
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Adjusted for day rollover, new diff: %ld", differenceTime);
            }
            else
            {
                differenceTime = 0;
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: diff command time was less than 0 and set to %ld", differenceTime);
            }
        }

        // Check if difference is within acceptable range
        unsigned long absDifferenceTime = static_cast<unsigned long>(abs(differenceTime));
        bool isValid = absDifferenceTime <= commandMaxAgeSeconds;

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Command time validation - abs difference: %lu seconds, max allowed: %lu seconds, valid: %s",
            absDifferenceTime, commandMaxAgeSeconds, isValid ? "true" : "false");

        return isValid;
    }

    /**
     * @brief Process device command notification
     */
    bool processDeviceCommand(const CommandDto &command)
    {
        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Processing device command - deviceUid: %s, state: %s, commandId: %d",
            command.deviceUid.c_str(), command.requestedState.c_str(), command.commandId);

        // Get device commands from server
        if (!hybridApiService)
        {
            logError("CommandService: HybridAPIService not available");
            return false;
        }

        std::vector<DeviceCommandDto> deviceCommands = hybridApiService->getDeviceCommand();
        
        // Check if we received any commands
        if (deviceCommands.empty())
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: No device commands received from server");
            return true; // Not an error, just no commands available
        }

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Received %d device commands from server", deviceCommands.size());

        bool allCommandsSuccessful = true;

        // Process each command
        for (const auto& deviceCommand : deviceCommands)
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: Processing command - deviceUid: %s, state: %s, commandId: %d",
                deviceCommand.uid.c_str(), deviceCommand.requestedState.c_str(), deviceCommand.id);

            // Validate command time using internal method
            if (!deviceCommand.createdDate.empty())
            {
                bool isTimeValid = checkCommandTime(deviceCommand.createdDate);
                
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Command time validation - createdDate: %s, isValid: %s",
                    deviceCommand.createdDate.c_str(), isTimeValid ? "true" : "false");

                if (!isTimeValid)
                {
                    logError("CommandService: Command is too old - createdDate: %s, maxAge: %lu seconds",
                            deviceCommand.createdDate.c_str(), commandMaxAgeSeconds);

                    // Send error status for expired command
                    DeviceCommandUpdateDto updateDto;
                    updateDto.uid = deviceCommand.uid;
                    updateDto.status = "error";
                    updateDto.errorMessage = "Command expired: too old to execute";
                    hybridApiService->sendDeviceCommandExecutionResult(updateDto);

                    allCommandsSuccessful = false;
                    continue; // Skip this command and process next one
                }
            }
            else
            {
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: No createdDate provided for command %d, proceeding without time validation",
                    deviceCommand.id);
            }

            // Execute the device command using the use case
            if (deviceCommandUseCase)
            {
                bool executionResult = deviceCommandUseCase->execute(
                    deviceCommand.uid, 
                    deviceCommand.requestedState, 
                    deviceCommand.id
                );

                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Device command execution %s for command %d",
                    executionResult ? "successful" : "failed", deviceCommand.id);

                DeviceCommandUpdateDto updateDto;
                updateDto.uid = deviceCommand.uid;
                updateDto.status = executionResult ? "done" : "error";
                if (!executionResult)
                {
                    updateDto.errorMessage = "Device command execution failed";
                    allCommandsSuccessful = false;
                }
                hybridApiService->sendDeviceCommandExecutionResult(updateDto);
            }
            else
            {
                logError("CommandService: DeviceCommandExecutionUseCase not available");
                
                // Send error status for this command
                DeviceCommandUpdateDto updateDto;
                updateDto.uid = deviceCommand.uid;
                updateDto.status = "error";
                updateDto.errorMessage = "Device command execution service not available";
                hybridApiService->sendDeviceCommandExecutionResult(updateDto);
                
                allCommandsSuccessful = false;
            }
        }

        return allCommandsSuccessful;
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
