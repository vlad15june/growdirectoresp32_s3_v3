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
    // The entity as it arrived at the address write: the real reader takes the store register, the
    // write command and the power-cycle flag from it, so a test can see what the device would get.
    SensorEntity lastWriteSensor;
    // Port of every address write, in order: "did anything write an address on THAT port" is a
    // different question from "how many address writes were there".
    std::vector<int> addressWritePins;
    // Broadcast address writes - the ones that do not know the address they write to - by port,
    // in order, and whether the device on the port takes them. Off by default: a port the test
    // says nothing about has nothing on it that moves.
    int writeSensorAddressByBroadcastCallCount = 0;
    std::vector<int> broadcastWritePins;
    SensorEntity lastBroadcastWriteSensor;
    bool writeSensorAddressByBroadcastReturnValue = false;
    // Which ports have their power up, and every request to move it, in order.
    std::map<int, bool> portPowered;
    std::vector<std::pair<int, bool> > portPowerCalls;

    // Configurable results
    bool readSensorReturnValue = true;
    float readSensorValue = 0.0f;
    // The addresses a single read is answered at. Empty - the default - means every address
    // answers alike, by readSensorReturnValue; a stand whose discovery asks a whole range of
    // addresses has to say which of them has something behind it.
    std::vector<int> readAnsweringAddresses;
    bool scanSensorReturnValue = true;
    bool readMultiChannelReturnValue = true;
    // "full channel uid" -> value, exactly what the real reader produces
    std::map<std::string, float> multiChannelValues;
    // The same two, per sensor, for a stand with more than one row on the line: a module where
    // every sensor answers or falls silent together cannot show what one row does while another
    // is doing something else. Empty - the default - means every sensor is answered alike.
    std::map<std::string, bool> multiChannelAnswerByUid;
    std::map<std::string, std::map<std::string, float> > multiChannelValuesByUid;
    bool writeSensorAddressReturnValue = true;
    // The same, per sensor, for a stand where one device takes the address the module writes it
    // and another does not: a reader that answers alike for every row cannot show what the module
    // makes of one refusal while another device moves. Empty - the default - means every device
    // answers alike.
    std::map<std::string, bool> writeSensorAddressAnswerByUid;
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
        lastWriteSensor = SensorEntity();
        addressWritePins.clear();
        writeSensorAddressByBroadcastCallCount = 0;
        broadcastWritePins.clear();
        lastBroadcastWriteSensor = SensorEntity();
        writeSensorAddressByBroadcastReturnValue = false;
        portPowered.clear();
        portPowerCalls.clear();

        readSensorReturnValue = true;
        readSensorValue = 0.0f;
        readAnsweringAddresses.clear();
        scanSensorReturnValue = true;
        readMultiChannelReturnValue = true;
        multiChannelValues.clear();
        multiChannelAnswerByUid.clear();
        multiChannelValuesByUid.clear();
        writeSensorAddressReturnValue = true;
        writeSensorAddressAnswerByUid.clear();
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
        const bool answered =
            readAnsweringAddresses.empty()
                ? readSensorReturnValue
                : std::find(readAnsweringAddresses.begin(), readAnsweringAddresses.end(),
                            sensor.modbusDeviceAddress) != readAnsweringAddresses.end();
        if (answered)
        {
            outValue = readSensorValue;
        }
        return answered;
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

        std::map<std::string, bool>::const_iterator answer = multiChannelAnswerByUid.find(sensor.uid);
        const bool answers = (answer != multiChannelAnswerByUid.end()) ? answer->second
                                                                       : readMultiChannelReturnValue;
        if (!answers)
        {
            return false;
        }

        std::map<std::string, std::map<std::string, float> >::const_iterator values =
            multiChannelValuesByUid.find(sensor.uid);
        channelValues = (values != multiChannelValuesByUid.end()) ? values->second : multiChannelValues;
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
        lastWriteSensor = sensor;
        addressWritePins.push_back(sensor.pin);

        std::map<std::string, bool>::const_iterator answer =
            writeSensorAddressAnswerByUid.find(sensor.uid);
        const bool takesTheAddress = (answer != writeSensorAddressAnswerByUid.end())
                                         ? answer->second
                                         : writeSensorAddressReturnValue;

        if (takesTheAddress && answersAtNewAddressAfterWrite)
        {
            auto answered = std::find(answeringAddresses.begin(), answeringAddresses.end(),
                                      sensor.modbusDeviceAddress);
            if (answered != answeringAddresses.end())
            {
                *answered = newAddress;
            }
        }

        return takesTheAddress;
    }

    /**
     * @brief Broadcast address write; a device that takes it answers at the new address from then
     *        on, exactly as after writeSensorAddress()
     */
    bool writeSensorAddressByBroadcast(const SensorEntity &sensor, int newAddress) override
    {
        writeSensorAddressByBroadcastCallCount++;
        lastSensorUid = sensor.uid;
        lastBroadcastWriteSensor = sensor;
        broadcastWritePins.push_back(sensor.pin);

        if (writeSensorAddressByBroadcastReturnValue &&
            std::find(answeringAddresses.begin(), answeringAddresses.end(), newAddress) ==
                answeringAddresses.end())
        {
            answeringAddresses.push_back(newAddress);
        }
        return writeSensorAddressByBroadcastReturnValue;
    }

    void prepareSensorsForReading(const std::vector<SensorEntity> &) override
    {
        prepareSensorsForReadingCallCount++;
    }

    /**
     * @brief Port power, with the state the real reader keeps behind it.
     *
     * ModbusRS485 answers "did this change anything" - false when the pin was already where the
     * caller wants it - and callers act on that answer: the isolation of an address write skips
     * its ten-second warm-up when the port was already on, and the discovery waits for a port to
     * come up only when it had to switch it on. A stand-in that always says "changed" makes every
     * one of those decisions the wrong way round.
     *
     * Ports start unpowered, as the board starts them: ModbusRS485's constructor drives every
     * SENSOR_POWER pin low.
     */
    bool setModbusPortPower(int portIndex, bool on) override
    {
        lastPortIndex = portIndex;
        lastPortPowerOn = on;
        portPowerCalls.push_back(std::make_pair(portIndex, on));

        if (!setModbusPortPowerReturnValue)
        {
            return false;
        }

        std::map<int, bool>::iterator known = portPowered.find(portIndex);
        const bool wasOn = (known != portPowered.end()) && known->second;
        portPowered[portIndex] = on;
        return wasOn != on;
    }

    /**
     * No clock, so no moments: a port is up or down as the stand says, and up for good - the
     * reads of this mock answer whatever the power. A stand about what a port needs before it
     * answers uses SimRs485Bus, which keeps the record ModbusRS485 keeps. A port no stand has
     * touched is up, as the reads have always found it.
     */
    PortPower modbusPortPower(int portIndex) override
    {
        PortPower power;
        std::map<int, bool>::const_iterator known = portPowered.find(portIndex);
        power.on = (known == portPowered.end()) || known->second;
        return power;
    }

    bool isPortPowered(int portIndex) const
    {
        std::map<int, bool>::const_iterator known = portPowered.find(portIndex);
        return (known != portPowered.end()) && known->second;
    }

    size_t portPowerCallCount(int portIndex, bool on) const
    {
        size_t count = 0;
        for (size_t i = 0; i < portPowerCalls.size(); i++)
        {
            if (portPowerCalls[i].first == portIndex && portPowerCalls[i].second == on)
            {
                count++;
            }
        }
        return count;
    }
};

#endif // MOCK_SENSOR_READER_H
