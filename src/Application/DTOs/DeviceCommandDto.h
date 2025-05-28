#pragma once

#include <string>

/**
 * @brief Data Transfer Object for device command information
 */
class DeviceCommandDto {
public:
    int id;                             // Command unique identifier
    std::string uid;                    // Device unique identifier
    std::string requestedState;         // Requested state (on/off)
    int analogControlPinValue;          // Analog control pin value (for PWM devices)
    std::string status;                 // Command status (pending, in_progress, completed, failed, etc.)
    bool needToSwitchBack;              // Whether device needs to switch back after operation
    int secondsToSwitchBack;            // Seconds until device switches back
    std::string switchType;             // Type of switch operation
    std::string errorMessage;           // Error message if command failed
    std::string createdDate;            // When the command was created (ISO format)
    std::string updatedDate;            // When the command was last updated (ISO format)
    
    /**
     * @brief Default constructor
     */
    DeviceCommandDto() : 
        id(0), 
        analogControlPinValue(0), 
        needToSwitchBack(false), 
        secondsToSwitchBack(0) {}
    
    /**
     * @brief Constructor with basic fields
     * @param id Command ID
     * @param uid Device UID
     * @param requestedState Requested state
     * @param status Command status
     */
    DeviceCommandDto(int id, const std::string& uid, const std::string& requestedState, const std::string& status) :
        id(id),
        uid(uid),
        requestedState(requestedState),
        analogControlPinValue(0),
        status(status),
        needToSwitchBack(false),
        secondsToSwitchBack(0) {}
    
    /**
     * @brief Check if this is an analog control command
     * @return true if analogControlPinValue is set
     */
    bool isAnalogCommand() const {
        return analogControlPinValue > 0;
    }
    
    /**
     * @brief Check if command has an error
     * @return true if errorMessage is not empty
     */
    bool hasError() const {
        return !errorMessage.empty();
    }
    
    /**
     * @brief Check if command is completed
     * @return true if status indicates completion
     */
    bool isCompleted() const {
        return status == "completed" || status == "done";
    }
    
    /**
     * @brief Check if command is pending
     * @return true if status indicates pending
     */
    bool isPending() const {
        return status == "pending" || status == "in_progress";
    }
    
    /**
     * @brief Check if command failed
     * @return true if status indicates failure
     */
    bool isFailed() const {
        return status == "failed" || status == "error";
    }
};
