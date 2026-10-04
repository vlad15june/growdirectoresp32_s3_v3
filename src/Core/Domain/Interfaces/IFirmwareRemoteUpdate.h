#pragma once

constexpr int FIRMWARE_UPDATE_HTTP_NOT_CONNECTED = -1;

class IFirmwareRemoteUpdate {
public:
    virtual ~IFirmwareRemoteUpdate() = default;
    
    /**
     * @brief Update firmware from a given URL
     *
     * @param url The URL to download firmware from
     * @param expectedMd5 MD5 digest the downloaded image must have, or nullptr/empty when the
     *        command did not carry one. Without a digest the image is installed on the size the
     *        server declared alone, which a truncated transfer can satisfy; with one, an image
     *        that does not match is refused and the running firmware stays.
     * @return true if update was successful, false otherwise
     */
    virtual bool updateFirmwareFromURL(const char* url, const char* expectedMd5) = 0;

    /**
     * @brief Check whether an OTA update session is currently running
     * @return true if an update is in progress, false otherwise
     */
    virtual bool isUpdateInProgress() = 0;

    /**
     * @brief Why the last update attempt stopped
     *
     * Filled on every failure path of updateFirmwareFromURL() and cleared when a new
     * session starts. The wording is the same one that goes to the OTA log, so
     * "HTTP code=-11" here and `ERROR: HTTP code=-11` there are the same event.
     *
     * @return Reason of the last failure, empty string if the last attempt did not fail
     */
    virtual const char* getLastErrorMessage() const = 0;

    /**
     * @brief The persisted-status value the last update session ended with
     *
     * The value ISettings::setUpdateStatus() expects (FIRMWARE_UPDATE_STATE_*), or 0 when the
     * session ended on a path that never recorded one. The updater does not write it to the
     * settings itself: it runs on the command network task, and the settings sector is written
     * from the control loop - so the updater reports the status here and the caller persists it
     * in its finish step, on the loop, next to the other state writes.
     *
     * @return Status of the last session, 0 if none was recorded
     */
    virtual uint8_t getLastUpdateStatus() const = 0;

    /**
     * @brief HTTP code the last download attempt got back
     * @return Code of the last request, negative for HTTPClient errors, 0 if no request was made
     */
    virtual int getLastHttpCode() const = 0;
};
