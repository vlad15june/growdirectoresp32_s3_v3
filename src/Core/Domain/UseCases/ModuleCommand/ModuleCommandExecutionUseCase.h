#pragma once

#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/IClearModuleDataService.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h" //TODO: move to Core/Domain/Interfaces/Adapters/Time/INTPTimeAdapter.h
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
    IModuleConfig *moduleConfig;
    ISettings *settings;
    IHybridAPIService *hybridApiService;
    IClearModuleDataService *clearModuleDataService;
    ModuleStructure *moduleStructure;
    static const unsigned long commandMaxAgeSeconds = 300;

public:
    ModuleCommandExecutionUseCase(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr, INTPTimeAdapter *timeAdapter = nullptr, IFirmwareRemoteUpdate *firmwareUpdater = nullptr, IArduinoAdapter *arduinoAdapter = nullptr, ISensorRepository *sensorRepository = nullptr, ISensorTypeRepository *sensorTypeRepository = nullptr, ISensorReader *sensorReader = nullptr, IModuleConfig *moduleConfig = nullptr, ISettings *settings = nullptr, IHybridAPIService *hybridApiService = nullptr, IClearModuleDataService *clearModuleDataService = nullptr, ModuleStructure *moduleStructure = nullptr)
        : logger(logger), debugSystem(debugSystem), timeAdapter(timeAdapter), firmwareUpdater(firmwareUpdater), arduinoAdapter(arduinoAdapter), sensorRepository(sensorRepository), sensorTypeRepository(sensorTypeRepository), sensorReader(sensorReader), moduleConfig(moduleConfig), settings(settings), hybridApiService(hybridApiService), clearModuleDataService(clearModuleDataService), moduleStructure(moduleStructure) {}
    
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
            {
                // Create a ModuleCommandDto for reboot with options
                ModuleCommandDto moduleCommand;
                moduleCommand.command = command;
                moduleCommand.options = options;
                moduleCommand.uid = commandUid;
                moduleCommand.createdAt = createdAt;
                return handleReboot(moduleCommand);
            }
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
        //TODO: bad arch!!! do not use ModuleCommandDto! Create separate Entity for Domain layer

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
            ModuleCommandResult cmdResult;
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
                        bool success = handleReboot(moduleCommand);
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
                        log(DebugFeatures::MODULE_COMMAND_DEBUG,
                            "ModuleCommandUseCase: SET_SENSOR_MODEL executed - status: %s, error: %s",
                            getModuleCommandStatusName(status), errorMessage.c_str());
                        break;
                    }
                    case ModuleCommandEnum::DELETE_SENSORS:
                    {
                        auto result = handleDeleteSensors(moduleCommand);
                        status = result.status;
                        errorMessage = result.errorMessage;
                        cmdResult = result.result;
                        break;
                    }
                    case ModuleCommandEnum::SCAN_SENSORS:
                    {
                        auto result = handleScanSensors(moduleCommand);
                        status = result.status;
                        errorMessage = result.errorMessage;
                        cmdResult = result.result;
                        break;
                    }
                    case ModuleCommandEnum::DELETE_MODULE:
                    {
                        auto result = handleDeleteModule(moduleCommand);
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

            results.emplace_back(moduleCommand.uid, statusStr, errorMessage, cmdResult);
        }

        return results;
    }

private:
    /**
     * @brief Helper structure for commands that return results with sensor data
     */
    struct CommandExecutionResult {
        ModuleCommandStatus status;
        std::string errorMessage;
        ModuleCommandResult result;
        
        CommandExecutionResult(ModuleCommandStatus status, const std::string& errorMessage)
            : status(status), errorMessage(errorMessage) {}
            
        CommandExecutionResult(ModuleCommandStatus status, const std::string& errorMessage, const ModuleCommandResult& result)
            : status(status), errorMessage(errorMessage), result(result) {}
    };

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

            settings->setNoScanSensors(1);
            settings->setBootType(BOOT_TYPE_FIRMWARE_UPDATE);
            moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);

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

        int pin = convertPinFromJson(extractJsonIntValue(moduleCommand.options, "pin"));
        if (pin < 0 || pin >= moduleConfig->getSensorPinCount())
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

        int pin = convertPinFromJson(extractJsonIntValue(moduleCommand.options, "pin"));
        std::string modelUid = extractJsonValue(moduleCommand.options, "uid");

        if ((pin < 0 || pin >= moduleConfig->getSensorPinCount()) || modelUid.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing pin or uid in options"};
        }

        if (!sensorTypeRepository || !sensorReader || !sensorRepository || !moduleConfig)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor services not available for SetSensorModel");
            return {ModuleCommandStatus::ERROR, "Sensor service not available"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: SetSensorModel command - Pin: %d, ModelUID: %s",
            pin, modelUid.c_str());

        // Find sensor type by UID (modelUid)
        auto allSensorTypes = sensorTypeRepository->getAll();
        SensorType* targetSensorType = nullptr;
        
        for (auto &sensorType : allSensorTypes)
        {
            if (sensorType.uid == modelUid)
            {
                targetSensorType = &sensorType;
                break;
            }
        }

        if (!targetSensorType)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor type with UID %s not found", modelUid.c_str());
            return {ModuleCommandStatus::ERROR, "Sensor type not found"};
        }

        // Generate sensor UID based on module UID and pin (e.g., "48:27:E2:E2:01:78.02")
        std::string moduleUid = moduleConfig->getModuleUid();
        char pinStr[3];
        sprintf(pinStr, "%02d", pin);
        std::string generatedSensorUid = moduleUid + "." + pinStr;

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Generated sensor UID: %s", generatedSensorUid.c_str());

        // Create sensor entity from sensor type
        SensorEntity newSensorEntity;
        newSensorEntity.uid = generatedSensorUid;
        newSensorEntity.pin = pin;
        newSensorEntity.sensorType = targetSensorType->name;
        newSensorEntity.model = targetSensorType->name;
        newSensorEntity.protocol = targetSensorType->getProtocolString();
        newSensorEntity.sensorTypeUid = targetSensorType->uid;
        newSensorEntity.modbusDeviceAddress = targetSensorType->modbusDeviceAddress;
        newSensorEntity.baudRate = targetSensorType->baudRate;
        newSensorEntity.i2cAddress = targetSensorType->i2cAddress;
        newSensorEntity.chipType = targetSensorType->chipType;
        newSensorEntity.modbusRegisterConfigs = targetSensorType->modbusRegisterConfig;
        newSensorEntity.i2cRegisterConfigs = targetSensorType->i2cRegisterConfig;
        newSensorEntity.isActive = true;
        newSensorEntity.isError = false;
        newSensorEntity.status = SensorStatusEnum::SENSOR_STATUS_ADDED;

        // Verify sensor is actually connected on this pin at default address
        bool sensorDetected = sensorReader->scanSensor(newSensorEntity);
        
        if (!sensorDetected)
        {
            // If not detected at default address, scan all ModBus addresses in the defined range
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor not found at default address, scanning range 0x%02X-0x%02X", 
                MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);
            
            for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
            {
                newSensorEntity.modbusDeviceAddress = address;
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Scanning ModBus address 0x%02X for sensor %s", 
                    address, targetSensorType->name.c_str());

                if (sensorReader->scanSensor(newSensorEntity))
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

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Sensor detected successfully, now checking for existing sensors on pin %d", pin);

        // Get all existing sensors to check if there's already one on this pin
        auto allSensors = sensorRepository->getAll();
        std::vector<SensorEntity> sensorsToDelete;
        
        for (const auto &existingSensor : allSensors)
        {
            if (existingSensor.pin == pin)
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Found existing sensor on pin %d with UID: %s, marking for deletion",
                    pin, existingSensor.uid.c_str());
                sensorsToDelete.push_back(existingSensor);
            }
        }

        // Now delete old sensors on the same pin (after new one is saved)
        for (const auto &sensorToDelete : sensorsToDelete)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Deleting old sensor with UID: %s from pin %d",
                sensorToDelete.uid.c_str(), pin);
            
            if (!sensorRepository->remove(sensorToDelete.uid.c_str()))
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Warning - Failed to delete old sensor UID: %s",
                    sensorToDelete.uid.c_str());
            }
        }

        // Update module structure - remove all sensors from this pin
        if (moduleStructure)
        {
            moduleStructure->removeSensorsByPin(pin);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Cleared moduleStructure on pin %d", pin);
        }

        // Save the new sensor first
        if (!sensorRepository->save(newSensorEntity))
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Failed to save sensor to repository");
            return {ModuleCommandStatus::ERROR, "Failed to save sensor"};
        }

        // Update module structure - add new sensor
        if (moduleStructure)
        {
            moduleStructure->addSensor(newSensorEntity);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Added new sensor to moduleStructure on pin %d", pin);
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Successfully saved %s sensor on pin %d with UID: %s",
            targetSensorType->name.c_str(), pin, generatedSensorUid.c_str());
        
        return {ModuleCommandStatus::DONE, ""};
    }

    /**
     * @brief Handle DeleteSensors command - delete sensors from a specific pin
     * @param moduleCommand Module command DTO with options
     * @return CommandExecutionResult with deleted sensors info
     */
    CommandExecutionResult handleDeleteSensors(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: DeleteSensors options: %s", moduleCommand.options.c_str());

        int pin = convertPinFromJson(extractJsonIntValue(moduleCommand.options, "pin"));
        if (pin < 0 || pin >= moduleConfig->getSensorPinCount())
        {
            return {ModuleCommandStatus::ERROR, "Missing or invalid pin in options"};
        }

        if (!sensorRepository || !moduleConfig)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor repository or module config not available for DeleteSensors");
            return {ModuleCommandStatus::ERROR, "Sensor service not available"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: DeleteSensors command - deleting sensors from pin %d", pin);

        // Get all sensors and find those on the specified pin
        std::vector<SensorEntity> allSensors = sensorRepository->getAll();
        std::vector<SensorEntity> sensorsToDelete;
        
        for (const auto& sensor : allSensors)
        {
            if (sensor.pin == pin)
            {
                sensorsToDelete.push_back(sensor);
            }
        }

        // Prepare result with sensor information before deletion
        ModuleCommandResult result;
        for (const auto& sensor : sensorsToDelete)
        {
            // Extract group from sensor UID (format: MAC.group.pin)
            int group = 0;
            size_t firstDot = sensor.uid.find('.');
            if (firstDot != std::string::npos)
            {
                size_t secondDot = sensor.uid.find('.', firstDot + 1);
                if (secondDot != std::string::npos)
                {
                    std::string groupStr = sensor.uid.substr(firstDot + 1, secondDot - firstDot - 1);
                    try {
                        group = std::stoi(groupStr);
                    } catch (...) {
                        group = 0;
                    }
                }
            }
            
            SensorResultDto sensorResult(
                sensor.uid,
                sensor.sensorType,
                sensor.pin,
                group,
                sensor.sensorTypeUid
            );
            result.sensors.push_back(sensorResult);
        }

        // If no sensors found on this pin, return success with empty result
        if (sensorsToDelete.empty())
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: No sensors found on pin %d", pin);
            return {ModuleCommandStatus::DONE, "", result};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Found %d sensor(s) on pin %d to delete", 
            sensorsToDelete.size(), pin);

        // Delete each sensor
        bool allDeleted = true;
        for (const auto& sensor : sensorsToDelete)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Deleting sensor %s from pin %d", 
                sensor.uid.c_str(), pin);
                
            if (!sensorRepository->remove(sensor.uid.c_str()))
            {
                logError("ModuleCommandUseCase: Failed to delete sensor %s", sensor.uid.c_str());
                allDeleted = false;
            }
        }

        if (!allDeleted)
        {
            return {ModuleCommandStatus::ERROR, "Failed to delete some sensors"};
        }

        // Update module structure - remove sensors from this pin
        if (moduleStructure)
        {
            moduleStructure->removeSensorsByPin(pin);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Removed sensors from moduleStructure on pin %d", pin);
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Successfully deleted %d sensor(s) from pin %d", 
            sensorsToDelete.size(), pin);

        return {ModuleCommandStatus::DONE, "", result};
    }

    /**
     * @brief Handle ScanSensors command - scan pin for connected sensors and return detected sensor info
     * @param moduleCommand Module command DTO with options
     * @return CommandExecutionResult with detected sensors info
     */
    CommandExecutionResult handleScanSensors(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: ScanSensors options: %s", moduleCommand.options.c_str());

        int pin = convertPinFromJson(extractJsonIntValue(moduleCommand.options, "pin"));
        if (pin < 0 || pin >= moduleConfig->getSensorPinCount())
        {
            return {ModuleCommandStatus::ERROR, "Missing or invalid pin in options"};
        }

        if (!sensorTypeRepository || !sensorReader || !moduleConfig)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor services not available for ScanSensors");
            return {ModuleCommandStatus::ERROR, "Sensor service not available"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: ScanSensors command - scanning pin %d", pin);

        // Get all sensor types for scanning
        auto allSensorTypes = sensorTypeRepository->getAll();
        if (allSensorTypes.empty())
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: No sensor types available for scanning");
            return {ModuleCommandStatus::ERROR, "No sensor types available"};
        }

        ModuleCommandResult result;
        std::string moduleUid = moduleConfig->getModuleUid();

        // Try to detect sensors on the pin
        for (const auto &sensorType : allSensorTypes)
        {
            // Create temporary sensor entity for testing
            SensorEntity tempSensor;
            tempSensor.pin = pin;
            tempSensor.sensorType = sensorType.name;
            tempSensor.model = sensorType.name;
            tempSensor.protocol = sensorType.getProtocolString();
            tempSensor.sensorTypeUid = sensorType.uid;
            tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
            tempSensor.baudRate = sensorType.baudRate;
            tempSensor.i2cAddress = sensorType.i2cAddress;
            tempSensor.chipType = sensorType.chipType;
            tempSensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
            tempSensor.i2cRegisterConfigs = sensorType.i2cRegisterConfig;

            char pinStr[3];
            sprintf(pinStr, "%02d", pin);
            tempSensor.uid = moduleUid + "." + pinStr;

            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Trying sensor type %s on pin %d", sensorType.name.c_str(), pin);

            // Try to scan this sensor at default address
            if (sensorReader->scanSensor(tempSensor))
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Found %s sensor on pin %d", sensorType.name.c_str(), pin);
                
                // Set sensor status to NEW for auto-detected sensor
                tempSensor.status = SensorStatusEnum::SENSOR_STATUS_NEW;
                tempSensor.isActive = true;
                tempSensor.isError = false;
                
                // Save sensor to repository
                if (sensorRepository)
                {
                    if (sensorRepository->createOrUpdateSensor(tempSensor))
                    {
                        log(DebugFeatures::MODULE_COMMAND_DEBUG,
                            "ModuleCommandUseCase: Successfully saved detected sensor %s to repository with NEW status", 
                            tempSensor.uid.c_str());
                        
                        // Update module structure - add detected sensor
                        if (moduleStructure)
                        {
                            moduleStructure->addSensor(tempSensor);
                            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                                "ModuleCommandUseCase: Added detected sensor to moduleStructure on pin %d", pin);
                        }
                    }
                    else
                    {
                        log(DebugFeatures::MODULE_COMMAND_DEBUG,
                            "ModuleCommandUseCase: Failed to save detected sensor %s to repository", 
                            tempSensor.uid.c_str());
                    }
                }
                
                // Add detected sensor to result
                SensorResultDto sensorResult(
                    tempSensor.uid,
                    tempSensor.sensorType,
                    tempSensor.pin,
                    1, // group
                    tempSensor.sensorTypeUid
                );
                result.sensors.push_back(sensorResult);
                
                // Return immediately after finding first sensor
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: ScanSensors completed - found sensor %s", 
                    tempSensor.sensorType.c_str());
                return {ModuleCommandStatus::DONE, "", result};
            }

            // For ModBus sensors, scan all addresses in the defined range if not found at default
            if (sensorType.isModbusSensor())
            {
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
                        
                        // Set sensor status to NEW for auto-detected sensor
                        tempSensor.status = SensorStatusEnum::SENSOR_STATUS_NEW;
                        tempSensor.isActive = true;
                        tempSensor.isError = false;
                        
                        // Save sensor to repository
                        if (sensorRepository)
                        {
                            if (sensorRepository->createOrUpdateSensor(tempSensor))
                            {
                                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                                    "ModuleCommandUseCase: Successfully saved detected sensor %s to repository with NEW status", 
                                    tempSensor.uid.c_str());
                                
                                // Update module structure - add detected sensor
                                if (moduleStructure)
                                {
                                    moduleStructure->addSensor(tempSensor);
                                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                                        "ModuleCommandUseCase: Added detected sensor to moduleStructure on pin %d", pin);
                                }
                            }
                            else
                            {
                                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                                    "ModuleCommandUseCase: Failed to save detected sensor %s to repository", 
                                    tempSensor.uid.c_str());
                            }
                        }
                        
                        // Add detected sensor to result
                        SensorResultDto sensorResult(
                            tempSensor.uid,
                            tempSensor.sensorType,
                            tempSensor.pin,
                            1, // group
                            tempSensor.sensorTypeUid
                        );
                        result.sensors.push_back(sensorResult);
                        
                        log(DebugFeatures::MODULE_COMMAND_DEBUG,
                            "ModuleCommandUseCase: ScanSensors completed - found sensor %s at address 0x%02X", 
                            tempSensor.sensorType.c_str(), address);
                        return {ModuleCommandStatus::DONE, "", result};
                    }
                }
            }
        }

        // No sensor detected on this pin
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: No sensor detected on pin %d", pin);
        return {ModuleCommandStatus::DONE, "", result};
    }

    /**
     * @brief Handle Reboot command
     * @param moduleCommand Module command DTO with options (may contain noScanSensors flag)
     * @return true if command was executed successfully, false otherwise
     */
    bool handleReboot(const ModuleCommandDto& moduleCommand)
    {
        if (!arduinoAdapter)
        {
            logError("ModuleCommandUseCase: Arduino adapter not available for reboot");
            return false;
        }
        
        // Check for noScanSensors flag in options
        bool noScan = false;
        if (!moduleCommand.options.empty() && settings)
        {
            std::string noScanValue = extractJsonValue(moduleCommand.options, "noScanSensors");
            if (!noScanValue.empty())
            {
                // Parse boolean value (accepts "true", "1", or true)
                noScan = (noScanValue == "true" || noScanValue == "1");
                settings->setNoScanSensors(noScan ? 1 : 0);
                
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Set noScanSensors flag to %d", noScan ? 1 : 0);
            }
        }
        
        // If noScan is false, clear sensor and sensor type data
        if (!noScan)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Clearing sensor data (noScan=false)");
            
            if (sensorRepository) //TODO: need add new flag clearSensor
            {
                bool sensorsCleared = false;
                // bool sensorsCleared = sensorRepository->clear();
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Sensor data clear %s", sensorsCleared ? "successful" : "failed");
            }
            else
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Sensor repository not available for clearing");
            }
            
            if (sensorTypeRepository)
            {
                bool sensorTypesCleared = sensorTypeRepository->clear();
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Sensor type data clear %s", sensorTypesCleared ? "successful" : "failed");
            }
            else
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Sensor type repository not available for clearing");
            }
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

    /**
     * @brief Handle DeleteModule command - wipes and resets the module to unconfigured state
     * @param moduleCommand Module command DTO with options
     * @return Pair of status and error message
     */
    std::pair<ModuleCommandStatus, std::string> handleDeleteModule(const ModuleCommandDto& moduleCommand)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Processing DeleteModule command - uid: %s", moduleCommand.uid.c_str());

        // Check if hybridApiService is available for verification
        if (!hybridApiService)
        {
            logError("ModuleCommandUseCase: HybridAPIService not available for module deletion verification");
            return {ModuleCommandStatus::ERROR, "API service not available"};
        }

        // Verify with server that module is actually marked as deleted
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Verifying module deletion status with server");
        
        bool isDeleted = hybridApiService->checkIsModuleDeleted();
        
        if (!isDeleted)
        {
            logError("ModuleCommandUseCase: Server verification failed - module is not marked as deleted");
            return {ModuleCommandStatus::ERROR, "Module deletion not confirmed by server"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Server confirmed module deletion - proceeding with factory reset");

        // Check if clearModuleDataService is available
        if (!clearModuleDataService)
        {
            logError("ModuleCommandUseCase: ClearModuleDataService not available");
            return {ModuleCommandStatus::ERROR, "Clear data service not available"};
        }

        // Execute factory reset
        bool clearResult = clearModuleDataService->clearAllData();
        
        if (clearResult)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Factory reset completed successfully - all module data cleared");
            return {ModuleCommandStatus::DONE, ""};
        }
        else
        {
            logError("ModuleCommandUseCase: Factory reset failed - could not clear all data");
            return {ModuleCommandStatus::ERROR, "Failed to clear module data"};
        }
    }

    /**
     * @brief Convert internal pin numbering to external JSON format
     * Mapping: internal -> JSON (0 -> 4, 1 -> 3, 2 -> 2, 3 -> 1)
     */
    int convertPinToJson(int internalPin)
    {
        switch (internalPin) {
            case 0: return 4;
            case 1: return 3;
            case 2: return 2;
            case 3: return 1;
            default: return internalPin;
        }
    }

    /**
     * @brief Convert external JSON pin numbering to internal format
     * Mapping: JSON -> internal (4 -> 0, 3 -> 1, 2 -> 2, 1 -> 3)
     */
    int convertPinFromJson(int jsonPin)
    {
        switch (jsonPin) {
            case 4: return 0;
            case 3: return 1;
            case 2: return 2;
            case 1: return 3;
            default: return jsonPin;
        }
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
        if (debugSystem && debugSystem->isFeatureEnabled(feature) && logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* debugFeature = debugSystem->getFeature(feature);
            if (debugFeature)
            {
                logger->println(feature, buffer, debugFeature->getLogChannel());
            }
            else
            {
                logger->println(feature, buffer);
            }
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
