#ifndef LED_ADAPTER_H
#define LED_ADAPTER_H

#include "Core/Domain/Interfaces/ILedAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <Arduino.h>
#include <vector>

// Implementation of LED adapter for ESP32 hardware
class LedAdapter : public ILedAdapter
{
private:
    // Default LED pin values as class constants
    static uint8_t DEFAULT_GREEN_LEDS[4];
    static uint8_t DEFAULT_RED_LEDS[4];

    // Current working LED pins
    uint8_t greenLedPins[4];
    uint8_t redLedPins[4];

    // Number of devices
    uint8_t ledsCount;

    // Logger instance
    ILogger *logger;

    // Debug system instance
    IDebugSystem *debugSystem;

    // Helper method for logging
    void log(const char *format, ...)
    {
        if (logger != nullptr &&
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG)))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println("led", buffer);
        }
    }

public:
    // Constructor with optional logger and debug system
    LedAdapter(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr)
        : ledsCount(0), logger(logger), debugSystem(debugSystem) {}

    // Set logger after construction
    void setLogger(ILogger *loggerInstance)
    {
        logger = loggerInstance;
    }

    // Set debug system after construction
    void setDebugSystem(IDebugSystem *debugSystemInstance)
    {
        debugSystem = debugSystemInstance;
    }

    // Implementation of ILedAdapter methods
    void setupLed(uint8_t pin, uint8_t mode) override
    {
        if (pin != 0)
        {
            pinMode(pin, mode);
            log("Setup LED pin %u with mode %u", pin, mode);
        }
    }

    void setLedOn(uint8_t pin) override
    {
        if (pin != 0)
        {
            digitalWrite(pin, HIGH); // Changed to HIGH for active high
            log("LED pin %u turned ON", pin);
        }
    }

    void setLedOff(uint8_t pin) override
    {
        if (pin != 0)
        {
            digitalWrite(pin, LOW); // Changed to LOW for active high
            log("LED pin %u turned OFF", pin);
        }
    }

    void toggleLed(uint8_t pin) override
    {
        if (pin != 0)
        {
            digitalWrite(pin, !digitalRead(pin));
            log("LED pin %u toggled", pin);
        }
    }

    void setupModuleLeds(const uint8_t *greenLeds = nullptr, const uint8_t *redLeds = nullptr,
                         uint8_t ledsCount = 1) override
    {
        // Set the LED count
        this->ledsCount = ledsCount;

        log("Setting up %u module LEDs", ledsCount);

        // Copy provided LED data or use defaults
        for (uint8_t i = 0; i < 4 && i < ledsCount; i++)
        {
            greenLedPins[i] = greenLeds ? greenLeds[i] : DEFAULT_GREEN_LEDS[i];
            redLedPins[i] = redLeds ? redLeds[i] : DEFAULT_RED_LEDS[i];

            log("LED %u: Green pin %u, Red pin %u", i, greenLedPins[i], redLedPins[i]);

            // Initialize pins as OUTPUT mode
            if (greenLedPins[i] != 0)
            {
                pinMode(greenLedPins[i], OUTPUT);
                digitalWrite(greenLedPins[i], LOW); // Turn off initially (HIGH active)
            }

            if (redLedPins[i] != 0)
            {
                pinMode(redLedPins[i], OUTPUT);
                digitalWrite(redLedPins[i], LOW); // Turn off initially (HIGH active)
            }
        }
    }

    uint8_t getGreenLedPin(uint8_t index) override
    {
        if (index < ledsCount)
        {
            return greenLedPins[index];
        }
        return 0;
    }

    uint8_t getRedLedPin(uint8_t index) override
    {
        if (index < ledsCount)
        {
            return redLedPins[index];
        }
        return 0;
    }

    void setAllGreenLedOn() override
    {
        log("All green LEDs turned ON");
        for (uint8_t i = 0; i < ledsCount; i++)
        {
            if (greenLedPins[i] != 0)
            {
                digitalWrite(greenLedPins[i], HIGH);
            }
        }
    }

    void setAllGreenLedOff() override
    {
        log("All green LEDs turned OFF");
        for (uint8_t i = 0; i < ledsCount; i++)
        {
            if (greenLedPins[i] != 0)
            {
                digitalWrite(greenLedPins[i], LOW);
            }
        }
    }

    void setGreenLedOn(uint8_t index) override
    {
        if (index < ledsCount && greenLedPins[index] != 0)
        {
            digitalWrite(greenLedPins[index], HIGH);
            log("Green LED %u turned ON", index);
        }
    }

    void setGreenLedOff(uint8_t index) override
    {
        if (index < ledsCount && greenLedPins[index] != 0)
        {
            digitalWrite(greenLedPins[index], LOW);
            log("Green LED %u turned OFF", index);
        }
    }

    void setRedLedOn(uint8_t index) override
    {
        if (index < ledsCount && redLedPins[index] != 0)
        {
            digitalWrite(redLedPins[index], HIGH);
            log("Red LED %u turned ON", index);
        }
    }

    void setRedLedOff(uint8_t index) override
    {
        if (index < ledsCount && redLedPins[index] != 0)
        {
            digitalWrite(redLedPins[index], LOW);
            log("Red LED %u turned OFF", index);
        }
    }

    void setAllRedLedOn() override
    {
        log("All red LEDs turned ON");
        for (uint8_t i = 0; i < ledsCount; i++)
        {
            if (redLedPins[i] != 0)
            {
                digitalWrite(redLedPins[i], HIGH);
            }
        }
    }

    void setAllRedLedOff() override
    {
        log("All red LEDs turned OFF");
        for (uint8_t i = 0; i < ledsCount; i++)
        {
            if (redLedPins[i] != 0)
            {
                digitalWrite(redLedPins[i], LOW);
            }
        }
    }

    void setAllYellowLedOn() override
    {
        log("All yellow LEDs turned ON");
        for (uint8_t i = 0; i < ledsCount; i++)
        {
            log("Yellow LED %u turned ON", i);
            if (greenLedPins[i] != 0)
            {
                digitalWrite(greenLedPins[i], HIGH);
            }
            log("Red LED %u turned ON", i);
            if (redLedPins[i] != 0)
            {
                digitalWrite(redLedPins[i], HIGH);
            }
        }
    }

    void setAllYellowLedOff() override
    {
        log("All yellow LEDs turned OFF");
        for (uint8_t i = 0; i < ledsCount; i++)
        {
            log("Yellow LED %u turned OFF", i);
            if (greenLedPins[i] != 0)
            {
                digitalWrite(greenLedPins[i], LOW);
            }
            log("Red LED %u turned OFF", i);
            if (redLedPins[i] != 0)
            {
                digitalWrite(redLedPins[i], LOW);
            }
        }
    }

    void setYellowLedOn(uint8_t index) override
    {
        if (index < ledsCount)
        {
            if (greenLedPins[index] != 0)
            {
                digitalWrite(greenLedPins[index], HIGH);
            }
            if (redLedPins[index] != 0)
            {
                digitalWrite(redLedPins[index], HIGH);
            }
            log("Yellow LED %u turned ON", index);
        }
    }

    void setYellowLedOff(uint8_t index) override
    {
        if (index < ledsCount)
        {
            if (greenLedPins[index] != 0)
            {
                digitalWrite(greenLedPins[index], LOW);
            }
            if (redLedPins[index] != 0)
            {
                digitalWrite(redLedPins[index], LOW);
            }
            log("Yellow LED %u turned OFF", index);
        }
    }
};

uint8_t LedAdapter::DEFAULT_GREEN_LEDS[4] = {38, 37, 38, 37};
uint8_t LedAdapter::DEFAULT_RED_LEDS[4] = {12, 13, 14, 15};

#endif // LED_ADAPTER_H