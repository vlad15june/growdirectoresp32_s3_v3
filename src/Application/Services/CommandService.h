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

class CommandService : public ICommandService
{
private:
    IDeviceCommandExecutionUseCase *deviceCommandUseCase;
    ISensorCommandUseCase *sensorCommandUseCase;
    IHybridAPIService *hybridApiService;
    ILogger *logger;
    IDebugSystem *debugSystem;
    INTPTimeAdapter *timeAdapter;
    IJsonConverter *jsonConverter;
    static const unsigned long commandMaxAgeSeconds = 300;

public:
    CommandService(
        IDeviceCommandExecutionUseCase *deviceCommandUseCase,
        ISensorCommandUseCase *sensorCommandUseCase,
        IHybridAPIService *hybridApiService,
        ILogger *logger,
        IDebugSystem *debugSystem,
        INTPTimeAdapter *timeAdapter = nullptr,
        IJsonConverter *jsonConverter = nullptr)
        : deviceCommandUseCase(deviceCommandUseCase),
          sensorCommandUseCase(sensorCommandUseCase),
          hybridApiService(hybridApiService),
          logger(logger),
          debugSystem(debugSystem),
          timeAdapter(timeAdapter),
          jsonConverter(jsonConverter) {}

    bool process(const CommandDto &command) override
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Processing command type: %s", command.type.c_str());

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
    bool checkCommandTime(const std::string& createdDate)
    {
        if (createdDate.empty())
        {
            logError("CommandService: Empty createdDate provided");
            return false;
        }

        unsigned long currentEpoch = 0;
        if (timeAdapter)
        {
            currentEpoch = timeAdapter->getEpochTime();
            if (currentEpoch == 0)
            {
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: Could not get current time from adapter");
                return false;
            }
        }
        else
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: No time adapter available for validation");
            return false;
        }

        std::string createdDateStr = createdDate;
        size_t splitT = createdDateStr.find("T");
        if (splitT == std::string::npos)
        {
            logError("CommandService: Invalid timestamp format - missing T separator");
            return false;
        }

        std::string datePart = createdDateStr.substr(0, splitT);
        std::string timePart = createdDateStr.substr(splitT + 1);

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

        int daysSinceEpoch = 0;
        for (int y = 1970; y < year; y++) {
            if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
                daysSinceEpoch += 366;
            } else {
                daysSinceEpoch += 365;
            }
        }
        int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        bool isLeapYear = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        if (isLeapYear) {
            daysInMonth[1] = 29;
        }
        for (int m = 1; m < month; m++) {
            daysSinceEpoch += daysInMonth[m - 1];
        }
        daysSinceEpoch += (day - 1);
        unsigned long commandEpoch = daysSinceEpoch * 86400UL + hours * 3600UL + minutes * 60UL + seconds;

        long timeDifference = static_cast<long>(currentEpoch) - static_cast<long>(commandEpoch);
        unsigned long absoluteDifference = abs(timeDifference);

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Time comparison - current epoch: %lu, command epoch: %lu, diff: %ld seconds",
            currentEpoch, commandEpoch, timeDifference);

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

    bool processDeviceCommand(const CommandDto &command)
    {
        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Processing device command - deviceUid: %s, state: %s, commandId: %d",
            command.deviceUid.c_str(), command.requestedState.c_str(), command.commandId);

        if (!hybridApiService)
        {
            logError("CommandService: HybridAPIService not available");
            return false;
        }

        std::vector<DeviceCommandDto> deviceCommands = hybridApiService->getDeviceCommand();
        if (deviceCommands.empty())
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: No device commands received from server");
            return true;
        }

        log(DebugFeatures::DEVICE_COMMAND_DEBUG,
            "CommandService: Received %d device commands from server", deviceCommands.size());

        bool allCommandsSuccessful = true;

        for (const auto& deviceCommand : deviceCommands)
        {
            log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                "CommandService: Processing command - deviceUid: %s, state: %s, commandId: %d",
                deviceCommand.uid.c_str(), deviceCommand.requestedState.c_str(), deviceCommand.id);

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

                    DeviceCommandUpdateDto updateDto;
                    updateDto.uid = deviceCommand.uid;
                    updateDto.status = "error";
                    updateDto.errorMessage = "Command expired: too old to execute";
                    hybridApiService->sendDeviceCommandExecutionResult(updateDto);

                    allCommandsSuccessful = false;
                    continue;
                }
            }
            else
            {
                log(DebugFeatures::DEVICE_COMMAND_DEBUG,
                    "CommandService: No createdDate provided for command %d, proceeding without time validation",
                    deviceCommand.id);
            }

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

    bool processRuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::RULE_DEBUG,
            "CommandService: Processing rule command - ruleUid: %s", command.ruleUid.c_str());

        logInfo("CommandService: Rule command processing not yet implemented");

        return true;
    }

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

        return true;
    }

    bool processSensorTypeCommand(const CommandDto &command)
    {
        log(DebugFeatures::SETUP_SENSOR_DEBUG,
            "CommandService: Processing sensor type command - sensorUid: %s, type: %s, model: %s, command: %s",
            command.sensorUid.c_str(), command.sensorType.c_str(), command.model.c_str(), command.command.c_str());

        CommandResultDto result("accepted", "SensorType");
        if (hybridApiService)
        {
            hybridApiService->sendCommandResult(result);
        }

        SensorEntity sensor = createSensorFromCommand(command);

        if (sensor.uid.empty() || sensor.sensorType.empty())
        {
            std::string errorMsg = "Invalid sensor data in command - missing UID or sensor type";
            logError("CommandService: %s", errorMsg.c_str());
            
            CommandResultDto errorResult("error", "SensorType", errorMsg);
            if (hybridApiService)
            {
                hybridApiService->sendCommandResult(errorResult);
            }
            
            return false;
        }

        if (sensorCommandUseCase)
        {
            SensorCommandResult commandResult = sensorCommandUseCase->execute(sensor);

            log(DebugFeatures::SETUP_SENSOR_DEBUG,
                "CommandService: Sensor command result - success: %s, status: %s, message: %s",
                commandResult.success ? "true" : "false",
                commandResult.status.c_str(),
                commandResult.message.c_str());

            CommandResultDto finalResult;
            if (commandResult.success)
            {
                if (commandResult.status == "warning")
                {
                    finalResult = CommandResultDto("warning", "SensorType", commandResult.message);
                }
                else
                {
                    finalResult = CommandResultDto("done", "SensorType", commandResult.message);
                }
                
                log(DebugFeatures::SETUP_SENSOR_DEBUG,
                    "CommandService: Sensor type command executed successfully - %s", commandResult.message.c_str());
            }
            else
            {
                finalResult = CommandResultDto("error", "SensorType", commandResult.message);
                logError("CommandService: Failed to execute sensor type command - %s", commandResult.message.c_str());
            }

            if (hybridApiService)
            {
                hybridApiService->sendCommandResult(finalResult);
            }

            return commandResult.success;
        }
        else
        {
            std::string errorMsg = "SensorCommandUseCase not available";
            logError("CommandService: %s", errorMsg.c_str());
            
            CommandResultDto errorResult("error", "SensorType", errorMsg);
            if (hybridApiService)
            {
                hybridApiService->sendCommandResult(errorResult);
            }
            
            return false;
        }
    }

private:
    SensorEntity createSensorFromCommand(const CommandDto& command)
    {
        SensorEntity sensor;
        sensor.uid = command.sensorUid;
        sensor.sensorType = command.sensorType;
        sensor.model = command.model;
        sensor.createdAt = command.createdAt;
        sensor.isActive = true;
        sensor.isError = false;

        if (!command.options.empty())
        {
            parseSensorOptions(command.options, sensor);
        }

        log(DebugFeatures::SETUP_SENSOR_DEBUG,
            "CommandService: Created sensor entity - UID: %s, Type: %s, Model: %s, Protocol: %s",
            sensor.uid.c_str(), sensor.sensorType.c_str(), sensor.model.c_str(), sensor.protocol.c_str());

        return sensor;
    }

    void parseSensorOptions(const std::string& optionsJson, SensorEntity& sensor)
    {
        sensor.protocol = extractJsonValue(optionsJson, "protocol");
        if (sensor.protocol == "RS485")
        {
            sensor.modbusDeviceAddress = extractJsonIntValue(optionsJson, "modbusDeviceAddress");
            sensor.baudRate = extractJsonIntValue(optionsJson, "baudRate");
            parseModbusRegisterConfig(optionsJson, sensor);
        }
        else if (sensor.protocol == "I2C")
        {
            sensor.i2cAddress = extractJsonValue(optionsJson, "i2cAddress");
            sensor.chipType = extractJsonValue(optionsJson, "chipType");
            parseI2CRegisterConfig(optionsJson, sensor);
        }

        log(DebugFeatures::SETUP_SENSOR_DEBUG,
            "CommandService: Parsed sensor options - Protocol: %s, Address: %s/%d, ChipType: %s",
            sensor.protocol.c_str(), sensor.i2cAddress.c_str(), sensor.modbusDeviceAddress, sensor.chipType.c_str());
    }

    std::string extractJsonValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return "";
        size_t valueStart = json.find("\"", colonPos);
        if (valueStart == std::string::npos) return "";
        size_t valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd == std::string::npos) return "";
        return json.substr(valueStart + 1, valueEnd - valueStart - 1);
    }

    int extractJsonIntValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return 0;
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return 0;
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && std::isspace(json[valueStart])) valueStart++;
        size_t valueEnd = valueStart;
        while (valueEnd < json.size() && (std::isdigit(json[valueEnd]) || json[valueEnd] == '-')) valueEnd++;
        if (valueEnd > valueStart)
        {
            try
            {
                return std::stoi(json.substr(valueStart, valueEnd - valueStart));
            }
            catch (const std::exception& e)
            {
                logError("CommandService: Failed to parse integer value for key '%s'", key.c_str());
            }
        }
        return 0;
    }

    void parseModbusRegisterConfig(const std::string& json, SensorEntity& sensor)
    {
        std::string key = "\"modbusRegisterConfig\"";
        size_t keyPos = json.find(key);
        if (keyPos == std::string::npos) return;

        size_t arrayStart = json.find('[', keyPos);
        size_t arrayEnd = std::string::npos;
        if (arrayStart != std::string::npos) {
            int depth = 1;
            size_t i = arrayStart + 1;
            while (i < json.size() && depth > 0) {
                if (json[i] == '[') depth++;
                else if (json[i] == ']') depth--;
                i++;
            }
            if (depth == 0) arrayEnd = i;
        }
        if (arrayStart == std::string::npos || arrayEnd == std::string::npos) return;

        std::string arrayJson = json.substr(arrayStart, arrayEnd - arrayStart);

        if (jsonConverter) {
            std::vector<ModbusRegisterConfig> configs;
            if (jsonConverter->modbusRegisterConfigsFromJson(arrayJson, configs)) {
                sensor.modbusRegisterConfigs = configs;
            }
        }
    }

    void parseI2CRegisterConfig(const std::string& json, SensorEntity& sensor)
    {
        std::string key = "\"i2cRegisterConfig\"";
        size_t keyPos = json.find(key);
        if (keyPos == std::string::npos) return;

        size_t objStart = json.find('{', keyPos);
        size_t objEnd = std::string::npos;
        if (objStart != std::string::npos) {
            int depth = 1;
            size_t i = objStart + 1;
            while (i < json.size() && depth > 0) {
                if (json[i] == '{') depth++;
                else if (json[i] == '}') depth--;
                i++;
            }
            if (depth == 0) objEnd = i;
        }
        if (objStart == std::string::npos || objEnd == std::string::npos) return;

        std::string objJson = json.substr(objStart, objEnd - objStart);

        if (jsonConverter) {
            std::vector<std::pair<std::string, I2CRegisterConfig>> configs;
            if (jsonConverter->i2cRegisterConfigsFromJson(objJson, configs)) {
                sensor.i2cRegisterConfigs = configs;
            }
        }
    }

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
