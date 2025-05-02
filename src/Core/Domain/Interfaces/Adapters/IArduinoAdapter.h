#ifndef I_ARDUINO_ADAPTER_H
#define I_ARDUINO_ADAPTER_H

#include <cstddef> // For size_t

class IArduinoAdapter {
public:
    virtual ~IArduinoAdapter() = default;

    // Constants
    virtual uint8_t getPinModeInput() const = 0;
    virtual uint8_t getPinModeOutput() const = 0;
    virtual uint8_t getPinModeInputPullup() const = 0;
    virtual uint8_t getDigitalLow() const = 0;
    virtual uint8_t getDigitalHigh() const = 0;

    // Time functions
    virtual void delay(unsigned long ms) = 0;
    virtual void delayMicroseconds(unsigned int us) = 0;
    virtual unsigned long millis() = 0;
    virtual unsigned long micros() = 0;

    // Digital I/O
    virtual void pinMode(uint8_t pin, uint8_t mode) = 0;
    virtual void digitalWrite(uint8_t pin, uint8_t val) = 0;
    virtual int digitalRead(uint8_t pin) = 0;

    // Analog I/O
    virtual int analogRead(uint8_t pin) = 0;
    virtual void analogWrite(uint8_t pin, int val) = 0;
    
    // Serial communication
    virtual void serialBegin(unsigned long baud) = 0;
    virtual size_t serialPrint(const char* str) = 0;
    virtual size_t serialPrintln(const char* str) = 0;
    virtual int serialAvailable() = 0;
    virtual int serialRead() = 0;
};

#endif // I_ARDUINO_ADAPTER_H
