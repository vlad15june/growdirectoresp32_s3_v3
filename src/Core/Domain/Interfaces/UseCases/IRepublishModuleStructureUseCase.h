#ifndef I_REPUBLISH_MODULE_STRUCTURE_USE_CASE_H
#define I_REPUBLISH_MODULE_STRUCTURE_USE_CASE_H

#include "Application/Interfaces/IOutbox.h"

/**
 * @brief Rebuild the sensor records of the module structure and send it again, right now.
 *
 * The catalog sync depends on this interface and not on the implementation: it only knows that
 * the structure has to go out again, not how it is built or where it is sent (review of PR #17,
 * 2026-08-13).
 */
class IRepublishModuleStructureUseCase
{
public:
    virtual ~IRepublishModuleStructureUseCase() = default;

    /**
     * @brief Rebuild the sensor records and send the structure
     * @param reason short tag that ends up in the log, e.g. "vpd_channel_shift"
     * @return whether the structure was accepted for sending
     */
    virtual bool execute(const char *reason) = 0;

    /**
     * Told, on the control loop, how a structure handed to the outbox ended. The send happens on
     * the network task; whether the backend took it is known only here, and this is where the
     * outcome reaches the log.
     *
     * Default: nothing, for implementations that hand over nothing.
     */
    virtual void onOutboxCompletion(const OutboxCompletion &completion) { (void)completion; }

    /**
     * @brief Is a structure this use case handed over still on its way to the server?
     *
     * What a caller that sends often asks before it sends again: a structure submitted while one
     * is in flight is not merged with it - both go out.
     *
     * Default: never, for implementations that hand over nothing.
     */
    virtual bool isSendPending() const { return false; }
};

#endif // I_REPUBLISH_MODULE_STRUCTURE_USE_CASE_H
