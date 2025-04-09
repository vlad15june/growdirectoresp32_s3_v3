#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#include <map>

// Заглушки для типов Arduino
// Определения режимов пинов
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

static std::map<int, int> pinModes;
static std::map<int, int> pinValues;

inline void pinMode(int pin, int mode)
{
    pinModes[pin] = mode;
}

inline int digitalRead(int pin)
{
    return pinValues[pin];
}

#endif // ARDUINO_MOCK_H
