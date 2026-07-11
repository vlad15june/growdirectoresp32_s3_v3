#ifndef I_REMOTE_MODULE_COMMAND_SENDER_H
#define I_REMOTE_MODULE_COMMAND_SENDER_H

#include <string>

class IRemoteModuleCommandSender
{
public:
    virtual ~IRemoteModuleCommandSender() = default;

    virtual bool sendReboot(const std::string& targetUid) = 0;
};

#endif // I_REMOTE_MODULE_COMMAND_SENDER_H
