#ifndef MOCK_ARDUINO_ADAPTER_H
#define MOCK_ARDUINO_ADAPTER_H

#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include <map>
#include <cstring>

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
    bool restartCalled = false;
    bool hardResetCalled = false;
    bool millisCalled = false;
    bool getResetReasonCalled = false;
    int resetReasonValue = 0;
    
    bool getFreeHeapCalled = false;
    bool getMinFreeHeapCalled = false;
    bool getHeapSizeCalled = false;
    bool getMaxAllocHeapCalled = false;
    bool getPsramSizeCalled = false;
    bool getFreePsramCalled = false;

    int delayCount = 0;
    int delayMicrosecondsCount = 0;
    unsigned long lastDelayMs = 0;
    unsigned long lastDelayUs = 0;
    unsigned long currentTime = 0;
    unsigned long currentMillis = 0;
    
    uint32_t freeHeapValue = 0;
    uint32_t minFreeHeapValue = 0;
    uint32_t heapSizeValue = 0;
    uint32_t maxAllocHeapValue = 0;
    uint32_t psramSizeValue = 0;
    uint32_t psramFreeValue = 0;

    // Store pin mode settings
    std::map<uint8_t, uint8_t> pinModes;
    // Store digital output states
    std::map<uint8_t, uint8_t> digitalOutputs;
    // Count digitalWrite calls per pin
    std::map<uint8_t, int> digitalWriteCounts;
    // Store digital input values to be returned by digitalRead
    std::map<uint8_t, int> digitalInputs;
    // Store analog input values to be returned by analogRead
    std::map<uint8_t, int> analogInputs;
    
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
        millisCalled = true;
        return currentMillis;
    }
    
    unsigned long micros() override {
        return currentTime * 1000; // Convert ms to us
    }
    
    // Test helper method
    void setMillis(unsigned long ms) {
        currentMillis = ms;
    }

    // Digital I/O
    void pinMode(uint8_t pin, uint8_t mode) override {
        pinModeCalled = true;
        pinModes[pin] = mode;
    }
    
    void digitalWrite(uint8_t pin, uint8_t val) override {
        digitalWriteCalled = true;
        digitalOutputs[pin] = val;
        digitalWriteCounts[pin]++;
    }
    
    int digitalRead(uint8_t pin) override {
        digitalReadCalled = true;
        // Return the specified input value if one has been set
        auto it = digitalInputs.find(pin);
        return (it != digitalInputs.end()) ? it->second : 0;
    }

    // Analog I/O
    int analogRead(uint8_t pin) override {
        analogReadCalled = true;
        // Return the specified analog value if one has been set
        auto it = analogInputs.find(pin);
        return (it != analogInputs.end()) ? it->second : 0;
    }
    
    void analogWrite(uint8_t pin, int val) override {
        analogWriteCalled = true;
        // Could store the analog output value if needed
    }

    // LEDC PWM (stub implementations for testing)
    void ledcSetupChannel(uint8_t channel, double freq, uint8_t resolution) override {}
    void ledcAttach(uint8_t pin, uint8_t channel) override {}
    void ledcDetach(uint8_t pin) override {}
    void ledcWriteChannel(uint8_t channel, uint32_t duty) override {}
    uint32_t ledcReadChannel(uint8_t channel) override { return 0; }
    
    // Serial communication
    void serialBegin(unsigned long baud) override {
        serialBeginCalled = true;
    }
    
    size_t serialPrint(const char* str) override {
        serialPrintCalled = true;
        return strlen(str);
    }
    
    size_t serialPrintln(const char* str) override {
        serialPrintlnCalled = true;
        return strlen(str) + 2; // +2 for CR+LF
    }
    
    int serialAvailable() override {
        serialAvailableCalled = true;
        return 0; // Default return
    }
    
    int serialRead() override {
        serialReadCalled = true;
        return -1; // Default return (no data)
    }
    
    // System control
    void restart() override {
        restartCalled = true;
        // In a real implementation, this would restart the device
        // For testing, we just set a flag
    }

    void hardReset() override {
        hardResetCalled = true;
    }

    int getResetReason() override {
        getResetReasonCalled = true;
        return resetReasonValue;
    }

    // Memory information
    uint32_t getFreeHeap() override {
        getFreeHeapCalled = true;
        return freeHeapValue;
    }
    
    uint32_t getMinFreeHeap() override {
        getMinFreeHeapCalled = true;
        return minFreeHeapValue;
    }
    
    uint32_t getHeapSize() override {
        getHeapSizeCalled = true;
        return heapSizeValue;
    }
    
    uint32_t getMaxAllocHeap() override {
        getMaxAllocHeapCalled = true;
        return maxAllocHeapValue;
    }

    uint32_t getPsramSize() override {
        getPsramSizeCalled = true;
        return psramSizeValue;
    }

    uint32_t getFreePsram() override {
        getFreePsramCalled = true;
        return psramFreeValue;
    }

    // Additional methods for testing
    
    // Set a digital input value to be returned by digitalRead
    void setDigitalInputValue(uint8_t pin, int value) {
        digitalInputs[pin] = value;
    }
    
    // Set an analog input value to be returned by analogRead
    void setAnalogInputValue(uint8_t pin, int value) {
        analogInputs[pin] = value;
    }
    
    // Additional methods for setting memory values
    void setFreeHeap(uint32_t value) {
        freeHeapValue = value;
    }
    
    void setMinFreeHeap(uint32_t value) {
        minFreeHeapValue = value;
    }
    
    void setHeapSize(uint32_t value) {
        heapSizeValue = value;
    }
    
    void setMaxAllocHeap(uint32_t value) {
        maxAllocHeapValue = value;
    }

    void setPsramSize(uint32_t value) {
        psramSizeValue = value;
    }

    void setFreePsram(uint32_t value) {
        psramFreeValue = value;
    }

    // Get the current pin mode
    uint8_t getPinMode(uint8_t pin) const {
        auto it = pinModes.find(pin);
        return (it != pinModes.end()) ? it->second : 255; // 255 means undefined
    }
    
    // Get the current digital output value
    uint8_t getDigitalOutput(uint8_t pin) const {
        auto it = digitalOutputs.find(pin);
        return (it != digitalOutputs.end()) ? it->second : 255; // 255 means undefined
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
        restartCalled = false;
        hardResetCalled = false;
        millisCalled = false;

        getFreeHeapCalled = false;
        getMinFreeHeapCalled = false;
        getHeapSizeCalled = false;
        getMaxAllocHeapCalled = false;
        getPsramSizeCalled = false;
        getFreePsramCalled = false;

        delayCount = 0;
        delayMicrosecondsCount = 0;
        lastDelayMs = 0;
        lastDelayUs = 0;
        currentTime = 0;
        currentMillis = 0;
        
        freeHeapValue = 0;
        minFreeHeapValue = 0;
        heapSizeValue = 0;
        maxAllocHeapValue = 0;
        psramSizeValue = 0;
        psramFreeValue = 0;

        pinModes.clear();
        digitalOutputs.clear();
        digitalWriteCounts.clear();
        digitalInputs.clear();
        analogInputs.clear();
    }
};

#endif // MOCK_ARDUINO_ADAPTER_H
