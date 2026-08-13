#ifndef I_SENSOR_PLAUSIBILITY_USE_CASE_H
#define I_SENSOR_PLAUSIBILITY_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include <string>

/**
 * @brief Plausibility gate for sensor readings (GDV1-3810).
 *
 * The reading path depends on this interface, not on the implementation: the gate is one
 * interchangeable decision (today: the physical range of the sensor type), so a test can hand
 * SensorService its own verdict instead of a repository full of sensor types.
 */
class ISensorPlausibilityUseCase
{
public:
    /**
     * @brief Physical range resolved for one sensor channel.
     *
     * defined == false means the server sent no bounds for this channel (both are still at the
     * constructor default 0.0f) - nothing to gate on.
     */
    struct Limits
    {
        float min;
        float max;
        bool defined;

        Limits() : min(0.0f), max(0.0f), defined(false) {}
    };

    virtual ~ISensorPlausibilityUseCase() = default;

    /**
     * @brief Resolve the physical range of one channel of a sensor
     * @param sensor Sensor being read
     * @param subSensorTypeName Name of the sub sensor type (channel), e.g. "air_temp"
     */
    virtual Limits getLimits(const SensorEntity &sensor, const std::string &subSensorTypeName) const = 0;

    /**
     * @brief The decision itself: is the value inside the range?
     * @param value Reading to check
     * @param limits Range resolved for the channel; an undefined range accepts everything
     */
    virtual bool isWithinLimits(float value, const Limits &limits) const = 0;

    /**
     * @brief Resolve the range and check the value against it in one call
     * @param outLimits Range that was applied - for the rejection log line
     */
    virtual bool isPlausible(const SensorEntity &sensor,
                             const std::string &subSensorTypeName,
                             float value,
                             Limits &outLimits) const = 0;
};

#endif // I_SENSOR_PLAUSIBILITY_USE_CASE_H
