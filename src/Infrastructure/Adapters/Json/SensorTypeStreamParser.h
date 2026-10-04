#pragma once

#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Infrastructure/Adapters/Json/JsonArrayStreamSplitter.h"
#include "Infrastructure/Adapters/Json/JsonMemoryPool.h"

#include <ArduinoJson.h>

#include <cstddef>
#include <string>

/**
 * @brief Reads one sensor type out of JSON that arrives in pieces, member by member.
 *
 * A sensor type is a handful of fixed fields and a few arrays whose length is the number of
 * channels the model has. Read into one document the whole type has to fit into it, so the model
 * with the most channels decides whether the module can read the catalog at all - and a document
 * large enough for the biggest model is a block the module has to have free every time it reads
 * the smallest one. This reads the fixed fields and the array elements separately: the fields go
 * into one document, each element into one of its own, and neither of the two is a function of how
 * many channels the model has.
 *
 * What is alive while a type is read: the text of its non-array members (bounded by
 * MAX_SCALAR_BYTES), the text of the array element being read (bounded by MAX_ELEMENT_BYTES) and
 * one JSON document. A model with twelve channels costs what a model with two costs.
 *
 * The arrays are cut by JsonArrayStreamSplitter - the same cutting the catalog itself is read
 * with - so an array of objects is found the same way wherever one appears.
 *
 * Members this entity does not carry are walked past rather than collected, whatever they are and
 * however they are named, so the cap on the collected text describes this entity's own fields and
 * nothing else. The backend adds a field to its sensor model from time to time and the module has
 * no say in what it is called or how long it is; a reader that charged the entity for it would let
 * one server-side edit make every model in every catalog unreadable at once.
 *
 * Where the object ends is decided by the reader that cut it out, and this one has to agree: a byte
 * arriving after it believes the object closed means it stopped in the middle, and it refuses the
 * type rather than hand up the members it had by then. A model read in part is a different model,
 * not a shorter one - it carries a real uid, and the merge writes it over the good copy.
 */
class SensorTypeStreamParser
{
public:
    /**
     * @brief Cap on the entity's own non-array members, as text.
     *
     * Identification, timestamps, the protocol and the Modbus addressing - eighteen fields fixed by
     * the published shape (SensorModelHwDto), about seven hundred bytes of JSON for the models in
     * the field. What the backend carries beside them is not counted here, so this number is about
     * this entity and stays true as the server's model grows. Three times what a model needs; a
     * model that exceeds it is refused rather than read in part.
     */
    static const size_t MAX_SCALAR_BYTES = 2048;

    /** @brief Cap on one element of one of the type's arrays, as text. */
    static const size_t MAX_ELEMENT_BYTES = 2048;

    /**
     * @brief Longest member name this reader keeps.
     *
     * Twice the longest name the entity answers to (registerToStoreModbusDeviceAddress, 34). A name
     * that outruns it cannot be one of this entity's, so the member is walked past - the length of
     * a name the backend chose is not a reason to refuse a model.
     */
    static const size_t MAX_KEY_BYTES = 64;

    /**
     * @brief Capacity of the document the fixed fields are read in.
     *
     * Twice the text they may reach, which is what a document costs above the JSON it holds: a
     * member takes a slot beside its value. Neither this nor the element document may go up to make
     * a wider model fit - a document is a block the module has to have free while it reads any
     * model at all, and one that follows the widest one is the ceiling this reader exists to
     * remove, moved rather than lifted.
     */
    static const size_t SCALAR_DOCUMENT_BYTES = 4096;

    /** And of the document one array element is read in, on the same reasoning. */
    static const size_t ELEMENT_DOCUMENT_BYTES = 4096;

    SensorTypeStreamParser() : _sensorType(nullptr), _state(State::FAILED), _finished(false)
    {
        _scalars.reserve(SCALARS_INITIAL_CAPACITY);
        _key.reserve(MAX_KEY_BYTES);
    }

    /** @brief Point the reader at the type to fill, and take it back to the start of an object. */
    void begin(SensorType& sensorType)
    {
        _sensorType = &sensorType;
        _state = State::BEFORE_OBJECT;
        _finished = false;
        _firstScalar = true;
        _collecting = false;
        _keyTooLong = false;
        _depth = 0;
        _inString = false;
        _escaped = false;
        _key.clear();
        _scalars.clear();
        _scalars.push_back('{');

        _sensorType->sensorTypes.clear();
        _sensorType->i2cRegisterConfig.clear();
        _sensorType->modbusRegisterConfig.clear();
        _sensorType->analogPinConfig.clear();
    }

    /**
     * @brief Take the next bytes of the type object.
     *
     * Bytes that arrive after this reader believes the object closed are not dropped: they are
     * what says the belief was wrong. Every byte of the object goes through consume().
     */
    void feed(const char* data, size_t length)
    {
        if (data == nullptr || _state == State::FAILED)
        {
            return;
        }

        for (size_t i = 0; i < length; i++)
        {
            if (!consume(data[i]))
            {
                return;
            }
        }
    }

    /**
     * @brief Read the fields collected on the way and answer whether a whole type came out.
     *
     * The entity is only usable when this answers true: a type that stopped in the middle has its
     * first members read and the rest missing, which is a different sensor model rather than a
     * shorter one.
     */
    bool finish()
    {
        if (_sensorType == nullptr || _state != State::DONE || _finished)
        {
            return false;
        }
        _finished = true;

        if (!appendScalar('}'))
        {
            return false;
        }

        PooledJsonDocument doc(SCALAR_DOCUMENT_BYTES);
        if (deserializeJson(doc, _scalars))
        {
            return false;
        }

        readFixedFields(doc);
        return true;
    }

    /** @brief True once the object is known to be unreadable; feeding more changes nothing. */
    bool failed() const { return _state == State::FAILED; }

private:
    /** Which of the type's arrays the element reader is filling. */
    enum class ArrayKind
    {
        SUB_SENSOR_TYPES,
        I2C_REGISTER_CONFIGS,
        MODBUS_REGISTER_CONFIGS,
        ANALOG_PIN_CONFIGS
    };

    /**
     * @brief Reads the elements of one of the type's arrays, one element at a time.
     *
     * Which array it is is set before the array is fed; the element is read out of a document
     * opened for that element alone, so the length of the array costs nothing.
     */
    class ElementReader : public BufferedJsonArraySplitter
    {
    public:
        ElementReader()
            : BufferedJsonArraySplitter(MAX_ELEMENT_BYTES, ELEMENT_INITIAL_CAPACITY),
              _sensorType(nullptr), _kind(ArrayKind::SUB_SENSOR_TYPES)
        {
        }

        void begin(SensorType& sensorType, ArrayKind kind)
        {
            reset();
            _sensorType = &sensorType;
            _kind = kind;
        }

    protected:
        bool onObject(const std::string& elementJson) override
        {
            if (_sensorType == nullptr)
            {
                return false;
            }

            PooledJsonDocument doc(ELEMENT_DOCUMENT_BYTES);
            if (deserializeJson(doc, elementJson))
            {
                return false;
            }

            JsonObject element = doc.as<JsonObject>();
            switch (_kind)
            {
            case ArrayKind::SUB_SENSOR_TYPES:
                readSubSensorType(element);
                return true;
            case ArrayKind::I2C_REGISTER_CONFIGS:
                readI2cRegisterConfig(element);
                return true;
            case ArrayKind::MODBUS_REGISTER_CONFIGS:
                readModbusRegisterConfig(element);
                return true;
            case ArrayKind::ANALOG_PIN_CONFIGS:
            default:
                // The entity carries the count and nothing else so far; the fields follow when the
                // firmware has something to do with them.
                _sensorType->analogPinConfig.push_back(AnalogPinConfig());
                return true;
            }
        }

    private:
        void readSubSensorType(JsonObject element)
        {
            SubSensorType subSensorType;
            subSensorType.name = element["name"] | "";
            subSensorType.displayName = element["displayName"] | "";
            subSensorType.isAnalog = element["isAnalog"] | false;
            subSensorType.canCalibrate = element["canCalibrate"] | false;
            subSensorType.validCalibrationPeriodDays = element["validCalibrationPeriodDays"] | 0;
            subSensorType.calibrationTimeInSeconds = element["calibrationTimeInSeconds"] | 0;
            subSensorType.minValue = element["minValue"] | 0.0f;
            subSensorType.maxValue = element["maxValue"] | 0.0f;
            subSensorType.limitMinValue = element["limitMinValue"] | 0.0f;
            subSensorType.limitMaxValue = element["limitMaxValue"] | 0.0f;
            subSensorType.unit = element["unit"] | "";
            subSensorType.step = element["step"] | 0.0f;
            subSensorType.precisionPercent = element["precisionPercent"] | 0.0f;
            subSensorType.precisionValue = element["precisionValue"] | 0.0f;
            subSensorType.createdAt = element["createdAt"] | "";
            subSensorType.updatedAt = element["updatedAt"] | "";
            _sensorType->sensorTypes.push_back(subSensorType);
        }

        void readI2cRegisterConfig(JsonObject element)
        {
            I2CRegisterConfig config;
            config.registerAddr = element["register"] | 0;
            config.length = element["length"] | 1;
            config.multiplier = element["multiplier"] | 1.0f;
            config.offset = element["offset"] | 0.0f;
            _sensorType->i2cRegisterConfig.push_back(config);
        }

        void readModbusRegisterConfig(JsonObject element)
        {
            ModbusRegisterConfig config;
            config.functionCode = element["functionCode"] | 3;
            config.startRegister = element["startRegister"] | 0;
            config.numRegisters = element["numRegisters"] | 1;
            config.multiplier = element["multiplier"] | 1.0f;
            config.offset = element["offset"] | 0.0f;
            config.channelName = element["channelName"] | "";
            config.channel = element["channel"] | 0;
            config.sensorType = element["sensorType"] | "";
            config.canConnectMinValue = element["canConnectMinValue"] | 0.0f;
            config.canConnectMaxValue = element["canConnectMaxValue"] | 100.0f;
            config.dataType = modbusDataTypeFromString(element["dataType"] | "u16");
            _sensorType->modbusRegisterConfig.push_back(config);
        }

        SensorType* _sensorType;
        ArrayKind _kind;
    };

    enum class State
    {
        BEFORE_OBJECT,    // waiting for '{'
        AFTER_OPEN,       // the first member, or the '}' of an empty object
        BEFORE_KEY,       // after a comma: a member name has to follow
        IN_KEY,           // collecting the member name
        AFTER_KEY,        // waiting for ':'
        BEFORE_VALUE,     // waiting for the value to start
        IN_STRING_VALUE,  // collecting a string value into the fixed fields
        IN_LITERAL_VALUE, // collecting a number, a boolean or a null
        IN_ARRAY_VALUE,   // handing an array of this entity's to the element reader
        SKIPPING_VALUE,   // an object or an array this entity has no field for
        AFTER_MEMBER,     // waiting for ',' or '}'
        DONE,
        FAILED
    };

    /** @return false when there is no point looking at the rest of the object */
    bool consume(char c)
    {
        switch (_state)
        {
        case State::BEFORE_OBJECT:
            if (isWhitespace(c))
            {
                return true;
            }
            if (c != '{')
            {
                return fail();
            }
            _state = State::AFTER_OPEN;
            return true;

        case State::AFTER_OPEN:
            if (isWhitespace(c))
            {
                return true;
            }
            if (c == '}')
            {
                _state = State::DONE;
                return true;
            }
            return startKey(c);

        case State::BEFORE_KEY:
            if (isWhitespace(c))
            {
                return true;
            }
            return startKey(c);

        case State::IN_KEY:
            return consumeKey(c);

        case State::AFTER_KEY:
            if (isWhitespace(c))
            {
                return true;
            }
            if (c != ':')
            {
                return fail();
            }
            _state = State::BEFORE_VALUE;
            return true;

        case State::BEFORE_VALUE:
            return startValue(c);

        case State::IN_STRING_VALUE:
            return consumeStringValue(c);

        case State::IN_LITERAL_VALUE:
            return consumeLiteralValue(c);

        case State::IN_ARRAY_VALUE:
            return consumeArrayValue(c);

        case State::SKIPPING_VALUE:
            return consumeSkippedValue(c);

        case State::AFTER_MEMBER:
            return consumeAfterMember(c);

        case State::DONE:
            // Where the object ends is decided by the reader that cut it out, not here. A byte
            // after the closing brace means this reader stopped somewhere in the middle and took
            // it for the end - which is how a model with half its members read would be handed on
            // as a model, uid and all, rather than refused. It is refused.
            return isWhitespace(c) ? true : fail();

        case State::FAILED:
        default:
            return false;
        }
    }

    bool startKey(char c)
    {
        if (c != '"')
        {
            return fail();
        }
        _key.clear();
        _keyTooLong = false;
        _inString = true;
        _escaped = false;
        _state = State::IN_KEY;
        return true;
    }

    bool consumeKey(char c)
    {
        if (_escaped)
        {
            _escaped = false;
        }
        else if (c == '\\')
        {
            _escaped = true;
        }
        else if (c == '"')
        {
            _state = State::AFTER_KEY;
            return true;
        }

        // A name longer than any this entity answers to is a member of somebody else's. It stops
        // being collected here, which makes it unknown below; refusing the type for it would let
        // one field added on the server, with a name of the server's choosing, make every model in
        // the catalog unreadable at once.
        if (_key.size() >= MAX_KEY_BYTES)
        {
            _keyTooLong = true;
            return true;
        }
        _key.push_back(c);
        return true;
    }

    bool startValue(char c)
    {
        if (isWhitespace(c))
        {
            return true;
        }

        if (c == '[')
        {
            ArrayKind kind = ArrayKind::SUB_SENSOR_TYPES;
            if (_keyTooLong || !arrayKindFor(_key, kind))
            {
                return startSkippingNested();
            }
            _elements.begin(*_sensorType, kind);
            _state = State::IN_ARRAY_VALUE;
            return consumeArrayValue(c);
        }

        if (c == '{')
        {
            return startSkippingNested();
        }

        if (c == ',' || c == '}' || c == ']' || c == ':')
        {
            return fail();
        }

        // A member this entity does not carry is walked past rather than collected. What the cap on
        // the collected text then describes is this entity's own fields, whose number and names are
        // fixed - and not whatever else the backend has put beside them.
        _collecting = !_keyTooLong && isKnownScalarKey(_key);
        if (_collecting && !beginScalarMember())
        {
            return fail();
        }

        if (c == '"')
        {
            _inString = true;
            _escaped = false;
            _state = State::IN_STRING_VALUE;
        }
        else
        {
            _state = State::IN_LITERAL_VALUE;
        }
        return collectByte(c) ? true : fail();
    }

    bool startSkippingNested()
    {
        _depth = 1;
        _inString = false;
        _escaped = false;
        _state = State::SKIPPING_VALUE;
        return true;
    }

    bool consumeStringValue(char c)
    {
        if (!collectByte(c))
        {
            return fail();
        }
        if (_escaped)
        {
            _escaped = false;
        }
        else if (c == '\\')
        {
            _escaped = true;
        }
        else if (c == '"')
        {
            _state = State::AFTER_MEMBER;
        }
        return true;
    }

    bool consumeLiteralValue(char c)
    {
        // A literal ends where the member ends: at a blank, at the comma before the next member or
        // at the brace that closes the object.
        if (isWhitespace(c))
        {
            _state = State::AFTER_MEMBER;
            return true;
        }
        if (c == ',' || c == '}')
        {
            _state = State::AFTER_MEMBER;
            return consumeAfterMember(c);
        }
        return collectByte(c) ? true : fail();
    }

    bool consumeArrayValue(char c)
    {
        _elements.feed(&c, 1);
        if (_elements.failed())
        {
            return fail();
        }
        if (!_elements.complete())
        {
            return true;
        }

        // An array of this entity's that holds something other than objects is a member this
        // module cannot read; an empty one is a model with no channels of that kind.
        if (_elements.finish() == JsonListParse::Failed)
        {
            return fail();
        }
        _state = State::AFTER_MEMBER;
        return true;
    }

    bool consumeSkippedValue(char c)
    {
        if (_inString)
        {
            if (_escaped)
            {
                _escaped = false;
            }
            else if (c == '\\')
            {
                _escaped = true;
            }
            else if (c == '"')
            {
                _inString = false;
            }
            return true;
        }

        if (c == '"')
        {
            _inString = true;
            return true;
        }
        if (c == '{' || c == '[')
        {
            _depth++;
            return true;
        }
        if (c == '}' || c == ']')
        {
            _depth--;
            if (_depth == 0)
            {
                _state = State::AFTER_MEMBER;
            }
            return true;
        }
        return true;
    }

    bool consumeAfterMember(char c)
    {
        if (isWhitespace(c))
        {
            return true;
        }
        if (c == ',')
        {
            _state = State::BEFORE_KEY;
            return true;
        }
        if (c == '}')
        {
            _state = State::DONE;
            return true;
        }
        return fail();
    }

    /** @brief Open one member of the fixed-field document: its name and the colon after it. */
    bool beginScalarMember()
    {
        if (!_firstScalar && !appendScalar(','))
        {
            return false;
        }
        _firstScalar = false;

        if (!appendScalar('"') || !appendScalar(_key.data(), _key.size()) || !appendScalar('"') ||
            !appendScalar(':'))
        {
            return false;
        }
        return true;
    }

    /** @brief One byte of the value being read - kept if the member is one of this entity's. */
    bool collectByte(char c) { return _collecting ? appendScalar(c) : true; }

    bool appendScalar(char c) { return appendScalar(&c, 1); }

    bool appendScalar(const char* data, size_t length)
    {
        // The one thing here that grows with what the peer sends, so it is capped, and reaching the
        // cap refuses the type instead of reading a part of it.
        if (_scalars.size() + length > MAX_SCALAR_BYTES)
        {
            return false;
        }
        _scalars.append(data, length);
        return true;
    }

    /**
     * @brief Whether a member name is one of the entity's own non-array fields.
     *
     * This list and readFixedFields() below are one thing said twice, and they have to agree: a
     * name read there and missing here reads as its default for ever, silently. What holds them
     * together is a test - test_catalog_type_size.cpp puts every field through a round trip - so a
     * field added to one and not the other is a red test rather than a field that is always empty.
     */
    static bool isKnownScalarKey(const std::string& key)
    {
        static const char* const knownKeys[] = {
            "uid",
            "name",
            "displayName",
            "canScan",
            "isSupported",
            "createdAt",
            "updatedAt",
            "protocol",
            "i2cAddress",
            "chipType",
            "modbusDeviceAddress",
            "baudRate",
            "modbusPingAddress",
            "registerToStoreModbusDeviceAddress",
            "writeRegistersCommand",
            "applyAddressAfterPowerCycle",
            "priorityForScan",
            "id"};

        for (size_t i = 0; i < sizeof(knownKeys) / sizeof(knownKeys[0]); i++)
        {
            if (key == knownKeys[i])
            {
                return true;
            }
        }
        return false;
    }

    /** @brief Which of this entity's arrays a member name stands for, if any. */
    static bool arrayKindFor(const std::string& key, ArrayKind& kind)
    {
        if (key == "sensorTypes")
        {
            kind = ArrayKind::SUB_SENSOR_TYPES;
            return true;
        }
        if (key == "i2CRegisterConfigs")
        {
            kind = ArrayKind::I2C_REGISTER_CONFIGS;
            return true;
        }
        if (key == "modbusRegisterConfigs")
        {
            kind = ArrayKind::MODBUS_REGISTER_CONFIGS;
            return true;
        }
        if (key == "analogPinConfigs")
        {
            kind = ArrayKind::ANALOG_PIN_CONFIGS;
            return true;
        }
        return false;
    }

    void readFixedFields(PooledJsonDocument& doc)
    {
        SensorType& sensorType = *_sensorType;

        sensorType.uid = doc["uid"] | "";
        sensorType.name = doc["name"] | "";
        sensorType.displayName = doc["displayName"] | "";
        sensorType.canScan = doc["canScan"] | true;
        sensorType.isSupported = doc["isSupported"] | true;
        sensorType.createdAt = doc["createdAt"] | "";
        sensorType.updatedAt = doc["updatedAt"] | "";

        const std::string protocolStr = doc["protocol"] | "";
        sensorType.setProtocolFromString(protocolStr);

        sensorType.i2cAddress = doc["i2cAddress"] | "";
        sensorType.chipType = doc["chipType"] | "";
        sensorType.modbusDeviceAddress = doc["modbusDeviceAddress"] | 1;
        sensorType.baudRate = doc["baudRate"] | 9600;
        sensorType.modbusPingAddress = doc["modbusPingAddress"] | 0;
        sensorType.registerToStoreModbusDeviceAddress = doc["registerToStoreModbusDeviceAddress"] | 0;
        sensorType.writeRegistersCommand = doc["writeRegistersCommand"] | 0;
        sensorType.applyAddressAfterPowerCycle = doc["applyAddressAfterPowerCycle"] | false;
        sensorType.priorityForScan = doc["priorityForScan"] | 0;

        // Legacy id, kept for catalogs written before the uid existed.
        sensorType.id = doc["id"] | 0;
    }

    bool fail()
    {
        _state = State::FAILED;
        return false;
    }

    static bool isWhitespace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    /** Room for the fixed fields of a published model without a reallocation. */
    static const size_t SCALARS_INITIAL_CAPACITY = 1024;

    /** And for one channel of one, on the same reasoning. */
    static const size_t ELEMENT_INITIAL_CAPACITY = 512;

    SensorType* _sensorType;
    State _state;
    bool _finished;
    bool _firstScalar = true;
    /** Whether the value being read belongs to a member this entity carries. */
    bool _collecting = false;
    /** Whether the member name outran MAX_KEY_BYTES, which makes it somebody else's. */
    bool _keyTooLong = false;
    size_t _depth = 0;
    bool _inString = false;
    bool _escaped = false;
    std::string _key;
    std::string _scalars;
    ElementReader _elements;
};
