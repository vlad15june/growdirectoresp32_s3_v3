#ifndef ARDUINO_ADAPTER_H
#define ARDUINO_ADAPTER_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "constants.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

class ArduinoAdapter : public IArduinoAdapter {
public:
    // Constants
    uint8_t getPinModeInput() const override {
        return INPUT;
    }
    
    uint8_t getPinModeOutput() const override {
        return OUTPUT;
    }
    
    uint8_t getPinModeInputPullup() const override {
        return INPUT_PULLUP;
    }
    
    uint8_t getDigitalLow() const override {
        return LOW;
    }
    
    uint8_t getDigitalHigh() const override {
        return HIGH;
    }

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

    // LEDC PWM (ESP32 hardware PWM for dimmer control)
    void ledcSetupChannel(uint8_t channel, double freq, uint8_t resolution) override {
        ::ledcSetup(channel, freq, resolution);
    }

    void ledcAttach(uint8_t pin, uint8_t channel) override {
        ::ledcAttachPin(pin, channel);
    }

    void ledcDetach(uint8_t pin) override {
        ::ledcDetachPin(pin);
    }

    void ledcWriteChannel(uint8_t channel, uint32_t duty) override {
        ::ledcWrite(channel, duty);
    }

    uint32_t ledcReadChannel(uint8_t channel) override {
        return ::ledcRead(channel);
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

    // System control
    void restart() override {
        // For ESP32
        ESP.restart();
    }

    void hardReset() override {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        ::delay(100);
        esp_sleep_enable_timer_wakeup((uint64_t)HARD_RESET_SLEEP_MS * 1000ULL);
        esp_deep_sleep_start();
        ESP.restart();
    }

    int getResetReason() override {
        return (int)esp_reset_reason();
    }

    void feedTaskWatchdog() override {
        esp_task_wdt_reset();
    }

    // Memory information
    uint32_t getFreeHeap() override {
        return ESP.getFreeHeap();
    }
    
    uint32_t getMinFreeHeap() override {
        return ESP.getMinFreeHeap();
    }
    
    uint32_t getHeapSize() override {
        return ESP.getHeapSize();
    }
    
    uint32_t getMaxAllocHeap() override {
        return ESP.getMaxAllocHeap();
    }

    uint32_t getPsramSize() override {
        return ESP.getPsramSize();
    }

    uint32_t getFreePsram() override {
        return ESP.getFreePsram();
    }
};

#endif // ARDUINO_ADAPTER_H
