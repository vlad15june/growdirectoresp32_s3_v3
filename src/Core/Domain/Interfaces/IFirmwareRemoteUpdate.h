#pragma once

// HTTPC_ERROR_CONNECTION_REFUSED of the Arduino HTTPClient: the connection was never opened.
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
     * @return Code of the last request, negative for HTTPClient errors, 0 if no request was made
     */
    virtual int getLastHttpCode() const = 0;
};
