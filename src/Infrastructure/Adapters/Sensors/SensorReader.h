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
            // return scanAnalogSensor(sensor);
            return false; // NOTE: temporary disable analog scan
        }
        else if (sensor.protocol == "ModBus")
        {
            return scanModbusSensor(sensor);
        }
        else if (sensor.protocol == "I2C")
        {
            // return scanI2CSensor(sensor);
            return false; // NOTE: temporary disable I2C scan
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
                bool success = false;

                // Try up to 3 times
                for (int attempt = 1; attempt <= 3; attempt++)
                {
                    if (_modbusReader->readRegister(sensor.modbusDeviceAddress, registerConfig, value, portIndex))
                    {
                        success = true;
                        break;
                    }

                    if (attempt < 3)
                    {
                        log("SensorReader: Failed to read modbus channel %s on port %d (attempt %d/3), retrying...",
                            registerConfig.channelName.c_str(), portIndex, attempt);
                        if (_arduino)
                        {
                            _arduino->delay(100); // Small delay between retries
                        }
                    }
                }

                if (success)
                {
                    std::string uid = sensor.uid + "." + (registerConfig.channel < 10 ? "0" : "") + std::to_string(registerConfig.channel);
                    channelValues[uid] = value;
                    log("SensorReader: Read modbus channel %s, value: %.2f on port %d", registerConfig.channelName.c_str(), value, portIndex);
                }
                else
                {
                    log("SensorReader: Failed to read modbus channel %s on port %d after 3 attempts", registerConfig.channelName.c_str(), portIndex);
                }
            }
        }

        return !channelValues.empty();
    }

    bool writeSensorAddress(const SensorEntity &sensor, int newAddress) override
    {
        if (sensor.protocol == "ModBus")
        {
            if (!_modbusReader)
            {
                log("SensorReader: ModBus reader not available for address write");
                return false;
            }

            int portIndex = sensor.pin;
            return _modbusReader->writeSensorAddress(sensor.modbusDeviceAddress, newAddress, portIndex);
        }
        else
        {
            log("SensorReader: Address writing not supported for protocol: %s", sensor.protocol.c_str());
            return false;
        }
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
        if (!_modbusReader)
        {
            return false;
        }

        if (sensor.modbusRegisterConfigs.empty())
        {
            return false;
        }

        int portIndex = sensor.pin;
        _modbusReader->init(sensor.baudRate);

        // Power control: Turn off all ports first to avoid interference
        log("SensorReader: Powering off all ports for clean scan");
        for (int i = 0; i < 4; i++) // NOTE: TODO: need read/set from config
        {
            _modbusReader->powerOffPort(i);            
        }
        if (_arduino)
        {
            _arduino->delay(500); // Wait for ports to power down completely
        }

        // Power on only the target port
        log("SensorReader: Powering on port %d for scanning", portIndex);
        _modbusReader->powerOnPort(portIndex);
        if (_arduino)
        {
            _arduino->delay(1000); // Wait for sensor to stabilize
        }

        // Try to query device address using 6EH function code first
        uint8_t actualDeviceAddress = queryDeviceAddress(portIndex);
        uint8_t deviceAddressToUse = (actualDeviceAddress != 0) ? actualDeviceAddress : sensor.modbusDeviceAddress;

        log("SensorReader: Using device address %d for scanning (queried: %d, configured: %d)",
            deviceAddressToUse, actualDeviceAddress, sensor.modbusDeviceAddress);

        // First, ping the device to check if it responds at this address
        if (_modbusReader->pingAddress(deviceAddressToUse, portIndex))
        {
            log("SensorReader: Device responds at address %d, proceeding with scan", deviceAddressToUse);
        }
        else
        {
            log("SensorReader: Device does not respond at address %d, trying address range 0x70-0x78", deviceAddressToUse);

            // Try addresses from 0x70 to 0x78
            bool foundAddress = false;
            for (uint8_t testAddress = 0x70; testAddress <= 0x78; testAddress++)
            {
                if (_modbusReader->pingAddress(testAddress, portIndex))
                {
                    deviceAddressToUse = testAddress;
                    foundAddress = true;
                    log("SensorReader: Found responding device at address 0x%02X", testAddress);
                    break;
                }
            }

            if (!foundAddress)
            {
                log("SensorReader: No responding device found in address range 0x70-0x78");
                return false;
            }
        }

        // Now scan all register configurations with the found address
        bool scanSuccess = true;
        for (const auto &config : sensor.modbusRegisterConfigs)
        {
            bool success = false;
            float dummyValue;

            // Try up to 3 times
            for (int attempt = 1; attempt <= 3; attempt++)
            {
                if (_modbusReader->readRegister(deviceAddressToUse, config, dummyValue, portIndex))
                {
                    // Check if the read value is within the expected range for this sensor type
                    if (dummyValue >= config.canConnectMinValue && dummyValue <= config.canConnectMaxValue)
                    {
                        success = true;
                        log("SensorReader: Scan successful - read value %.2f is within range [%.2f, %.2f] for channel %s",
                            dummyValue, config.canConnectMinValue, config.canConnectMaxValue, config.channelName.c_str());
                        break;
                    }
                    else
                    {
                        log("SensorReader: Read value %.2f is outside expected range [%.2f, %.2f] for channel %s",
                            dummyValue, config.canConnectMinValue, config.canConnectMaxValue, config.channelName.c_str());
                    }
                }

                if (attempt < 3)
                {
                    log("SensorReader: Failed to scan modbus register on port %d (attempt %d/3), retrying...",
                        portIndex, attempt);
                    if (_arduino)
                    {
                        _arduino->delay(100); // Small delay between retries
                    }
                }
            }

            if (!success)
            {
                log("SensorReader: Failed to scan modbus register %s on port %d after 3 attempts or value out of range",
                    config.channelName.c_str(), portIndex);
                scanSuccess = false;
                break;
            }
        }

        return scanSuccess;
    }

    // Helper method to query device address using 6EH function code
    uint8_t queryDeviceAddress(int portIndex)
    {
        if (!_modbusReader || portIndex < 0)
        {
            return 0;
        }

        ModbusRegisterConfig queryConfig;
        queryConfig.functionCode = 0x6E;
        queryConfig.startRegister = 0x0012;
        queryConfig.numRegisters = 1;
        queryConfig.multiplier = 1.0f;
        queryConfig.offset = 0.0f;

        float deviceAddressFloat;
        if (_modbusReader->readRegister(0xFF, queryConfig, deviceAddressFloat, portIndex))
        {
            uint8_t deviceAddress = (uint8_t)(deviceAddressFloat) & 0xFF; // Lower 8 bits are device address
            log("SensorReader: Successfully queried device address: %d", deviceAddress);
            return deviceAddress;
        }
        else
        {
            log("SensorReader: Failed to query device address using 6EH function code");
            return 0;
        }
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
