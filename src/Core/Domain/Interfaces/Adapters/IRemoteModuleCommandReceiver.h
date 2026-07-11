#ifndef I_REMOTE_MODULE_COMMAND_RECEIVER_H
#define I_REMOTE_MODULE_COMMAND_RECEIVER_H

class IRemoteModuleCommandReceiver
{
public:
    virtual ~IRemoteModuleCommandReceiver() = default;

    virtual void init() = 0;
};

#endif // I_REMOTE_MODULE_COMMAND_RECEIVER_H
