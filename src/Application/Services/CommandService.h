#pragma once

#include "Application/Interfaces/ICommandService.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorCommandUseCase.h"
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
    ISensorCommandUseCase *sensorCommandUseCase;
    IHybridAPIService *hybridApiService;
    ILogger *logger;
    IDebugSystem *debugSystem;
    INTPTimeAdapter *timeAdapter;
    static const unsigned long commandMaxAgeSeconds = 300; // 5 minutes

public:
    /**
     * @brief Constructor
     * @param deviceCommandUseCase Use case for device command execution
     * @param sensorCommandUseCase Use case for sensor command execution
     * @param hybridApiService Hybrid API service for server communication
     * @param logger Logger instance
     * @param debugSystem Debug system instance
     * @param timeAdapter Time adapter for command validation
     */
    CommandService(
        IDeviceCommandExecutionUseCase *deviceCommandUseCase,
        ISensorCommandUseCase *sensorCommandUseCase,
        IHybridAPIService *hybridApiService,
        ILogger *logger,
        IDebugSystem *debugSystem,
        INTPTimeAdapter *timeAdapter = nullptr)
        : deviceCommandUseCase(deviceCommandUseCase),
          sensorCommandUseCase(sensorCommandUseCase),
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

        // Get current epoch time from time adapter
        unsigned long currentEpoch = 0;
        if (timeAdapter)
        {
            currentEpoch = timeAdapter->getEpochTime();
            if (currentEpoch == 0)
            {
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Could not get current time from adapter");
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

        // Extract date part (YYYY-MM-DD)
        std::string datePart = createdDateStr.substr(0, splitT);
        std::string timePart = createdDateStr.substr(splitT + 1);

        // Remove milliseconds and timezone from time part
        size_t dotIndex = timePart.find(".");
        if (dotIndex != std::string::npos)
        {
            timePart = timePart.substr(0, dotIndex);
        }
        size_t zIndex = timePart.find("Z");
        if (zIndex != std::string::npos)
        {
            timePart = timePart.substr(0, zIndex);
        }
        size_t plusIndex = timePart.find("+");
        if (plusIndex != std::string::npos)
        {
            timePart = timePart.substr(0, plusIndex);
        }

        // Parse date (YYYY-MM-DD)
        size_t firstDash = datePart.find("-");
        size_t secondDash = datePart.find("-", firstDash + 1);
        if (firstDash == std::string::npos || secondDash == std::string::npos)
        {
            logError("CommandService: Invalid date format in timestamp");
            return false;
        }

        std::string yearStr = datePart.substr(0, firstDash);
        std::string monthStr = datePart.substr(firstDash + 1, secondDash - firstDash - 1);
        std::string dayStr = datePart.substr(secondDash + 1);

        // Parse time (HH:MM:SS)
        size_t firstColon = timePart.find(":");
        size_t secondColon = timePart.find(":", firstColon + 1);
        if (firstColon == std::string::npos || secondColon == std::string::npos)
        {
            logError("CommandService: Invalid time format in timestamp");
            return false;
        }

        std::string hoursStr = timePart.substr(0, firstColon);
        std::string minutesStr = timePart.substr(firstColon + 1, secondColon - firstColon - 1);
        std::string secondsStr = timePart.substr(secondColon + 1);

        // Convert to integers
        int year, month, day, hours, minutes, seconds;
        try {
            year = std::stoi(yearStr);
            month = std::stoi(monthStr);
            day = std::stoi(dayStr);
            hours = std::stoi(hoursStr);
            minutes = std::stoi(minutesStr);
            seconds = std::stoi(secondsStr);
        } catch (const std::exception& e) {
            logError("CommandService: Failed to parse timestamp values");
            return false;
        }

        // Validate parsed values
        if (year < 2020 || year > 2050 || month < 1 || month > 12 || 
            day < 1 || day > 31 || hours < 0 || hours > 23 || 
            minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59)
        {
            logError("CommandService: Invalid datetime values - Y:%d M:%d D:%d H:%d Min:%d S:%d", 
                     year, month, day, hours, minutes, seconds);
            return false;
        }

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Parsed command timestamp - %04d-%02d-%02d %02d:%02d:%02d",
            year, month, day, hours, minutes, seconds);

        // Convert command timestamp to epoch time (simplified calculation)
        // Note: This is a basic calculation and doesn't account for leap years, etc.
        // For production use, consider using a proper date/time library
        
        // Days since epoch (1970-01-01) - simplified calculation
        int daysSinceEpoch = 0;
        
        // Add years (approximate - 365 days per year, plus leap years)
        for (int y = 1970; y < year; y++) {
            if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
                daysSinceEpoch += 366; // Leap year
            } else {
                daysSinceEpoch += 365;
            }
        }
        
        // Days in months for current year (non-leap year)
        int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        
        // Check if current year is leap year
        bool isLeapYear = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        if (isLeapYear) {
            daysInMonth[1] = 29;
        }
        
        // Add days for completed months in current year
        for (int m = 1; m < month; m++) {
            daysSinceEpoch += daysInMonth[m - 1];
        }
        
        // Add days in current month
        daysSinceEpoch += (day - 1);
        
        // Convert to seconds and add time
        unsigned long commandEpoch = daysSinceEpoch * 86400UL + hours * 3600UL + minutes * 60UL + seconds;

        // Calculate time difference in seconds
        long timeDifference = static_cast<long>(currentEpoch) - static_cast<long>(commandEpoch);
        unsigned long absoluteDifference = abs(timeDifference);

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Time comparison - current epoch: %lu, command epoch: %lu, diff: %ld seconds",
            currentEpoch, commandEpoch, timeDifference);

        // Check if within allowed time window
        bool isValid = absoluteDifference <= commandMaxAgeSeconds;

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Command time validation - abs difference: %lu seconds, max allowed: %lu seconds, valid: %s",
            absoluteDifference, commandMaxAgeSeconds, isValid ? "true" : "false");

        if (!isValid)
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: Command rejected - time difference (%lu seconds) exceeds maximum allowed (%lu seconds)",
                absoluteDifference, commandMaxAgeSeconds);
        }

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

        // Send accepted status first
        CommandResultDto result("accepted", "SensorType");
        if (hybridApiService)
        {
            hybridApiService->sendCommandResult(result);
        }

        // Execute the sensor command using the use case
        bool success = false;
        if (sensorCommandUseCase)
        {
            //TODO; will return status and message instead of bool
            //TODO: convert to sensor domain model
            success = sensorCommandUseCase->execute(command.sensorType, command.sensorUid);

            if (success)
            {
                //TODO: send CommandResultDto result("done", "SensorType");
                //TODO: send structure to server
                log(DebugFeatures::SETUP_SENSOR_DEBUG,
                    "CommandService: Sensor type command executed successfully");
            }
            else
            {
                //TODO: send CommandResultDto result("error", "SensorType", errorMessage);
                logError("CommandService: Failed to execute sensor type command");
            }
        }
        else
        {
            logError("CommandService: SensorCommandUseCase not available");
        }

        return success;
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
