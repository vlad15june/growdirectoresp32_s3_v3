#ifndef MOCK_ARDUINO_ADAPTER_H
#define MOCK_ARDUINO_ADAPTER_H

#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

class MockArduinoAdapter : public IArduinoAdapter {
public:
    // Tracking variables for test verification
    bool pinModeCalled = false;
    bool digitalWriteCalled = false;
    bool digitalReadCalled = false;
    bool analogReadCalled = false;
    bool analogWriteCalled = false;
    bool serialBeginCalled = false;
    bool serialPrintCalled = false;
    bool serialPrintlnCalled = false;
    bool serialAvailableCalled = false;
    bool serialReadCalled = false;
    
    int delayCount = 0;
    int delayMicrosecondsCount = 0;
    unsigned long lastDelayMs = 0;
    unsigned long lastDelayUs = 0;
    unsigned long currentTime = 0;
    
    // Implementation of IArduinoAdapter interface
    
    // Constants
    uint8_t getPinModeInput() const override { return 0; }
    uint8_t getPinModeOutput() const override { return 1; }
    uint8_t getPinModeInputPullup() const override { return 2; }
    uint8_t getDigitalLow() const override { return 0; }
    uint8_t getDigitalHigh() const override { return 1; }

    // Time functions
    void delay(unsigned long ms) override {
        delayCount++;
        lastDelayMs = ms;
        currentTime += ms;
    }
    
    void delayMicroseconds(unsigned int us) override {
        delayMicrosecondsCount++;
        lastDelayUs = us;
        currentTime += us / 1000; // Convert to ms for simplicity
    }
    
    unsigned long millis() override {
        return currentTime;
    }
    
    unsigned long micros() override {
        return currentTime * 1000; // Convert ms to us
    }

    // Digital I/O
    void pinMode(uint8_t pin, uint8_t mode) override {
        pinModeCalled = true;
    }
    
    void digitalWrite(uint8_t pin, uint8_t val) override {
        digitalWriteCalled = true;
    }
    
    int digitalRead(uint8_t pin) override {
        digitalReadCalled = true;
        return 0; // Default return
    }

    // Analog I/O
    int analogRead(uint8_t pin) override {
        analogReadCalled = true;
        return 0; // Default return
    }
    
    void analogWrite(uint8_t pin, int val) override {
        analogWriteCalled = true;
    }
    
    // Serial communication
    void serialBegin(unsigned long baud) override {
        serialBeginCalled = true;
    }
    
    size_t serialPrint(const char* str) override {
        serialPrintCalled = true;
        return 0; // Default return
    }
    
    size_t serialPrintln(const char* str) override {
        serialPrintlnCalled = true;
        return 0; // Default return
    }
    
    int serialAvailable() override {
        serialAvailableCalled = true;
        return 0; // Default return
    }
    
    int serialRead() override {
        serialReadCalled = true;
        return -1; // Default return (no data)
    }
    
    // Reset method for tests
    void reset() {
        pinModeCalled = false;
        digitalWriteCalled = false;
        digitalReadCalled = false;
        analogReadCalled = false;
        analogWriteCalled = false;
        serialBeginCalled = false;
        serialPrintCalled = false;
        serialPrintlnCalled = false;
        serialAvailableCalled = false;
        serialReadCalled = false;
        delayCount = 0;
        delayMicrosecondsCount = 0;
        lastDelayMs = 0;
        lastDelayUs = 0;
        currentTime = 0;
    }
};

#endif // MOCK_ARDUINO_ADAPTER_H
