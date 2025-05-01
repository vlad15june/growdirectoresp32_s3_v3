#ifndef ARDUINO_ADAPTER_H
#define ARDUINO_ADAPTER_H

#include <Arduino.h>
#include "Core/Domain/Interfaces/IArduinoAdapter.h"

class ArduinoAdapter : public IArduinoAdapter {
public:
    // Time functions
    void delay(unsigned long ms) override {
        ::delay(ms);
    }
    
    void delayMicroseconds(unsigned int us) override {
        ::delayMicroseconds(us);
    }
    
    unsigned long millis() override {
        return ::millis();
    }
    
    unsigned long micros() override {
        return ::micros();
    }

    // Digital I/O
    void pinMode(uint8_t pin, uint8_t mode) override {
        ::pinMode(pin, mode);
    }
    
    void digitalWrite(uint8_t pin, uint8_t val) override {
        ::digitalWrite(pin, val);
    }
    
    int digitalRead(uint8_t pin) override {
        return ::digitalRead(pin);
    }

    // Analog I/O
    int analogRead(uint8_t pin) override {
        return ::analogRead(pin);
    }
    
    void analogWrite(uint8_t pin, int val) override {
        ::analogWrite(pin, val);
    }
    
    // Serial communication
    void serialBegin(unsigned long baud) override {
        Serial.begin(baud);
    }
    
    size_t serialPrint(const char* str) override {
        return Serial.print(str);
    }
    
    size_t serialPrintln(const char* str) override {
        return Serial.println(str);
    }
    
    int serialAvailable() override {
        return Serial.available();
    }
    
    int serialRead() override {
        return Serial.read();
    }
};

#endif // ARDUINO_ADAPTER_H
