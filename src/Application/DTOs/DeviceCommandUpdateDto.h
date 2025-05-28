#pragma once

#include <string>

/**
 * @brief Data Transfer Object for updating device command status
 */
class DeviceCommandUpdateDto {
public:
    std::string uid;            // Device unique identifier
    std::string status;         // Command status (pending, in_progress, completed, failed, etc.)
    std::string errorMessage;   // Error message if command failed
    
    /**
     * @brief Default constructor
     */
    DeviceCommandUpdateDto() {}
    
    /**
     * @brief Constructor with all fields
     * @param uid Device UID
     * @param status Command status
     * @param errorMessage Error message (optional)
     */
    DeviceCommandUpdateDto(const std::string& uid, const std::string& status, const std::string& errorMessage = "") :
        uid(uid),
        status(status),
        errorMessage(errorMessage) {}    
};
