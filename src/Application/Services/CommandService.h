#pragma once

#include "Application/Interfaces/ICommandService.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/RawIoResultDto.h"
#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorCommandUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Application/Services/Rules/RuleValidator.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <cstdarg>
#include <cstdio>

class CommandService : public ICommandService
{
private:
    IDeviceCommandExecutionUseCase *deviceCommandUseCase;
    IModuleCommandExecutionUseCase *moduleCommandUseCase;
    ISensorCommandUseCase *sensorCommandUseCase;
    IHybridAPIService *hybridApiService;
    ModuleStructure *moduleStructure;
    ILogger *logger;
    IDebugSystem *debugSystem;
    INTPTimeAdapter *timeAdapter;
    IJsonConverter *jsonConverter;
    IRemoteSensorReceiver *_remoteSensorReceiver = nullptr;
    IRuleRepository *ruleRepository;
    ISettings *settings;
    RuleValidator _ruleValidator;
    static const unsigned long commandMaxAgeSeconds = 300;

    std::string accountUid()
    {
        if (!settings)
        {
            return "";
        }
        char buffer[256];
        buffer[0] = '\0';
        settings->getAccountUid(buffer);
        return std::string(buffer);
    }

public:
    CommandService(
        IDeviceCommandExecutionUseCase *deviceCommandUseCase,
        IModuleCommandExecutionUseCase *moduleCommandUseCase,
        ISensorCommandUseCase *sensorCommandUseCase,
        IHybridAPIService *hybridApiService,
        ModuleStructure *moduleStructure,
        ILogger *logger,
        IDebugSystem *debugSystem,
        INTPTimeAdapter *timeAdapter = nullptr,
        IJsonConverter *jsonConverter = nullptr,
        IRuleRepository *ruleRepository = nullptr,
        ISettings *settings = nullptr)
        : deviceCommandUseCase(deviceCommandUseCase),
          moduleCommandUseCase(moduleCommandUseCase),
          sensorCommandUseCase(sensorCommandUseCase),
          hybridApiService(hybridApiService),
          moduleStructure(moduleStructure),
          logger(logger),
          debugSystem(debugSystem),
          timeAdapter(timeAdapter),
          jsonConverter(jsonConverter),
          ruleRepository(ruleRepository),
          settings(settings),
          _ruleValidator(logger, debugSystem) {}

    void setRemoteSensorReceiver(IRemoteSensorReceiver *remoteSensorReceiver)
    {
        _remoteSensorReceiver = remoteSensorReceiver;
    }

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
        else if (command.isDeleteModuleCommand())
        {
            return processDeleteModuleCommand(command);
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

        return timeAdapter->isCommandTimeValid(createdDate.c_str(), commandMaxAgeSeconds);
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
                    if (timeAdapter)
                        updateDto.timestamp = timeAdapter->getFormattedTime();
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
                    deviceCommand.id,
                    deviceCommand.needToSwitchBack,
                    deviceCommand.secondsToSwitchBack,
                    timeAdapter ? timeAdapter->getFormattedTime() : ""
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
                if (timeAdapter)
                    updateDto.timestamp = timeAdapter->getFormattedTime();
                hybridApiService->sendDeviceCommandExecutionResult(updateDto);
            }
            else
            {
                logError("CommandService: DeviceCommandExecutionUseCase not available");
                DeviceCommandUpdateDto updateDto;
                updateDto.uid = deviceCommand.uid;
                updateDto.status = "error";
                updateDto.errorMessage = "Device command execution service not available";
                if (timeAdapter)
                    updateDto.timestamp = timeAdapter->getFormattedTime();
                hybridApiService->sendDeviceCommandExecutionResult(updateDto);
                allCommandsSuccessful = false;
            }
        }

        return allCommandsSuccessful;
    }

    const char *ruleModeName(RuleMode mode) const
    {
        return mode == RuleMode::ON ? "On" : (mode == RuleMode::OFF ? "Off" : "WaitingForUserConfirm");
    }

    bool processRuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::RULE_DEBUG,
            "CommandService: RULE_PUSH recv ruleUid=%s len=%d",
            command.ruleUid.c_str(), (int)command.rulesPayload.size());

        if (!jsonConverter || !ruleRepository)
        {
            log(DebugFeatures::RULE_DEBUG,
                "CommandService: RULE_PUSH drop reason=deps ruleUid=%s", command.ruleUid.c_str());
            return true;
        }

        std::vector<Rule> rules;
        if (!jsonConverter->rulesFromJson(command.rulesPayload, rules) || rules.empty())
        {
            log(DebugFeatures::RULE_DEBUG,
                "CommandService: RULE_PUSH drop reason=%s ruleUid=%s",
                rules.empty() ? "empty" : "parse", command.ruleUid.c_str());
            return true;
        }

        std::string timestamp = timeAdapter ? timeAdapter->getFormattedTime() : "";
        int applied = 0;
        int errors = 0;

        for (const auto &rule : rules)
        {
            RuleValidationResult vr = _ruleValidator.validateRule(rule);

            RuleStateDto ackItem;
            bool emitAck = true;

            if (vr.isValid)
            {
                int synced = ruleRepository->synchronizeRules({rule});
                if (synced > 0)
                {
                    ackItem = RuleStateDto(rule.uid, "Synchronized", ruleModeName(rule.mode),
                                           rule.modeReason, "", rule.version);
                    applied++;
                    log(DebugFeatures::RULE_DEBUG,
                        "CommandService: RULE_PUSH applied ruleUid=%s version=%d state=Synchronized",
                        rule.uid.c_str(), rule.version);
                }
                else
                {
                    emitAck = false;
                    errors++;
                    log(DebugFeatures::RULE_DEBUG,
                        "CommandService: RULE_PUSH store_fail ruleUid=%s version=%d",
                        rule.uid.c_str(), rule.version);
                }
            }
            else
            {
                ackItem = RuleStateDto(rule.uid, "Error", ruleModeName(rule.mode),
                                       vr.errorMessage, "wrong_format", rule.version);
                errors++;
                log(DebugFeatures::RULE_DEBUG,
                    "CommandService: RULE_PUSH error ruleUid=%s version=%d errorType=wrong_format",
                    rule.uid.c_str(), rule.version);
            }

            if (emitAck && hybridApiService)
            {
                ackItem.accountUid = accountUid();
                std::string json = jsonConverter->ruleStateUpdateSingleToJson(ackItem, timestamp);
                log(DebugFeatures::RULE_DEBUG,
                    "CommandService: RULE_PUSH ack uid=%s body=%s", ackItem.uid.c_str(), json.c_str());
                hybridApiService->sendByUidRuleStateCommand(ackItem.uid.c_str(), json.c_str());
            }
        }

        if (_remoteSensorReceiver)
        {
            _remoteSensorReceiver->onRuleSetChanged();
        }

        log(DebugFeatures::RULE_DEBUG,
            "CommandService: RULE_PUSH done applied=%d errors=%d", applied, errors);

        return true;
    }

    bool processModuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Processing module command - command: %s",
            command.command.c_str());

        bool isComplex = isComplexModuleCommand(command.command);
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: isComplexModuleCommand('%s') = %s",
            command.command.c_str(), isComplex ? "true" : "false");

        if (isComplex)
        {
            return processComplexModuleCommand(command);
        }

        if (isRawModuleCommand(command.command))
        {
            return processRawModuleCommand(command);
        }

        if (getModuleCommandEnum(command.command) == ModuleCommandEnum::REBOOT_REMOTE_MODULE)
        {
            return processRawModuleCommand(command);
        }

        if (moduleCommandUseCase)
        {
            return moduleCommandUseCase->execute(command.command, command.options, command.commandUid, command.createdAt);
        }
        else
        {
            logError("CommandService: ModuleCommandExecutionUseCase not available");
            return false;
        }
    }

    bool isRawModuleCommand(const std::string& commandName) const
    {
        ModuleCommandEnum command = getModuleCommandEnum(commandName);
        return command == ModuleCommandEnum::RAW_MODBUS_WRITE ||
               command == ModuleCommandEnum::RAW_MODBUS_READ ||
               command == ModuleCommandEnum::RAW_SERIAL_WRITE;
    }

    bool processRawModuleCommand(const CommandDto &command)
    {
        if (!moduleCommandUseCase)
        {
            logError("CommandService: ModuleCommandExecutionUseCase not available for raw command");
            return false;
        }

        RawBusCommandResult result = moduleCommandUseCase->executeRaw(command.command, command.options);

        bool isDone = (result.status == ModuleCommandStatus::DONE);
        std::string status = isDone ? "accepted" : "error";

        CommandResultDto ack(status, command.command, result.errorMessage);
        if (hybridApiService)
        {
            hybridApiService->sendCommandResult(ack, ApiPriority::MQTT_PREFERRED);
        }

        if (result.hasData && hybridApiService)
        {
            RawIoResultDto dataResult(command.command, result.channel, result.dataHex, !isDone, result.errorMessage);
            hybridApiService->sendRawIoResult(dataResult);
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Raw command processed - command: %s, status: %s, hasData: %s",
            command.command.c_str(), status.c_str(), result.hasData ? "true" : "false");

        return isDone;
    }

    bool isComplexModuleCommand(const std::string& commandName) const
    {
        ModuleCommandEnum command = getModuleCommandEnum(commandName);
        return command == ModuleCommandEnum::UPDATE_FIRMWARE ||
               command == ModuleCommandEnum::PING_PIN ||
               command == ModuleCommandEnum::SET_SENSOR_MODEL ||
               command == ModuleCommandEnum::SCAN_SENSORS ||
               command == ModuleCommandEnum::DELETE_SENSORS ||
               command == ModuleCommandEnum::ADD_WIFI;
    }

    std::vector<ModuleCommandDto> validateAndFilterModuleCommands(
        const std::vector<ModuleCommandDto>& moduleCommands,
        bool& allCommandsSuccessful)
    {
        std::vector<ModuleCommandDto> validCommands;
        
        for (const auto& moduleCommand : moduleCommands)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Processing module command - uid: %s, command: %s, createdAt: %s",
                moduleCommand.uid.c_str(), moduleCommand.command.c_str(), moduleCommand.createdAt.c_str());

            if (moduleCommand.createdAt.empty())
            {
                logError("CommandService: Module command has empty createdAt - uid: %s",
                        moduleCommand.uid.c_str());

                ModuleCommandUpdateDto updateDto;
                updateDto.uid = moduleCommand.uid;
                updateDto.status = getModuleCommandStatusName(ModuleCommandStatus::ERROR);
                updateDto.errorMessage = "Command has no timestamp";
                if (timeAdapter)
                {
                    updateDto.timestamp = timeAdapter->getFormattedTime();
                }
                std::vector<ModuleCommandUpdateDto> updates = {updateDto};
                hybridApiService->sendModuleCommandExecutionResult(updates);

                allCommandsSuccessful = false;
                continue;
            }

            bool isTimeValid = checkCommandTime(moduleCommand.createdAt);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Module command time validation - createdAt: %s, isValid: %s",
                moduleCommand.createdAt.c_str(), isTimeValid ? "true" : "false");

            if (!isTimeValid)
            {
                logError("CommandService: Module command is too old - createdAt: %s, maxAge: %lu seconds",
                        moduleCommand.createdAt.c_str(), commandMaxAgeSeconds);

                ModuleCommandUpdateDto updateDto;
                updateDto.uid = moduleCommand.uid;
                updateDto.status = getModuleCommandStatusName(ModuleCommandStatus::ERROR);
                updateDto.errorMessage = "Command expired: too old to execute";
                if (timeAdapter)
                {
                    updateDto.timestamp = timeAdapter->getFormattedTime();
                }
                std::vector<ModuleCommandUpdateDto> updates = {updateDto};
                hybridApiService->sendModuleCommandExecutionResult(updates);

                allCommandsSuccessful = false;
                continue;
            }

            // Command is valid, set to InProgress and add to valid list
            ModuleCommandUpdateDto inProgressDto;
            inProgressDto.uid = moduleCommand.uid;
            inProgressDto.status = getModuleCommandStatusName(ModuleCommandStatus::IN_PROGRESS);
            if (timeAdapter)
            {
                inProgressDto.timestamp = timeAdapter->getFormattedTime();
            }
            std::vector<ModuleCommandUpdateDto> updates = {inProgressDto};
            hybridApiService->sendModuleCommandExecutionResult(updates);

            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Set command to InProgress - uid: %s, command: %s",
                moduleCommand.uid.c_str(), moduleCommand.command.c_str());

            validCommands.push_back(moduleCommand);
        }
        
        return validCommands;
    }

    bool processComplexModuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: processComplexModuleCommand entered - command: %s",
            command.command.c_str());
        
        if (!hybridApiService)
        {
            logError("CommandService: HybridAPIService not available for complex module command");
            return false;
        }

        // Get module commands from API
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Calling hybridApiService->getModuleCommand()");
        std::vector<ModuleCommandDto> moduleCommands = hybridApiService->getModuleCommand();
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: getModuleCommand returned %d command(s)", (int)moduleCommands.size());

        if (moduleCommands.empty())
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: No module commands received from server - returning true");
            return true;
        }

        bool allCommandsSuccessful = true;

        if (moduleCommandUseCase)
        {
            std::vector<ModuleCommandDto> validCommands = validateAndFilterModuleCommands(moduleCommands, allCommandsSuccessful);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: validateAndFilter: %d valid of %d, allSuccess=%s",
                (int)validCommands.size(), (int)moduleCommands.size(),
                allCommandsSuccessful ? "true" : "false");

            // Execute only valid commands
            if (!validCommands.empty())
            {
                std::vector<ModuleCommandExecutionResult> results = moduleCommandUseCase->executeCommands(validCommands);
                std::vector<ModuleCommandUpdateDto> updates;
                bool structureChanged = false;
                
                for (const auto& result : results)
                {
                    ModuleCommandUpdateDto updateDto;
                    updateDto.uid = result.uid;
                    updateDto.status = result.status;
                    updateDto.result = result.result;
                    if (timeAdapter)
                    {
                        updateDto.timestamp = timeAdapter->getFormattedTime();
                    }
                    
                    ModuleCommandStatus status = getModuleCommandStatus(result.status);
                    if (status == ModuleCommandStatus::ERROR)
                    {
                        log(DebugFeatures::MODULE_COMMAND_DEBUG,
                            "CommandService: Command uid=%s returned ERROR: %s",
                            result.uid.c_str(), result.errorMessage.c_str());
                        updateDto.errorMessage = result.errorMessage;
                        allCommandsSuccessful = false;
                    }
                    else if (status == ModuleCommandStatus::DONE)
                    {
                        // Find the corresponding command by uid to check command type
                        for (const auto& cmd : validCommands)
                        {
                            if (cmd.uid == result.uid)
                            {
                                ModuleCommandEnum commandEnum = getModuleCommandEnum(cmd.command);
                                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                                    "CommandService: Command completed successfully - type: %s (%d)",
                                    cmd.command.c_str(), static_cast<int>(commandEnum));
                                
                                if (commandEnum == ModuleCommandEnum::SET_SENSOR_MODEL ||
                                    commandEnum == ModuleCommandEnum::SCAN_SENSORS ||
                                    commandEnum == ModuleCommandEnum::DELETE_SENSORS)
                                {
                                    structureChanged = true;
                                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                                        "CommandService: Structure change flag set to TRUE for command: %s",
                                        cmd.command.c_str());
                                }
                                break;
                            }
                        }
                    }
                    updates.push_back(updateDto);

                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "CommandService: Sent result - uid: %s, status: %s, error: %s, sensors count: %d",
                        result.uid.c_str(), result.status.c_str(), result.errorMessage.c_str(), result.result.sensors.size());
                }
                
                if (!updates.empty())
                {
                    hybridApiService->sendModuleCommandExecutionResult(updates);
                }
                
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "CommandService: After all commands - structureChanged: %s, moduleStructure: %p",
                    structureChanged ? "true" : "false", moduleStructure);
                
                // Send updated module structure to server if any command modified sensors
                if (structureChanged && moduleStructure)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "CommandService: Sending updated module structure to server after sensor changes");
                    
                    bool sent = hybridApiService->sendToServerModuleStructure(*moduleStructure, ApiPriority::BOTH);
                    if (sent)
                    {
                        log(DebugFeatures::MODULE_COMMAND_DEBUG,
                            "CommandService: Successfully sent updated module structure");
                    }
                    else
                    {
                        logError("CommandService: Failed to send updated module structure");
                    }
                }
                else if (structureChanged && !moduleStructure)
                {
                    logError("CommandService: Structure changed but moduleStructure is NULL!");
                }
                else if (!structureChanged)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "CommandService: Structure not changed, skipping module structure send");
                }
            }
            else
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "CommandService: No valid module commands to execute (all expired or invalid)");
            }
        }
        else
        {
            logError("CommandService: ModuleCommandExecutionUseCase not available");
            allCommandsSuccessful = false;
        }

        return allCommandsSuccessful;
    }

    //TODO: there is no such command type, deside use or not or delete
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

    bool processDeleteModuleCommand(const CommandDto &command)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Processing delete module command - commandUid: %s, moduleUid: %s",
            command.commandUid.c_str(), command.growDirectorUid.c_str());

        // Send immediate acknowledgment
        CommandResultDto acceptedResult("accepted", "DeleteModule");
        if (hybridApiService)
        {
            bool sent = hybridApiService->sendCommandResult(acceptedResult, ApiPriority::MQTT_PREFERRED);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Delete command acknowledgment sent - success: %s", sent ? "true" : "false");
        }

        // Create module command DTO for execution
        ModuleCommandDto moduleCommand;
        moduleCommand.command = "DeleteModule";
        moduleCommand.uid = command.commandUid;
        moduleCommand.createdAt = command.createdAt;
        moduleCommand.options = "{}"; // No options needed for delete

        if (!moduleCommandUseCase)
        {
            logError("CommandService: ModuleCommandUseCase not available for delete command");
            
            CommandResultDto errorResult("error", "DeleteModule", "Module command service not available");
            if (hybridApiService)
            {
                hybridApiService->sendCommandResult(errorResult, ApiPriority::MQTT_PREFERRED);
            }
            
            return false;
        }

        // Execute the delete command (which includes server verification)
        std::vector<ModuleCommandDto> commands = {moduleCommand};
        std::vector<ModuleCommandExecutionResult> results = moduleCommandUseCase->executeCommands(commands);
        
        if (results.empty())
        {
            logError("CommandService: No result received from delete command execution");
            
            CommandResultDto errorResult("error", "DeleteModule", "Command execution failed");
            if (hybridApiService)
            {
                hybridApiService->sendCommandResult(errorResult, ApiPriority::MQTT_PREFERRED);
            }
            
            return false;
        }

        // Process the result
        const auto& result = results[0];
        ModuleCommandStatus status = getModuleCommandStatus(result.status);
        
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "CommandService: Delete command execution completed - status: %s, error: %s",
            result.status.c_str(), result.errorMessage.c_str());

        CommandResultDto finalResult;
        if (status == ModuleCommandStatus::DONE)
        {
            finalResult = CommandResultDto("done", "DeleteModule", "Module successfully deleted and reset");
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "CommandService: Module deletion and factory reset completed successfully");
        }
        else
        {
            finalResult = CommandResultDto("error", "DeleteModule", result.errorMessage);
            logError("CommandService: Module deletion failed - %s", result.errorMessage.c_str());
        }

        if (hybridApiService)
        {
            hybridApiService->sendCommandResult(finalResult, ApiPriority::MQTT_PREFERRED);
        }

        return (status == ModuleCommandStatus::DONE);
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
        if (sensor.protocol == "ModBus")
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
            std::vector<I2CRegisterConfig> configs;
            if (jsonConverter->i2cRegisterConfigsFromJson(arrayJson, configs)) {
                sensor.i2cRegisterConfigs = configs;
            }
        }
    }

    void log(const char *feature, const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(feature) && logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* debugFeature = debugSystem->getFeature(feature);
            if (debugFeature)
            {
                logger->println(feature, buffer, debugFeature->getLogChannel());
            }
            else
            {
                logger->println(feature, buffer);
            }
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
