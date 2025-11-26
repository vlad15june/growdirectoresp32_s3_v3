#pragma once

#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "constants.h"
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
    ISensorRepository *sensorRepository;
    ISensorTypeRepository *sensorTypeRepository;
    ISensorReader *sensorReader;
    static const unsigned long commandMaxAgeSeconds = 300;

public:
    ModuleCommandExecutionUseCase(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr, INTPTimeAdapter *timeAdapter = nullptr, IFirmwareRemoteUpdate *firmwareUpdater = nullptr, IArduinoAdapter *arduinoAdapter = nullptr, ISensorRepository *sensorRepository = nullptr, ISensorTypeRepository *sensorTypeRepository = nullptr, ISensorReader *sensorReader = nullptr)
        : logger(logger), debugSystem(debugSystem), timeAdapter(timeAdapter), firmwareUpdater(firmwareUpdater), arduinoAdapter(arduinoAdapter), sensorRepository(sensorRepository), sensorTypeRepository(sensorTypeRepository), sensorReader(sensorReader) {}
    
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
     * @brief Handle PingPin command - scan pin for any connected sensor
     * @param moduleCommand Module command DTO with options
     * @return Pair of status and error message
     */
    std::pair<ModuleCommandStatus, std::string> handlePingPin(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: PingPin options: %s", moduleCommand.options.c_str());

        int pin = extractJsonIntValue(moduleCommand.options, "pin");
        if (pin == 0)
        {
            return {ModuleCommandStatus::ERROR, "Missing or invalid pin in options"};
        }

        if (!sensorTypeRepository || !sensorReader)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor repository or reader not available for PingPin");
            return {ModuleCommandStatus::ERROR, "Sensor service not available"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: PingPin command - scanning pin %d", pin);

        // Get all sensor types for scanning
        auto allSensorTypes = sensorTypeRepository->getAll();
        if (allSensorTypes.empty())
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: No sensor types available for scanning");
            return {ModuleCommandStatus::ERROR, "No sensor types available"};
        }

        // Try to detect any sensor on the pin
        for (const auto &sensorType : allSensorTypes)
        {
            // Create temporary sensor entity for testing
            SensorEntity tempSensor;
            tempSensor.pin = pin;
            tempSensor.sensorType = sensorType.name;
            tempSensor.model = sensorType.name;
            tempSensor.protocol = sensorType.getProtocolString();
            tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
            tempSensor.baudRate = sensorType.baudRate;
            tempSensor.i2cAddress = sensorType.i2cAddress;
            tempSensor.chipType = sensorType.chipType;
            tempSensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
            tempSensor.i2cRegisterConfigs = sensorType.i2cRegisterConfig;

            // Try to scan this sensor at default address
            if (sensorReader->scanSensor(tempSensor))
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Found %s sensor on pin %d", sensorType.name.c_str(), pin);
                return {ModuleCommandStatus::DONE, ""};
            }

            // If not detected at default address, scan all ModBus addresses in the defined range
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor not found at default address, scanning range 0x%02X-0x%02X", 
                MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);
            
            for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
            {
                tempSensor.modbusDeviceAddress = address;
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Scanning ModBus address 0x%02X for sensor type %s", 
                    address, sensorType.name.c_str());

                if (sensorReader->scanSensor(tempSensor))
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Found %s sensor on pin %d at address 0x%02X", 
                        sensorType.name.c_str(), pin, address);
                    return {ModuleCommandStatus::DONE, ""};
                }
            }
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: No sensor detected on pin %d", pin);
        return {ModuleCommandStatus::ERROR, "No sensor detected on pin"};
    }

    /**
     * @brief Handle SetSensorModel command - verify specific sensor and save to repository
     * @param moduleCommand Module command DTO with options
     * @return Pair of status and error message
     */
    std::pair<ModuleCommandStatus, std::string> handleSetSensorModel(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: SetSensorModel options: %s", moduleCommand.options.c_str());

        int pin = extractJsonIntValue(moduleCommand.options, "pin");
        std::string sensorUid = extractJsonValue(moduleCommand.options, "uid");

        if (pin == 0 || sensorUid.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing pin or uid in options"};
        }

        if (!sensorTypeRepository || !sensorReader || !sensorRepository)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor services not available for SetSensorModel");
            return {ModuleCommandStatus::ERROR, "Sensor service not available"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: SetSensorModel command - Pin: %d, SensorUID: %s",
            pin, sensorUid.c_str());

        // Find sensor type by UID
        auto allSensorTypes = sensorTypeRepository->getAll();
        SensorType* targetSensorType = nullptr;
        
        for (auto &sensorType : allSensorTypes)
        {
            if (sensorType.uid == sensorUid)
            {
                targetSensorType = &sensorType;
                break;
            }
        }

        if (!targetSensorType)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor type with UID %s not found", sensorUid.c_str());
            return {ModuleCommandStatus::ERROR, "Sensor type not found"};
        }

        // Create sensor entity from sensor type
        SensorEntity sensorEntity;
        sensorEntity.uid = targetSensorType->uid;
        sensorEntity.pin = pin;
        sensorEntity.sensorType = targetSensorType->name;
        sensorEntity.model = targetSensorType->name;
        sensorEntity.protocol = targetSensorType->getProtocolString();
        sensorEntity.modbusDeviceAddress = targetSensorType->modbusDeviceAddress;
        sensorEntity.baudRate = targetSensorType->baudRate;
        sensorEntity.i2cAddress = targetSensorType->i2cAddress;
        sensorEntity.chipType = targetSensorType->chipType;
        sensorEntity.modbusRegisterConfigs = targetSensorType->modbusRegisterConfig;
        sensorEntity.i2cRegisterConfigs = targetSensorType->i2cRegisterConfig;
        sensorEntity.isActive = true;
        sensorEntity.isError = false;
        sensorEntity.status = SensorStatusEnum::SENSOR_STATUS_ADDED;

        // Verify sensor is actually connected on this pin at default address
        bool sensorDetected = sensorReader->scanSensor(sensorEntity);
        
        if (!sensorDetected)
        {
            // If not detected at default address, scan all ModBus addresses in the defined range
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor not found at default address, scanning range 0x%02X-0x%02X", 
                MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);
            
            for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
            {
                sensorEntity.modbusDeviceAddress = address;
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Scanning ModBus address 0x%02X for sensor %s", 
                    address, targetSensorType->name.c_str());

                if (sensorReader->scanSensor(sensorEntity))
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Found %s sensor on pin %d at address 0x%02X", 
                        targetSensorType->name.c_str(), pin, address);
                    sensorDetected = true;
                    break;
                }
            }
        }
        
        if (!sensorDetected)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor %s not detected on pin %d",
                targetSensorType->name.c_str(), pin);
            return {ModuleCommandStatus::ERROR, "Sensor not detected on specified pin"};
        }

        // Save sensor to repository
        if (!sensorRepository->save(sensorEntity))
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Failed to save sensor to repository");
            return {ModuleCommandStatus::ERROR, "Failed to save sensor"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Successfully saved %s sensor on pin %d",
            targetSensorType->name.c_str(), pin);
        
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
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Extracting int value - JSON: %s, Key: %s", json.c_str(), key.c_str());

        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return 0;
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return 0;
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && std::isspace(json[valueStart])) valueStart++;
        
        // Check if value is a string (starts with quote)
        if (valueStart < json.size() && json[valueStart] == '"')
        {
            // Extract string value
            size_t stringStart = valueStart + 1;
            size_t stringEnd = json.find("\"", stringStart);
            if (stringEnd != std::string::npos)
            {
                std::string stringValue = json.substr(stringStart, stringEnd - stringStart);
                try
                {
                    int result = std::stoi(stringValue);
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Extracted int from string - value: %d", result);
                    return result;
                }
                catch (const std::exception& e)
                {
                    logError("ModuleCommandUseCase: Failed to parse integer from string value '%s' for key '%s'", 
                             stringValue.c_str(), key.c_str());
                }
            }
        }
        else
        {
            // Extract numeric value
            size_t valueEnd = valueStart;
            while (valueEnd < json.size() && (std::isdigit(json[valueEnd]) || json[valueEnd] == '-')) valueEnd++;
            if (valueEnd > valueStart)
            {
                try
                {
                    int result = std::stoi(json.substr(valueStart, valueEnd - valueStart));
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Extracted int from number - value: %d", result);
                    return result;
                }
                catch (const std::exception& e)
                {
                    logError("ModuleCommandUseCase: Failed to parse integer value for key '%s'", key.c_str());
                }
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
