#pragma once

// HTTPClient answers this when it could not open the connection at all (HTTPC_ERROR_CONNECTION_REFUSED
// in the Arduino library). It is the one failure a restart is known to cure: the download itself
// never started, and the very next attempt after a reboot has always succeeded (GDV1-3834).
constexpr int FIRMWARE_UPDATE_HTTP_NOT_CONNECTED = -1;

class IFirmwareRemoteUpdate {
public:
    virtual ~IFirmwareRemoteUpdate() = default;
    
    /**
     * @brief Update firmware from a given URL
     * 
     * @param url The URL to download firmware from
     * @return true if update was successful, false otherwise
     */
    virtual bool updateFirmwareFromURL(const char* url) = 0;

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
     * @brief HTTP code the last download attempt got back
     *
     * Zero when the attempt never reached the request (no WiFi, bad URL). Negative values are
     * HTTPClient errors, and FIRMWARE_UPDATE_HTTP_NOT_CONNECTED separates "the link is not
     * usable right now" from a server answer such as 404 or 500, which a restart would not fix.
     *
     * @return HTTP code of the last attempt, 0 if no request was made
     */
    virtual int getLastHttpCode() const = 0;
};
