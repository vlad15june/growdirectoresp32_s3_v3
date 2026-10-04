#ifndef I_FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
#define I_FIRMWARE_UPDATE_RECOVERY_USE_CASE_H

#include <string>
#include "Application/DTOs/ModuleCommandDto.h"

class IFirmwareUpdateRecoveryUseCase
{
public:
    virtual ~IFirmwareUpdateRecoveryUseCase() = default;

    /**
     * @brief Whether the module reports its HTTP link as down right now
     */
    virtual bool isHttpConnectionDown() = 0;

    /**
     * @brief Whether the image the module booted still has to prove itself
     *
     * While it does, the other app slot holds the image the bootloader falls back to - and that
     * slot is what a firmware update writes. No update may start until this answers false,
     * whichever door the update came through.
     */
    virtual bool isRunningImageOnProbation() = 0;

    /**
     * @brief Keep the update for the next boot and ask the module to restart
     *
     * @param command The command as it arrived. Kept whole rather than rebuilt from the fields
     *        this class happens to know: the options may carry more than the url and the version
     *        - the image checksum among them - and an update resumed without it would install an
     *        unverified image, on the bad link where the check is needed most.
     * @return true if the update is kept and a restart was requested; false when nothing was
     *         kept - an update is already being carried this boot, or the store refused it -
     *         and the caller has to report the failure as it always did
     */
    virtual bool keepForNextBoot(const ModuleCommandDto &command) = 0;

    /**
     * @brief Take the update kept before the restart, as the command it came from
     *
     * The stored update is dropped before it is returned, so a boot that dies in the middle of
     * the resumed download does not take it a second time.
     *
     * An update found while the running image is still on probation is not returned here: it is
     * held back until the image is confirmed and comes out of takeDeferredUpdate() then.
     *
     * @return true if there was an update to resume and @p command was filled
     */
    virtual bool takeKeptUpdate(ModuleCommandDto &command) = 0;

    /**
     * @brief Hold an update until the running image is confirmed, instead of executing it now
     *
     * The command is kept in the same file the recovery path uses, so a reset before the
     * confirmation - which on a probation image is a rollback - finds it again on the image the
     * bootloader lands on. No restart is requested: a restart now would be that rollback.
     *
     * @return true if the update is now held, or already was the one being held - the caller
     *         reports it as still in progress; false when nothing was taken, and the caller
     *         must leave the command without an outcome so the server offers it again
     */
    virtual bool holdUntilImageConfirmed(const ModuleCommandDto &command) = 0;

    /**
     * @brief Take the update that was held back at boot, once the running image is confirmed
     *
     * Asked on the control loop. Answers false - without touching the file system or the
     * bootloader bookkeeping - unless an update is actually being held.
     *
     * @return true if the image is confirmed and @p command was filled with the held update
     */
    virtual bool takeDeferredUpdate(ModuleCommandDto &command) = 0;
};

#endif // I_FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
