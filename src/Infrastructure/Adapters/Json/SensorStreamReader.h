#pragma once

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/Json/JsonArrayStreamSplitter.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Builds the sensor registry out of a registry array, one sensor at a time.
 *
 * The array is cut into its objects as the text goes past and each object is handed to the
 * converter on its own, so what is alive at any moment is one sensor's text, one document and the
 * sensors built so far - never the whole registry in a document sized for it. The number of sensors
 * a module has therefore decides neither what reading the registry costs nor whether it can be read
 * at all: a document holding the whole array is a wall rather than a limit, and a registry one
 * sensor past such a wall yields no sensors at all rather than the sensors that fit. A registry
 * read as empty is a module that comes up with no sensors after a restart, and the first write
 * after that makes the loss permanent.
 *
 * A record the converter cannot read is left out, counted and passed over; the records around it
 * stand. Failing the whole registry over one record is what turns a single damaged entry into a
 * module with no sensors, and the entry that is damaged is the only one that has to be lost.
 *
 * What is still refused whole is a registry that stops in the middle or holds an object that never
 * closes: there is no telling a truncated file from a shorter one, and reading its complete prefix
 * would retire every sensor after the cut. That boundary is the splitter's, and it is the same one
 * the rule file and the sensor type catalog are read across.
 *
 * Sensors past the module's capacity are refused before they are built, and counted. The capacity
 * is the registry's, not this reader's - see SensorLimits::MAX_SENSORS_PER_MODULE - and it is given
 * by the caller so that a caller reading something other than this module's own registry can say
 * so.
 */
class SensorStreamReader : public BufferedJsonArraySplitter
{
public:
    /**
     * @brief The largest single sensor this reader builds one out of, in bytes of stored JSON.
     *
     * The capacity the converter parses one sensor into. A record longer than that cannot become a
     * sensor whatever this class does, so measuring it here only means the record is refused
     * without being cut into a buffer first.
     */
    static const size_t MAX_SENSOR_BYTES = 4096;

    /**
     * @brief The run of bytes past which the text is not a registry at all.
     *
     * Twice the largest sensor: an object that goes on beyond that has no end in sight, and
     * reaching this fails the text rather than truncating an object into a shorter one. It also
     * bounds the buffer one object is cut into.
     */
    static const size_t MAX_OBJECT_BYTES = 2 * MAX_SENSOR_BYTES;

    /**
     * @brief Room taken for the sensor being cut out, reserved once.
     *
     * A stored sensor of the shape the field has - four Modbus channels - is about 1.4 KB of JSON;
     * the buffer is reused for every record of the array, so this is the whole reader's appetite
     * for a registry of any length.
     */
    static const size_t SENSOR_INITIAL_CAPACITY = 1536;

    /**
     * @param maxSensors how many sensors to build out of this text, or zero for no limit
     */
    SensorStreamReader(IJsonConverter& converter, std::vector<SensorEntity>& sensors,
                       size_t maxSensors = SensorLimits::MAX_SENSORS_PER_MODULE)
        : BufferedJsonArraySplitter(MAX_OBJECT_BYTES, SENSOR_INITIAL_CAPACITY),
          _converter(converter),
          _sensors(sensors),
          _maxSensors(maxSensors)
    {
    }

    /** @brief What the text turned out to be - and no sensors at all unless it was readable. */
    JsonListParse finish() override
    {
        JsonListParse verdict = JsonArrayStreamSplitter::finish();
        if (verdict == JsonListParse::Failed)
        {
            _sensors.clear();
        }
        return verdict;
    }

    /** @brief Sensors built out of the text. */
    size_t readCount() const { return _readCount; }

    /** @brief Records that were sensors this module could not read, and were left out. */
    size_t skippedCount() const { return _skippedCount; }

    /** @brief Sensors the module did not take: past its capacity, or larger than it accepts. */
    size_t refusedCount() const { return _refusedCount; }

protected:
    bool onObject(const std::string& objectJson) override
    {
        // Refused before it is built, not after: a record past the module's capacity or larger than
        // one sensor may be costs nothing but the bytes it was cut into, which the next record
        // reuses.
        if (_maxSensors != 0 && _readCount >= _maxSensors)
        {
            _refusedCount++;
            return true;
        }

        if (objectJson.size() > MAX_SENSOR_BYTES)
        {
            _refusedCount++;
            return true;
        }

        SensorEntity sensor;
        if (!_converter.sensorFromJson(objectJson, sensor))
        {
            _skippedCount++;
            return true;
        }

        // Moved in: the strings of a sensor are its uids and channel names, and copying them to
        // hand the sensor over would put a second sensor in memory for the length of the call.
        _sensors.push_back(std::move(sensor));
        _readCount++;
        return true;
    }

private:
    IJsonConverter& _converter;
    std::vector<SensorEntity>& _sensors;
    size_t _maxSensors;
    size_t _readCount = 0;
    size_t _skippedCount = 0;
    size_t _refusedCount = 0;
};
