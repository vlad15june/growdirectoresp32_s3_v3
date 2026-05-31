#pragma once

#include <string>
#include <vector>
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"

/**
 * @brief Result of module command execution
 */
struct ModuleCommandExecutionResult {
    std::string uid;
    std::string status;
    std::string errorMessage;
    ModuleCommandResult result;
    
    ModuleCommandExecutionResult(const std::string& uid, const std::string& status, const std::string& errorMessage = "")
        : uid(uid), status(status), errorMessage(errorMessage) {}
        
    ModuleCommandExecutionResult(const std::string& uid, const std::string& status, const std::string& errorMessage, const ModuleCommandResult& result)
        : uid(uid), status(status), errorMessage(errorMessage), result(result) {}
};

/**
 * @brief Result of a raw bus command execution
 */
struct RawBusCommandResult {
    ModuleCommandStatus status;
    std::string errorMessage;
    std::string dataHex;
    bool hasData;
    int channel;

    RawBusCommandResult()
        : status(ModuleCommandStatus::ERROR), hasData(false), channel(0) {}

    RawBusCommandResult(ModuleCommandStatus status, const std::string& errorMessage, const std::string& dataHex, bool hasData, int channel)
        : status(status), errorMessage(errorMessage), dataHex(dataHex), hasData(hasData), channel(channel) {}
};

/**
 * @brief Interface for module command execution use case
 */
class IModuleCommandExecutionUseCase
{
public:
    virtual ~IModuleCommandExecutionUseCase() = default;

    /**
     * @brief Execute a single module command
     * @param command Command name (e.g., "BlinkingStart")
     * @param options JSON string containing command options
     * @param commandUid Command unique identifier
     * @param createdAt Timestamp when command was created
     * @return true if command was executed successfully, false otherwise
     */
    virtual bool execute(const std::string &command, const std::string &options, const std::string &commandUid, const std::string &createdAt) = 0;

    /**
     * @brief Execute multiple module commands
     * @param moduleCommands Vector of module commands to execute
     * @return Vector of execution results
     */
    virtual std::vector<ModuleCommandExecutionResult> executeCommands(const std::vector<ModuleCommandDto>& moduleCommands) = 0;

    /**
     * @brief Execute a raw bus command (ModbusWrite/ModbusRead/SerialWrite) inline
     * @param command Command name
     * @param options JSON string containing command parameters
     * @return RawBusCommandResult with status and optional returned data
     */
    virtual RawBusCommandResult executeRaw(const std::string &command, const std::string &options) = 0;
};
