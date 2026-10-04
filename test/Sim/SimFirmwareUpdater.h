#pragma once

// A stand-in for the OTA download, so that a scenario can hold a module "inside a firmware
// download" for minutes of virtual time.
//
// What it substitutes is hardware and SDK: HTTPClient streaming the image and Update writing it
// to flash. What it keeps is the shape of the real FirmwareRemoteUpdate that matters to the
// scheduler and the watchdog: the call blocks its caller for the whole download, and while bytes
// keep arriving it does what `Update.onProgress` does on the device - calls esp_task_wdt_reset(),
// which feeds the watchdog entry of *whichever task is downloading*. The names of those tasks are
// recorded so a test can assert whose liveness the download was vouching for; that attribution is
// the whole point of the watchdog behaviour under test.

#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Sim/HostTaskScheduler.h"

#include <string>
#include <vector>

namespace hostsim {

class SimFirmwareUpdater : public IFirmwareRemoteUpdate {
public:
    explicit SimFirmwareUpdater(HostTaskScheduler& scheduler) : _scheduler(scheduler) {}

    /** How long the whole download takes. The real one has no limit of its own. */
    uint32_t downloadMs = 120000;

    /** How often the stream makes progress - each progress event feeds the watchdog. */
    uint32_t progressEveryMs = 1000;

    /** What updateFirmwareFromURL() answers once the download ends. */
    bool updateResult = true;

    /** The reason reported when updateResult is false. */
    std::string errorMessage;

    /**
     * The FIRMWARE_UPDATE_STATE_* value the session ends with, as the real updater records it:
     * 3 (DONE) on success, 4 (ERROR) on a failed transfer. The caller persists it; this class,
     * like the real one, only reports it.
     */
    uint8_t successStatus = 3;
    uint8_t failureStatus = 4;

    /**
     * The HTTP code the session ends with, as the real updater reports it: 200 on a transfer that
     * happened, and whatever the scenario sets when it did not. FIRMWARE_UPDATE_HTTP_NOT_CONNECTED
     * is the one the caller acts on - it means no connection was opened at all.
     */
    int httpCode = 200;

    // --- What happened -------------------------------------------------------------------------

    bool updateCalled = false;
    std::string lastUrl;
    std::string lastExpectedMd5;

    /**
     * The task on whose stack each progress-time esp_task_wdt_reset() ran. On the device that
     * call feeds the current task's watchdog entry and nobody else's, so this list answers the
     * question the watchdog scenario asks: while the image was coming down, whose liveness did
     * the download vouch for?
     */
    std::vector<std::string> progressFeedTaskNames;

    // --- IFirmwareRemoteUpdate -----------------------------------------------------------------

    bool updateFirmwareFromURL(const char* url, const char* expectedMd5) override
    {
        updateCalled = true;
        lastUrl = (url != nullptr) ? url : "";
        lastExpectedMd5 = (expectedMd5 != nullptr) ? expectedMd5 : "";
        _inProgress = true;

        uint32_t elapsed = 0;
        while (elapsed < downloadMs)
        {
            const uint32_t slice = (downloadMs - elapsed > progressEveryMs)
                                       ? progressEveryMs
                                       : (downloadMs - elapsed);
            _scheduler.delayMs(slice);
            elapsed += slice;

            progressFeedTaskNames.push_back(_scheduler.currentTaskName());
            _scheduler.watchdogFeed();
        }

        _inProgress = false;
        _lastUpdateStatus = updateResult ? successStatus : failureStatus;
        return updateResult;
    }

    bool isUpdateInProgress() override { return _inProgress; }

    const char* getLastErrorMessage() const override { return errorMessage.c_str(); }

    uint8_t getLastUpdateStatus() const override { return _lastUpdateStatus; }

    int getLastHttpCode() const override { return httpCode; }

private:
    HostTaskScheduler& _scheduler;
    bool _inProgress = false;
    uint8_t _lastUpdateStatus = 0;
};

} // namespace hostsim
