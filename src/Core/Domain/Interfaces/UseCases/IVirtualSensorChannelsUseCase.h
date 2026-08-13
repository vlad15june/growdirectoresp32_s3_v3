#ifndef I_VIRTUAL_SENSOR_CHANNELS_USE_CASE_H
#define I_VIRTUAL_SENSOR_CHANNELS_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include <map>
#include <string>
#include <vector>

/**
 * @brief Virtual (computed) channels of a sensor - the single place that knows about them.
 *
 * A virtual channel has no register config behind it: the module calculates it from register
 * channels of the same sensor and then publishes it like any other sub-sensor. The reading path,
 * the update packet, the module structure and the catalog sync all depend on this interface, so
 * none of them knows which computed channels exist or how they are calculated.
 */
class IVirtualSensorChannelsUseCase
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
        float firstSource; // the sources it was computed from, for the log line
        float secondSource;
    };

    virtual ~IVirtualSensorChannelsUseCase() = default;

    /**
     * @brief Virtual channels of this sensor - what the structure and the packet must list
     * @return empty when the sensor exposes none
     */
    virtual std::vector<Channel> channels(const SensorEntity &sensor) const = 0;

    /**
     * @brief Values computable from THIS read, calculated from the raw channel values
     * @param rawValues untouched read of the register channels ("<uid>.<NN>" -> value)
     * @param partial   channels whose sources were not all read this time: the caller keeps the
     *                  cached value, exactly like for any register channel that did not answer
     */
    virtual std::vector<Value> values(const SensorEntity &sensor,
                                      const std::map<std::string, float> &rawValues,
                                      std::vector<Channel> &partial) const = 0;

    /**
     * @brief Sensor has the sources of a virtual channel, but the channel number did not fit
     * @param type sub-sensor type of the channel that could not be created (for the log)
     */
    virtual bool hasOverflow(const SensorEntity &sensor, std::string &type) const = 0;

    /** @brief Highest channel number a virtual channel can take (two-digit uid format) */
    virtual uint8_t maxChannel() const = 0;
};

#endif // I_VIRTUAL_SENSOR_CHANNELS_USE_CASE_H
