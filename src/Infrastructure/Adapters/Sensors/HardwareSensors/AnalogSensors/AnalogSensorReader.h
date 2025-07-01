#ifndef ANALOG_SENSOR_READER_H
#define ANALOG_SENSOR_READER_H

#include "../../../../../Core/Domain/Interfaces/Adapters/Sensors/IAnalogSensorReader.h"
#include "../../../../../Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

class AnalogSensorReader : public IAnalogSensorReader
{
private:
    IArduinoAdapter& _arduino;

public:
    AnalogSensorReader(IArduinoAdapter& arduino) : _arduino(arduino) {}

    /**
     * @brief Read raw analog value from sensor
     * @param pin Analog pin number
     * @return Raw analog reading (0-4095 for ESP32)
     */
    int readRawValue(uint8_t pin) override
    {
        return _arduino.analogRead(pin);
    }

    /**
     * @brief Read voltage from analog sensor
     * @param pin Analog pin number
     * @param maxVoltage Maximum voltage reference (default 3.3V for ESP32)
     * @param adcResolution ADC resolution in bits (default 12 for ESP32)
     * @return Voltage value in volts
     */
    float readVoltage(uint8_t pin, float maxVoltage = 3.3f, uint8_t adcResolution = 12) override
    {
        int rawValue = _arduino.analogRead(pin);
        int maxAdcValue = (1 << adcResolution) - 1; // 2^adcResolution - 1
        return (float)rawValue * maxVoltage / maxAdcValue;
    }

    /**
     * @brief Read calibrated sensor value
     * @param pin Analog pin number
     * @param multiplier Calibration multiplier
     * @param offset Calibration offset
     * @return Calibrated sensor value
     */
    float readCalibratedValue(uint8_t pin, float multiplier, float offset) override
    {
        int rawValue = _arduino.analogRead(pin);
        return rawValue * multiplier + offset;
    }

    /**
     * @brief Read multiple samples and return average
     * @param pin Analog pin number
     * @param samples Number of samples to average
     * @param delayMs Delay between samples in milliseconds
     * @return Average raw value
     */
    int readAverageValue(uint8_t pin, uint8_t samples = 10, uint8_t delayMs = 10) override
    {
        if (samples == 0) samples = 1;
        
        long sum = 0;
        for (uint8_t i = 0; i < samples; i++)
        {
            sum += _arduino.analogRead(pin);
            if (i < samples - 1 && delayMs > 0)
            {
                _arduino.delay(delayMs);
            }
        }
        
        return sum / samples;
    }
};

#endif // ANALOG_SENSOR_READER_H
