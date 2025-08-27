#pragma once

#include <string>

/**
 * @brief Interface for module command execution use case
 */
class IModuleCommandExecutionUseCase
{
public:
    virtual ~IModuleCommandExecutionUseCase() = default;

    /**
     * @brief Execute module commands from JSON options
     * @param command Command name (e.g., "BlinkingStart")
     * @param options JSON string containing array of module commands
     * @param commandId Command identifier
     * @param createdAt Timestamp when command was created
     * @return true if all commands were executed successfully, false otherwise
     */
    virtual bool execute(const std::string &command, const std::string &options, int commandId, const std::string &createdAt) = 0;
};
