#pragma once

#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/Http/IHttpBodySink.h"
#include "Infrastructure/Adapters/Json/JsonArrayStreamSplitter.h"
#include "Infrastructure/Adapters/Json/SensorTypeStreamParser.h"

#include <cstddef>
#include <string>
#include <vector>

/**
 * @brief Builds the sensor type catalog out of a body that arrives in pieces.
 *
 * The array is cut into its objects and each object is read member by member, with the body never
 * held in one piece and no sensor type held in one piece either. What is alive at any moment is
 * the fields of the type being read, one array element of it, one JSON document and the types read
 * so far - so neither the length of the catalog nor the number of channels a model has decides
 * what reading the answer costs.
 *
 * A type this module cannot read is skipped, counted and left out of the catalog; the types around
 * it are unaffected. Failing the whole answer over one malformed model would stop a module from
 * receiving every other model's edits for as long as the malformed one is published, which is a
 * catalog frozen by a fault in one entry of it.
 *
 * What is not degradation is a body that yields nothing: an answer that carried objects and no
 * readable type is Failed, never Empty. Empty travels on as "the account publishes no sensor
 * models", the merge retires everything the module holds, and a module would empty its catalog
 * because it could not read it.
 *
 * A type that is skipped is also a type the answer no longer carries, and the merge retires what
 * the answer does not carry - so a model the module holds and the server republishes broken leaves
 * the stored catalog. That is the deliberate side of the trade: the alternative is the whole
 * catalog frozen until someone fixes the published model.
 */
class SensorTypeStreamReader : public JsonArrayStreamSplitter, public IHttpBodySink
{
public:
    /**
     * @brief Cap on one sensor type object, in bytes.
     *
     * Nothing accumulates the object - the fields and the array elements are read as they go past,
     * each under a cap of its own - so this bounds how long a single object may run before the body
     * is called nonsense, not a buffer. At around six hundred bytes of JSON per channel it covers a
     * model with roughly two dozen of them.
     */
    static const size_t MAX_TYPE_BYTES = 16384;

    explicit SensorTypeStreamReader(std::vector<SensorType>& sensorTypes)
        : JsonArrayStreamSplitter(MAX_TYPE_BYTES), _sensorTypes(sensorTypes)
    {
    }

    // --- IHttpBodySink ---------------------------------------------------------------------

    bool write(const char* data, size_t length) override
    {
        keepPrefix(data, length);
        feed(data, length);
        return !failed();
    }

    /**
     * @brief What the body turned out to be - and no sensor types at all unless it was readable.
     *
     * A body that stops in the middle has usually yielded some types before it stops. Handing
     * those on would be handing on a shorter catalog: the merge prunes what the answer does not
     * contain, so every type after the cut would be retired from a module that is working
     * perfectly well.
     */
    JsonListParse finish() override
    {
        JsonListParse verdict = JsonArrayStreamSplitter::finish();

        // Objects came out of the body and not one of them was a sensor type. That is an answer
        // this module cannot read, and it must not reach the merge as an empty catalog.
        if (verdict == JsonListParse::Ok && _readCount == 0)
        {
            verdict = JsonListParse::Failed;
        }

        if (verdict == JsonListParse::Failed)
        {
            _sensorTypes.clear();
        }
        return verdict;
    }

    /** @brief Sensor types read out of the body. */
    size_t readCount() const { return _readCount; }

    /** @brief Objects that were sensor types this module could not read, and were left out. */
    size_t skippedCount() const { return _skippedCount; }

    /** @brief The first bytes of the body, kept for the log that reports an unusable answer. */
    const char* prefix() const { return _prefix; }

protected:
    bool onObjectStart() override
    {
        _sensorTypes.push_back(SensorType());
        _parser.begin(_sensorTypes.back());
        return true;
    }

    bool onObjectByte(char c) override
    {
        _parser.feed(&c, 1);
        return true;
    }

    bool onObjectEnd() override
    {
        if (_parser.finish())
        {
            _readCount++;
            return true;
        }

        _sensorTypes.pop_back();
        _skippedCount++;
        return true;
    }

private:
    /**
     * @brief Keep the head of the body for the log, up to a fixed length.
     *
     * The whole-body path logged the first 500 characters of an answer it had in a string anyway.
     * There is no such string here, and reading one back for the log would be the buffer this
     * class exists to avoid - so a bounded head is copied as it goes past, into storage that is
     * part of this object.
     */
    void keepPrefix(const char* data, size_t length)
    {
        size_t room = PREFIX_BYTES - _prefixLength;
        if (room == 0 || data == nullptr)
        {
            return;
        }
        if (length < room)
        {
            room = length;
        }
        for (size_t i = 0; i < room; i++)
        {
            _prefix[_prefixLength + i] = data[i];
        }
        _prefixLength += room;
        _prefix[_prefixLength] = '\0';
    }

    static const size_t PREFIX_BYTES = 200;

    std::vector<SensorType>& _sensorTypes;
    SensorTypeStreamParser _parser;
    size_t _readCount = 0;
    size_t _skippedCount = 0;
    char _prefix[PREFIX_BYTES + 1] = {0};
    size_t _prefixLength = 0;
};
