#pragma once

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
};
