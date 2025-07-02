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
    IAnalogSensorReader *_analogReader;
    IModbusSensorReader *_modbusReader;
    IArduinoAdapter *_arduino;
    ILogger *_logger;
    IDebugSystem *_debugSystem;

public:
    SensorReader(IAnalogSensorReader *analogReader,
                 IModbusSensorReader *modbusReader,
                 IArduinoAdapter *arduino,
                 ILogger *logger,
                 IDebugSystem *debugSystem)
        : _analogReader(analogReader),
          _modbusReader(modbusReader),
          _arduino(arduino),
          _logger(logger),
          _debugSystem(debugSystem) {}

    void init(unsigned long modbusBaud = 9600)
    {
        if (_modbusReader)
        {
            _modbusReader->init(modbusBaud);
            log("SensorReader: Initialized Modbus reader at %lu baud", modbusBaud);
        }
    }

    bool readSensor(const SensorEntity &sensor, float &outValue) override
    {
        if (sensor.protocol == "Analog")
        {
            return readAnalogSensor(sensor, outValue);
        }
        else if (sensor.protocol == "ModBus")
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

    bool scanSensor(const SensorEntity &sensor) override
    {
        if (sensor.protocol == "Analog")
        {
            return scanAnalogSensor(sensor);
        }
        else if (sensor.protocol == "ModBus")
        {
            return scanModbusSensor(sensor);
        }
        else if (sensor.protocol == "I2C")
        {
            return scanI2CSensor(sensor);
        }

        return false;
    }

    SensorReadResult readSensorDetailed(const SensorEntity &sensor) override
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

            if (!scanSensor(sensor))
            {
                return SensorReadResult::createError("Sensor is offline", SensorReadStatus::SENSOR_OFFLINE);
            }

            return SensorReadResult::createError("Communication error", SensorReadStatus::COMMUNICATION_ERROR);
        }
    }

    bool readMultiChannelSensor(const SensorEntity &sensor, std::map<std::string, float> &channelValues) override
    {
        if (!sensor.isMultiChannel())
        {
            return false;
        }

        if (sensor.protocol == "ModBus")
        {
            if (!_modbusReader || sensor.modbusDeviceAddress == 0)
            {
                return false;
            }

            int portIndex = sensor.pin;

            for (const auto &registerConfig : sensor.modbusRegisterConfigs)
            {
                float value;
                if (_modbusReader->readRegister(sensor.modbusDeviceAddress, registerConfig, value, portIndex))
                {
                    std::string uid = sensor.uid + "." + (registerConfig.channel < 10 ? "0" : "") + std::to_string(registerConfig.channel);
                    channelValues[uid] = value;
                    log("SensorReader: Read modbus channel %s, value: %.2f on port %d", registerConfig.channelName.c_str(), value, portIndex);
                }
                else
                {
                    log("SensorReader: Failed to read modbus channel %s on port %d", registerConfig.channelName.c_str(), portIndex);
                }
            }
        }

        return !channelValues.empty();
    }

private:
    bool readAnalogSensor(const SensorEntity &sensor, float &outValue)
    {
        if (!_analogReader)
        {
            return false;
        }

        try
        {
            outValue = _analogReader->readCalibratedValue(sensor.pin, sensor.multiplier, sensor.offset);
            log("SensorReader: Read analog sensor %s, pin %d, value: %.2f", sensor.uid.c_str(), sensor.pin, outValue);
            return true;
        }
        catch (...)
        {
            log("SensorReader: Exception reading analog sensor %s", sensor.uid.c_str());
            return false;
        }
    }

    bool readModbusSensor(const SensorEntity &sensor, float &outValue, int channelIndex = 0)
    {
        if (!_modbusReader || sensor.modbusDeviceAddress == 0)
        {
            return false;
        }

        if (sensor.modbusRegisterConfigs.empty())
        {
            log("SensorReader: No modbus register configs for sensor %s", sensor.uid.c_str());
            return false;
        }

        if (channelIndex >= sensor.modbusRegisterConfigs.size())
        {
            channelIndex = 0;
        }

        try
        {
            int portIndex = sensor.pin;
            const ModbusRegisterConfig &config = sensor.modbusRegisterConfigs[channelIndex];
            bool success = _modbusReader->readRegister(sensor.modbusDeviceAddress, config, outValue, portIndex);
            if (success)
            {
                log("SensorReader: Read modbus sensor %s, channel %d, value: %.2f on port %d",
                    sensor.uid.c_str(), channelIndex, outValue, portIndex);
            }
            return success;
        }
        catch (...)
        {
            log("SensorReader: Exception reading modbus sensor %s", sensor.uid.c_str());
            return false;
        }
    }

    bool readI2CSensor(const SensorEntity &sensor, float &outValue)
    {
        log("SensorReader: I2C sensor reading not yet implemented for %s", sensor.uid.c_str());
        return false;
    }

    bool scanAnalogSensor(const SensorEntity &sensor)
    {
        if (!_analogReader)
        {
            return false;
        }

        int rawValue = _analogReader->readRawValue(sensor.pin);
        return rawValue >= 0 && rawValue <= 4095;
    }

    bool scanModbusSensor(const SensorEntity &sensor)
    {
        if (!_modbusReader || sensor.modbusDeviceAddress == 0)
        {
            return false;
        }

        if (sensor.modbusRegisterConfigs.empty())
        {
            return false;
        }

        int portIndex = sensor.pin;
        float dummyValue;
        _modbusReader->init(sensor.baudRate);
        for (const auto &config : sensor.modbusRegisterConfigs)
        {
            if (!_modbusReader->readRegister(sensor.modbusDeviceAddress, config, dummyValue, portIndex))
            {
                return false;
            }
        }
        return true;
    }

    bool scanI2CSensor(const SensorEntity &sensor)
    {
        return false;
    }

    void log(const char *format, ...)
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

    int extractPinFromUid(const std::string &uid) const
    {
        // Extract pin number from UID format: <module_uid>.<pin>
        size_t lastDot = uid.find_last_of('.');
        if (lastDot != std::string::npos && lastDot < uid.length() - 1)
        {
            try
            {
                std::string pinStr = uid.substr(lastDot + 1);
                return std::stoi(pinStr);
            }
            catch (...)
            {
                return -1;
            }
        }
        return -1;
    }
};

#endif // SENSOR_READER_H
