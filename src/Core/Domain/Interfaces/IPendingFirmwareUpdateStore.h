#ifndef I_PENDING_FIRMWARE_UPDATE_STORE_H
#define I_PENDING_FIRMWARE_UPDATE_STORE_H

#include "Application/DTOs/ModuleCommandDto.h"

class IPendingFirmwareUpdateStore
{
public:
    virtual ~IPendingFirmwareUpdateStore() = default;

    /**
     * @brief Store the command, replacing whatever was stored before
     * @return true if it reached the file system
     */
    virtual bool save(const ModuleCommandDto &command) = 0;

    /**
     * @brief Read the stored command
     * @return true if there was one and @p command was filled
     */
    virtual bool load(ModuleCommandDto &command) = 0;

    virtual void clear() = 0;
};

#endif // I_PENDING_FIRMWARE_UPDATE_STORE_H
