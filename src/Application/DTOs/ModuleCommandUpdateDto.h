#pragma once

#include <string>

/**
 * @brief Data Transfer Object for updating module command status
 */
class ModuleCommandUpdateDto {
public:
    std::string uid;            // Module unique identifier
    std::string status;         // Command status (New, etc.)
    std::string errorMessage;   // Error message if command failed
    std::string timestamp;      // Timestamp when status was updated
    std::string accountUid;     // Account unique identifier
    
    /**
     * @brief Default constructor
     */
    ModuleCommandUpdateDto() {}
    
    /**
     * @brief Constructor with all fields
     * @param uid Module UID
     * @param status Command status
     * @param errorMessage Error message (optional)
     * @param timestamp Timestamp (optional)
     * @param accountUid Account UID (optional)
     */
    ModuleCommandUpdateDto(const std::string& uid, const std::string& status, 
                          const std::string& errorMessage = "",
                          const std::string& timestamp = "",
                          const std::string& accountUid = "") :
        uid(uid),
        status(status),
        errorMessage(errorMessage),
        timestamp(timestamp),
        accountUid(accountUid) {}    
};
