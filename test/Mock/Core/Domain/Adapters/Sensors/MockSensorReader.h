#ifndef MOCK_SENSOR_READER_H
#define MOCK_SENSOR_READER_H

#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include <map>
#include <string>
#include <vector>

class MockSensorReader : public ISensorReader
{
public:
    // Call tracking
    bool initCalled = false;
    unsigned long lastInitBaud = 0;
    int readSensorCallCount = 0;
    int scanSensorCallCount = 0;
    int readMultiChannelCallCount = 0;
    int prepareSensorsForReadingCallCount = 0;
    std::string lastSensorUid;
    int lastPortIndex = -1;
    bool lastPortPowerOn = false;

    // Configurable results
    bool readSensorReturnValue = true;
    float readSensorValue = 0.0f;
    bool scanSensorReturnValue = true;
    bool readMultiChannelReturnValue = true;
    // "full channel uid" -> value, exactly what the real reader produces
    std::map<std::string, float> multiChannelValues;
    bool writeSensorAddressReturnValue = true;
    bool setModbusPortPowerReturnValue = true;

    void reset()
    {
        initCalled = false;
        lastInitBaud = 0;
        readSensorCallCount = 0;
        scanSensorCallCount = 0;
        readMultiChannelCallCount = 0;
        prepareSensorsForReadingCallCount = 0;
        lastSensorUid.clear();
        lastPortIndex = -1;
        lastPortPowerOn = false;

        readSensorReturnValue = true;
        readSensorValue = 0.0f;
        scanSensorReturnValue = true;
        readMultiChannelReturnValue = true;
        multiChannelValues.clear();
        writeSensorAddressReturnValue = true;
        setModbusPortPowerReturnValue = true;
    }

    void init(unsigned long modbusBaud = 9600) override
    {
        initCalled = true;
        lastInitBaud = modbusBaud;
    }

    bool readSensor(const SensorEntity &sensor, float &outValue) override
    {
        readSensorCallCount++;
        lastSensorUid = sensor.uid;
        if (readSensorReturnValue)
        {
            outValue = readSensorValue;
        }
        return readSensorReturnValue;
    }

    bool scanSensor(const SensorEntity &sensor) override
    {
        scanSensorCallCount++;
        lastSensorUid = sensor.uid;
        return scanSensorReturnValue;
    }

    SensorReadResult readSensorDetailed(const SensorEntity &sensor) override
    {
        SensorReadResult result;
        result.value = readSensorValue;
        result.success = readSensorReturnValue;
        lastSensorUid = sensor.uid;
        return result;
    }

    bool readMultiChannelSensor(const SensorEntity &sensor, std::map<std::string, float> &channelValues) override
    {
        readMultiChannelCallCount++;
        lastSensorUid = sensor.uid;
        if (!readMultiChannelReturnValue)
        {
            return false;
        }
        channelValues = multiChannelValues;
        return true;
    }

    bool writeSensorAddress(const SensorEntity &sensor, int) override
    {
        lastSensorUid = sensor.uid;
        return writeSensorAddressReturnValue;
    }

    void prepareSensorsForReading(const std::vector<SensorEntity> &) override
    {
        prepareSensorsForReadingCallCount++;
    }

    bool setModbusPortPower(int portIndex, bool on) override
    {
        lastPortIndex = portIndex;
        lastPortPowerOn = on;
        return setModbusPortPowerReturnValue;
    }
};

#endif // MOCK_SENSOR_READER_H
