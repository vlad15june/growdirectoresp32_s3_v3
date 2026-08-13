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

// Serial sink: swallows the output instead of flooding the test log.
class NativeSerialStub
{
public:
    int printf(const char *, ...) { return 0; }
    void print(const char *) {}
    void println(const char *) {}
    void flush() {}
};

inline NativeSerialStub Serial;

// FreeRTOS critical sections: single-threaded on the host, so no-ops.
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
