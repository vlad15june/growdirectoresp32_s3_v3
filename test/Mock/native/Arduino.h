#pragma once

// Minimal Arduino.h stand-in for the native (host) test environment.
//
// It exists so that host tests can include production headers that pull
// <Arduino.h> in - first of all Infrastructure/Adapters/Json/JsonMemoryPool.h,
// which ArduinoJsonConverter.h depends on. Only what those headers actually
// use is declared here: the Serial sink and the FreeRTOS critical-section
// primitives. On the device the real Arduino core header is used, this file is
// reachable only through the include path of env:native (see platformio.ini).

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// The core reaches the system clock through this header; so does production code that includes
// Arduino.h and calls getLocalTime()/configTime().
#include "esp32-hal-time.h"

// Arduino's name for an octet. Production headers written against the core spell retry counters
// and frame lengths this way.
typedef uint8_t byte;

// delay(): on the device this blocks the calling task for the whole wait. There is no scheduler
// behind a bare Arduino.h on the host, so the wait is *recorded* rather than taken - a test that
// wants to say how long a retry loop makes its caller wait reads hostDelayTotalMs and resets it
// itself. This is not a second clock: anything that needs time to advance uses Sim/VirtualClock.h,
// and a caller that needs delay() to yield to other tasks goes through IArduinoAdapter, which
// Sim/SimArduino.h implements on top of the scheduler.
inline unsigned long hostDelayTotalMs = 0;
inline void delay(unsigned long ms) { hostDelayTotalMs += ms; }

// millis(): the core's uptime counter, for production classes that read it directly instead of
// through IArduinoAdapter. A test moves it itself - through hostUptimeAdvance() or by driving a
// stand-in that spends time - so a duration a class measures is whatever the test decided.
// The simulated module does not read this one: everything on the scheduler goes through
// IArduinoAdapter, which Sim/SimArduino.h answers from Sim/VirtualClock.h.
inline unsigned long hostUptimeMs = 0;
inline unsigned long millis() { return hostUptimeMs; }
inline void hostUptimeAdvance(unsigned long ms) { hostUptimeMs += ms; }

// Arduino core String, reduced to what production headers on the host build use: construction,
// assignment, concatenation, c_str()/length(). Every operator+ bumps concatCount - on the device
// each such concatenation is a heap-allocating temporary, so a test that claims a code path does
// no String work resets the counter, runs the path, and asserts it stayed put.
class String
{
public:
    static inline unsigned long concatCount = 0;

    String() {}
    String(const char *s) : _s(s ? s : "") {}
    String(const std::string &s) : _s(s) {}

    String &operator=(const char *s)
    {
        _s = s ? s : "";
        return *this;
    }

    unsigned int length() const { return static_cast<unsigned int>(_s.size()); }
    const char *c_str() const { return _s.c_str(); }

    bool startsWith(const String &prefix) const
    {
        return _s.compare(0, prefix._s.size(), prefix._s) == 0;
    }

    // Search and slicing, with the core's answers: -1 for "not found", and a substring that
    // clamps to the end of the text instead of throwing.
    int indexOf(const char *needle) const
    {
        std::string::size_type at = _s.find(needle ? needle : "");
        return at == std::string::npos ? -1 : static_cast<int>(at);
    }

    String substring(unsigned int from, unsigned int to) const
    {
        if (from > _s.size())
        {
            return String();
        }
        if (to > _s.size())
        {
            to = static_cast<unsigned int>(_s.size());
        }
        if (to <= from)
        {
            return String();
        }
        return String(_s.substr(from, to - from));
    }

    String substring(unsigned int from) const
    {
        return substring(from, static_cast<unsigned int>(_s.size()));
    }

    long toInt() const { return strtol(_s.c_str(), nullptr, 10); }

    // The core compares the text itself (String::compareTo -> strcmp), so an ordered container of
    // Strings in production code - MqttManager's set of subscribed topics - sorts by the topic.
    bool operator==(const String &other) const { return _s == other._s; }
    bool operator!=(const String &other) const { return _s != other._s; }
    bool operator<(const String &other) const { return _s < other._s; }

    friend String operator+(const String &lhs, const char *rhs)
    {
        ++concatCount;
        return String(lhs._s + (rhs ? rhs : ""));
    }

    friend String operator+(const String &lhs, const String &rhs)
    {
        ++concatCount;
        return String(lhs._s + rhs._s);
    }

private:
    std::string _s;
};

// Print/Stream, reduced to the contract the production code that runs on the host actually meets.
//
// The one such class today is HttpClientAdapter::SinkStream, which HTTPClient::writeToStream()
// drives: it is written against `size_t write(uint8_t)`, `size_t write(const uint8_t*, size_t)`
// and the write-error pair, and it never reads. Anything a stand-in here does not declare is
// something no host test executes - which is the point of keeping it this small.
class Print
{
public:
    virtual ~Print() = default;

    virtual size_t write(uint8_t value) = 0;

    // The core's default: one octet at a time, stopping at the first refusal. Overridden by
    // anything that can take a block.
    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        size_t written = 0;
        for (size_t i = 0; i < size; i++)
        {
            if (write(buffer[i]) != 1)
            {
                break;
            }
            written++;
        }
        return written;
    }

    int getWriteError() const { return _writeError; }
    void clearWriteError() { _writeError = 0; }

protected:
    void setWriteError(int error = 1) { _writeError = error; }

private:
    int _writeError = 0;
};

class Stream : public Print
{
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() {}
};

// Serial sink: swallows the output instead of flooding the test log. Calls are counted and the
// last text kept, so a test can assert both that a path printed and that a hot path did not.
class NativeSerialStub
{
public:
    int printfCalls = 0;
    int printCalls = 0;
    int printlnCalls = 0;
    std::string lastText;

    int printf(const char *format, ...)
    {
        ++printfCalls;
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        lastText = buffer;
        return 0;
    }
    void print(const char *text)
    {
        ++printCalls;
        lastText = text ? text : "";
    }
    void println(const char *text)
    {
        ++printlnCalls;
        lastText = text ? text : "";
    }
    void flush() {}
};

inline NativeSerialStub Serial;

// FreeRTOS critical sections: single-threaded on the host, so no-ops.
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
