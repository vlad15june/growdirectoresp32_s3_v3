#pragma once

#include <string>

/**
 * @brief Data Transfer Object for module command information
 */
class ModuleCommandDto {
public:
    std::string uid;                    // Module unique identifier
    std::string command;                // Command name (e.g., "BlinkingStart")
    std::string options;                // JSON string with command options
    std::string createdAt;              // When the command was created (ISO format)
    
    /**
     * @brief Default constructor
     */
    ModuleCommandDto() {}
    
    /**
     * @brief Constructor with all fields
     * @param uid Module UID
     * @param command Command name
     * @param options JSON options string
     * @param createdAt Creation timestamp
     */
    ModuleCommandDto(const std::string& uid, const std::string& command, 
                     const std::string& options, const std::string& createdAt) :
        uid(uid),
        command(command),
        options(options),
        createdAt(createdAt) {}
    
    /**
     * @brief Check if command has options
     * @return true if options string is not empty
     */
    bool hasOptions() const {
        return !options.empty();
    }
};
