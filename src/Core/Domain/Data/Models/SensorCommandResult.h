#pragma once

#include <string>

/**
 * @brief Result of sensor command execution
 */
struct SensorCommandResult {
    bool success;
    std::string status;        // "success", "error", "warning"
    std::string message;       // Detailed message about the operation
    std::string errorCode;     // Optional error code for specific errors

    SensorCommandResult() : success(false), status("error"), message("Unknown error") {}

    SensorCommandResult(bool success, const std::string& status, const std::string& message, const std::string& errorCode = "")
        : success(success), status(status), message(message), errorCode(errorCode) {}

    static SensorCommandResult createSuccess(const std::string& message) {
        return SensorCommandResult(true, "success", message);
    }

    static SensorCommandResult createError(const std::string& message, const std::string& errorCode = "") {
        return SensorCommandResult(false, "error", message, errorCode);
    }

    static SensorCommandResult createWarning(const std::string& message) {
        return SensorCommandResult(true, "warning", message);
    }
};
