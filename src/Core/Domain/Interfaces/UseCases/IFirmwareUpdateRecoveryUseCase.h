#ifndef I_FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
#define I_FIRMWARE_UPDATE_RECOVERY_USE_CASE_H

#include <string>
#include "Application/DTOs/ModuleCommandDto.h"

/**
 * @brief Carries a firmware update over a restart when the link is not usable (GDV1-3834)
 *
 * An update can fail because the module cannot open a connection at all, and it keeps failing
 * that way until it is rebooted. This use case is what turns that into one restart plus one
 * retry the user does not have to ask for: it decides whether to restart, keeps the update
 * across the restart, and hands it back on the next boot.
 */
class IFirmwareUpdateRecoveryUseCase
{
public:
    virtual ~IFirmwareUpdateRecoveryUseCase() = default;

    /**
     * @brief Whether the module reports its HTTP link as down right now
     */
    virtual bool isHttpConnectionDown() = 0;

    /**
     * @brief Keep the update for the next boot and ask the module to restart
     *
     * @return true if the update is kept and a restart was requested; false when nothing was
     *         kept - an update is already being carried, the url does not fit, or the settings
     *         are unavailable - and the caller has to report the failure as it always did
     */
    virtual bool keepForNextBoot(const std::string &url,
                                 const std::string &version,
                                 const std::string &commandUid) = 0;

    /**
     * @brief Take the update kept before the restart, as the command it came from
     *
     * Consumes the in-progress marker before returning, so an update that is interrupted again
     * is not taken a second time.
     *
     * @return true if there was an update to resume and @p command was filled
     */
    virtual bool takeKeptUpdate(ModuleCommandDto &command) = 0;

    /**
     * @brief Stop carrying the update, whatever the resumed attempt ended with
     */
    virtual void finishKeptUpdate() = 0;
};

#endif // I_FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
