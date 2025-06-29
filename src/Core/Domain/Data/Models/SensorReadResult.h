#ifndef SENSOR_READ_RESULT_H
#define SENSOR_READ_RESULT_H

#include <string>
#include <map>

enum class SensorReadStatus
{
    SUCCESS,
    SENSOR_OFFLINE,
    COMMUNICATION_ERROR,
    INVALID_VALUE,
    CALIBRATION_ERROR,
    UNKNOWN_ERROR
};

struct SensorReadResult
{
    bool success;
    float value;
    SensorReadStatus status;
    std::string errorMessage;
    std::map<std::string, float> channelValues; // For multi-channel sensors
    uint32_t readTime;

    SensorReadResult() : success(false), value(0.0f), status(SensorReadStatus::UNKNOWN_ERROR), readTime(0) {}

    static SensorReadResult createSuccess(float val, uint32_t time = 0)
    {
        SensorReadResult result;
        result.success = true;
        result.value = val;
        result.status = SensorReadStatus::SUCCESS;
        result.readTime = time;
        return result;
    }

    static SensorReadResult createError(const std::string& message, SensorReadStatus stat = SensorReadStatus::UNKNOWN_ERROR)
    {
        SensorReadResult result;
        result.success = false;
        result.errorMessage = message;
        result.status = stat;
        return result;
    }

    static SensorReadResult createMultiChannelSuccess(const std::map<std::string, float>& channels, uint32_t time = 0)
    {
        SensorReadResult result;
        result.success = true;
        result.channelValues = channels;
        result.status = SensorReadStatus::SUCCESS;
        result.readTime = time;
        return result;
    }
};

#endif // SENSOR_READ_RESULT_H
