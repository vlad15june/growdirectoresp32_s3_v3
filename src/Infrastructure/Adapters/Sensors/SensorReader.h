#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include "../../../Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "../../../Core/Domain/Interfaces/Adapters/Sensors/IAnalogSensorReader.h"
#include "../../../Core/Domain/Interfaces/Adapters/Sensors/IModbusSensorReader.h"
#include "../../../Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "../../../Core/Domain/Interfaces/ILogger.h"
#include "../../../Core/Domain/Interfaces/IDebugSystem.h"
#include "../../../Core/Domain/Data/Models/SensorReadResult.h"
#include "../../../Core/Domain/Enums/SensorTypeEnum.h"
#include <map>
#include <string>

class SensorReader : public ISensorReader
{
private:
    IAnalogSensorReader* _analogReader;
    IModbusSensorReader* _modbusReader;
    IArduinoAdapter* _arduino;
    ILogger* _logger;
    IDebugSystem* _debugSystem;

public:
    SensorReader(IAnalogSensorReader* analogReader, 
                IModbusSensorReader* modbusReader,
                IArduinoAdapter* arduino,
                ILogger* logger,
                IDebugSystem* debugSystem)
        : _analogReader(analogReader), 
          _modbusReader(modbusReader),
          _arduino(arduino),
          _logger(logger),
          _debugSystem(debugSystem) {}

    bool readSensor(const SensorEntity& sensor, float& outValue) override
    {
        if (sensor.protocol == "Analog")
        {
            return readAnalogSensor(sensor, outValue);
        }
        else if (sensor.protocol == "RS485" || sensor.protocol == "ModBus")
        {
            return readModbusSensor(sensor, outValue);
        }
        else if (sensor.protocol == "I2C")
        {
            return readI2CSensor(sensor, outValue);
        }

        log("SensorReader: Unsupported protocol: %s", sensor.protocol.c_str());
        return false;
    }

    bool scanSensor(const SensorEntity& sensor) override
    {
        if (sensor.protocol == "Analog")
        {
            return scanAnalogSensor(sensor);
        }
        else if (sensor.protocol == "RS485" || sensor.protocol == "ModBus")
        {
            return scanModbusSensor(sensor);
        }
        else if (sensor.protocol == "I2C")
        {
            return scanI2CSensor(sensor);
        }

        return false;
    }

    SensorReadResult readSensorDetailed(const SensorEntity& sensor) override
    {
        uint32_t readTime = _arduino ? _arduino->millis() : 0;
        
        if (sensor.isMultiChannel())
        {
            std::map<std::string, float> channelValues;
            if (readMultiChannelSensor(sensor, channelValues))
            {
                return SensorReadResult::createMultiChannelSuccess(channelValues, readTime);
            }
            return SensorReadResult::createError("Failed to read multi-channel sensor", SensorReadStatus::COMMUNICATION_ERROR);
        }
        else
        {
            float value;
            if (readSensor(sensor, value))
            {
                return SensorReadResult::createSuccess(value, readTime);
            }
            
            // Determine error type based on scan result
            if (!scanSensor(sensor))
            {
                return SensorReadResult::createError("Sensor is offline", SensorReadStatus::SENSOR_OFFLINE);
            }
            
            return SensorReadResult::createError("Communication error", SensorReadStatus::COMMUNICATION_ERROR);
        }
    }

    bool readMultiChannelSensor(const SensorEntity& sensor, std::map<std::string, float>& channelValues) override
    {
        if (!sensor.isMultiChannel())
        {
            return false;
        }

        // Read each channel
        for (const auto& channel : sensor.channels)
        {
            float value;
            if (sensor.protocol == "Analog")
            {
                if (_analogReader && channel.pin >= 0)
                {
                    value = _analogReader->readCalibratedValue(channel.pin, channel.multiplier, channel.offset);
                    channelValues[channel.name] = value;
                }
            }
            // Add other protocols as needed
        }

        return !channelValues.empty();
    }

private:
    bool readAnalogSensor(const SensorEntity& sensor, float& outValue)
    {
        if (!_analogReader || sensor.pin < 0)
        {
            return false;
        }

        try
        {
            outValue = _analogReader->readCalibratedValue(sensor.pin, sensor.multiplier, sensor.offset);
            log("SensorReader: Read analog sensor %s, value: %.2f", sensor.uid.c_str(), outValue);
            return true;
        }
        catch (...)
        {
            log("SensorReader: Exception reading analog sensor %s", sensor.uid.c_str());
            return false;
        }
    }

    bool readModbusSensor(const SensorEntity& sensor, float& outValue)
    {
        if (!_modbusReader || sensor.modbusConfig.deviceAddress == 0)
        {
            return false;
        }

        try
        {
            bool success = _modbusReader->readRegister(sensor.modbusConfig.deviceAddress, sensor.modbusConfig, outValue);
            if (success)
            {
                log("SensorReader: Read modbus sensor %s, value: %.2f", sensor.uid.c_str(), outValue);
            }
            return success;
        }
        catch (...)
        {
            log("SensorReader: Exception reading modbus sensor %s", sensor.uid.c_str());
            return false;
        }
    }

    bool readI2CSensor(const SensorEntity& sensor, float& outValue)
    {
        // Placeholder for I2C sensor reading
        log("SensorReader: I2C sensor reading not yet implemented for %s", sensor.uid.c_str());
        return false;
    }

    bool scanAnalogSensor(const SensorEntity& sensor)
    {
        if (!_analogReader || sensor.pin < 0)
        {
            return false;
        }

        // For analog sensors, check if pin gives reasonable values
        int rawValue = _analogReader->readRawValue(sensor.pin);
        return rawValue >= 0 && rawValue <= 4095; // ESP32 ADC range
    }

    bool scanModbusSensor(const SensorEntity& sensor)
    {
        if (!_modbusReader || sensor.modbusConfig.deviceAddress == 0)
        {
            return false;
        }

        // Try to read a register to check if sensor responds
        float dummyValue;
        return _modbusReader->readRegister(sensor.modbusConfig.deviceAddress, sensor.modbusConfig, dummyValue);
    }

    bool scanI2CSensor(const SensorEntity& sensor)
    {
        // Placeholder for I2C sensor scanning
        return false;
    }

    void log(const char* format, ...)
    {
        if (_debugSystem && _logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::SETUP_SENSOR_DEBUG, buffer);
        }
    }
};

#endif // SENSOR_READER_H
