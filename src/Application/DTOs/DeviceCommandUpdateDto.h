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
    std::string timestamp;      // Timestamp when status was updated
    std::string accountUid;     // Account unique identifier

    /**
     * @brief Default constructor
     */
    DeviceCommandUpdateDto() {}

    /**
     * @brief Constructor with all fields
     * @param uid Device UID
     * @param status Command status
     * @param errorMessage Error message (optional)
     * @param timestamp Timestamp (optional)
     * @param accountUid Account UID (optional)
     */
    DeviceCommandUpdateDto(const std::string& uid, const std::string& status, const std::string& errorMessage = "",
                           const std::string& timestamp = "", const std::string& accountUid = "") :
        uid(uid),
        status(status),
        errorMessage(errorMessage),
        timestamp(timestamp),
        accountUid(accountUid) {}
};
