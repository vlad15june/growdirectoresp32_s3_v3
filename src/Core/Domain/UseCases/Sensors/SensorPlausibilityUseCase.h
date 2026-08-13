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
     * @brief Resolve the physical range of one channel of a sensor from its sensor type.
     * @param sensor Sensor being read
     * @param subSensorTypeName Name of the sub sensor type (channel), e.g. "air_temp"
     * @return Range; defined == false when there is no sensor type, no such channel, or the
     *         server sent limitMinValue == limitMaxValue == 0
     */
    Limits getLimits(const SensorEntity &sensor, const std::string &subSensorTypeName) const override
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
