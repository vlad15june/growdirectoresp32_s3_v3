#pragma once

#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <string>

/**
 * @brief Implementation of module command execution use case
 */
class ModuleCommandExecutionUseCase : public IModuleCommandExecutionUseCase
{
private:
    ILogger *logger;
    IDebugSystem *debugSystem;
    INTPTimeAdapter *timeAdapter;
    IFirmwareRemoteUpdate *firmwareUpdater;
    static const unsigned long commandMaxAgeSeconds = 300;

public:
    ModuleCommandExecutionUseCase(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr, INTPTimeAdapter *timeAdapter = nullptr, IFirmwareRemoteUpdate *firmwareUpdater = nullptr)
        : logger(logger), debugSystem(debugSystem), timeAdapter(timeAdapter), firmwareUpdater(firmwareUpdater) {}
    
    virtual ~ModuleCommandExecutionUseCase() = default;

    /**
     * @brief Execute module commands from JSON options
     * @param command Command name (e.g., "BlinkingStart")
     * @param options JSON string containing array of module commands
     * @param commandUid Command unique identifier
     * @param createdAt Timestamp when command was created
     * @return true if all commands were executed successfully, false otherwise
     */
    bool execute(const std::string &command, const std::string &options, const std::string &commandUid, const std::string &createdAt) override
    {
        // TODO: Parse JSON options and execute commands
        if (command == "BlinkingStart") {
            return handleBlinkingStart("", options);
        } else if (command == "BlinkingStop") {
            return handleBlinkingStop("", options);
        }
        return false;
    }

    /**
     * @brief Execute multiple module commands
     * @param moduleCommands Vector of module commands to execute
     * @return Vector of execution results
     */
    std::vector<ModuleCommandExecutionResult> executeCommands(const std::vector<ModuleCommandDto>& moduleCommands) override
    {
        std::vector<ModuleCommandExecutionResult> results;
        
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Processing %d module commands", moduleCommands.size());

        // Execute each command one by one
        for (const auto& moduleCommand : moduleCommands)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Processing command - command: %s, commandUid: %s",
                moduleCommand.command.c_str(), moduleCommand.uid.c_str());

            std::string status = "Error";
            std::string errorMessage = "Unknown command";
            bool isExpired = false;

            // Check if command is expired
            if (!moduleCommand.createdAt.empty())
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Checking command time - createdAt: %s", moduleCommand.createdAt.c_str());
                isExpired = !checkCommandTime(moduleCommand.createdAt);
            }

            if (isExpired)
            {
                status = "Error";
                errorMessage = "Command is expired";
            }
            else
            {
                // Process specific commands
                if (moduleCommand.command == "AddWifi")
                {
                    auto result = processAddWifiCommand(moduleCommand);
                    status = result.first;
                    errorMessage = result.second;
                }
                else if (moduleCommand.command == "UpdateFirmware")
                {
                    auto result = processUpdateFirmwareCommand(moduleCommand);
                    status = result.first;
                    errorMessage = result.second;
                }
                else if (moduleCommand.command == "BlinkingStart")
                {
                    bool success = handleBlinkingStart("", moduleCommand.options);
                    status = success ? "Done" : "Error";
                    errorMessage = success ? "" : "BlinkingStart command failed";
                }
                else if (moduleCommand.command == "BlinkingStop")
                {
                    bool success = handleBlinkingStop("", moduleCommand.options);
                    status = success ? "Done" : "Error";
                    errorMessage = success ? "" : "BlinkingStop command failed";
                }
            }

            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Command execution result - command: %s, status: %s, error: %s",
                moduleCommand.command.c_str(), status.c_str(), errorMessage.c_str());

            results.emplace_back(moduleCommand.uid, status, errorMessage);
        }

        return results;
    }

private:
    std::pair<std::string, std::string> processAddWifiCommand(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {"Error", "Missing options"};
        }

        std::string ssid = extractJsonValue(moduleCommand.options, "ssid");
        std::string password = extractJsonValue(moduleCommand.options, "password");

        if (ssid.empty() || password.empty())
        {
            return {"Error", "Missing ssid or password in options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Adding WiFi - SSID: %s", ssid.c_str());

        // TODO: Implement addWifiToFile functionality
        // addWifiToFile(ssid, password);
        
        return {"Done", ""};
    }

    std::pair<std::string, std::string> processUpdateFirmwareCommand(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {"Error", "Missing options"};
        }

        std::string url = extractJsonValue(moduleCommand.options, "url");
        std::string version = extractJsonValue(moduleCommand.options, "version");

        if (url.empty() || version.empty())
        {
            return {"Error", "Missing url or version in options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Firmware update - URL: %s, Version: %s, UID: %s",
            url.c_str(), version.c_str(), moduleCommand.uid.c_str());

        if (!firmwareUpdater)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Firmware updater not available");
            return {"Error", "Firmware updater not available"};
        }

        // Execute firmware update
        bool updateResult = firmwareUpdater->updateFirmwareFromURL(url.c_str());
        
        if (updateResult)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Firmware update completed successfully");
            return {"Done", ""};
        }
        else
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Firmware update failed to start");
            return {"Error", "Failed to start firmware update"};
        }
    }

    /**
     * @brief Handle BlinkingStart command
     * @param uid Module unique identifier
     * @param commandOptions JSON object with command-specific options
     * @return true if command was executed successfully, false otherwise
     */
    bool handleBlinkingStart(const std::string &uid, const std::string &commandOptions)
    {
        // TODO: Implement blinking start logic
        void* module = getModuleByUid(uid);
        if (!module) {
            return false;
        }
        // Start blinking logic here
        return true;
    }

    /**
     * @brief Handle BlinkingStop command
     * @param uid Module unique identifier
     * @param commandOptions JSON object with command-specific options
     * @return true if command was executed successfully, false otherwise
     */
    bool handleBlinkingStop(const std::string &uid, const std::string &commandOptions)
    {
        // TODO: Implement blinking stop logic
        void* module = getModuleByUid(uid);
        if (!module) {
            return false;
        }
        // Stop blinking logic here
        return true;
    }

    /**
     * @brief Get module by UID
     * @param uid Module unique identifier
     * @return Pointer to module or nullptr if not found
     */
    void* getModuleByUid(const std::string &uid)
    {
        // TODO: Implement module lookup by UID
        return nullptr;
    }

    bool checkCommandTime(const std::string& createdDate)
    {
        if (createdDate.empty())
        {
            logError("ModuleCommandUseCase: Empty createdDate provided");
            return false;
        }

        if (!timeAdapter)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: No time adapter available for validation");
            return true; // Allow command if no time validation possible
        }

        unsigned long currentEpoch = timeAdapter->getEpochTime();
        if (currentEpoch == 0)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Could not get current time from adapter");
            return true; // Allow command if time cannot be retrieved
        }

        // TODO: Implement proper timestamp parsing (similar to CommandService)
        // For now, just return true
        return true;
    }

    std::string extractJsonValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return "";
        size_t valueStart = json.find("\"", colonPos);
        if (valueStart == std::string::npos) return "";
        size_t valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd == std::string::npos) return "";
        return json.substr(valueStart + 1, valueEnd - valueStart - 1);
    }

    void log(const char *feature, const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(feature))
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            if (logger) logger->println(feature, buffer);
        }
    }

    void logError(const char *format, ...)
    {
        if (logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println("ERROR", buffer);
        }
    }
};
