#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#include <map>

// Mock function for digital pin operations
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
void setDigitalReadMockValue(int pin, int value);

// Pin mode definitions
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// Digital values
#define LOW 0
#define HIGH 1

static std::map<int, int> pinModes;
static std::map<int, int> pinValues;

inline long	 random(int range)
{
    return rand() % range;
}

inline void setDigitalReadMockValue(int pin, int value) {
    pinValues[pin] = value;
}

inline void pinMode(int pin, int mode)
{
    pinModes[pin] = mode;
}

inline int digitalRead(int pin)
{
    return pinValues[pin];
}

#endif // ARDUINO_MOCK_H
