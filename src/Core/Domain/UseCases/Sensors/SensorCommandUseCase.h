#pragma once

#include "Core/Domain/Interfaces/UseCases/ISensorCommandUseCase.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/SensorTypeEnum.h"
#include <string>
#include <cstdarg>
#include <cstdio>

/**
 * @brief Implementation of sensor command execution use case
 */
class SensorCommandUseCase : public ISensorCommandUseCase
{
private:
    IModuleConfig *moduleConfig;
    ISettings *settings;
    ILogger *logger;
    IDebugSystem *debugSystem;

public:
    /**
     * @brief Constructor
     * @param moduleConfig Module configuration for accessing module type
     * @param settings Settings for storing sensor configuration
     * @param logger Logger instance
     * @param debugSystem Debug system instance
     */
    SensorCommandUseCase(
        IModuleConfig *moduleConfig,
        ISettings *settings,
        ILogger *logger,
        IDebugSystem *debugSystem)
        : moduleConfig(moduleConfig),
          settings(settings),
          logger(logger),
          debugSystem(debugSystem) {}

    /**
     * @brief Execute a sensor type configuration command
     * @param sensorType Sensor type name
     * @param sensorUid Sensor unique identifier
     * @return true if command was executed successfully, false otherwise
     */
    bool execute(const std::string &sensorType, const std::string &sensorUid) override
    {
        //TODO: use sensor domain models instead of strings
        //TODO: store in repository instead of settings
        //TODO: check sensor connection, try to connect to sensor and read sensor data
        //TODO: return not bool but status and message
        //TODO: use ModuleStructure &_moduleStructure to update structure and then send structure to server(send in APP layer)
        log("SensorCommandUseCase: Processing sensor type command - type: %s, uid: %s",
            sensorType.c_str(), sensorUid.c_str());

        if (!moduleConfig || !settings)
        {
            log("SensorCommandUseCase: Missing dependencies - moduleConfig or settings is null");
            return false;
        }

        int moduleType = moduleConfig->getModuleType();
        bool configChanged = false;

        if (moduleType == MODULE_TYPE_HYDRO)
        {
            configChanged = processHydroSensorCommand(sensorType);
        }
        else if (moduleType == MODULE_TYPE_SENSOR)
        {
            configChanged = processSensorModuleCommand(sensorType, sensorUid);
        }
        else
        {
            log("SensorCommandUseCase: Module type %d does not support sensor configuration", moduleType);
            return false;
        }

        if (configChanged)
        {
            // Set boot type and request restart
            settings->setBootType(BOOT_TYPE_SENSOR_CHNG);
            moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
            
            log("SensorCommandUseCase: Sensor configuration changed, restart requested");
        }

        return configChanged;
    }

private:
    /**
     * @brief Process sensor command for hydro module
     * @param sensorType Sensor type name
     * @return true if configuration was changed
     */
    bool processHydroSensorCommand(const std::string &sensorType)
    {
        int currentType = getSensorTypeDoOrp();
        
        if (currentType != ANALOG_DO_CONNECTED && sensorType == getSensorTypeName(ANALOG_DO_CONNECTED))
        {
            putSensorTypeDoOrp(ANALOG_DO_CONNECTED);
            log("SensorCommandUseCase: Hydro module - configured DO sensor");
            return true;
        }

        if (currentType != ANALOG_ORP_CONNECTED && sensorType == getSensorTypeName(ANALOG_ORP_CONNECTED))
        {
            putSensorTypeDoOrp(ANALOG_ORP_CONNECTED);
            log("SensorCommandUseCase: Hydro module - configured ORP sensor");
            return true;
        }

        return false;
    }

    /**
     * @brief Process sensor command for sensor module
     * @param sensorType Sensor type name
     * @param sensorUid Sensor unique identifier
     * @return true if configuration was changed
     */
    bool processSensorModuleCommand(const std::string &sensorType, const std::string &sensorUid)
    {
        // Extract channel from sensorUid (format: "84:F7:03:FE:1B:30.00")
        uint8_t channel = extractChannelFromUid(sensorUid);
        if (channel == 255) // Invalid channel
        {
            log("SensorCommandUseCase: Invalid channel extracted from sensorUid: %s", sensorUid.c_str());
            return false;
        }

        uint8_t currentType = getSensorType(channel);

        log("SensorCommandUseCase: Channel: %d, current type: %d, requested type: %s", 
            channel, currentType, sensorType.c_str());

        if (currentType != ANALOG_SOIL_MOISTURE && sensorType == getSensorTypeName(ANALOG_SOIL_MOISTURE))
        {
            putSensorType(ANALOG_SOIL_MOISTURE, channel);
            log("SensorCommandUseCase: Sensor module - configured soil moisture sensor on channel %d", channel);
            return true;
        }

        if (currentType != ANALOG_LUX_CONNECTED && sensorType == getSensorTypeName(ANALOG_LUX_CONNECTED))
        {
            putSensorType(ANALOG_LUX_CONNECTED, channel);
            log("SensorCommandUseCase: Sensor module - configured LUX sensor on channel %d", channel);
            return true;
        }

        if (currentType != ANALOG_PAR_CONNECTED && sensorType == getSensorTypeName(ANALOG_PAR_CONNECTED))
        {
            putSensorType(ANALOG_PAR_CONNECTED, channel);
            log("SensorCommandUseCase: Sensor module - configured PAR sensor on channel %d", channel);
            return true;
        }

        if (currentType != ANALOG_TEMP_HUM_CONNECTED && sensorType == getSensorTypeName(ANALOG_TEMP_HUM_CONNECTED))
        {
            putSensorType(ANALOG_TEMP_HUM_CONNECTED, channel);
            log("SensorCommandUseCase: Sensor module - configured temperature humidity sensor on channel %d", channel);
            return true;
        }

        return false;
    }

    /**
     * @brief Extract channel number from sensor UID
     * @param sensorUid Sensor UID in format "84:F7:03:FE:1B:30.00"
     * @return Channel number (0-3) or 255 if invalid
     */
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

    /**
     * @brief Get current sensor type for DO/ORP (hydro module)
     * @return Current sensor type
     */
    int getSensorTypeDoOrp()
    {
        // This should be implemented to read from settings
        // For now, return a default value
        return settings ? settings->getSensorTypeDoOrp() : 0;
    }

    /**
     * @brief Set sensor type for DO/ORP (hydro module)
     * @param type Sensor type to set
     */
    void putSensorTypeDoOrp(int type)
    {
        if (settings)
        {
            settings->setSensorTypeDoOrp(type);
        }
    }

    /**
     * @brief Get sensor type for a specific channel (sensor module)
     * @param channel Channel number
     * @return Sensor type
     */
    uint8_t getSensorType(uint8_t channel)
    {
        // This should be implemented to read from settings
        return settings ? settings->getSensorType(channel) : 0;
    }

    /**
     * @brief Set sensor type for a specific channel (sensor module)
     * @param type Sensor type to set
     * @param channel Channel number
     */
    void putSensorType(uint8_t type, uint8_t channel)
    {
        if (settings)
        {
            settings->setSensorType(type, channel);
        }
    }

    /**
     * @brief Log with debug feature check
     */
    void log(const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_SENSOR_DEBUG))
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(DebugFeatures::SETUP_SENSOR_DEBUG, buffer);
        }
    }
};
