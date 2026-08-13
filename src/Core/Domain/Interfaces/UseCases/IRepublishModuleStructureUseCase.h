#ifndef I_REPUBLISH_MODULE_STRUCTURE_USE_CASE_H
#define I_REPUBLISH_MODULE_STRUCTURE_USE_CASE_H

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
};

#endif // I_REPUBLISH_MODULE_STRUCTURE_USE_CASE_H
