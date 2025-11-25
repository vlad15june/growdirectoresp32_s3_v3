#pragma once

#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"
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
    IArduinoAdapter *arduinoAdapter;
    static const unsigned long commandMaxAgeSeconds = 300;

public:
    ModuleCommandExecutionUseCase(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr, INTPTimeAdapter *timeAdapter = nullptr, IFirmwareRemoteUpdate *firmwareUpdater = nullptr, IArduinoAdapter *arduinoAdapter = nullptr)
        : logger(logger), debugSystem(debugSystem), timeAdapter(timeAdapter), firmwareUpdater(firmwareUpdater), arduinoAdapter(arduinoAdapter) {}
    
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
        ModuleCommandEnum commandEnum = getModuleCommandEnum(command);
        
        switch (commandEnum) {
            case ModuleCommandEnum::BLINKING_START:
                return handleBlinkingStart("", options);
            case ModuleCommandEnum::BLINKING_STOP:
                return handleBlinkingStop("", options);
            case ModuleCommandEnum::REBOOT:
                return handleReboot();
            default:
                return false;
        }
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

            ModuleCommandStatus status = ModuleCommandStatus::ERROR;
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
                status = ModuleCommandStatus::ERROR;
                errorMessage = "Command is expired";
            }
            else
            {
                // Process specific commands
                ModuleCommandEnum commandEnum = getModuleCommandEnum(moduleCommand.command);
                
                switch (commandEnum) {
                    case ModuleCommandEnum::ADD_WIFI:
                    {
                        auto result = handleAddWifi(moduleCommand);
                        status = result.first;
                        errorMessage = result.second;
                        break;
                    }
                    case ModuleCommandEnum::UPDATE_FIRMWARE:
                    {
                        auto result = handleUpdateFirmware(moduleCommand);
                        status = result.first;
                        errorMessage = result.second;
                        break;
                    }
                    case ModuleCommandEnum::BLINKING_START:
                    {
                        bool success = handleBlinkingStart("", moduleCommand.options);
                        status = success ? ModuleCommandStatus::DONE : ModuleCommandStatus::ERROR;
                        errorMessage = success ? "" : "BlinkingStart command failed";
                        break;
                    }
                    case ModuleCommandEnum::BLINKING_STOP:
                    {
                        bool success = handleBlinkingStop("", moduleCommand.options);
                        status = success ? ModuleCommandStatus::DONE : ModuleCommandStatus::ERROR;
                        errorMessage = success ? "" : "BlinkingStop command failed";
                        break;
                    }
                    case ModuleCommandEnum::REBOOT:
                    {
                        bool success = handleReboot();
                        status = success ? ModuleCommandStatus::DONE : ModuleCommandStatus::ERROR;
                        errorMessage = success ? "" : "Reboot command failed";
                        break;
                    }
                    case ModuleCommandEnum::PING_PIN:
                    {
                        auto result = handlePingPin(moduleCommand);
                        status = result.first;
                        errorMessage = result.second;
                        break;
                    }
                    case ModuleCommandEnum::SET_SENSOR_MODEL:
                    {
                        auto result = handleSetSensorModel(moduleCommand);
                        status = result.first;
                        errorMessage = result.second;
                        break;
                    }
                    case ModuleCommandEnum::UNKNOWN:
                    default:
                        // Keep default error message
                        break;
                }
            }

            std::string statusStr = getModuleCommandStatusName(status);
            
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Command execution result - command: %s, status: %s, error: %s",
                moduleCommand.command.c_str(), statusStr.c_str(), errorMessage.c_str());

            results.emplace_back(moduleCommand.uid, statusStr, errorMessage);
        }

        return results;
    }

private:
    /**
     * @brief Handle AddWifi command
     * @param moduleCommand Module command DTO with options
     * @return Pair of status and error message
     */
    std::pair<ModuleCommandStatus, std::string> handleAddWifi(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        std::string ssid = extractJsonValue(moduleCommand.options, "ssid");
        std::string password = extractJsonValue(moduleCommand.options, "password");

        if (ssid.empty() || password.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing ssid or password in options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Adding WiFi - SSID: %s", ssid.c_str());

        // TODO: Implement addWifiToFile functionality
        // addWifiToFile(ssid, password);
        
        return {ModuleCommandStatus::DONE, ""};
    }

    /**
     * @brief Handle UpdateFirmware command
     * @param moduleCommand Module command DTO with options
     * @return Pair of status and error message
     */
    std::pair<ModuleCommandStatus, std::string> handleUpdateFirmware(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        std::string url = extractJsonValue(moduleCommand.options, "url");
        std::string version = extractJsonValue(moduleCommand.options, "version");

        if (url.empty() || version.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing url or version in options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Firmware update - URL: %s, Version: %s, UID: %s",
            url.c_str(), version.c_str(), moduleCommand.uid.c_str());

        if (!firmwareUpdater)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Firmware updater not available");
            return {ModuleCommandStatus::ERROR, "Firmware updater not available"};
        }

        // Execute firmware update
        bool updateResult = firmwareUpdater->updateFirmwareFromURL(url.c_str());
        
        if (updateResult)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Firmware update completed successfully");
            return {ModuleCommandStatus::DONE, ""};
        }
        else
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Firmware update failed to start");
            return {ModuleCommandStatus::ERROR, "Failed to start firmware update"};
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
     * @brief Handle PingPin command
     * @param moduleCommand Module command DTO with options
     * @return Pair of status and error message
     */
    std::pair<ModuleCommandStatus, std::string> handlePingPin(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        int pin = extractJsonIntValue(moduleCommand.options, "pin");
        if (pin == 0)
        {
            return {ModuleCommandStatus::ERROR, "Missing or invalid pin in options"};
        }

        if (!arduinoAdapter)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Arduino adapter not available for PingPin");
            return {ModuleCommandStatus::ERROR, "Arduino adapter not available"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: PingPin command - Pin: %d", pin);

        // TODO: Implement actual ping logic
        // For now, just validate the pin exists
        
        return {ModuleCommandStatus::DONE, ""};
    }

    /**
     * @brief Handle SetSensorModel command
     * @param moduleCommand Module command DTO with options
     * @return Pair of status and error message
     */
    std::pair<ModuleCommandStatus, std::string> handleSetSensorModel(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        int pin = extractJsonIntValue(moduleCommand.options, "pin");
        std::string sensorUid = extractJsonValue(moduleCommand.options, "uid");

        if (pin == 0 || sensorUid.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing pin or uid in options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: SetSensorModel command - Pin: %d, SensorUID: %s",
            pin, sensorUid.c_str());

        // TODO: Implement actual sensor model update logic
        
        return {ModuleCommandStatus::DONE, ""};
    }

    /**
     * @brief Handle Reboot command
     * @return true if command was executed successfully, false otherwise
     */
    bool handleReboot()
    {
        if (!arduinoAdapter)
        {
            logError("ModuleCommandUseCase: Arduino adapter not available for reboot");
            return false;
        }
        
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Executing reboot command");
        
        arduinoAdapter->restart();
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

    int extractJsonIntValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return 0;
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return 0;
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && std::isspace(json[valueStart])) valueStart++;
        size_t valueEnd = valueStart;
        while (valueEnd < json.size() && (std::isdigit(json[valueEnd]) || json[valueEnd] == '-')) valueEnd++;
        if (valueEnd > valueStart)
        {
            try
            {
                return std::stoi(json.substr(valueStart, valueEnd - valueStart));
            }
            catch (const std::exception& e)
            {
                logError("ModuleCommandUseCase: Failed to parse integer value for key '%s'", key.c_str());
            }
        }
        return 0;
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
