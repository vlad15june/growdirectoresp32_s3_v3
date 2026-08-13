#ifndef VPD_CHANNEL_USE_CASE_H
#define VPD_CHANNEL_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/ModbusRegisterConfig.h"
#include "Core/Domain/Interfaces/UseCases/IVpdChannelUseCase.h"
#include <cmath>
#include <string>
#include <vector>

/**
 * @brief Computed VPD (vapor pressure deficit) channel of a multi-channel sensor.
 *
 * Pure domain logic: no Arduino, no logger, no repositories - so it is covered by native tests.
 * A sensor that exposes both an "air_temp" and a "humidity" register channel additionally
 * exposes a computed "vpd" channel numbered max(register channel) + 1.
 *
 * An object with an interface and no static methods (review of PR #17, 2026-08-13): callers get
 * it as IVpdChannelUseCase, so the arithmetic can be replaced in a test and the dependency is
 * visible in the constructor of whoever uses it.
 */
class VpdChannelUseCase : public IVpdChannelUseCase
{
public:
    static constexpr const char *AIR_TEMP_TYPE = "air_temp";
    static constexpr const char *HUMIDITY_TYPE = "humidity";
    static constexpr const char *VPD_TYPE = "vpd";
    static constexpr uint8_t MAX_CHANNEL = 99;

    const char *channelType() const override
    {
        return VPD_TYPE;
    }

    /**
     * @brief Find the air temperature / humidity channel pair of one sensor
     * @return true only when BOTH channels are present
     */
    bool findAirPair(const std::vector<ModbusRegisterConfig> &configs,
                     uint8_t &tempCh,
                     uint8_t &humCh) const override
    {
        bool hasTemp = false;
        bool hasHum = false;

        for (const auto &config : configs)
        {
            if (!hasTemp && config.sensorType == AIR_TEMP_TYPE)
            {
                tempCh = config.channel;
                hasTemp = true;
            }
            else if (!hasHum && config.sensorType == HUMIDITY_TYPE)
            {
                humCh = config.channel;
                hasHum = true;
            }
        }

        return hasTemp && hasHum;
    }

    /**
     * @brief Channel number of the computed VPD channel: max(register channel) + 1
     * @return false when the sensor has no air pair or the number does not fit two digits
     */
    bool vpdChannelNumber(const std::vector<ModbusRegisterConfig> &configs, uint8_t &out) const override
    {
        uint8_t tempCh = 0;
        uint8_t humCh = 0;
        if (!findAirPair(configs, tempCh, humCh))
        {
            return false;
        }

        int maxCh = 0;
        for (const auto &config : configs)
        {
            if (static_cast<int>(config.channel) > maxCh)
            {
                maxCh = config.channel;
            }
        }

        int vpdCh = maxCh + 1;
        if (vpdCh > static_cast<int>(MAX_CHANNEL))
        {
            return false;
        }

        out = static_cast<uint8_t>(vpdCh);
        return true;
    }

    /**
     * @brief Whether the sensor exposes a computed VPD channel at all
     */
    bool hasVpdChannel(const std::vector<ModbusRegisterConfig> &configs) const override
    {
        uint8_t channel = 0;
        return vpdChannelNumber(configs, channel);
    }

    /**
     * @brief VPD in kPa (Magnus formula, same as S2) - no rounding, no clamping
     */
    float compute(float temperatureC, float humidityPercent) const override
    {
        float es = 0.6108f * expf(17.27f * temperatureC / (temperatureC + 237.3f));
        float ea = humidityPercent / 100.0f * es;
        return es - ea;
    }

    /**
     * @brief Full channel uid: "<sensor uid>.<two digits>" - same format as register channels
     */
    std::string channelUid(const std::string &sensorUid, uint8_t channel) const override
    {
        return sensorUid + "." + (channel < 10 ? "0" : "") + std::to_string(channel);
    }

    uint8_t maxChannel() const override
    {
        return MAX_CHANNEL;
    }
};

#endif // VPD_CHANNEL_USE_CASE_H
