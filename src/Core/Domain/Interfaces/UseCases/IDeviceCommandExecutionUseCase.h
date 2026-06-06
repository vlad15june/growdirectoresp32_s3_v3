#pragma once

#include <string>

/**
 * @brief Interface for device command execution use case
 */
class IDeviceCommandExecutionUseCase
{
public:
    virtual ~IDeviceCommandExecutionUseCase() = default;

    /**
     * @brief Execute a device command
     * @param deviceUid Device unique identifier
     * @param requestedState Requested state (on/off)
     * @param commandId Command identifier
     * @return true if command was executed successfully, false otherwise
     */
    virtual bool execute(const std::string &deviceUid, const std::string &requestedState, int commandId, bool needToSwitchBack = false, int secondsToSwitchBack = 0, const std::string &switchedAt = "") = 0;
};
