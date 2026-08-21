#ifndef MOCK_SENSOR_READER_H
#define MOCK_SENSOR_READER_H

#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include <algorithm>
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
    std::vector<int> probedAddresses;
    int writeSensorAddressCallCount = 0;
    int lastWriteFromAddress = -1;
    int lastWriteToAddress = -1;

    // Configurable results
    bool readSensorReturnValue = true;
    float readSensorValue = 0.0f;
    bool scanSensorReturnValue = true;
    bool readMultiChannelReturnValue = true;
    // "full channel uid" -> value, exactly what the real reader produces
    std::map<std::string, float> multiChannelValues;
    bool writeSensorAddressReturnValue = true;
    bool setModbusPortPowerReturnValue = true;
    std::vector<int> answeringAddresses;
    bool answersAtNewAddressAfterWrite = true;

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
        probedAddresses.clear();
        writeSensorAddressCallCount = 0;
        lastWriteFromAddress = -1;
        lastWriteToAddress = -1;

        readSensorReturnValue = true;
        readSensorValue = 0.0f;
        scanSensorReturnValue = true;
        readMultiChannelReturnValue = true;
        multiChannelValues.clear();
        writeSensorAddressReturnValue = true;
        setModbusPortPowerReturnValue = true;
        answeringAddresses.clear();
        answersAtNewAddressAfterWrite = true;
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

    /**
     * @brief Probe of one address; answeringAddresses, when filled, says which addresses answer
     * @param sensor Entity carrying the address under probe
     */
    bool scanSensor(const SensorEntity &sensor) override
    {
        scanSensorCallCount++;
        lastSensorUid = sensor.uid;
        probedAddresses.push_back(sensor.modbusDeviceAddress);

        if (answeringAddresses.empty())
        {
            return scanSensorReturnValue;
        }

        return std::find(answeringAddresses.begin(), answeringAddresses.end(),
                         sensor.modbusDeviceAddress) != answeringAddresses.end();
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

    /**
     * @brief Address write; on success the device answers at the new address unless the test says
     *        it does not (answersAtNewAddressAfterWrite)
     * @param sensor Entity carrying the address the device answers at today
     * @param newAddress Address the device is moved to
     */
    bool writeSensorAddress(const SensorEntity &sensor, int newAddress) override
    {
        writeSensorAddressCallCount++;
        lastSensorUid = sensor.uid;
        lastWriteFromAddress = sensor.modbusDeviceAddress;
        lastWriteToAddress = newAddress;

        if (writeSensorAddressReturnValue && answersAtNewAddressAfterWrite)
        {
            auto answered = std::find(answeringAddresses.begin(), answeringAddresses.end(),
                                      sensor.modbusDeviceAddress);
            if (answered != answeringAddresses.end())
            {
                *answered = newAddress;
            }
        }

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
