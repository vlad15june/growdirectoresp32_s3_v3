#ifndef SENSOR_STRUCTURE_USE_CASE_H
#define SENSOR_STRUCTURE_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/UseCases/ISensorStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IVirtualSensorChannelsUseCase.h"
#include <cstdarg>
#include <cstdio>
#include <string>

/**
 * @brief Sensor records of the module structure: one sensor entity -> the Sensor records of a Node.
 *
 * Extracted from ModuleSetupService (review of PR #17, 2026-08-13): the records are needed by two
 * different flows - the full structure fill at setup and the cheap republish after the sensor type
 * catalog moved a channel (RepublishModuleStructureUseCase) - and ModuleSetupService is large
 * enough without owning both. The class does exactly one thing: it upserts records into a Node.
 *
 * Everything it needs arrives through the constructor, including the computed channels
 * (IVirtualSensorChannelsUseCase); there are no static calls, so a test can give it a logger and
 * its own set of computed channels.
 *
 * Behaviour, log text and the SETUP_DEBUG feature are unchanged from the original methods.
 */
class SensorStructureUseCase : public ISensorStructureUseCase
{
private:
    ILogger *logger;
    IDebugSystem *debugSystem;
    IVirtualSensorChannelsUseCase *virtualChannels;

    void log(const char *format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            char buffer[1024];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature *feature = debugSystem->getFeature(DebugFeatures::SETUP_DEBUG);
            if (feature)
            {
                logger->println(DebugFeatures::SETUP_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::SETUP_DEBUG, buffer);
            }
        }
    }

    // Replace the record with the same uid, or append it. Returns true when it was an update.
    bool upsert(Node &node, const Sensor &sensor)
    {
        for (auto &existingSensor : node.sensors)
        {
            if (existingSensor.uid == sensor.uid)
            {
                existingSensor = sensor;
                return true;
            }
        }

        node.sensors.push_back(sensor);
        return false;
    }

public:
    SensorStructureUseCase(IVirtualSensorChannelsUseCase *virtualChannels = nullptr,
                           ILogger *logger = nullptr,
                           IDebugSystem *debugSystem = nullptr)
        : logger(logger), debugSystem(debugSystem), virtualChannels(virtualChannels)
    {
    }

    void addMultiChannelModbusSensor(Node &node, const SensorEntity &sensorEntity) override
    {
        for (const auto &config : sensorEntity.modbusRegisterConfigs)
        {
            Sensor sensor;
            sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
            sensor.type = config.sensorType.empty() ? sensorEntity.sensorType : config.sensorType;
            sensor.pin = sensorEntity.pin;
            sensor.group = sensorEntity.modbusDeviceAddress;
            sensor.modelUid = sensorEntity.sensorTypeUid;
            log("SETUP: Adding multi-channel ModBus sensor - sensorTypeUid: %s", sensorEntity.sensorTypeUid.c_str());

            if (upsert(node, sensor))
            {
                log("SETUP: Updated existing multi-channel ModBus sensor %s, type: %s, channel: %d, pin: %d",
                    sensor.uid.c_str(), sensor.type.c_str(), config.channel, sensor.pin);
            }
            else
            {
                log("SETUP: Added new multi-channel ModBus sensor %s, type: %s, channel: %d, pin: %d",
                    sensor.uid.c_str(), sensor.type.c_str(), config.channel, sensor.pin);
            }
        }

        addVirtualChannelSensors(node, sensorEntity);
    }

    void addMultiChannelI2CSensor(Node &node, const SensorEntity &sensorEntity) override
    {
        for (const auto &config : sensorEntity.i2cRegisterConfigs)
        {
            Sensor sensor;
            sensor.uid = sensorEntity.uid + "." + (config.channel < 10 ? "0" : "") + std::to_string(config.channel);
            sensor.type = config.sensorType.empty() ? sensorEntity.sensorType : config.sensorType;
            sensor.pin = sensorEntity.pin;
            sensor.group = 1; // I2C typically uses a single address/group
            sensor.modelUid = sensorEntity.sensorTypeUid;

            if (upsert(node, sensor))
            {
                log("SETUP: Updated existing multi-channel I2C sensor %s, type: %s, channel: %d, pin: %d",
                    sensor.uid.c_str(), sensor.type.c_str(), config.channel, sensor.pin);
            }
            else
            {
                log("SETUP: Added new multi-channel I2C sensor %s, type: %s, channel: %d, pin: %d",
                    sensor.uid.c_str(), sensor.type.c_str(), config.channel, sensor.pin);
            }
        }
    }

    void addSingleChannelSensor(Node &node, const SensorEntity &sensorEntity) override
    {
        Sensor sensor;
        sensor.uid = sensorEntity.uid + ".01";
        sensor.type = sensorEntity.sensorType;
        sensor.pin = sensorEntity.pin;
        sensor.group = 1;
        sensor.modelUid = sensorEntity.sensorTypeUid;

        if (upsert(node, sensor))
        {
            log("SETUP: Updated existing single-channel sensor %s, type: %s, protocol: %s, pin: %d",
                sensor.uid.c_str(), sensor.type.c_str(), sensorEntity.protocol.c_str(), sensor.pin);
        }
        else
        {
            log("SETUP: Added new single-channel sensor %s, type: %s, protocol: %s, pin: %d",
                sensor.uid.c_str(), sensor.type.c_str(), sensorEntity.protocol.c_str(), sensor.pin);
        }
    }

    void addSensor(Node &node, const SensorEntity &sensorEntity) override
    {
        if (sensorEntity.isMultiChannel())
        {
            if (sensorEntity.protocol == "ModBus")
            {
                addMultiChannelModbusSensor(node, sensorEntity);
            }
            else if (sensorEntity.protocol == "I2C")
            {
                addMultiChannelI2CSensor(node, sensorEntity);
            }
            else
            {
                log("SETUP: Unsupported protocol for multi-channel sensor: %s", sensorEntity.protocol.c_str());
            }
        }
        else
        {
            addSingleChannelSensor(node, sensorEntity);
        }
    }

    /**
     * @brief Records of the computed channels of this sensor (they have no register config)
     *
     * Without this the channel would silently be missing from the structure, and the backend
     * drops readings of a uid it does not know (INV-1, INV-5, INV-6, D-15). What a computed
     * channel is and which ones exist is IVirtualSensorChannelsUseCase.
     */
    void addVirtualChannelSensors(Node &node, const SensorEntity &sensorEntity) override
    {
        if (!virtualChannels)
        {
            return;
        }

        for (const auto &channel : virtualChannels->channels(sensorEntity))
        {
            Sensor sensor;
            sensor.uid = channel.uid;
            sensor.type = channel.type;
            sensor.pin = sensorEntity.pin;
            sensor.group = sensorEntity.modbusDeviceAddress;
            sensor.modelUid = sensorEntity.sensorTypeUid;

            upsert(node, sensor);

            log("SETUP: VIRTUAL_CHANNEL uid=%s type=%s channel=%d pin=%d",
                sensor.uid.c_str(), sensor.type.c_str(), channel.channel, sensor.pin);
        }

        std::string overflowType;
        if (virtualChannels->hasOverflow(sensorEntity, overflowType))
        {
            log("SETUP: VIRTUAL_CHANNEL_SKIP uid=%s type=%s reason=overflow max=%d",
                sensorEntity.uid.c_str(), overflowType.c_str(), virtualChannels->maxChannel());
        }
    }
};

#endif // SENSOR_STRUCTURE_USE_CASE_H
