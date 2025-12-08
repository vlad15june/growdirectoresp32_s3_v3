#pragma once

#include <string>
#include <vector>

// Forward declaration for sensor result
struct SensorResultDto {
    std::string uid;
    std::string type;
    int pin;
    int group;
    std::string modelUid;
    
    SensorResultDto() : pin(0), group(0) {}
    SensorResultDto(const std::string& uid, const std::string& type, int pin, int group, const std::string& modelUid)
        : uid(uid), type(type), pin(pin), group(group), modelUid(modelUid) {}
};

struct ModuleCommandResult {
    std::vector<SensorResultDto> sensors;
    
    ModuleCommandResult() {}
};

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
    ModuleCommandResult result; // Command execution result (optional)
    
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
