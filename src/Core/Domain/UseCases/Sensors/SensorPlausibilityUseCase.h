#ifndef SENSOR_PLAUSIBILITY_USE_CASE_H
#define SENSOR_PLAUSIBILITY_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/UseCases/ISensorPlausibilityUseCase.h"
#include <string>

/**
 * @brief Plausibility gate for sensor readings (GDV1-3810).
 *
 * Every sensor type carries a physical range in limitMinValue/limitMaxValue. A reading outside
 * that range means a broken transaction, a stuck register or a disconnected probe, not a real
 * measurement, so it must never reach the value model, the server or the rules.
 *
 * minValue/maxValue are NOT used here: those are the recommended working range (UI/agronomy),
 * not plausibility.
 *
 * The range of a channel is the UNION of the two ranges the module already knows about (GDV1-3826):
 * limitMinValue/limitMaxValue of the sub sensor type and canConnectMinValue/canConnectMaxValue of
 * the register config of that very channel — the same pair the scan has always accepted a sensor
 * by (SensorReader::scanSensor). A value is dropped only when it is outside BOTH, because either
 * source alone is regularly the narrower one: a wide-range channel is described by its register
 * config (raw counts up to 100000) while its sub type says 0..100, and mp-508c/soil_temp is the
 * other way round (canConnect 10..40 against a sub type of 0..100). Gating on one of them alone
 * rejects healthy readings and reports a live channel as disconnected.
 *
 * The reading path gets this object through ISensorPlausibilityUseCase (review of PR #18,
 * 2026-08-13): the sensor type repository is a dependency of the gate, not of the service.
 */
class SensorPlausibilityUseCase : public ISensorPlausibilityUseCase
{
public:
    explicit SensorPlausibilityUseCase(ISensorTypeRepository *sensorTypeRepository = nullptr)
        : _sensorTypeRepository(sensorTypeRepository) {}

    void setSensorTypeRepository(ISensorTypeRepository *sensorTypeRepository)
    {
        _sensorTypeRepository = sensorTypeRepository;
    }

    /**
     * @brief The decision itself: is the value inside the physical range?
     * @param value Reading to check
     * @param limits Range resolved for the channel; undefined range accepts everything
     */
    bool isWithinLimits(float value, const Limits &limits) const override
    {
        if (!limits.defined)
        {
            return true;
        }
        return value >= limits.min && value <= limits.max;
    }

    /**
     * @brief Resolve the physical range of one channel of a sensor.
     * @param sensor Sensor being read
     * @param subSensorTypeName Name of the sub sensor type (channel), e.g. "air_temp"
     * @return Union of the sub sensor type range and the register config range of that channel;
     *         defined == false when the sub sensor type declares no range, and then everything is
     *         accepted exactly as before the gate existed
     */
    Limits getLimits(const SensorEntity &sensor, const std::string &subSensorTypeName) const override
    {
        Limits limits = subSensorTypeRange(sensor, subSensorTypeName);

        if (!limits.defined)
        {
            // Nothing to gate on (no sensor type, no such channel, no bounds sent) — and the
            // register config alone must not start gating a channel the catalogue says nothing
            // about: that would make the gate stricter than it is today, which is the very defect
            // this change is about. Adding a source may only widen the range, never create one.
            return limits;
        }

        widen(limits, registerConfigRange(sensor, subSensorTypeName));
        return limits;
    }

    /**
     * @brief Resolve the range and check the value against it in one call.
     * @param outLimits Range that was applied — for the rejection log line
     */
    bool isPlausible(const SensorEntity &sensor,
                     const std::string &subSensorTypeName,
                     float value,
                     Limits &outLimits) const override
    {
        outLimits = getLimits(sensor, subSensorTypeName);
        return isWithinLimits(value, outLimits);
    }

private:
    ISensorTypeRepository *_sensorTypeRepository;

    /**
     * @brief Grow a range so that it also covers another one (union).
     *
     * A source that declares nothing contributes nothing, so the result is never narrower than any
     * single source — the gate can only become more permissive by adding a source, never stricter.
     */
    void widen(Limits &limits, const Limits &source) const
    {
        if (!source.defined)
        {
            return;
        }

        if (!limits.defined)
        {
            limits = source;
            return;
        }

        if (source.min < limits.min)
        {
            limits.min = source.min;
        }
        if (source.max > limits.max)
        {
            limits.max = source.max;
        }
    }

    /**
     * @brief Physical range of the channel as the sensor type catalogue declares it (GDV1-3810).
     * @return Range; defined == false when there is no sensor type, no such channel, or the server
     *         sent limitMinValue == limitMaxValue == 0
     */
    Limits subSensorTypeRange(const SensorEntity &sensor, const std::string &subSensorTypeName) const
    {
        Limits limits;

        if (!_sensorTypeRepository)
        {
            return limits;
        }

        SensorType sensorType;
        if (!findSensorType(sensor, sensorType))
        {
            return limits;
        }

        const SubSensorType *subSensorType = nullptr;
        if (!subSensorTypeName.empty())
        {
            subSensorType = sensorType.getSensorTypeByName(subSensorTypeName);
        }
        if (!subSensorType && sensorType.sensorTypes.size() == 1)
        {
            // Single-channel sensor type: the only sub type is unambiguously the one being read
            subSensorType = &sensorType.sensorTypes[0];
        }
        if (!subSensorType)
        {
            return limits;
        }

        if (subSensorType->limitMinValue == 0.0f && subSensorType->limitMaxValue == 0.0f)
        {
            return limits;
        }

        limits.min = subSensorType->limitMinValue;
        limits.max = subSensorType->limitMaxValue;
        limits.defined = true;
        return limits;
    }

    /**
     * @brief Range the register config of this channel declares a working sensor answers inside
     *        (canConnectMinValue/canConnectMaxValue) — what the scan accepts a sensor by.
     *
     * canConnectMaxValue <= canConnectMinValue means the config declares nothing usable: both at
     * the 0.0f default, or a min/max pair the server got the wrong way round. Such a config is
     * skipped, so the sub sensor type stays the only source — which is also the case for I2C
     * channels (their config has no canConnect fields at all) and for single-channel sensors (no
     * register configs at all).
     *
     * @return Range of the first register config of this channel, or an undefined range
     */
    Limits registerConfigRange(const SensorEntity &sensor, const std::string &subSensorTypeName) const
    {
        Limits limits;

        for (const auto &registerConfig : sensor.modbusRegisterConfigs)
        {
            if (!isChannelOf(registerConfig, subSensorTypeName, sensor.modbusRegisterConfigs.size()))
            {
                continue;
            }

            if (registerConfig.canConnectMaxValue <= registerConfig.canConnectMinValue)
            {
                return limits;
            }

            limits.min = registerConfig.canConnectMinValue;
            limits.max = registerConfig.canConnectMaxValue;
            limits.defined = true;
            return limits;
        }

        return limits;
    }

    /**
     * @brief Is this register config the channel being read?
     *
     * The name of a channel is taken the same way the reading path takes it
     * (SensorService::channelSensorTypeName): sensorType, and channelName when sensorType is empty.
     * An empty name with a single register config resolves like a single sub sensor type does — the
     * only channel is unambiguously the one being read.
     */
    bool isChannelOf(const ModbusRegisterConfig &registerConfig,
                     const std::string &subSensorTypeName,
                     size_t registerConfigCount) const
    {
        const std::string &channelName =
            registerConfig.sensorType.empty() ? registerConfig.channelName : registerConfig.sensorType;

        if (subSensorTypeName.empty())
        {
            return registerConfigCount == 1;
        }

        return channelName == subSensorTypeName;
    }

    bool findSensorType(const SensorEntity &sensor, SensorType &outSensorType) const
    {
        if (!sensor.sensorTypeUid.empty())
        {
            Utils::Optional<SensorType> found = _sensorTypeRepository->get(sensor.sensorTypeUid.c_str());
            if (found.has_value())
            {
                outSensorType = found.value();
                return true;
            }
        }

        if (!sensor.model.empty())
        {
            SensorType *byName = _sensorTypeRepository->getSensorTypeByName(sensor.model);
            if (byName)
            {
                outSensorType = *byName;
                return true;
            }
        }

        return false;
    }
};

#endif // SENSOR_PLAUSIBILITY_USE_CASE_H
