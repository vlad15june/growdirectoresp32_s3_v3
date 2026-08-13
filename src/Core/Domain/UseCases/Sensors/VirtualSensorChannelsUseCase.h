#ifndef VIRTUAL_SENSOR_CHANNELS_USE_CASE_H
#define VIRTUAL_SENSOR_CHANNELS_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Interfaces/UseCases/IVirtualSensorChannelsUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IVpdChannelUseCase.h"
#include <map>
#include <string>
#include <vector>

/**
 * @brief Virtual (computed) channels of a sensor - the single place that knows about them.
 *
 * A virtual channel has no register config behind it: the module calculates it from register
 * channels of the same sensor and then publishes it like any other sub-sensor. VPD is the first
 * one, more are expected, and the point of this use case is that the next one is wired in the
 * composition root: the reading path, the update packet and the module structure keep their
 * single call.
 *
 * The kind of channel is a dependency, not knowledge of this class (review of PR #17,
 * 2026-08-13): it is given IVpdChannelUseCase through the constructor, so nothing here is static
 * and a test can substitute the arithmetic.
 *
 * Pure domain logic - no Arduino, no logger, no repositories - so it is covered by native tests.
 * Callers do the logging: the text belongs to the service that has the debug feature.
 *
 * Order of operations (owner decision, 2026-08-13): a virtual channel is computed from the RAW
 * readings of the current read, and the computed value is then subject to the same checks as any
 * other value of that read (see the plausibility gate of GDV1-3810).
 */
class VirtualSensorChannelsUseCase : public IVirtualSensorChannelsUseCase
{
private:
    IVpdChannelUseCase *vpdChannel;

public:
    explicit VirtualSensorChannelsUseCase(IVpdChannelUseCase *vpdChannel = nullptr)
        : vpdChannel(vpdChannel)
    {
    }

    /**
     * @brief Virtual channels of this sensor - what the structure and the packet must list
     * @return empty when the sensor exposes none
     */
    std::vector<Channel> channels(const SensorEntity &sensor) const override
    {
        std::vector<Channel> result;

        if (!vpdChannel)
        {
            return result;
        }

        uint8_t channelNumber = 0;
        if (vpdChannel->vpdChannelNumber(sensor.modbusRegisterConfigs, channelNumber))
        {
            result.push_back({vpdChannel->channelUid(sensor.uid, channelNumber),
                              vpdChannel->channelType(),
                              channelNumber});
        }

        return result;
    }

    /**
     * @brief Values computable from THIS read, calculated from the raw channel values
     * @param rawValues untouched read of the register channels ("<uid>.<NN>" -> value)
     * @param partial   channels whose sources were not all read this time: the caller keeps the
     *                  cached value, exactly like for any register channel that did not answer
     */
    std::vector<Value> values(const SensorEntity &sensor,
                              const std::map<std::string, float> &rawValues,
                              std::vector<Channel> &partial) const override
    {
        std::vector<Value> result;

        if (!vpdChannel)
        {
            return result;
        }

        uint8_t tempChannel = 0;
        uint8_t humidityChannel = 0;
        uint8_t channelNumber = 0;
        if (vpdChannel->findAirPair(sensor.modbusRegisterConfigs, tempChannel, humidityChannel) &&
            vpdChannel->vpdChannelNumber(sensor.modbusRegisterConfigs, channelNumber))
        {
            Channel channel{vpdChannel->channelUid(sensor.uid, channelNumber),
                            vpdChannel->channelType(),
                            channelNumber};

            auto temperature = rawValues.find(vpdChannel->channelUid(sensor.uid, tempChannel));
            auto humidity = rawValues.find(vpdChannel->channelUid(sensor.uid, humidityChannel));

            if (temperature != rawValues.end() && humidity != rawValues.end())
            {
                result.push_back({channel.uid,
                                  channel.type,
                                  vpdChannel->compute(temperature->second, humidity->second),
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
    bool hasOverflow(const SensorEntity &sensor, std::string &type) const override
    {
        if (!vpdChannel)
        {
            return false;
        }

        uint8_t tempChannel = 0;
        uint8_t humidityChannel = 0;
        uint8_t channelNumber = 0;
        if (vpdChannel->findAirPair(sensor.modbusRegisterConfigs, tempChannel, humidityChannel) &&
            !vpdChannel->vpdChannelNumber(sensor.modbusRegisterConfigs, channelNumber))
        {
            type = vpdChannel->channelType();
            return true;
        }

        return false;
    }

    /** @brief Highest channel number a virtual channel can take (two-digit uid format) */
    uint8_t maxChannel() const override
    {
        return vpdChannel ? vpdChannel->maxChannel() : 0;
    }
};

#endif // VIRTUAL_SENSOR_CHANNELS_USE_CASE_H
