#ifndef I_CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H
#define I_CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H

class ICheckFirmwareUpdateOnStartupUseCase {
public:
    virtual ~ICheckFirmwareUpdateOnStartupUseCase() = default;

    /**
     * @brief Pull commands on startup and apply a pending UpdateFirmware before the deletion check
     *
     * Fetches module commands over the API, keeps only UpdateFirmware, executes the fresh one
     * through the existing command path (OTA), reports the result back, and returns whether an
     * update was applied (a restart-on-update was requested).
     * @return true if an update was applied (deletion check must be skipped this boot), false otherwise
     */
    virtual bool run() = 0;
};

#endif // I_CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H
