#pragma once

#include "Application/DTOs/CommandDto.h"

/**
 * @brief Interface for command processing service
 */
class ICommandService
{
public:
    virtual ~ICommandService() = default;

    /**
     * @brief Process a command DTO
     * @param command Command to process
     * @return true if command was processed successfully, false otherwise
     */
    virtual bool process(const CommandDto &command) = 0;
};
