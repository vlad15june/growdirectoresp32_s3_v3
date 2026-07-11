#ifndef MOCK_REMOTE_MODULE_COMMAND_SENDER_H
#define MOCK_REMOTE_MODULE_COMMAND_SENDER_H

#include <string>
#include "Core/Domain/Interfaces/Udp/IRemoteModuleCommandSender.h"

class MockRemoteModuleCommandSender : public IRemoteModuleCommandSender
{
public:
    int sendRebootCallCount = 0;
    std::string lastTargetUid;
    bool sendRebootReturnValue = true;

    bool sendReboot(const std::string& targetUid) override
    {
        sendRebootCallCount++;
        lastTargetUid = targetUid;
        return sendRebootReturnValue;
    }
};

#endif // MOCK_REMOTE_MODULE_COMMAND_SENDER_H
