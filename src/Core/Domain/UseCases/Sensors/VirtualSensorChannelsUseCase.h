#ifndef VIRTUAL_SENSOR_CHANNELS_USE_CASE_H
#define VIRTUAL_SENSOR_CHANNELS_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include <map>
#include <string>
#include <vector>

/**
 * @brief Virtual (computed) channels of a sensor - the single place that knows about them.
 *
 * A virtual channel has no register config behind it: the module calculates it from register
 * channels of the same sensor and then publishes it like any other sub-sensor. VPD is the first
 * one, more are expected, and the point of this use case is that the next one is added HERE:
 * the reading path, the update packet and the module structure keep their single call.
 *
 * Pure domain logic - no Arduino, no logger, no repositories - so it is covered by native tests.
 * Callers do the logging: the text belongs to the service that has the debug feature.
 *
 * Order of operations (owner decision, 2026-08-13): a virtual channel is computed from the RAW
 * readings of the current read, and the computed value is then subject to the same checks as any
 * other value of that read (see the plausibility gate of GDV1-3810).
 */
class VirtualSensorChannelsUseCase
{
public:
    /** @brief A virtual channel the sensor exposes */
    struct Channel
    {
        std::string uid;  // "<sensor uid>.<two digits>", same format as register channels
        std::string type; // sub-sensor type name, e.g. "vpd"
        uint8_t channel;  // channel number inside the sensor
    };

    /** @brief A value of a virtual channel computed from one read */
    struct Value
    {
        std::string uid;
        std::string type;
        float value;
        float firstSource;  // the sources it was computed from, for the log line
        float secondSource;
    };

    /**
     * @brief Virtual channels of this sensor - what the structure and the packet must list
     * @return empty when the sensor exposes none
     */
    static std::vector<Channel> channels(const SensorEntity &sensor)
    {
        std::vector<Channel> result;

        uint8_t vpdChannel = 0;
        if (VpdChannelUseCase::vpdChannelNumber(sensor.modbusRegisterConfigs, vpdChannel))
        {
            result.push_back({VpdChannelUseCase::channelUid(sensor.uid, vpdChannel),
                              VpdChannelUseCase::VPD_TYPE,
                              vpdChannel});
        }

        return result;
    }

    /**
     * @brief Values computable from THIS read, calculated from the raw channel values
     * @param rawValues untouched read of the register channels ("<uid>.<NN>" -> value)
     * @param partial   channels whose sources were not all read this time: the caller keeps the
     *                  cached value, exactly like for any register channel that did not answer
     */
    static std::vector<Value> values(const SensorEntity &sensor,
                                     const std::map<std::string, float> &rawValues,
                                     std::vector<Channel> &partial)
    {
        std::vector<Value> result;

        uint8_t tempChannel = 0;
        uint8_t humidityChannel = 0;
        uint8_t vpdChannel = 0;
        if (VpdChannelUseCase::findAirPair(sensor.modbusRegisterConfigs, tempChannel, humidityChannel) &&
            VpdChannelUseCase::vpdChannelNumber(sensor.modbusRegisterConfigs, vpdChannel))
        {
            Channel channel{VpdChannelUseCase::channelUid(sensor.uid, vpdChannel),
                            VpdChannelUseCase::VPD_TYPE,
                            vpdChannel};

            auto temperature = rawValues.find(VpdChannelUseCase::channelUid(sensor.uid, tempChannel));
            auto humidity = rawValues.find(VpdChannelUseCase::channelUid(sensor.uid, humidityChannel));

            if (temperature != rawValues.end() && humidity != rawValues.end())
            {
                result.push_back({channel.uid,
                                  channel.type,
                                  VpdChannelUseCase::compute(temperature->second, humidity->second),
                                  temperature->second,
                                  humidity->second});
            }
            else
            {
                partial.push_back(channel);
            }
        }

        return result;
    }

    /**
     * @brief Sensor has the sources of a virtual channel, but the channel number did not fit
     * @param type sub-sensor type of the channel that could not be created (for the log)
     */
    static bool hasOverflow(const SensorEntity &sensor, std::string &type)
    {
        uint8_t tempChannel = 0;
        uint8_t humidityChannel = 0;
        uint8_t vpdChannel = 0;
        if (VpdChannelUseCase::findAirPair(sensor.modbusRegisterConfigs, tempChannel, humidityChannel) &&
            !VpdChannelUseCase::vpdChannelNumber(sensor.modbusRegisterConfigs, vpdChannel))
        {
            type = VpdChannelUseCase::VPD_TYPE;
            return true;
        }

        return false;
    }

    /** @brief Highest channel number a virtual channel can take (two-digit uid format) */
    static uint8_t maxChannel()
    {
        return VpdChannelUseCase::MAX_CHANNEL;
    }
};

#endif // VIRTUAL_SENSOR_CHANNELS_USE_CASE_H
