#pragma once

#include "Core/Domain/Interfaces/UseCases/ISensorCommandUseCase.h"
#include "Core/Domain/Data/Models/SensorCommandResult.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/SensorTypeEnum.h"
#include <string>
#include <cstdarg>

class SensorCommandUseCase : public ISensorCommandUseCase
{
private:
    IModuleConfig *moduleConfig;
    ISettings *settings;
    ILogger *logger;
    IDebugSystem *debugSystem;
    ISensorRepository *sensorRepository;

public:
    SensorCommandUseCase(
        IModuleConfig *moduleConfig,
        ISettings *settings,
        ILogger *logger,
        IDebugSystem *debugSystem,
        ISensorRepository *sensorRepository)
        : moduleConfig(moduleConfig),
          settings(settings),
          logger(logger),
          debugSystem(debugSystem),
          sensorRepository(sensorRepository) {}

    SensorCommandResult execute(const SensorEntity& sensor) override
    {
        //TODO: (done) use sensor domain models instead of strings
        //TODO: store in repository and in settings
        //TODO: check sensor connection, try to connect to sensor and read sensor data
        //TODO: return not bool but status and message
        //TODO: use ModuleStructure &_moduleStructure to update structure and then send structure to server(send in APP layer)

        log("SensorCommandUseCase: Processing sensor command - type: %s, uid: %s",
            sensor.sensorType.c_str(), sensor.uid.c_str());

        // Validate dependencies
        if (!moduleConfig || !settings || !sensorRepository)
        {
            std::string errorMsg = "Missing dependencies - moduleConfig, settings, or sensorRepository is null";
            log("SensorCommandUseCase: %s", errorMsg.c_str());
            return SensorCommandResult::createError(errorMsg, "MISSING_DEPENDENCIES");
        }

        // Validate sensor entity
        if (sensor.uid.empty())
        {
            std::string errorMsg = "Sensor UID cannot be empty";
            log("SensorCommandUseCase: %s", errorMsg.c_str());
            return SensorCommandResult::createError(errorMsg, "INVALID_SENSOR_UID");
        }

        if (sensor.sensorType.empty())
        {
            std::string errorMsg = "Sensor type cannot be empty";
            log("SensorCommandUseCase: %s", errorMsg.c_str());
            return SensorCommandResult::createError(errorMsg, "INVALID_SENSOR_TYPE");
        }

        int moduleType = moduleConfig->getModuleType();

        SensorCommandResult result;
        if (moduleType == MODULE_TYPE_HYDRO)
        {
            result = processHydroSensorCommand(sensor);
        }
        else if (moduleType == MODULE_TYPE_SENSOR)
        {
            result = processSensorModuleCommand(sensor);
        }
        else
        {
            std::string errorMsg = "Module type " + std::to_string(moduleType) + " does not support sensor configuration";
            log("SensorCommandUseCase: %s", errorMsg.c_str());
            return SensorCommandResult::createError(errorMsg, "UNSUPPORTED_MODULE_TYPE");
        }

        if (result.success)
        {
            settings->setBootType(BOOT_TYPE_SENSOR_CHNG);
            moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
            
            log("SensorCommandUseCase: Sensor configuration changed, restart requested");
            
            result.message += "System restart requested to apply changes";
        }

        return result;
    }

private:

    SensorCommandResult processHydroSensorCommand(const SensorEntity& sensor)
    {
        int currentType = getSensorTypeDoOrp();
        
        if (currentType != ANALOG_DO_CONNECTED && sensor.sensorType == getSensorTypeName(ANALOG_DO_CONNECTED))
        {
            putSensorTypeDoOrp(ANALOG_DO_CONNECTED);
            log("SensorCommandUseCase: Hydro module - configured DO sensor");
            return SensorCommandResult::createSuccess("Successfully configured DO sensor for hydro module");
        }

        if (currentType != ANALOG_ORP_CONNECTED && sensor.sensorType == getSensorTypeName(ANALOG_ORP_CONNECTED))
        {
            putSensorTypeDoOrp(ANALOG_ORP_CONNECTED);
            log("SensorCommandUseCase: Hydro module - configured ORP sensor");
            return SensorCommandResult::createSuccess("Successfully configured ORP sensor for hydro module");
        }

        // Check if sensor is already configured
        if ((currentType == ANALOG_DO_CONNECTED && sensor.sensorType == getSensorTypeName(ANALOG_DO_CONNECTED)) ||
            (currentType == ANALOG_ORP_CONNECTED && sensor.sensorType == getSensorTypeName(ANALOG_ORP_CONNECTED)))
        {
            std::string warningMsg = "Sensor type " + sensor.sensorType + " is already configured";
            log("SensorCommandUseCase: %s", warningMsg.c_str());
            return SensorCommandResult::createWarning(warningMsg);
        }

        std::string errorMsg = "Unsupported sensor type '" + sensor.sensorType + "' for hydro module";
        log("SensorCommandUseCase: %s", errorMsg.c_str());
        return SensorCommandResult::createError(errorMsg, "UNSUPPORTED_SENSOR_TYPE");
    }

    SensorCommandResult processSensorModuleCommand(const SensorEntity& sensor)
    {
        // Extract channel from sensor UID (format: "84:F7:03:FE:1B:30.00")
        uint8_t channel = extractChannelFromUid(sensor.uid);
        if (channel == 255) // Invalid channel
        {
            std::string errorMsg = "Invalid channel extracted from sensor UID: " + sensor.uid;
            log("SensorCommandUseCase: %s", errorMsg.c_str());
            return SensorCommandResult::createError(errorMsg, "INVALID_CHANNEL");
        }

        // uint8_t currentType = getSensorType(channel); //TODO: store "protocol": "I2C"
        auto existingSensor = sensorRepository->get(sensor.uid.c_str());
        uint8_t currentType = existingSensor.has_value() ? getSensorTypeId(existingSensor.value().sensorType) : getSensorType(channel);
        
        log("SensorCommandUseCase: Channel: %d, current type: %d, requested type: %s, protocol: %s", 
            channel, currentType, sensor.sensorType.c_str(), sensor.protocol.c_str());

        // Handle different protocols
        if (sensor.protocol == "Analog")
        {
            // Check each supported analog sensor type
            if (currentType != ANALOG_SOIL_MOISTURE && sensor.sensorType == getSensorTypeName(ANALOG_SOIL_MOISTURE))
            {
                putSensorType(ANALOG_SOIL_MOISTURE, channel);
                storeSensorInRepository(sensor);
                log("SensorCommandUseCase: Sensor module - configured soil moisture sensor on channel %d", channel);
                return SensorCommandResult::createSuccess("Successfully configured soil moisture sensor on channel " + std::to_string(channel));
            }

            if (currentType != ANALOG_LUX_CONNECTED && sensor.sensorType == getSensorTypeName(ANALOG_LUX_CONNECTED))
            {
                putSensorType(ANALOG_LUX_CONNECTED, channel);
                storeSensorInRepository(sensor);
                log("SensorCommandUseCase: Sensor module - configured LUX sensor on channel %d", channel);
                return SensorCommandResult::createSuccess("Successfully configured LUX sensor on channel " + std::to_string(channel));
            }

            if (currentType != ANALOG_PAR_CONNECTED && sensor.sensorType == getSensorTypeName(ANALOG_PAR_CONNECTED))
            {
                putSensorType(ANALOG_PAR_CONNECTED, channel);
                storeSensorInRepository(sensor);
                log("SensorCommandUseCase: Sensor module - configured PAR sensor on channel %d", channel);
                return SensorCommandResult::createSuccess("Successfully configured PAR sensor on channel " + std::to_string(channel));
            }

            if (currentType != ANALOG_TEMP_HUM_CONNECTED && sensor.sensorType == getSensorTypeName(ANALOG_TEMP_HUM_CONNECTED))
            {
                putSensorType(ANALOG_TEMP_HUM_CONNECTED, channel);
                storeSensorInRepository(sensor);
                log("SensorCommandUseCase: Sensor module - configured temperature humidity sensor on channel %d", channel);
                return SensorCommandResult::createSuccess("Successfully configured temperature humidity sensor on channel " + std::to_string(channel));
            }

            // Check if sensor is already configured with the requested type
            uint8_t requestedTypeId = getSensorTypeId(sensor.sensorType);
            if (requestedTypeId != 255 && currentType == requestedTypeId)
            {
                std::string warningMsg = "Sensor type " + sensor.sensorType + " is already configured on channel " + std::to_string(channel);
                log("SensorCommandUseCase: %s", warningMsg.c_str());
                return SensorCommandResult::createWarning(warningMsg);
            }
        }
        else if (sensor.protocol == "I2C" || sensor.protocol == "ModBus")
        {
            // For I2C and ModBus sensors, just store in repository
            storeSensorInRepository(sensor);
            log("SensorCommandUseCase: Sensor module - configured %s sensor with protocol %s", 
                sensor.sensorType.c_str(), sensor.protocol.c_str());
            return SensorCommandResult::createSuccess("Successfully configured " + sensor.sensorType + " sensor with " + sensor.protocol + " protocol");
        }
        else
        {
            std::string errorMsg = "Unsupported protocol '" + sensor.protocol + "' for sensor configuration";
            log("SensorCommandUseCase: %s", errorMsg.c_str());
            return SensorCommandResult::createError(errorMsg, "UNSUPPORTED_PROTOCOL");
        }

        std::string errorMsg = "Unsupported sensor type '" + sensor.sensorType + "' for sensor module or invalid configuration";
        log("SensorCommandUseCase: %s", errorMsg.c_str());
        return SensorCommandResult::createError(errorMsg, "UNSUPPORTED_SENSOR_TYPE");
    }

    uint8_t extractChannelFromUid(const std::string &sensorUid)
    {
        // Find the last dot in the UID
        size_t dotPos = sensorUid.find_last_of('.');
        if (dotPos == std::string::npos || dotPos + 2 >= sensorUid.length())
        {
            return 255; // Invalid format
        }

        // Extract the two characters after the dot
        std::string channelStr = sensorUid.substr(dotPos + 1, 2);
        
        try {
            int channel = std::stoi(channelStr);
            if (channel >= 0 && channel < moduleConfig->getSensorPinCount())
            {
                return static_cast<uint8_t>(channel);
            }
        } catch (const std::exception& e) {
            log("SensorCommandUseCase: Failed to parse channel from: %s", channelStr.c_str());
        }

        return 255; // Invalid channel
    }

    uint8_t getSensorTypeId(const std::string& sensorType)
    {
        if (sensorType == getSensorTypeName(ANALOG_SOIL_MOISTURE)) return ANALOG_SOIL_MOISTURE;
        if (sensorType == getSensorTypeName(ANALOG_LUX_CONNECTED)) return ANALOG_LUX_CONNECTED;
        if (sensorType == getSensorTypeName(ANALOG_PAR_CONNECTED)) return ANALOG_PAR_CONNECTED;
        if (sensorType == getSensorTypeName(ANALOG_TEMP_HUM_CONNECTED)) return ANALOG_TEMP_HUM_CONNECTED;
        if (sensorType == getSensorTypeName(ANALOG_DO_CONNECTED)) return ANALOG_DO_CONNECTED;
        if (sensorType == getSensorTypeName(ANALOG_ORP_CONNECTED)) return ANALOG_ORP_CONNECTED;
        return 255; // Unknown type
    }

    int getSensorTypeDoOrp()
    {
        // This should be implemented to read from settings
        // For now, return a default value
        return settings ? settings->getSensorTypeDoOrp() : 0;
    }

    void putSensorTypeDoOrp(int type)
    {
        if (settings)
        {
            settings->setSensorTypeDoOrp(type);
        }
    }

    uint8_t getSensorType(uint8_t channel)
    {
        // This should be implemented to read from settings
        return settings ? settings->getSensorType(channel) : 0;
    }

    void putSensorType(uint8_t type, uint8_t channel)
    {
        if (settings)
        {
            settings->setSensorType(type, channel);
        }
    }

    void log(const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_SENSOR_DEBUG))
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* feature = debugSystem->getFeature(DebugFeatures::SETUP_SENSOR_DEBUG);
            if (feature)
            {
                logger->println(DebugFeatures::SETUP_SENSOR_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::SETUP_SENSOR_DEBUG, buffer);
            }
        }
    }

    bool storeSensorInRepository(const SensorEntity& sensor)
    {
        if (!sensorRepository)
        {
            log("SensorCommandUseCase: Repository is null, cannot store sensor");
            return false;
        }

        bool success = sensorRepository->save(sensor);
        if (success)
        {
            log("SensorCommandUseCase: Successfully stored sensor %s in repository", sensor.uid.c_str());
        }
        else
        {
            log("SensorCommandUseCase: Failed to store sensor %s in repository", sensor.uid.c_str());
        }
        
        return success;
    }
};
