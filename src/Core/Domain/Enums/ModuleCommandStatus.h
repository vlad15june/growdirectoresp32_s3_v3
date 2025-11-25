#ifndef MODULE_COMMAND_STATUS_H
#define MODULE_COMMAND_STATUS_H

#include <string>

enum class ModuleCommandStatus {
    NEW,
    IN_PROGRESS,
    ERROR,
    DONE
};

// Utility function to get string representation of ModuleCommandStatus
inline const char* getModuleCommandStatusName(ModuleCommandStatus status) {
    switch (status) {
        case ModuleCommandStatus::NEW:
            return "New";
        case ModuleCommandStatus::IN_PROGRESS:
            return "InProgress";
        case ModuleCommandStatus::ERROR:
            return "Error";
        case ModuleCommandStatus::DONE:
            return "Done";
        default:
            return "Unknown";
    }
}

// Utility function to get ModuleCommandStatus from string
inline ModuleCommandStatus getModuleCommandStatus(const std::string& statusStr) {
    if (statusStr == "New") {
        return ModuleCommandStatus::NEW;
    } else if (statusStr == "InProgress") {
        return ModuleCommandStatus::IN_PROGRESS;
    } else if (statusStr == "Error") {
        return ModuleCommandStatus::ERROR;
    } else if (statusStr == "Done") {
        return ModuleCommandStatus::DONE;
    }
    return ModuleCommandStatus::NEW;
}

#endif // MODULE_COMMAND_STATUS_H
