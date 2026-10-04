#pragma once

/**
 * @brief Confirms a freshly booted firmware image once the module proves it can do its job.
 */
class IConfirmRunningFirmwareUseCase {
public:
    virtual ~IConfirmRunningFirmwareUseCase() = default;

    /**
     * @brief One completed iteration of the CONFIGURED_DONE control loop
     *
     * Called by the loop service once per pass. The implementation decides when enough
     * iterations amount to "the module works" and confirms the image exactly once.
     */
    virtual void onControlLoopIteration() = 0;
};
