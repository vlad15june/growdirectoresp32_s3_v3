#ifndef I_VPD_CHANNEL_USE_CASE_H
#define I_VPD_CHANNEL_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/ModbusRegisterConfig.h"
#include <string>
#include <vector>

/**
 * @brief The computed VPD (vapor pressure deficit) channel of a multi-channel sensor.
 *
 * One kind of computed channel behind an interface: VirtualSensorChannelsUseCase is given the
 * kinds it publishes instead of naming them itself, so the next computed channel is a new
 * implementation of this interface plus a line in the composition root, and a test can hand the
 * caller its own arithmetic (review of PR #17, 2026-08-13).
 */
class IVpdChannelUseCase
{
public:
    virtual ~IVpdChannelUseCase() = default;

    /** @brief Sub-sensor type name this channel is published under, e.g. "vpd" */
    virtual const char *channelType() const = 0;

    /**
     * @brief Find the air temperature / humidity channel pair of one sensor
     * @return true only when BOTH channels are present
     */
    virtual bool findAirPair(const std::vector<ModbusRegisterConfig> &configs,
                             uint8_t &tempCh,
                             uint8_t &humCh) const = 0;

    /**
     * @brief Channel number of the computed VPD channel: max(register channel) + 1
     * @return false when the sensor has no air pair or the number does not fit two digits
     */
    virtual bool vpdChannelNumber(const std::vector<ModbusRegisterConfig> &configs, uint8_t &out) const = 0;

    /** @brief Whether the sensor exposes a computed VPD channel at all */
    virtual bool hasVpdChannel(const std::vector<ModbusRegisterConfig> &configs) const = 0;

    /** @brief VPD in kPa from one temperature / humidity reading */
    virtual float compute(float temperatureC, float humidityPercent) const = 0;

    /** @brief Full channel uid: "<sensor uid>.<two digits>" - same format as register channels */
    virtual std::string channelUid(const std::string &sensorUid, uint8_t channel) const = 0;

    /** @brief Highest channel number a computed channel can take (two-digit uid format) */
    virtual uint8_t maxChannel() const = 0;
};

#endif // I_VPD_CHANNEL_USE_CASE_H
