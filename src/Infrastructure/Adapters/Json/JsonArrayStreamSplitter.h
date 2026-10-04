#pragma once

#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"

#include <cstddef>
#include <string>

/**
 * @brief Cuts a JSON array that arrives in pieces into its top-level objects.
 *
 * Reading a list endpoint any other way means holding the whole answer in one heap block before
 * looking at the first item of it - about 16 KB for the twelve sensor types a stand has, and
 * proportionally more for an account with forty. Fed the body in the pieces the transport reads it
 * in, this hands one object over as it goes past, so what reading the answer costs is not a
 * function of how long the answer is.
 *
 * The object is handed over as bytes rather than as a string, and that is the difference between
 * "one object at a time" and "one object in memory": a reader that can consume an object as it
 * arrives never holds it. Readers that do need the object whole - a short element of a short array
 * that goes into a parser expecting a string - use BufferedJsonArraySplitter below, which is this
 * class plus the buffer, so that the buffer exists where it is wanted and nowhere else.
 *
 * Three outcomes, because callers act on them differently and classifyJsonArray() answers with the
 * same three:
 *
 *  - `Ok`      - the body is an array of objects and at least one of them came out of it;
 *  - `Empty`   - the body is an array with nothing in it, which is a server saying "none";
 *  - `Failed`  - anything else: no body, a body that is not an array, an array that never closed,
 *                an object cut in half, or an array holding anything that is not an object.
 *
 * That last one is the whole array and not just that entry, and deliberately: what a caller gets
 * out of this is a list, a list one entry shorter is a statement the caller will act on, and there
 * is nothing in a bare value to act on instead. Reading `[{..},null,{..}]` as two of anything is
 * the one answer that must not come out of here.
 *
 * Objects are found by brace depth, with quoted strings and their escapes skipped. This reader ends
 * a body at the first thing it cannot make sense of.
 */
class JsonArrayStreamSplitter
{
public:
    /**
     * @param maxObjectBytes hard cap on one object, in bytes. Reaching it fails the body rather
     *        than truncating it: a half-read object must never be mistaken for a shorter one.
     *        What the number has to cover depends on what is being read, so it is given by the
     *        reader rather than fixed here.
     */
    explicit JsonArrayStreamSplitter(size_t maxObjectBytes) : _maxObjectBytes(maxObjectBytes) {}

    virtual ~JsonArrayStreamSplitter() = default;

    /**
     * @brief Take the next piece of the body. Objects are handed over as they are cut out; a piece
     *        may hold any number of them, or a fraction of one.
     */
    void feed(const char* data, size_t length)
    {
        // A closed array is not the end of the body: what follows it still decides the verdict,
        // because classifyJsonArray() insists that the last non-blank character be the ']'.
        if (data == nullptr || _state == State::FAILED)
        {
            return;
        }

        _bytesFed += length;

        for (size_t i = 0; i < length; i++)
        {
            if (!consume(data[i]))
            {
                return;
            }
        }
    }

    /** @brief What the body turned out to be, once there is no more of it. */
    virtual JsonListParse finish()
    {
        if (_state == State::FAILED)
        {
            return JsonListParse::Failed;
        }

        // An array that never closed is a body that stopped in the middle - a truncated answer,
        // never a shorter list. Reading its complete prefix would retire everything that did not
        // arrive.
        if (_state != State::DONE)
        {
            return JsonListParse::Failed;
        }

        if (_objectCount > 0)
        {
            return JsonListParse::Ok;
        }

        // `[]` is the server's answer, `[1,2]` is a body this module cannot use: an array holding
        // something that is not an object yields nothing and must not be read as "none".
        return _sawArrayContent ? JsonListParse::Failed : JsonListParse::Empty;
    }

    /** @brief Bytes of body handed to this splitter. */
    size_t bytesFed() const { return _bytesFed; }

    /** @brief Objects cut out and accepted so far. */
    size_t objectCount() const { return _objectCount; }

    /** @brief True once the body is known to be unusable; feeding more changes nothing. */
    bool failed() const { return _state == State::FAILED; }

    /**
     * @brief True once the closing bracket has been seen.
     *
     * What a reader nested inside a larger structure watches: the array is one member's value
     * there, and the bytes after the bracket belong to the enclosing object.
     */
    bool complete() const { return _state == State::DONE; }

    /** @brief The cap one object of this array may reach. */
    size_t maxObjectBytes() const { return _maxObjectBytes; }

protected:
    /** @brief One top-level object begins. Its bytes follow through onObjectByte(). */
    virtual bool onObjectStart() = 0;

    /** @brief The next byte of the object being cut, its braces included. */
    virtual bool onObjectByte(char c) = 0;

    /**
     * @brief The object ended.
     * @return false when the object could not be used, which fails the whole body
     */
    virtual bool onObjectEnd() = 0;

    /** @brief Back to the start, so one instance can read one array after another. */
    virtual void reset()
    {
        _state = State::BEFORE_ARRAY;
        _depth = 0;
        _inString = false;
        _escaped = false;
        _sawArrayContent = false;
        _objectCount = 0;
        _bytesFed = 0;
        _objectBytes = 0;
    }

private:
    enum class State
    {
        BEFORE_ARRAY,  // waiting for '['
        IN_ARRAY,      // between objects
        IN_OBJECT,     // cutting one object
        DONE,          // ']' seen at the top level
        FAILED
    };

    /** @return false when there is no point looking at the rest of the body */
    bool consume(char c)
    {
        switch (_state)
        {
        case State::BEFORE_ARRAY:
            if (isWhitespace(c))
            {
                return true;
            }
            if (c != '[')
            {
                return fail();
            }
            _state = State::IN_ARRAY;
            return true;

        case State::IN_ARRAY:
            if (c == '{')
            {
                _sawArrayContent = true;
                _depth = 0;
                _inString = false;
                _escaped = false;
                _objectBytes = 0;
                _state = State::IN_OBJECT;
                if (!onObjectStart())
                {
                    return fail();
                }
                // The brace itself is part of the object, and taking the depth to one is what the
                // object branch does with it.
                return consumeInsideObject(c);
            }
            if (c == ']')
            {
                _state = State::DONE;
                return true;
            }
            if (isWhitespace(c))
            {
                return true;
            }
            if (c == ',')
            {
                // A separator says nothing about what the array holds, and this reader does not
                // count them: `[,]` yields no object and is refused for that in finish(), which is
                // also why it is content and not an empty array.
                _sawArrayContent = true;
                return true;
            }
            // An element that is not an object at all. Passing over it would hand the caller a
            // list one entry shorter than the one that arrived, with nothing to say so - and a
            // list one entry short is what every caller here acts on: a catalog missing a model
            // retires it, a model missing a channel reads that channel without its limits. An
            // array of objects holding something else is refused, exactly as one holding nothing
            // but non-objects already is.
            _sawArrayContent = true;
            return fail();

        case State::IN_OBJECT:
            return consumeInsideObject(c);

        case State::DONE:
            // Trailing whitespace is a body that ended; anything else means the array was not the
            // whole answer, which classifyJsonArray() called Failed by insisting the last
            // non-blank character be ']'.
            return isWhitespace(c) ? true : fail();

        case State::FAILED:
        default:
            return false;
        }
    }

    bool consumeInsideObject(char c)
    {
        if (_objectBytes >= _maxObjectBytes)
        {
            return fail();
        }

        _objectBytes++;
        if (!onObjectByte(c))
        {
            return fail();
        }

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
        if (c == '{')
        {
            _depth++;
            return true;
        }
        if (c != '}')
        {
            return true;
        }

        _depth--;
        if (_depth > 0)
        {
            return true;
        }

        _state = State::IN_ARRAY;
        if (!onObjectEnd())
        {
            return fail();
        }
        _objectCount++;
        return true;
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

    size_t _maxObjectBytes;
    State _state = State::BEFORE_ARRAY;
    size_t _depth = 0;
    bool _inString = false;
    bool _escaped = false;
    bool _sawArrayContent = false;
    size_t _objectCount = 0;
    size_t _bytesFed = 0;
    size_t _objectBytes = 0;
};

/**
 * @brief The same cutting, with each object handed over as one string.
 *
 * For readers whose parser wants a whole value - the elements of the short arrays inside one sensor
 * type go into a JSON document that way. What is held is one element, and the cap the splitter is
 * built with is what bounds it.
 */
class BufferedJsonArraySplitter : public JsonArrayStreamSplitter
{
public:
    BufferedJsonArraySplitter(size_t maxObjectBytes, size_t initialCapacity)
        : JsonArrayStreamSplitter(maxObjectBytes)
    {
        // Reserved once, so that cutting an object out costs no reallocation per object.
        _object.reserve(initialCapacity);
    }

protected:
    /**
     * @brief One complete object, as text.
     * @return false when the object could not be used, which fails the whole array
     */
    virtual bool onObject(const std::string& objectJson) = 0;

    bool onObjectStart() override
    {
        // clear() keeps the capacity, so the next object reuses this buffer instead of asking the
        // allocator for one per element.
        _object.clear();
        return true;
    }

    bool onObjectByte(char c) override
    {
        _object.push_back(c);
        return true;
    }

    bool onObjectEnd() override
    {
        const bool used = onObject(_object);
        _object.clear();
        return used;
    }

    void reset() override
    {
        JsonArrayStreamSplitter::reset();
        _object.clear();
    }

private:
    std::string _object;
};
